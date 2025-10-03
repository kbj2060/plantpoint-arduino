/*
 * MPINO-16A8R8T 펌프 제어 시스템
 * - 릴레이 출력(R62~R69)에 12V 펌프 연결
 * - 디지털 입력(I22~I29)에 YF-TM02 유량 센서 연결
 *
 * 하드웨어 연결:
 * - R(62): 12V 워터 펌프 (COM3에 12V 전원 연결)
 * - I(22): YF-TM02 유량 센서 펄스 출력 (NPN)
 * - COM0: 12V (Pull-up 전압)
 *
 * 유량센서 연결:
 * 유량센서 빨강 → 24V+
 * 유량센서 검정 → GND
 * 유량센서 노랑 → I(22)
 * COM0 → 24V+ (Pull-up)
 */

// 핀 정의
#define PUMP_RELAY 62        // 릴레이 출력 핀 (R62)
#define FLOW_SENSOR 22       // 유량 센서 입력 핀 (I22)

// 유량 센서 변수
volatile unsigned long pulseCount = 0;  // 펄스 카운트
unsigned long oldTime = 0;
float flowRate = 0.0;        // 유량 (L/min)
float totalVolume = 0.0;     // 총 물량 (L)

// YF-TM02 유량 센서 스펙
// K Factor: 86 pulses/L/min (86*Q)
// 1L = 5160 pulses
const float pulsesPerLiter = 5160.0;  // 1L당 펄스 수

void setup() {
  // 시리얼 통신 초기화
  Serial.begin(9600);

  // 릴레이 출력 핀 설정
  pinMode(PUMP_RELAY, OUTPUT);
  digitalWrite(PUMP_RELAY, LOW);  // 펌프 초기 OFF

  // 유량 센서 입력 핀 설정
  pinMode(FLOW_SENSOR, INPUT);

  // 유량 센서 인터럽트 설정 (NPN 출력이므로 하강 에지에서 펄스 카운트)
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR), pulseCounter, FALLING);

  oldTime = millis();

  Serial.println("MPINO 펌프 제어 시스템 시작");
  Serial.println("명령어: ON - 펌프 켜기, OFF - 펌프 끄기");
}

void loop() {
  // 1초마다 유량 계산
  if (millis() - oldTime > 1000) {
    // 인터럽트 비활성화
    detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR));

    // 유량 계산 (L/min)
    // K Factor 공식: F = 86*Q (F: frequency(Hz), Q: flow rate(L/min))
    // 1초당 펄스를 60초로 환산하여 분당 펄스 계산 후 86으로 나눔
    flowRate = (pulseCount * 60.0) / 86.0;

    // 총 물량 계산 (L)
    // 1초당 펄스를 리터로 변환: pulses / 5160 pulses/L
    totalVolume += pulseCount / pulsesPerLiter;

    // 시리얼 모니터에 출력
    Serial.print("펄스: ");
    Serial.print(pulseCount);
    Serial.print("\t유량: ");
    Serial.print(flowRate, 3);
    Serial.print(" L/min\t총 물량: ");
    Serial.print(totalVolume, 3);
    Serial.println(" L");

    // 카운트 초기화
    pulseCount = 0;
    oldTime = millis();

    // 인터럽트 재활성화 (NPN이므로 FALLING)
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR), pulseCounter, FALLING);
  }

  // 시리얼 명령어 처리
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "ON") {
      digitalWrite(PUMP_RELAY, HIGH);
      Serial.println("펌프 ON");
    }
    else if (command == "OFF") {
      digitalWrite(PUMP_RELAY, LOW);
      Serial.println("펌프 OFF");
    }
    else if (command == "RESET") {
      totalVolume = 0.0;
      Serial.println("총 물량 초기화");
    }
  }
}

// 유량 센서 펄스 카운터 인터럽트 함수
void pulseCounter() {
  pulseCount++;
}
