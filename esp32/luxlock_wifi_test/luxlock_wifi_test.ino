#include <WiFi.h>
#include "secrets.h"

static const uint32_t WIFI_RETRY_MS = 5000;
static const uint32_t STATUS_PRINT_MS = 5000;
static const uint32_t WIFI_SCAN_MS = 15000;

uint32_t lastWifiAttemptAt = 0;
uint32_t lastStatusPrintAt = 0;
uint32_t lastWifiScanAt = 0;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;

const char *wifiStatusLabel(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD:
      return "WL_NO_SHIELD";
    case WL_IDLE_STATUS:
      return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
      return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
      return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
      return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
      return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
      return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
      return "WL_DISCONNECTED";
    default:
      return "UNKNOWN";
  }
}

void scanNearbyNetworksIfNeeded() {
  uint32_t now = millis();
  if (lastWifiScanAt != 0 && now - lastWifiScanAt < WIFI_SCAN_MS) {
    return;
  }

  lastWifiScanAt = now;

  Serial.println();
  Serial.println("Scanning nearby Wi-Fi networks...");
  int networkCount = WiFi.scanNetworks();
  if (networkCount <= 0) {
    Serial.println("No Wi-Fi networks found.");
    return;
  }

  bool targetFound = false;
  for (int i = 0; i < networkCount; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    wifi_auth_mode_t encryption = WiFi.encryptionType(i);

    Serial.print("  ");
    Serial.print(i + 1);
    Serial.print(". ");
    Serial.print(ssid);
    Serial.print(" | RSSI ");
    Serial.print(rssi);
    Serial.print(" dBm | encryption ");
    Serial.println(static_cast<int>(encryption));

    if (ssid == WIFI_SSID) {
      targetFound = true;
    }
  }

  if (targetFound) {
    Serial.println("Target SSID is visible to the ESP32.");
  } else {
    Serial.println("Target SSID is NOT visible to the ESP32.");
  }
}

void startWifiConnection() {
  uint32_t now = millis();
  if (lastWifiAttemptAt != 0 && now - lastWifiAttemptAt < WIFI_RETRY_MS) {
    return;
  }

  lastWifiAttemptAt = now;

  Serial.println();
  Serial.println("Starting Wi-Fi connection...");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  Serial.println("Please wait up to 5 seconds for a result...");

  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void printWifiStatus() {
  const wl_status_t status = WiFi.status();

  if (status != lastWifiStatus) {
    lastWifiStatus = status;
    Serial.print("Wi-Fi status changed: ");
    Serial.print(status);
    Serial.print(" (");
    Serial.print(wifiStatusLabel(status));
    Serial.println(")");
  }

  if (status == WL_CONNECTED) {
    Serial.println("Wi-Fi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    return;
  }

  Serial.println("Wi-Fi not connected yet. Waiting before the next retry...");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("LuxLock ESP32 Wi-Fi Test");
  Serial.println("Power the board from USB only for this test.");
  Serial.println("Open Serial Monitor at 115200 baud.");

  startWifiConnection();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    startWifiConnection();
    scanNearbyNetworksIfNeeded();
  }

  uint32_t now = millis();
  if (lastStatusPrintAt == 0 || now - lastStatusPrintAt >= STATUS_PRINT_MS) {
    lastStatusPrintAt = now;
    printWifiStatus();
  }
}
