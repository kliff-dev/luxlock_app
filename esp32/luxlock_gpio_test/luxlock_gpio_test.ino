#include <Arduino.h>

static const uint8_t TEST_OUTPUT_PINS[] = {2, 4, 5, 13, 14, 18, 19, 23, 25, 26, 27, 32, 33};
static const uint32_t STEP_MS = 1000;

uint32_t lastStepAt = 0;
size_t currentIndex = 0;
bool pinHigh = false;

void setAllPinsLow() {
  for (size_t i = 0; i < sizeof(TEST_OUTPUT_PINS) / sizeof(TEST_OUTPUT_PINS[0]); i++) {
    digitalWrite(TEST_OUTPUT_PINS[i], LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("LuxLock ESP32 GPIO Test");
  Serial.println("This test does not use Wi-Fi, Firebase, or peripherals.");
  Serial.println("It toggles one output pin at a time every second.");

  for (size_t i = 0; i < sizeof(TEST_OUTPUT_PINS) / sizeof(TEST_OUTPUT_PINS[0]); i++) {
    pinMode(TEST_OUTPUT_PINS[i], OUTPUT);
    digitalWrite(TEST_OUTPUT_PINS[i], LOW);
  }
}

void loop() {
  uint32_t now = millis();
  if (lastStepAt != 0 && now - lastStepAt < STEP_MS) {
    return;
  }

  lastStepAt = now;

  if (!pinHigh) {
    setAllPinsLow();
    uint8_t pin = TEST_OUTPUT_PINS[currentIndex];
    digitalWrite(pin, HIGH);
    Serial.print("Pin HIGH: GPIO ");
    Serial.println(pin);
    pinHigh = true;
    return;
  }

  uint8_t pin = TEST_OUTPUT_PINS[currentIndex];
  digitalWrite(pin, LOW);
  Serial.print("Pin LOW: GPIO ");
  Serial.println(pin);
  pinHigh = false;
  currentIndex = (currentIndex + 1) % (sizeof(TEST_OUTPUT_PINS) / sizeof(TEST_OUTPUT_PINS[0]));
}
