#include <Arduino.h>
#include <Adafruit_Fingerprint.h>
#include <Preferences.h>

static const int FP_RX_PIN = 16;
static const int FP_TX_PIN = 17;
static const uint16_t FINGERPRINT_MAX_TEMPLATES = 127;

HardwareSerial fingerprintSerial(2);
Adafruit_Fingerprint finger(&fingerprintSerial);
Preferences prefs;

String prefsKeyForFinger(uint16_t fingerId) {
  return "fp_" + String(fingerId);
}

void wipeLuxLockMappings() {
  prefs.begin("luxlock", false);

  uint16_t removedCount = 0;
  for (uint16_t id = 1; id <= FINGERPRINT_MAX_TEMPLATES; id++) {
    const String key = prefsKeyForFinger(id);
    if (prefs.isKey(key.c_str())) {
      prefs.remove(key.c_str());
      removedCount++;
    }
  }

  prefs.end();

  Serial.print("Removed LuxLock fingerprint mappings: ");
  Serial.println(removedCount);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("LuxLock Fingerprint Wipe");
  Serial.println("This will clear all templates from the fingerprint sensor.");
  Serial.println("It will also clear saved LuxLock fingerprint access mappings.");
  Serial.println("Power the ESP32 from USB only for this wipe.");

  fingerprintSerial.begin(57600, SERIAL_8N1, FP_RX_PIN, FP_TX_PIN);
  finger.begin(57600);
  delay(100);

  if (!finger.verifyPassword()) {
    Serial.println("Fingerprint sensor not detected or password mismatch.");
    Serial.println("Nothing was deleted.");
    return;
  }

  finger.getTemplateCount();
  Serial.print("Templates before wipe: ");
  Serial.println(finger.templateCount);

  uint8_t result = finger.emptyDatabase();
  if (result == FINGERPRINT_OK) {
    Serial.println("Fingerprint sensor database cleared.");
  } else {
    Serial.print("Failed to clear fingerprint sensor database. Code: ");
    Serial.println(result);
  }

  finger.getTemplateCount();
  Serial.print("Templates after wipe: ");
  Serial.println(finger.templateCount);

  wipeLuxLockMappings();
  Serial.println("Wipe complete.");
}

void loop() {
}
