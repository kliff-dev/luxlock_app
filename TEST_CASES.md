# LuxLock No-Hardware Test Cases

These tests verify software behavior without requiring the ESP32, fingerprint
sensor, actuator drivers, ADXL345 sensors, or buzzer.

## App Tests

Command:

```powershell
flutter test
```

Covered cases:

- App launches and shows the main LuxLock screen.
- ESP32 status data is parsed correctly from Firebase/local status payloads.
- Direct local control fields are detected from both current and legacy status
  formats.
- Finished enrollment states such as `success`, `failed`, `canceled`, and
  `wipe_success` do not keep registration blocked.
- Registered fingerprint access masks correctly map access to Case 1, Case 2,
  and Case 3.
- Active actuator state is detected from status payloads.
- Stale ESP32 status is treated as not responsive.
- Invalid status data falls back safely instead of crashing the app.

## Cloud Function Tests

Command:

```powershell
cd functions
npm test
```

Covered cases:

- `Tamper L1` creates a buzzer notification.
- Tamper logs below the configured sensitivity level are ignored.
- Failed fingerprint logs create a denied-access notification.
- Normal successful access and override logs do not create notifications.
- Tamper level parsing works for short and long labels.
- Fingerprint failure matching avoids false positives.
- Notification cooldown blocks repeated alerts inside the cooldown window.

## Static Checks

Commands:

```powershell
flutter analyze
cd functions
npm run lint
```

Covered cases:

- Flutter/Dart analyzer reports no app issues.
- Cloud Function JavaScript parses correctly before deployment.

## Hardware Still Required

These cannot be fully proven by no-hardware tests:

- Actual fingerprint matching reliability.
- Actuator movement timing under real load.
- ADXL345 vibration sensitivity and false positives.
- Buzzer/transistor wiring behavior.
- ESP32 Wi-Fi stability on the phone hotspot.
- Firebase/FCM delivery on the real phone while the app is closed.
