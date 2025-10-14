// MPINO-16A8R8T 스위치 제어 및 센서 모니터링 시스템 (v3.1 개선 버전)
#include <ArduinoJson.h>

// --- 설정값 ---
#define MAX_DEVICES 10                 // 최대 장비 수
const unsigned long CURRENT_CHECK_INTERVAL = 2000; // 전류 상태 확인 주기 (2000ms = 2초)

// --- 장비 정보 구조체 ---
struct DeviceInfo {
  String name;
  int relayPin;
  int currentPin;
};

// --- 전역 변수 ---
DeviceInfo devices[MAX_DEVICES];
int DEVICE_COUNT = 0;

bool lastCurrentValues[MAX_DEVICES] = {false};
bool initialized[MAX_DEVICES] = {false};

// 시리얼 통신 버퍼
char serialBuffer[512];
bool commandReady = false;

// --- 함수 선언 ---
void checkSerial();
void processCommand(String cmd);
void handleJsonCommand(String jsonMessage);
DeviceInfo* findDevice(String deviceName);
void measureAndSendCurrent();
void sendResponse(String response);

// =================================================================
//  설정 (Setup)
// =================================================================
void setup() {
  Serial.begin(115200);
  sendResponse("{\"cmd\":\"init_complete\",\"status\":\"ready\"}");
  Serial.println("System Ready.");
}

// =================================================================
//  메인 루프 (Loop)
// =================================================================
void loop() {
  // 1. 라즈베리파이로부터 비동기 명령 수신
  checkSerial();

  // 2. 수신된 명령 처리
  if (commandReady) {
    String command = String(serialBuffer);
    command.trim();
    Serial.println(">> RECV (" + String(command.length()) + " bytes): " + command);
    processCommand(command);
    commandReady = false; // 플래그 초기화
  }

  // 3. 주기적으로 디지털 전류값 측정 및 전송
  measureAndSendCurrent();
}

// =================================================================
//  라즈베리파이 명령 처리 로직
// =================================================================

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

/**
 * @brief 수신된 명령이 JSON 형식인지 확인하고 처리 함수를 호출합니다.
 */
void processCommand(String cmd) {
  if (cmd.startsWith("{")) {
    handleJsonCommand(cmd);
  } else {
    Serial.println("   ERROR: Non-JSON command ignored.");
    sendResponse("{\"status\":\"error\",\"message\":\"Only JSON format supported\"}");
  }
}

/**
 * @brief JSON 명령을 파싱하고 각 명령에 맞는 핸들러를 실행합니다.
 */
void handleJsonCommand(String jsonMessage) {
  DynamicJsonDocument doc(512); // JSON 버퍼 크기 최적화
  DeserializationError error = deserializeJson(doc, jsonMessage);

  if (error) {
    Serial.println("   ERROR: JSON parse failed - " + String(error.c_str()));
    sendResponse("{\"status\":\"error\",\"message\":\"JSON parse error: " + String(error.c_str()) + "\"}");
    return;
  }

  String cmd = doc["cmd"];

  if (cmd == "config_start") {
    DEVICE_COUNT = 0;
    for (int i = 0; i < MAX_DEVICES; i++) {
      lastCurrentValues[i] = false;
      initialized[i] = false;
    }
    Serial.println("   OK: Device configuration started.");
    sendResponse("{\"status\":\"ok\",\"message\":\"config_started\"}");

  } else if (cmd == "config_device") {
    int index = doc["index"];
    if (index < MAX_DEVICES) {
      devices[index].name = doc["name"].as<String>();
      devices[index].relayPin = doc["relay"];
      devices[index].currentPin = doc["current"];

      pinMode(devices[index].relayPin, OUTPUT);
      digitalWrite(devices[index].relayPin, LOW);
      pinMode(devices[index].currentPin, INPUT_PULLUP);
      
      DEVICE_COUNT = max(DEVICE_COUNT, index + 1);
      Serial.println("   OK: Configured device " + String(index) + " - " + devices[index].name);
      sendResponse("{\"status\":\"ok\",\"device\":\"" + devices[index].name + "\",\"index\":" + String(index) + "}");
    } else {
      Serial.println("   ERROR: Device index out of range.");
      sendResponse("{\"status\":\"error\",\"message\":\"index out of range\"}");
    }

  } else if (cmd == "config_end") {
    Serial.println("   OK: Device configuration finished. Total: " + String(DEVICE_COUNT));
    sendResponse("{\"status\":\"ok\",\"message\":\"config_end\",\"count\":" + String(DEVICE_COUNT) + "}");

  } else if (cmd == "switch") {
    String deviceName = doc["dev"];
    bool value = doc["val"];
    DeviceInfo* device = findDevice(deviceName);
    if (device != nullptr) {
      digitalWrite(device->relayPin, value ? HIGH : LOW);
      Serial.println("   OK: Switched " + deviceName + " -> " + (value ? "ON" : "OFF"));
      sendResponse("{\"status\":\"ok\",\"device\":\"" + deviceName + "\",\"value\":" + String(value ? "true" : "false") + "}");
    } else {
      Serial.println("   ERROR: Unknown device name '" + deviceName + "'");
      sendResponse("{\"status\":\"error\",\"message\":\"Unknown device: " + deviceName + "\"}");
    }

  } else {
    Serial.println("   ERROR: Unknown command '" + cmd + "'");
    sendResponse("{\"status\":\"error\",\"message\":\"Unknown command: " + cmd + "\"}");
  }
}

// =================================================================
//  유틸리티 함수
// =================================================================

/**
 * @brief 장비 이름으로 장비 정보를 찾습니다.
 * @return 찾은 DeviceInfo 포인터, 없으면 nullptr
 */
DeviceInfo* findDevice(String deviceName) {
  for (int i = 0; i < DEVICE_COUNT; i++) {
    if (devices[i].name == deviceName) {
      return &devices[i];
    }
  }
  return nullptr;
}

/**
 * @brief 주기적으로 모든 장비의 전류 상태를 확인하고, 상태 변경 시 라즈베리파이로 전송합니다.
 */
void measureAndSendCurrent() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < CURRENT_CHECK_INTERVAL) return;
  lastCheck = millis();

  for (int i = 0; i < DEVICE_COUNT; i++) {
    bool currentState = digitalRead(devices[i].currentPin);
    if (!initialized[i] || currentState != lastCurrentValues[i]) {
      initialized[i] = true;
      lastCurrentValues[i] = currentState;

      DynamicJsonDocument doc(128);
      doc["cmd"] = "current";
      doc["dev"] = devices[i].name;
      doc["val"] = currentState;

      String currentData;
      serializeJson(doc, currentData);
      sendResponse(currentData);
      Serial.println("   SENT: Current status for " + devices[i].name + " -> " + (currentState ? "ON" : "OFF"));
    }
  }
}

/**
 * @brief 라즈베리파이로 응답 메시지를 전송합니다.
 */
void sendResponse(String response) {
  Serial.println(response);
  Serial.flush();
}