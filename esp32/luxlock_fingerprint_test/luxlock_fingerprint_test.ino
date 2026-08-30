#include <Arduino.h>
#include <Adafruit_Fingerprint.h>
#include <WiFi.h>
#include "secrets.h"

static const int FP_RX_PIN = 16;
static const int FP_TX_PIN = 17;
static const uint32_t SCAN_COOLDOWN_MS = 1500;
static const uint32_t RELEASE_TIMEOUT_MS = 5000;
static const uint32_t WIFI_RETRY_MS = 10000;
static const uint32_t WIFI_STATUS_PRINT_MS = 10000;
static const wifi_power_t WIFI_TX_POWER = WIFI_POWER_11dBm;

HardwareSerial fingerprintSerial(2);
Adafruit_Fingerprint finger(&fingerprintSerial);

uint32_t lastScanAt = 0;
uint32_t lastWifiRetryAt = 0;
uint32_t lastWifiStatusPrintAt = 0;
bool lastReady = false;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;

const char *wifiStatusLabel(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    default:
      return "UNKNOWN";
  }
}

void printHelp() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  h = show this help");
  Serial.println("  i = sensor info / template count");
  Serial.println("  l = LED on");
  Serial.println("  o = LED off");
  Serial.println("  s = scan once for a stored fingerprint");
  Serial.println("  w = show Wi-Fi status");
  Serial.println("Continuous ready mode is ON.");
}

void printWifiStatus() {
  wl_status_t status = WiFi.status();
  Serial.print("Wi-Fi status: ");
  Serial.println(wifiStatusLabel(status));

  if (status == WL_CONNECTED) {
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  }
}

void connectWifiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  uint32_t now = millis();
  if (lastWifiRetryAt != 0 && now - lastWifiRetryAt < WIFI_RETRY_MS) {
    return;
  }

  lastWifiRetryAt = now;
  Serial.println("Starting Wi-Fi connection...");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_TX_POWER);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void monitorWifiState() {
  wl_status_t status = WiFi.status();
  if (status != lastWifiStatus) {
    lastWifiStatus = status;
    Serial.print("Wi-Fi status changed: ");
    Serial.println(wifiStatusLabel(status));
    if (status == WL_CONNECTED) {
      printWifiStatus();
    }
  }

  uint32_t now = millis();
  if (status == WL_CONNECTED &&
      (lastWifiStatusPrintAt == 0 ||
       now - lastWifiStatusPrintAt >= WIFI_STATUS_PRINT_MS)) {
    lastWifiStatusPrintAt = now;
    printWifiStatus();
  }
}

void printSensorInfo() {
  if (!lastReady) {
    Serial.println("Fingerprint sensor is not ready.");
    return;
  }

  finger.getTemplateCount();
  Serial.println("Fingerprint sensor detected.");
  Serial.print("Stored templates: ");
  Serial.println(finger.templateCount);
}

void setSensorLed(bool on) {
  if (!lastReady) {
    Serial.println("Cannot change LED because the sensor is not ready.");
    return;
  }

  uint8_t result = finger.LEDcontrol(on);
  if (result == FINGERPRINT_OK) {
    Serial.print("Fingerprint LED ");
    Serial.println(on ? "ON" : "OFF");
  } else {
    Serial.print("LED command failed with code: ");
    Serial.println(result);
    Serial.println("Your sensor may not support software LED control.");
  }
}

void checkFingerprint() {
  fingerprintSerial.begin(57600, SERIAL_8N1, FP_RX_PIN, FP_TX_PIN);
  finger.begin(57600);
  delay(100);

  bool ready = finger.verifyPassword();
  if (ready) {
    printSensorInfo();
  } else {
    Serial.println("Fingerprint sensor NOT detected.");
    Serial.println("Check VCC, GND, TX->GPIO16, RX->GPIO17.");
  }

  lastReady = ready;
}

void waitForFingerRelease() {
  uint32_t startedAt = millis();
  while (millis() - startedAt < RELEASE_TIMEOUT_MS) {
    if (finger.getImage() == FINGERPRINT_NOFINGER) {
      return;
    }
    delay(50);
  }
}

void scanFingerprintOnce(bool verbose = true) {
  if (!lastReady) {
    Serial.println("Cannot scan because the sensor is not ready.");
    return;
  }

  if (verbose) {
    Serial.println("Waiting for finger...");
  }

  uint8_t result = finger.getImage();
  if (result == FINGERPRINT_NOFINGER) {
    if (verbose) {
      Serial.println("No finger detected.");
    }
    return;
  }

  if (result != FINGERPRINT_OK) {
    Serial.print("getImage failed with code: ");
    Serial.println(result);
    lastScanAt = millis();
    return;
  }

  result = finger.image2Tz();
  if (result != FINGERPRINT_OK) {
    Serial.print("image2Tz failed with code: ");
    Serial.println(result);
    lastScanAt = millis();
    return;
  }

  result = finger.fingerFastSearch();
  if (result == FINGERPRINT_OK) {
    Serial.print("Matched fingerprint ID: ");
    Serial.println(finger.fingerID);
    Serial.print("Match confidence: ");
    Serial.println(finger.confidence);
    lastScanAt = millis();
    waitForFingerRelease();
    return;
  }

  if (result == FINGERPRINT_NOTFOUND) {
    Serial.println("Finger detected, but no stored template matched.");
    lastScanAt = millis();
    waitForFingerRelease();
    return;
  }

  Serial.print("fingerFastSearch failed with code: ");
  Serial.println(result);
  lastScanAt = millis();
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  char command = static_cast<char>(Serial.read());
  switch (command) {
    case 'h':
    case 'H':
      printHelp();
      break;
    case 'i':
    case 'I':
      printSensorInfo();
      break;
    case 'l':
    case 'L':
      setSensorLed(true);
      break;
    case 'o':
    case 'O':
      setSensorLed(false);
      break;
    case 's':
    case 'S':
      scanFingerprintOnce(true);
      lastScanAt = millis();
      break;
    case 'w':
    case 'W':
      printWifiStatus();
      break;
    case '\r':
    case '\n':
      break;
    default:
      Serial.print("Unknown command: ");
      Serial.println(command);
      printHelp();
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("LuxLock Fingerprint Test");
  Serial.println("Connect only ESP32 + fingerprint sensor.");
  Serial.println("Power the ESP32 from USB only for this test.");
  Serial.println("Fingerprint wiring:");
  Serial.println("  Sensor TX -> ESP32 GPIO 16");
  Serial.println("  Sensor RX -> ESP32 GPIO 17");
  Serial.println("  Sensor VCC -> 5V");
  Serial.println("  Sensor GND -> GND");
  Serial.println("Wi-Fi is enabled in this test.");
  Serial.println("The sensor stays in continuous ready mode in this test.");
  Serial.println("Type h then press Enter for commands.");

  checkFingerprint();
  connectWifiIfNeeded();
  printHelp();
}

void loop() {
  handleSerialCommand();
  connectWifiIfNeeded();
  monitorWifiState();

  if (!lastReady) {
    return;
  }

  if (millis() - lastScanAt < SCAN_COOLDOWN_MS) {
    return;
  }

  scanFingerprintOnce(false);
}
