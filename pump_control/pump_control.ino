/*
 * MPINO-16A8R8T 펌프 제어 시스템
 * - 릴레이 출력(R62~R69)에 12V 펌프 연결
 * - 디지털 입력(I22~I29)에 YF-TM02 유량 센서 연결
 *
 * 하드웨어 연결:
 * - R(69): 12V 워터 펌프 (COM6에 12V 전원 연결)
 * - I(29): YF-TM02 유량 센서 펄스 출력 (NPN)
 * - COM0: GND (필수!)
 *
 * 유량센서 연결:
 * 유량센서 빨강 → 24V+
 * 유량센서 검정 → GND
 * 유량센서 노랑 → I(29)
 * COM0 → GND (필수!)
 *
 * 펌프 릴레이 연결:
 * R(69) - 12V 펌프 + 극
 * COM6 - 12V 전원 +
 * 펌프 - 극 → GND
 */


// 핀 정의
#define PUMP_RELAY 69        // 릴레이 출력 핀 (R69)
#define FLOW_SENSOR 29       // 유량 센서 입력 핀 (I29)

// 유량 센서 변수
volatile unsigned long pulseCount = 0;  // 펄스 카운트
unsigned long oldTime = 0;              // 시간 측정 기준점
float flowRate = 0.0;                   // 유량 (L/min)
float totalVolume = 0.0;                // 총 물량 (L)

// YF-TM02 유량 센서 스펙
// K Factor: 86*Q (Q=L/min)
// 1L = 5160 pulses
const float PULSES_PER_LITER = 5160.0;  // 1L당 펄스 수
const float K_FACTOR = 86.0;            // K Factor

// 펄스 감지를 위한 이전 상태 저장
int lastFlowState = LOW;

// ---------------------------
// 초기 설정 (setup)
// ---------------------------
void setup() {  
  Serial.begin(9600);

  // 릴레이 출력 핀 설정
  pinMode(PUMP_RELAY, OUTPUT);
  digitalWrite(PUMP_RELAY, LOW);  // 펌프 초기 OFF

  // 유량 센서 입력 핀 설정
  pinMode(FLOW_SENSOR, INPUT);

  oldTime = millis();

  Serial.println("========================================");
  Serial.println("MPINO 펌프 제어 시스템 시작");
  Serial.println("========================================");
  Serial.println("명령어:");
  Serial.println("  ON    - 펌프 켜기");
  Serial.println("  OFF   - 펌프 끄기");
  Serial.println("  RESET - 총 물량 초기화");
  Serial.println("========================================");
}

// ---------------------------
// 메인 루프 (loop)
// ---------------------------
void loop() {
  // ⭐ 펄스 카운트 (폴링 방식 - MPINO는 I22~I29에 외부 인터럽트 미지원)
  int currentFlowState = digitalRead(FLOW_SENSOR);
  
  // 상승 에지 감지 (LOW → HIGH)
  if (currentFlowState == HIGH && lastFlowState == LOW) {
    pulseCount++;
  }
  lastFlowState = currentFlowState;

  // 1초마다 유량 계산
  if (millis() - oldTime >= 1000) {
    
    unsigned long currentPulse = pulseCount;
    pulseCount = 0;  // 카운터 리셋

    unsigned long timeInterval = millis() - oldTime;
    oldTime = millis(); // 시간 업데이트

    // 1. 유량 (L/min) 계산
    // 공식: Q (L/min) = (펄스/초) / K_FACTOR
    // 펄스/초 = currentPulse / (timeInterval / 1000.0)
    float pulsesPerSecond = currentPulse / (timeInterval / 1000.0);
    flowRate = pulsesPerSecond / K_FACTOR;

    // 2. 총 물량 계산 (L)
    totalVolume += currentPulse / PULSES_PER_LITER;

    // 시리얼 모니터에 출력
    Serial.print("펄스: ");
    Serial.print(currentPulse);
    Serial.print("\t유량: ");
    Serial.print(flowRate, 3);
    Serial.print(" L/min\t총 물량: ");
    Serial.print(totalVolume, 3);
    Serial.println(" L");
  }

  // 시리얼 명령어 처리
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toUpperCase();  // 대소문자 구분 없이

    if (command == "ON") {
      digitalWrite(PUMP_RELAY, HIGH);
      Serial.println(">>> 펌프 ON");
    }
    else if (command == "OFF") {
      digitalWrite(PUMP_RELAY, LOW);
      Serial.println(">>> 펌프 OFF");
    }
    else if (command == "RESET") {
      totalVolume = 0.0;
      pulseCount = 0;
      Serial.println(">>> 총 물량 초기화");
    }
    else {
      Serial.println(">>> 알 수 없는 명령어");
    }
  }
}