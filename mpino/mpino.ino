// MPINO-16A8R8T 스위치 제어 및 센서 모니터링 시스템
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

=== 스마트팜 장비 매핑 ===
채널 | 릴레이핀 | 장비명     | 전류감지   | 설명
-----|----------|------------|----------|----------|------------------
1    | GPIO62   | LED        | GPIO22   | 식물 생장 LED
2    | GPIO63   | WATERSPRAY | GPIO23   | 물 분무 시스템
3    | GPIO64   | FAN        | GPIO24   | 환기 팬
4    | GPIO65   | COOLER     | GPIO25   | 냉각 시스템
5    | GPIO66   | HEATER     | GPIO26   | -        | 가열 시스템
6    | GPIO67   | 예비장비1  | GPIO27   | -        | 확장용
7    | GPIO68   | 예비장비2  | GPIO28   | -        | 확장용
8    | GPIO69   | 예비장비3  | GPIO29   | -        | 확장용
*/

/*
  통신 프로토콜:

  1. 장비 설정 (Raspberry Pi → MPINO):
  {"cmd":"config","devices":[
    {"name":"led","relay":62,"current":22},
    {"name":"waterspray","relay":63,"current":23}
  ]}

  2. 릴레이 제어 (Raspberry Pi → MPINO):
  {"cmd":"switch","dev":"waterspray","val":true}

  3. 전류 상태 전송 (MPINO → Raspberry Pi):
  {"cmd":"current","dev":"led","val":true}
*/

// 장비 정보 구조체
struct DeviceInfo {
  String name;
  int relayPin;
  int currentPin;
};

// 스마트팜 장비 딕셔너리 (동적 할당)
#define MAX_DEVICES 10
DeviceInfo devices[MAX_DEVICES];
int DEVICE_COUNT = 0;

// 전류 상태 추적을 위한 전역 변수
bool lastCurrentValues[MAX_DEVICES] = {false};
bool initialized[MAX_DEVICES] = {false};

bool statusLedState = false;

// 시리얼 통신 버퍼
char serialBuffer[512];
bool commandReady = false;

// 함수 선언
void checkSerial();
void processCommand(String cmd);
void handleJsonCommand(String jsonMessage);
void handleConfigCommand(DynamicJsonDocument& doc);
DeviceInfo* findDevice(String deviceName);
void measureAndSendCurrent();
void sendResponse(String response);

void setup() {
  Serial.begin(115200);
  
  // 초기화 완료 신호 전송
  sendResponse("{\"cmd\":\"init_complete\",\"status\":\"ready\"}");
}

void loop() {
  // 1. 라즈베리파이로부터 비동기 명령 수신
  checkSerial();

  // 2. 수신된 명령 처리
  if (commandReady) {
    String command = String(serialBuffer);
    command.trim();
    processCommand(command);
    commandReady = false; // 플래그 초기화
  }

  // 3. 주기적으로 디지털 전류값 측정 및 전송
  measureAndSendCurrent();
}

/**
 * @brief 시리얼 포트를 비동기 방식으로 확인하여 '\n' 문자를 기준으로 명령어를 완성합니다.
 */
void checkSerial() {
  static byte i = 0;
  while (Serial.available() > 0) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r' && i < sizeof(serialBuffer) - 1) {
      serialBuffer[i++] = ch;
    } else {
      serialBuffer[i] = '\0'; // 문자열 종료
      i = 0;
      if (strlen(serialBuffer) > 0) {
        commandReady = true;
      }
    }
  }
}

// 라즈베리파이 명령 처리
void processCommand(String cmd) {
  // JSON 형태 명령 처리만 지원
  if (cmd.startsWith("{")) {
    handleJsonCommand(cmd);
  }
  else {
    sendResponse("{\"status\":\"error\",\"message\":\"only JSON format supported\"}");
  }
}

// JSON 명령 처리
void handleJsonCommand(String jsonMessage) {
  DynamicJsonDocument doc(512); // JSON 버퍼 크기 최적화
  DeserializationError error = deserializeJson(doc, jsonMessage);

  if (error) {
    sendResponse("{\"status\":\"error\",\"message\":\"JSON parse error\"}");
    return;
  }

  // 짧은 형식으로 데이터 추출
  String cmd = doc["cmd"];

  // config 명령 처리 (장비 설정)
  if (cmd == "config") {
    handleConfigCommand(doc);
    return;
  }

  // config_start 명령 처리
  if (cmd == "config_start") {
    DEVICE_COUNT = 0;
    // 전류 상태 배열 초기화
    for (int i = 0; i < MAX_DEVICES; i++) {
      lastCurrentValues[i] = false;
      initialized[i] = false;
    }
    sendResponse("{\"status\":\"ok\",\"message\":\"config_started\"}");
    return;
  }

  // config_device 명령 처리
  if (cmd == "config_device") {
    int index = doc["index"];
    String name = doc["name"].as<String>();
    int relay = doc["relay"];
    int current = doc["current"];

    if (index < MAX_DEVICES) {
      devices[index].name = name;
      devices[index].relayPin = relay;
      devices[index].currentPin = current;

      // 릴레이 핀 설정
      pinMode(relay, OUTPUT);
      digitalWrite(relay, LOW);

      // 전류 감지 핀 설정
      pinMode(current, INPUT_PULLUP);

      DEVICE_COUNT = max(DEVICE_COUNT, index + 1);
      sendResponse("{\"status\":\"ok\",\"device\":\"" + name + "\",\"index\":" + String(index) + "}");
    } else {
      sendResponse("{\"status\":\"error\",\"message\":\"index out of range\"}");
    }
    return;
  }

  // config_end 명령 처리
  if (cmd == "config_end") {
    sendResponse("{\"status\":\"ok\",\"count\":" + String(DEVICE_COUNT) + "}");
    return;
  }

  // switch 명령 처리 (릴레이 제어)
  if (cmd == "switch") {
    String deviceName = doc["dev"];
    bool value = doc["val"];

    // 장비 딕셔너리에서 장비 정보 찾기
    DeviceInfo* device = findDevice(deviceName);
    if (device != nullptr) {
      digitalWrite(device->relayPin, value ? HIGH : LOW);
      
      // 응답 전송
      sendResponse("{\"status\":\"ok\",\"device\":\"" + deviceName + "\",\"value\":" + String(value ? "true" : "false") + "}");
    } else {
      sendResponse("{\"status\":\"error\",\"message\":\"unknown device: " + deviceName + "\"}");
    }
    return;
  }

  // 알 수 없는 명령
  sendResponse("{\"status\":\"error\",\"message\":\"unknown command: " + cmd + "\"}");
}

// config 명령 처리 (장비 동적 설정)
void handleConfigCommand(DynamicJsonDocument& doc) {
  JsonArray devicesArray = doc["devices"];

  if (devicesArray.isNull()) {
    sendResponse("{\"status\":\"error\",\"message\":\"devices array required\"}");
    return;
  }

  // 기존 장비 초기화
  DEVICE_COUNT = 0;
  
  // 전류 상태 배열 초기화
  for (int i = 0; i < MAX_DEVICES; i++) {
    lastCurrentValues[i] = false;
    initialized[i] = false;
  }

  // 새로운 장비 설정
  for (JsonObject deviceObj : devicesArray) {
    if (DEVICE_COUNT >= MAX_DEVICES) {
      break;
    }

    String name = deviceObj["name"].as<String>();
    int relay = deviceObj["relay"];
    int current = deviceObj["current"];

    devices[DEVICE_COUNT].name = name;
    devices[DEVICE_COUNT].relayPin = relay;
    devices[DEVICE_COUNT].currentPin = current;

    // 릴레이 핀 설정
    pinMode(relay, OUTPUT);
    digitalWrite(relay, LOW);

    // 전류 감지 핀 설정
    pinMode(current, INPUT_PULLUP);

    DEVICE_COUNT++;
  }

  sendResponse("{\"status\":\"ok\",\"count\":" + String(DEVICE_COUNT) + "}");
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
  
  // 2초마다 전류값 측정
  if (millis() - lastCurrentCheck < 2000) return;
  lastCurrentCheck = millis();
  
  // 장비 딕셔너리를 사용하여 전류 측정 (24V 디지털 입력)
  for (int i = 0; i < DEVICE_COUNT; i++) {
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
    }
  }
}

// 응답 전송 유틸리티
void sendResponse(String response) {
  Serial.println(response);
  Serial.flush();
}

