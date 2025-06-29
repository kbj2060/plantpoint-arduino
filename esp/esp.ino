// ESP32-S2 Mini 스위치 제어 시스템
// switch/* 토픽 구독 → MPINO 디지털 출력 제어
// MPINO current/input 값 수신 → 라즈베리파이 publish

/*
=== ESP32-S2 Mini 하드웨어 구성 ===
장비명: ESP32-S2 Mini PlantPoint Controller
기능: MQTT 브리지, WiFi 통신, MPINO 제어

핀 번호 | 기능          | 설명
--------|---------------|------------------
GPIO37  | RX_PIN        | MPINO 통신 수신
GPIO38  | TX_PIN        | MPINO 통신 송신  

=== 스마트팜 장비 매핑 (MPINO 제어) ===
채널번호 | 장비명        | MQTT 토픽     | 설명
---------|---------------|---------------|------------------
1        | LED           | switch/1      | 식물 생장 LED
2        | WATERSPRAY    | switch/2      | 물 분무 시스템
3        | FAN           | switch/3      | 환기 팬
4        | COOLER        | switch/4      | 냉각 시스템
5        | HEATER        | switch/5      | 가열 시스템
*/

/*
  ESP32 → MPINO:

  {"cmd":"switch","dev":"waterspray","val":true}

  MPINO → ESP32:

  {"cmd":"current","dev":"led","val":true}

  ESP32 → MQTT (변환):

  {"pattern":"current/led","data":{"name":"led","value":true}}
*/
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define RX_PIN 37
#define TX_PIN 38
#define WATCHDOG_TIMEOUT 30000

// WiFi 설정
const char* ssid = "ASUS_00_2G";
const char* password = "llewyn_3f";

// MQTT 설정
const char* mqtt_server = "192.168.1.89";  // 라즈베리파이 IP
const int mqtt_port = 1883;
const char* client_id = "ESP32_PlantPoint_Controller";

WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastWifiCheck = 0;
bool mpinoConnected = false;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  Serial1.setTimeout(2000); // 2초 타임아웃
  Serial1.setRxBufferSize(512); // RX 버퍼 크기 증가
    
  connectWiFi();
  
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);
  
  connectMQTT();
  
  Serial.println("시스템 준비 완료!");
}

void loop() {
  // WiFi 연결 상태 체크 (30초마다)
  if (millis() - lastWifiCheck > 30000) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi 연결 끊어짐, 재연결 시도...");
      connectWiFi();
    }
  }
  
  // MQTT 연결 유지
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();
  
  // MPINO로부터 데이터 수신 → 라즈베리파이 publish
  if (Serial1.available()) {
    String mpino_data = Serial1.readStringUntil('\n');
    mpino_data.trim();
    
    if (mpino_data.length() > 0) {
      mpinoConnected = true;
      Serial.println("MPINO 수신: " + mpino_data);
      
      // JSON 형태 데이터 처리
      if (mpino_data.startsWith("{")) {
        processMpinoJsonData(mpino_data);
      }
    }
  }
  
  delay(10);
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("WiFi 연결 중");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.println("WiFi 연결 완료! IP: " + WiFi.localIP().toString());
}

void connectMQTT() {
  int attempts = 0;
  while (!mqtt.connected() && attempts < 5) {
    Serial.print("MQTT 연결 중... 시도 " + String(attempts + 1));
    
    if (mqtt.connect(client_id)) {
      Serial.println("MQTT 연결 완료!");
      
      // 토픽 구독
      mqtt.subscribe("switch/+");       // 스위치 제어
      
      Serial.println("토픽 구독 완료: switch/+");
      
    } else {
      Serial.println("실패 (상태: " + String(mqtt.state()) + ")");
      attempts++;
      delay(2000);
    }
  }
  
  if (!mqtt.connected()) {
    Serial.println("MQTT 연결 실패, 재부팅...");
    ESP.restart();
  }
}

// MQTT 메시지 수신 처리
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  String topicStr = String(topic);
  Serial.println("MQTT 수신: " + topicStr + " = " + message);
  
  // JSON 형태 메시지 처리
  handleJsonCommand(message);
}

// JSON 명령 처리
void handleJsonCommand(String jsonMessage) {
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, jsonMessage);
  
  if (error) {
    Serial.println("JSON 파싱 실패: " + String(error.c_str()));
    return;
  }
  
  // pattern과 data 추출
  String pattern = doc["pattern"];
  JsonObject data = doc["data"];
  String deviceName = data["name"];
  bool value = data["value"];
  
  Serial.println("장비: " + deviceName + ", 값: " + String(value ? "ON" : "OFF"));
  
  // 짧은 JSON 형태로 MPINO에 전송
  DynamicJsonDocument commandDoc(256);
  commandDoc["cmd"] = "switch";
  commandDoc["dev"] = deviceName;
  commandDoc["val"] = value;
  
  String jsonCommand;
  serializeJson(commandDoc, jsonCommand);
  sendCommandToMpino(jsonCommand);
}


// MPINO JSON 데이터 처리
void processMpinoJsonData(String jsonData) {
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, jsonData);
  
  if (error) {
    Serial.println("MPINO JSON 파싱 실패: " + String(error.c_str()));
    return;
  }
  
  String cmd = doc["cmd"];
  String device = doc["dev"];
  bool value = doc["val"];
  
  if (cmd == "current") {
    // 전류 데이터 처리 - MQTT 형식으로 변환
    DynamicJsonDocument currentDoc(256);
    currentDoc["pattern"] = "current/" + device;
    currentDoc["data"]["name"] = device;
    currentDoc["data"]["value"] = value;
    
    String currentPayload;
    serializeJson(currentDoc, currentPayload);
    
    if (mqtt.publish("current", currentPayload.c_str())) {
      Serial.println("전류값 전송: current = " + currentPayload);
    }
  }
}

// 유틸리티 함수들
void sendCommandToMpino(String command) {
  Serial.println("MPINO 전송 (" + String(command.length()) + "자): " + command);
  
  // 천천히 문자별로 전송
  for (int i = 0; i < command.length(); i++) {
    Serial1.write(command[i]);
    delayMicroseconds(100); // 문자 간 간격
  }
  Serial1.write('\n');
  Serial1.flush();
  delay(50); // 전송 완료 대기
}
