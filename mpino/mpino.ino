// MPINO-16A8R8T 스위치 제어 및 센서 모니터링 시스템
// ESP32 명령 수신 → 디지털 출력 제어
// 디지털 입력 감지 → ESP32로 전송
// 전류값 측정 → ESP32로 전송

// Serial 버퍼 크기 증가 (기본 64바이트 → 256바이트)
#define SERIAL_RX_BUFFER_SIZE 256
#define SERIAL_TX_BUFFER_SIZE 256

#include <ArduinoJson.h>

/*
=== MPINO-16A8R8T 하드웨어 구성 ===
장비명: MPINO-16A8R8T PlantPoint Controller
기능: 릴레이 제어, 디지털 입력, 아날로그 전류 측정

핀 번호 | 기능          | 설명
--------|---------------|------------------
GPIO13  | LED_STATUS    | 시스템 상태 LED
GPIO62  | RELAY_1       | 출력 릴레이 1번
GPIO63  | RELAY_2       | 출력 릴레이 2번
GPIO64  | RELAY_3       | 출력 릴레이 3번
GPIO65  | RELAY_4       | 출력 릴레이 4번
GPIO66  | RELAY_5       | 출력 릴레이 5번
GPIO67  | RELAY_6       | 출력 릴레이 6번
GPIO68  | RELAY_7       | 출력 릴레이 7번
GPIO69  | RELAY_8       | 출력 릴레이 8번
GPIO22  | INPUT_1       | 디지털 입력 1번
GPIO23  | INPUT_2       | 디지털 입력 2번
GPIO24  | INPUT_3       | 디지털 입력 3번
GPIO25  | INPUT_4       | 디지털 입력 4번
GPIO26  | INPUT_5       | 디지털 입력 5번
GPIO27  | INPUT_6       | 디지털 입력 6번
GPIO28  | INPUT_7       | 디지털 입력 7번
GPIO29  | INPUT_8       | 디지털 입력 8번
A0      | ANALOG_1      | 아날로그 전류 측정 1번
A1      | ANALOG_2      | 아날로그 전류 측정 2번
A2      | ANALOG_3      | 아날로그 전류 측정 3번
A3      | ANALOG_4      | 아날로그 전류 측정 4번

=== 스마트팜 장비 매핑 ===
채널 | 릴레이핀 | 장비명     | 입력핀   | 전류감지 | 설명
-----|----------|------------|----------|----------|------------------
1    | GPIO62   | LED        | GPIO22   | D14(A0)  | 식물 생장 LED
2    | GPIO63   | WATERSPRAY | GPIO23   | D15(A1)  | 물 분무 시스템
3    | GPIO64   | FAN        | GPIO24   | D16(A2)  | 환기 팬
4    | GPIO65   | COOLER     | GPIO25   | D17(A3)  | 냉각 시스템
5    | GPIO66   | HEATER     | GPIO26   | -        | 가열 시스템
6    | GPIO67   | 예비장비1  | GPIO27   | -        | 확장용
7    | GPIO68   | 예비장비2  | GPIO28   | -        | 확장용
8    | GPIO69   | 예비장비3  | GPIO29   | -        | 확장용
*/

/*
  ESP32 → MPINO:

  {"cmd":"switch","dev":"waterspray","val":true}

  MPINO → ESP32:

  {"cmd":"current","dev":"led","val":true}

  ESP32 → MQTT (변환):

  {"pattern":"current/led","data":{"name":"led","value":true}}
*/
#define LED_STATUS 13
#define RELAY_START_PIN 62
#define RELAY_END_PIN 69
#define INPUT_START_PIN 22
#define INPUT_END_PIN 29
#define ANALOG_CHANNELS 4

// 장비 정보 구조체
struct DeviceInfo {
  String name;
  int relayPin;
  int inputPin;
  int currentPin;
};

// 스마트팜 장비 딕셔너리
DeviceInfo devices[] = {
  {"led", 62, 22, 14},        // A0=14
  {"waterspray", 63, 23, 15}, // A1=15
  {"fan", 64, 24, 16},        // A2=16
  {"cooler", 65, 25, 17},     // A3=17
  {"heater", 66, 26, -1}      // 전류 측정 없음
};
const int DEVICE_COUNT = 5;

bool statusLedState = false;

void setup() {
  Serial.begin(115200);
  Serial3.begin(9600);
  Serial3.setTimeout(2000); // 2초 타임아웃 설정
  
  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, HIGH);
  
  Serial.println("=== MPINO-16A8R8T PlantPoint 제어 시스템 ===");
  Serial.println("버전: 2.0");
  Serial.println("릴레이 출력: R62-R69 (8채널)");
  Serial.println("디지털 입력: I22-I29 (8채널)");
  Serial.println("아날로그 입력: A0-A3 (4채널)");
  
  
  
  // 장비 딕셔너리를 사용하여 핀 설정
  for (int i = 0; i < DEVICE_COUNT; i++) {
    // 릴레이 출력 핀 설정
    pinMode(devices[i].relayPin, OUTPUT);
    digitalWrite(devices[i].relayPin, LOW);
    
    
    // 전류 감지 핀 설정
    if (devices[i].currentPin > 0) {
      pinMode(devices[i].currentPin, INPUT_PULLUP);
    }
  }
  
  Serial.println("MPINO 초기화 완료 - " + String(millis()) + "ms");
  
}

void loop() {  
  // ESP32로부터 명령 수신 및 처리
  if (Serial3.available()) {
    delay(200); // 충분한 대기 시간 (속도 낮춤)
    String command = Serial3.readStringUntil('\n');
    command.trim();
    
    if (command.length() > 0) {
      Serial.println("ESP32 명령 수신 (" + String(command.length()) + "자): " + command);
      processCommand(command);
    }
  }
  
  // 디지털 전류값 측정 및 전송
  measureAndSendCurrent();
  
  delay(50);
}

// ESP32 명령 처리
void processCommand(String cmd) {
  // JSON 형태 명령 처리만 지원
  if (cmd.startsWith("{")) {
    handleJsonCommand(cmd);
  }
  else {
    Serial.println("JSON 형태가 아닌 명령: " + cmd);
    sendResponse("{\"status\":\"error\",\"message\":\"only JSON format supported\"}");
  }
}

// JSON 명령 처리
void handleJsonCommand(String jsonMessage) {
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, jsonMessage);
  
  if (error) {
    Serial.println("JSON 파싱 실패: " + String(error.c_str()));
    sendResponse("{\"status\":\"error\",\"message\":\"json parse error\"}");
    return;
  }
  
  // 짧은 형식으로 데이터 추출
  String cmd = doc["cmd"];
  String deviceName = doc["dev"];
  bool value = doc["val"];
  
  Serial.println("JSON 수신 - 장비: " + deviceName + ", 값: " + String(value ? "ON" : "OFF"));
  
  // 장비 딕셔너리에서 장비 정보 찾기
  DeviceInfo* device = findDevice(deviceName);
  if (device != nullptr) {
    digitalWrite(device->relayPin, value ? HIGH : LOW);
    
    Serial.println("릴레이 R" + String(device->relayPin) + " (" + device->name + ") " + String(value ? "ON" : "OFF"));
  } else {
    Serial.println("알 수 없는 장비: " + deviceName);
  }
}

// 장비 딕셔너리에서 장비 찾기
DeviceInfo* findDevice(String deviceName) {
  for (int i = 0; i < DEVICE_COUNT; i++) {
    if (devices[i].name == deviceName) {
      return &devices[i];
    }
  }
  return nullptr; // 장비를 찾지 못함
}



// 디지털 전류값 측정 및 전송
void measureAndSendCurrent() {
  static unsigned long lastCurrentCheck = 0;
  static bool lastCurrentValues[DEVICE_COUNT] = {false};
  static bool initialized[DEVICE_COUNT] = {false};
  
  // 2초마다 전류값 측정
  if (millis() - lastCurrentCheck < 2000) return;
  lastCurrentCheck = millis();
  
  // 장비 딕셔너리를 사용하여 전류 측정
  for (int i = 0; i < DEVICE_COUNT; i++) {
    if (devices[i].currentPin > 0) { // 전류 측정 핀이 있는 장비만
      bool currentState = digitalRead(devices[i].currentPin);
      
      // 초기화 또는 상태 변화 시 전송
      if (!initialized[i] || currentState != lastCurrentValues[i]) {
        initialized[i] = true;
        lastCurrentValues[i] = currentState;
        
        // 짧은 JSON 형태로 전류 상태 전송
        DynamicJsonDocument currentDoc(128);
        currentDoc["cmd"] = "current";
        currentDoc["dev"] = devices[i].name;
        currentDoc["val"] = currentState;
        
        String currentData;
        serializeJson(currentDoc, currentData);
        sendResponse(currentData);
        Serial.println("디지털 전류 " + devices[i].name + " (핀" + String(devices[i].currentPin) + "): " + String(currentState ? "ON" : "OFF"));
      }
    }
  }
}

// 응답 전송 유틸리티
void sendResponse(String response) {
  Serial3.println(response);
  Serial3.flush();
}

