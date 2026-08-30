#include <Arduino.h>

static const uint8_t TEST_LED_PIN = 2;
static const uint32_t HEARTBEAT_MS = 1000;

uint32_t lastBeatAt = 0;
bool ledState = false;

void setup() {
  pinMode(TEST_LED_PIN, OUTPUT);
  digitalWrite(TEST_LED_PIN, LOW);

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("LuxLock ESP32 Alive Test");
  Serial.println("If you see this, setup() is running.");
  Serial.println("LED on GPIO 2 will toggle every second.");
}

void loop() {
  uint32_t now = millis();
  if (lastBeatAt == 0 || now - lastBeatAt >= HEARTBEAT_MS) {
    lastBeatAt = now;
    ledState = !ledState;
    digitalWrite(TEST_LED_PIN, ledState ? HIGH : LOW);

    Serial.print("alive | millis=");
    Serial.print(now);
    Serial.print(" | led=");
    Serial.println(ledState ? "ON" : "OFF");
  }
}
