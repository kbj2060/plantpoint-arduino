// MPINO-16A8R8T 스위치 제어 및 센서 모니터링 시스템
// Serial 버퍼 크기를 넉넉하게 증가시켜 긴 JSON 데이터의 유실을 방지합니다.
// (기본 64바이트 → 512바이트)
#define SERIAL_RX_BUFFER_SIZE 512
#define SERIAL_TX_BUFFER_SIZE 256

#include <ArduinoJson.h>

/*
=== MPINO-16A8R8T 하드웨어 구성 ===
장비명: MPINO-16A8R8T PlantPoint Controller
기능: 릴레이 제어, 디지털 입력, 아날로그 전류 측정

핀 번호 | 기능        | 설명
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
*/

// 장비 정보 구조체
struct DeviceInfo {
  String name;
  String type;      // "machine" 또는 "sensor"
  int relayPin;     // machine만 사용
  int sensorPin;    // machine의 경우 currentPin, sensor의 경우 센서 입력 핀
};

// 스마트팜 장비 딕셔너리
#define MAX_DEVICES 10
DeviceInfo devices[MAX_DEVICES];
int DEVICE_COUNT = 0;

// 함수 선언
void processCommand(String cmd);
void handleJsonCommand(String jsonMessage);
void handleConfigCommand(DynamicJsonDocument& doc);
DeviceInfo* findDevice(String deviceName);
void measureAndSendCurrent();
void measureAndSendEnvironment();
void sendResponse(String response);

void setup() {
  Serial.begin(115200);
  
  // 초기화 완료 신호 전송
  sendResponse("{\"cmd\":\"init_complete\",\"status\":\"ready\"}");
}

void loop() {
  // 1. 시리얼 포트에 데이터가 있는지 확인
  if (Serial.available() > 0) {
    // 2. 개행문자('\n')를 만날 때까지 문자열을 한번에 읽어옵니다. (안정성 향상)
    String command = Serial.readStringUntil('\n');
    command.trim(); // 앞뒤 공백 제거
    
    // 3. 읽어온 명령어가 있다면 처리
    if (command.length() > 0) {
      processCommand(command);
    }
  }

  // 4. 주기적으로 디지털 전류값 측정 및 전송 (machine만)
  measureAndSendCurrent();

  // 5. 주기적으로 센서값 측정 및 전송 (sensor만)
  measureAndSendEnvironment();
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
    String errorMessage = "{\"status\":\"error\",\"message\":\"JSON parse error: ";
    errorMessage += error.c_str();
    errorMessage += "\"}";
    sendResponse(errorMessage);
    return;
  }

  String cmd = doc["cmd"];

  if (cmd == "config") {
    handleConfigCommand(doc);
    return;
  } 

  if (cmd == "switch") {
    String deviceName = doc["dev"];
    bool value = doc["val"];

    DeviceInfo* device = findDevice(deviceName);
    if (device != nullptr) {
      if (device->type == "machine") {
        digitalWrite(device->relayPin, value ? HIGH : LOW);
        sendResponse("{\"status\":\"ok\",\"device\":\"" + deviceName + "\",\"value\":" + String(value ? "true" : "false") + "}");
      } else {
        sendResponse("{\"status\":\"error\",\"message\":\"device is not a machine: " + deviceName + "\"}");
      }
    } else {
      sendResponse("{\"status\":\"error\",\"message\":\"unknown device: " + deviceName + "\"}");
    }
    return;
  }

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
  
  // 새로운 장비 설정
  for (JsonObject deviceObj : devicesArray) {
    if (DEVICE_COUNT >= MAX_DEVICES) {
      sendResponse("{\"status\":\"error\",\"message\":\"Maximum number of devices reached\"}");
      break;
    }

    String name = deviceObj["name"].as<String>();
    String type = deviceObj["type"].as<String>();
    int relay = deviceObj["relay"];
    int sensor = deviceObj["sensor"];

    devices[DEVICE_COUNT].name = name;
    devices[DEVICE_COUNT].type = type;
    devices[DEVICE_COUNT].relayPin = relay;
    devices[DEVICE_COUNT].sensorPin = sensor;

    if (type == "machine") {
      if (relay != 0) {
        pinMode(relay, OUTPUT);
        digitalWrite(relay, LOW);
      }
      if (sensor != 0) {
        pinMode(sensor, INPUT_PULLUP);
      }
    }
    else if (type == "sensor") {
      if (sensor != 0) {
        pinMode(sensor, INPUT);
      }
    }

    DEVICE_COUNT++;
  }

  sendResponse("{\"status\":\"ok\",\"message\":\"config complete\",\"count\":" + String(DEVICE_COUNT) + "}");
}

// 장비 딕셔너리에서 장비 찾기
DeviceInfo* findDevice(String deviceName) {
  for (int i = 0; i < DEVICE_COUNT; i++) {
    if (devices[i].name == deviceName) {
      return &devices[i];
    }
  }
  return nullptr;
}

// 디지털 전류값 측정 및 전송 (machine만)
void measureAndSendCurrent() {
  static unsigned long lastCurrentCheck = 0;

  if (millis() - lastCurrentCheck < 3000) return;
  lastCurrentCheck = millis();

  for (int i = 0; i < DEVICE_COUNT; i++) {
    if (devices[i].type != "machine" || devices[i].sensorPin == 0) continue;

    bool currentState = digitalRead(devices[i].sensorPin); // INPUT_PULLUP이므로 반전

    DynamicJsonDocument currentDoc(128);
    currentDoc["cmd"] = "current";
    currentDoc["dev"] = devices[i].name;
    currentDoc["val"] = currentState;

    String currentData;
    serializeJson(currentDoc, currentData);
    sendResponse(currentData);
  }
}

// 센서값 측정 및 전송 (sensor만)
void measureAndSendEnvironment() {
  static unsigned long lastSensorCheck = 0;

  if (millis() - lastSensorCheck < 5000) return;
  lastSensorCheck = millis();

  for (int i = 0; i < DEVICE_COUNT; i++) {
    if (devices[i].type != "sensor" || devices[i].sensorPin == 0) continue;

    int sensorValue = digitalRead(devices[i].sensorPin);

    DynamicJsonDocument sensorDoc(128);
    sensorDoc["cmd"] = "environment";
    sensorDoc["dev"] = devices[i].name;
    sensorDoc["val"] = sensorValue;

    String sensorData;
    serializeJson(sensorDoc, sensorData);
    sendResponse(sensorData);
  }
}

// 응답 전송 유틸리티
void sendResponse(String response) {
  Serial.println(response);
  Serial.flush();
}
