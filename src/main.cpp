#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h> // MQTT 라이브러리

// 핀 정의
#define LED_PIN 4
#define NUM_LEDS 8
#define LDR_PIN 36
#define ENCODER_CLK 21
#define ENCODER_DT 22
#define ENCODER_SW 23
#define ENCODER2_CLK 18
#define ENCODER2_DT 19
#define ENCODER2_SW 5

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// WiFi 설정
const char* ssid = "IoT518";       // WiFi 이름
const char* password = "iot123456"; // WiFi 비밀번호

// MQTT 설정
const char* mqtt_server = "172.20.10.12";   // MQTT 브로커 IP 주소
const int mqtt_port = 1883;                // MQTT 포트
const char* mqtt_topic = "home/lux";       // MQTT 토픽

WiFiClient espClient; // WiFi 클라이언트
PubSubClient mqttClient(espClient); // MQTT 클라이언트

// 전역 변수
bool manualMode = false;            // 수동 모드 활성화 여부
int currentLEDCount = NUM_LEDS;     // 현재 켜진 LED 개수
int hue = 0;                        // 색상 값 (0~360)
int lastClkState, lastClkState2;    // 로터리 인코더 이전 상태

// 조도 센서 평균값 계산 변수
const int ldrSampleInterval = 5000; // 평균값 계산 간격 (5초)
const int maxSamples = 10;          // 최대 샘플 개수
int ldrValues[maxSamples] = {0};    // 조도 값 배열
int sampleIndex = 0;                // 현재 샘플 인덱스
unsigned long lastLDRSampleTime = 0; // 마지막 샘플 시간

// 함수 선언
void reconnectMQTT();                  // MQTT 재연결
void handleFirstEncoder();             // 첫 번째 로터리 인코더 처리
void handleSecondEncoder();            // 두 번째 로터리 인코더 처리
void lightUpLEDs();                    // LED 상태 갱신
int calculateAverageLDRValue();        // 평균 조도 값 계산
int determineLEDCountFromLDR(int averageLDRValue); // 조도값 기반 LED 개수 결정
void sendToMQTT(int averageLDRValue);  // MQTT로 데이터 전송

void setup() {
    pinMode(LDR_PIN, INPUT);
    pinMode(ENCODER_CLK, INPUT);
    pinMode(ENCODER_DT, INPUT);
    pinMode(ENCODER_SW, INPUT_PULLUP);
    pinMode(ENCODER2_CLK, INPUT);
    pinMode(ENCODER2_DT, INPUT);
    pinMode(ENCODER2_SW, INPUT_PULLUP);

    Serial.begin(115200);
    strip.begin();
    strip.show();
    lastClkState = digitalRead(ENCODER_CLK);
    lastClkState2 = digitalRead(ENCODER2_CLK);

    // WiFi 연결
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");

    // MQTT 브로커 설정
    mqttClient.setServer(mqtt_server, mqtt_port);

    // MQTT 연결 시도
    while (!mqttClient.connected()) {
        Serial.print("Connecting to MQTT...");
        if (mqttClient.connect("ESP32Client")) { // 클라이언트 이름
            Serial.println("connected");
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }

    Serial.println("System Initialized");
}

void loop() {
    unsigned long currentTime = millis();

    // MQTT 연결 유지
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    mqttClient.loop();

    // 수동 모드 전환 버튼 처리
    if (digitalRead(ENCODER_SW) == LOW) {
        manualMode = !manualMode;  // 모드 전환
        delay(500);  // 디바운스 처리
        Serial.println(manualMode ? "Manual Mode Activated" : "Automatic Mode Activated");
    }

    if (manualMode) {
        handleFirstEncoder();  // 수동 모드에서 LED 개수 조절
    } else {
        // 자동 모드: 조도 센서 평균값 기반 LED 제어
        if (currentTime - lastLDRSampleTime >= (ldrSampleInterval / maxSamples)) {
            ldrValues[sampleIndex] = analogRead(LDR_PIN); // 현재 조도 값 저장
            sampleIndex = (sampleIndex + 1) % maxSamples; // 인덱스 순환
            lastLDRSampleTime = currentTime;
        }

        if (currentTime % ldrSampleInterval < 50) {
            int averageLDRValue = calculateAverageLDRValue();
            Serial.print("Average LDR Value: ");
            Serial.println(averageLDRValue);

            currentLEDCount = determineLEDCountFromLDR(averageLDRValue);
            sendToMQTT(averageLDRValue); // MQTT로 조도 평균값 전송
        }
    }

    handleSecondEncoder(); // 색상 변경은 항상 작동
    lightUpLEDs();          // LED 상태 갱신

    delay(50);  // 루프 딜레이
}

// 평균 조도 값 계산
int calculateAverageLDRValue() {
    int sum = 0;
    for (int i = 0; i < maxSamples; i++) {
        sum += ldrValues[i];
    }
    return sum / maxSamples;
}

// 평균값 기반 LED 개수 결정
int determineLEDCountFromLDR(int averageLDRValue) {
    if (averageLDRValue > 3000) {
        return 2;  // 아주 밝으면 2개만 켜기
    } else if (averageLDRValue > 1000) {
        return 4;  // 중간 밝기면 4개 켜기
    } else {
        return NUM_LEDS;  // 어두우면 모두 켜기
    }
}

// MQTT로 조도 평균값 전송
void sendToMQTT(int averageLDRValue) {
    if (mqttClient.connected()) {
        String payload = String(averageLDRValue);
        mqttClient.publish(mqtt_topic, payload.c_str()); // "home/lux" 토픽으로 전송
        Serial.print("MQTT Published: ");
        Serial.println(payload);
    } else {
        Serial.println("MQTT not connected, unable to send data");
    }
}

// MQTT 재연결
void reconnectMQTT() {
    while (!mqttClient.connected()) {
        Serial.print("Reconnecting to MQTT...");
        if (mqttClient.connect("ESP32Client")) {
            Serial.println("connected");
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

// LED 상태 갱신
void lightUpLEDs() {
    uint32_t color = strip.ColorHSV((hue * 65536L) / 360, 255, 255);  // HSV 색상 설정
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, (i < currentLEDCount) ? color : strip.Color(0, 0, 0));  // LED 켜기/끄기
    }
    strip.show();
}

// 첫 번째 로터리 인코더 처리 (LED 개수 조절)
void handleFirstEncoder() {
    int clkState = digitalRead(ENCODER_CLK);
    if (clkState != lastClkState) {
        if (digitalRead(ENCODER_DT) != clkState) {
            currentLEDCount++;  // 시계 방향
        } else {
            currentLEDCount--;  // 반시계 방향
        }
        currentLEDCount = constrain(currentLEDCount, 1, NUM_LEDS);  // 1~8 범위로 제한
        Serial.print("Manual LED Count: ");
        Serial.println(currentLEDCount);
    }
    lastClkState = clkState;  // 이전 상태 업데이트
}

// 두 번째 로터리 인코더 처리 (LED 색상 변경)
void handleSecondEncoder() {
    int clkState2 = digitalRead(ENCODER2_CLK);
    if (clkState2 != lastClkState2) {
        if (digitalRead(ENCODER2_DT) != clkState2) {
            hue += 30;  // 시계 방향 (색상 변화 폭 증가)
        } else {
            hue -= 30;  // 반시계 방향
        }
        hue = (hue + 360) % 360;  // 0~360 범위 유지
        Serial.print("Hue: ");
        Serial.println(hue);
    }
    lastClkState2 = clkState2;
}
