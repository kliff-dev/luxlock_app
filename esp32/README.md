# LuxLock ESP32 Firmware

This folder contains a full Arduino-style ESP32 sketch for the LuxLock device.

## Assumptions

- Board: ESP32 DevKit / WROOM class board using the Arduino framework
- Firebase library: `Firebase ESP Client` by Mobizt (`Firebase_ESP_Client.h`)
- Fingerprint library: `Adafruit Fingerprint Sensor Library`
- RTC library: `RTClib`
- Fingerprint sensor connected on UART2
- DS3231 on I2C pins `21` and `22`
- BTS7960 motor drivers using:
  - `RPWM`
  - `LPWM`
  - `R_EN`
  - `L_EN`

## Firebase contract

The sketch is built to match the Flutter app:

- Override path: `devices/esp32_1/commands/override`
- Enroll path: `devices/esp32_1/commands/enroll`
- History path: `devices/esp32_1/history`
- Status path: `devices/esp32_1/status`

## Before uploading

Copy `secrets.example.h` to `secrets.h` in the sketch folder, then update:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `FIREBASE_API_KEY`
- `FIREBASE_DATABASE_URL`

Do not commit `secrets.h`; it is ignored by Git.

## Notes

- `open` and `close` are mapped to motor directions in code. If a case moves the wrong way, swap the direction flags or motor pins for that actuator.
- Fingerprint access mappings are stored locally in ESP32 `Preferences`, so the device remembers which enrolled template maps to which access string.
- Fingerprint attempts log to Firebase history using:
  - success: actual access string + `authorized = Yes`
  - failure: `case = N/A` + `authorized = No`
