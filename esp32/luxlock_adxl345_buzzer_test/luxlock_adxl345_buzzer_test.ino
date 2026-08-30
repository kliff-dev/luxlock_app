#include <Wire.h>
#include <math.h>

// ---------------------------------------------------------------------------
// LuxLock single ADXL345 + active buzzer test
// ---------------------------------------------------------------------------
//
// Upload this to an ESP32 when you want to test:
//   1) one ADXL345 vibration sensor
//   2) one active buzzer
//   3) one 2N2222 transistor driver for the buzzer
//
// This sketch does not use Wi-Fi, Firebase, fingerprint, or actuators.
//
// Serial Monitor:
//   Baud: 115200
//   Line ending: New Line
//
// Commands:
//   h = show help
//   i = sensor info
//   s = scan I2C bus
//   d = detect sensor again
//   r = read sensor once
//   l = toggle live readings
//   c = recalibrate baseline
//   b = beep buzzer once
//   a = toggle auto buzzer on vibration events
//   0 = buzzer off
//   1 = buzzer on
//   2 = set auto buzzer trigger to level 2
//   3 = set auto buzzer trigger to level 3
//   4 = set auto buzzer trigger to level 4
//   5 = set auto buzzer trigger to level 5
//
// ADXL345 wiring:
//   VCC -> ESP32 3.3V
//   GND -> ESP32 GND
//   SDA -> ESP32 GPIO21
//   SCL -> ESP32 GPIO22
//   SDO/ALT -> GND for address 0x53, or 3.3V for address 0x1D
//   CS -> optional if your module already works in I2C mode without it;
//         otherwise connect CS to 3.3V.
//
// Active buzzer + 2N2222 wiring:
//   ESP32 GPIO27 -> 1k resistor -> 2N2222 base
//   2N2222 emitter -> GND
//   2N2222 collector -> buzzer negative
//   Active buzzer positive -> 5V
//   Active buzzer negative -> 2N2222 collector
//   Buzzer GND / transistor GND must share ESP32 GND
//
// GPIO27 HIGH turns the transistor on and powers the buzzer.

static const uint32_t SERIAL_BAUD = 115200;

static const int SDA_PIN = 21;
static const int SCL_PIN = 22;
static const int BUZZER_PIN = 27;
static const bool BUZZER_ACTIVE_HIGH = true;

static const uint32_t LIVE_PRINT_MS = 500;
static const uint32_t ALIVE_PRINT_MS = 3000;
static const uint32_t SENSOR_BASELINE_SAMPLES = 80;
static const uint32_t EVENT_COOLDOWN_MS = 1500;
static const uint32_t BUZZER_BEEP_MS = 500;

// Same thresholds used in the LuxLock tamper tests.
static const float LEVEL_1_G = 0.35f;
static const float LEVEL_2_G = 0.75f;
static const float LEVEL_3_G = 1.25f;
static const float LEVEL_4_G = 2.00f;
static const float LEVEL_5_G = 3.00f;

static const uint8_t ADXL345_DEVID = 0x00;
static const uint8_t ADXL345_BW_RATE = 0x2C;
static const uint8_t ADXL345_POWER_CTL = 0x2D;
static const uint8_t ADXL345_DATA_FORMAT = 0x31;
static const uint8_t ADXL345_DATAX0 = 0x32;
static const uint8_t ADXL345_EXPECTED_DEVID = 0xE5;
static const float ADXL345_FULL_RES_G_PER_LSB = 0.0039f;

struct SensorConfig {
  const char *name;
  uint8_t address;
  bool ready;
  float baselineG;
  uint8_t lastEventLevel;
  uint32_t lastEventAt;
};

SensorConfig sensor = {"Test Sensor", 0x53, false, 1.0f, 0, 0};

bool liveReadingsEnabled = false;
bool autoBuzzerEnabled = true;
uint8_t buzzerTriggerLevel = 3;
uint32_t lastLivePrintAt = 0;
uint32_t lastAlivePrintAt = 0;
uint32_t buzzerOffAt = 0;

void setBuzzer(bool on) {
  digitalWrite(
      BUZZER_PIN,
      on == BUZZER_ACTIVE_HIGH ? HIGH : LOW);
}

void beepBuzzer(uint32_t durationMs = BUZZER_BEEP_MS) {
  setBuzzer(true);
  buzzerOffAt = millis() + durationMs;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegisters(
    uint8_t address,
    uint8_t startRegister,
    uint8_t *buffer,
    size_t length) {
  Wire.beginTransmission(address);
  Wire.write(startRegister);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t bytesRead = Wire.requestFrom(
      static_cast<int>(address),
      static_cast<int>(length));
  if (bytesRead != length) {
    return false;
  }

  for (size_t i = 0; i < length; i++) {
    buffer[i] = Wire.read();
  }

  return true;
}

bool readAccelerationG(float &x, float &y, float &z) {
  if (!sensor.ready) {
    return false;
  }

  uint8_t buffer[6] = {0};
  if (!readRegisters(sensor.address, ADXL345_DATAX0, buffer, 6)) {
    return false;
  }

  const int16_t rawX = static_cast<int16_t>((buffer[1] << 8) | buffer[0]);
  const int16_t rawY = static_cast<int16_t>((buffer[3] << 8) | buffer[2]);
  const int16_t rawZ = static_cast<int16_t>((buffer[5] << 8) | buffer[4]);

  x = rawX * ADXL345_FULL_RES_G_PER_LSB;
  y = rawY * ADXL345_FULL_RES_G_PER_LSB;
  z = rawZ * ADXL345_FULL_RES_G_PER_LSB;
  return true;
}

float accelerationMagnitude(float x, float y, float z) {
  return sqrtf((x * x) + (y * y) + (z * z));
}

uint8_t shockLevelForDelta(float deltaG) {
  if (deltaG >= LEVEL_5_G) return 5;
  if (deltaG >= LEVEL_4_G) return 4;
  if (deltaG >= LEVEL_3_G) return 3;
  if (deltaG >= LEVEL_2_G) return 2;
  if (deltaG >= LEVEL_1_G) return 1;
  return 0;
}

bool addressLooksLikeAdxl345(uint8_t address) {
  uint8_t deviceId = 0;
  if (!readRegisters(address, ADXL345_DEVID, &deviceId, 1)) {
    return false;
  }

  return deviceId == ADXL345_EXPECTED_DEVID;
}

bool initAdxl345() {
  uint8_t detectedAddress = 0;
  if (addressLooksLikeAdxl345(0x53)) {
    detectedAddress = 0x53;
  } else if (addressLooksLikeAdxl345(0x1D)) {
    detectedAddress = 0x1D;
  } else {
    Serial.println("Test Sensor: no ADXL345 found at 0x53 or 0x1D");
    return false;
  }

  sensor.address = detectedAddress;

  if (!writeRegister(sensor.address, ADXL345_BW_RATE, 0x0B)) {
    Serial.println("Test Sensor: failed to set data rate");
    return false;
  }

  if (!writeRegister(sensor.address, ADXL345_DATA_FORMAT, 0x0B)) {
    Serial.println("Test Sensor: failed to set data format");
    return false;
  }

  if (!writeRegister(sensor.address, ADXL345_POWER_CTL, 0x08)) {
    Serial.println("Test Sensor: failed to enter measurement mode");
    return false;
  }

  return true;
}

void calibrateSensor() {
  if (!sensor.ready) {
    Serial.println("Test Sensor: skipped calibration because sensor is offline");
    return;
  }

  float totalMagnitude = 0.0f;
  uint32_t samples = 0;

  Serial.println("Test Sensor: calibrating, keep the sensor still...");
  for (uint32_t i = 0; i < SENSOR_BASELINE_SAMPLES; i++) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (readAccelerationG(x, y, z)) {
      totalMagnitude += accelerationMagnitude(x, y, z);
      samples++;
    }
    delay(10);
  }

  if (samples > 0) {
    sensor.baselineG = totalMagnitude / samples;
  }

  Serial.printf(
      "Test Sensor: baseline %.3fg using %lu samples\n",
      sensor.baselineG,
      static_cast<unsigned long>(samples));
}

void scanI2CBus() {
  uint8_t found = 0;

  Serial.println();
  Serial.printf("I2C scan GPIO%d/GPIO%d:", SDA_PIN, SCL_PIN);
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", address);
      found++;
    }
  }

  if (found == 0) {
    Serial.print(" no devices found");
  }

  Serial.println();
  Serial.println("Expected ADXL345 address: 0x53 or 0x1D.");
}

void detectSensor(bool calibrateReady) {
  Serial.println();
  Serial.println("Detecting one ADXL345 sensor...");

  sensor.ready = initAdxl345();
  sensor.lastEventLevel = 0;
  sensor.lastEventAt = 0;

  Serial.printf(
      "Test Sensor at 0x%02X: %s\n",
      sensor.address,
      sensor.ready ? "READY" : "OFFLINE");

  if (calibrateReady) {
    calibrateSensor();
  }
}

void printHelp() {
  Serial.println();
  Serial.println("LuxLock ADXL345 + Buzzer Test Commands");
  Serial.println("  h = show this help");
  Serial.println("  i = sensor info");
  Serial.println("  s = scan I2C bus");
  Serial.println("  d = detect sensor again");
  Serial.println("  r = read sensor once");
  Serial.println("  l = toggle live readings");
  Serial.println("  c = recalibrate baseline");
  Serial.println("  b = beep buzzer once");
  Serial.println("  a = toggle auto buzzer on events");
  Serial.println("  0 = buzzer off");
  Serial.println("  1 = buzzer on");
  Serial.println("  2/3/4/5 = set auto buzzer trigger level");
  Serial.println();
  Serial.println("Shock levels:");
  Serial.printf("  L1 >= %.2fg\n", LEVEL_1_G);
  Serial.printf("  L2 >= %.2fg\n", LEVEL_2_G);
  Serial.printf("  L3 >= %.2fg\n", LEVEL_3_G);
  Serial.printf("  L4 >= %.2fg\n", LEVEL_4_G);
  Serial.printf("  L5 >= %.2fg\n", LEVEL_5_G);
  Serial.printf("Auto buzzer: %s, trigger level: %u\n",
                autoBuzzerEnabled ? "ON" : "OFF",
                buzzerTriggerLevel);
  Serial.println();
}

void printSensorInfo() {
  Serial.println();
  Serial.println("Sensor status:");
  Serial.printf(
      "  Test Sensor: %s, address=0x%02X, baseline=%.3fg\n",
      sensor.ready ? "READY" : "OFFLINE",
      sensor.address,
      sensor.baselineG);
  Serial.printf(
      "  Auto buzzer: %s, trigger level: %u\n",
      autoBuzzerEnabled ? "ON" : "OFF",
      buzzerTriggerLevel);
}

void printAliveStatus() {
  Serial.printf(
      "ADXL345 + buzzer test alive. Ready sensors: %u/1. Auto buzzer %s at level %u.\n",
      sensor.ready ? 1 : 0,
      autoBuzzerEnabled ? "ON" : "OFF",
      buzzerTriggerLevel);
}

void printSensorReading() {
  if (!sensor.ready) {
    Serial.println("Test Sensor: OFFLINE");
    return;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!readAccelerationG(x, y, z)) {
    Serial.println("Test Sensor: read failed");
    return;
  }

  const float magnitude = accelerationMagnitude(x, y, z);
  const float deltaG = fabsf(magnitude - sensor.baselineG);
  const uint8_t level = shockLevelForDelta(deltaG);

  Serial.printf(
      "Test Sensor: x=% .2fg y=% .2fg z=% .2fg mag=%.2fg delta=%.2fg level=%u\n",
      x,
      y,
      z,
      magnitude,
      deltaG,
      level);
}

void serviceBuzzer() {
  if (buzzerOffAt != 0 &&
      static_cast<int32_t>(millis() - buzzerOffAt) >= 0) {
    setBuzzer(false);
    buzzerOffAt = 0;
  }
}

void detectEvent() {
  if (!sensor.ready) {
    return;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!readAccelerationG(x, y, z)) {
    return;
  }

  const float magnitude = accelerationMagnitude(x, y, z);
  const float deltaG = fabsf(magnitude - sensor.baselineG);
  const uint8_t level = shockLevelForDelta(deltaG);
  if (level == 0) {
    sensor.lastEventLevel = 0;
    return;
  }

  const uint32_t now = millis();
  const bool cooldownDone = now - sensor.lastEventAt >= EVENT_COOLDOWN_MS;
  const bool levelIncreased = level > sensor.lastEventLevel;
  if (!cooldownDone && !levelIncreased) {
    return;
  }

  sensor.lastEventAt = now;
  sensor.lastEventLevel = level;

  Serial.printf(
      "EVENT: Test Sensor shock level %u, delta=%.2fg\n",
      level,
      deltaG);

  if (autoBuzzerEnabled && level >= buzzerTriggerLevel) {
    Serial.printf(
        "EVENT: buzzer triggered at level %u for %lu ms\n",
        level,
        static_cast<unsigned long>(BUZZER_BEEP_MS));
    beepBuzzer();
  }
}

void setBuzzerTriggerLevel(uint8_t level) {
  if (level < 2) {
    level = 2;
  }
  if (level > 5) {
    level = 5;
  }

  buzzerTriggerLevel = level;
  Serial.printf("Auto buzzer trigger level set to %u.\n", buzzerTriggerLevel);
}

void handleSerialCommand(char command) {
  switch (command) {
    case '\r':
    case '\n':
      return;
    case 'h':
    case 'H':
      printHelp();
      break;
    case 'i':
    case 'I':
      printSensorInfo();
      break;
    case 's':
    case 'S':
      scanI2CBus();
      break;
    case 'd':
    case 'D':
      detectSensor(true);
      printSensorInfo();
      break;
    case 'r':
    case 'R':
      printSensorReading();
      break;
    case 'l':
    case 'L':
      liveReadingsEnabled = !liveReadingsEnabled;
      Serial.printf("Live readings %s.\n", liveReadingsEnabled ? "ON" : "OFF");
      break;
    case 'c':
    case 'C':
      calibrateSensor();
      break;
    case 'b':
    case 'B':
      Serial.println("Manual buzzer beep.");
      beepBuzzer();
      break;
    case 'a':
    case 'A':
      autoBuzzerEnabled = !autoBuzzerEnabled;
      Serial.printf("Auto buzzer %s.\n", autoBuzzerEnabled ? "ON" : "OFF");
      break;
    case '0':
      buzzerOffAt = 0;
      setBuzzer(false);
      Serial.println("Buzzer OFF.");
      break;
    case '1':
      buzzerOffAt = 0;
      setBuzzer(true);
      Serial.println("Buzzer ON.");
      break;
    case '2':
    case '3':
    case '4':
    case '5':
      setBuzzerTriggerLevel(static_cast<uint8_t>(command - '0'));
      break;
    default:
      Serial.printf("Unknown command '%c'. Type h for help.\n", command);
      break;
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  pinMode(BUZZER_PIN, OUTPUT);
  setBuzzer(false);

  Serial.println();
  Serial.println("LuxLock ADXL345 + active buzzer test booting...");
  Serial.println("No Wi-Fi. No Firebase. One ADXL345 and one transistor-driven buzzer.");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  Wire.setTimeOut(50);

  scanI2CBus();
  detectSensor(true);
  printSensorInfo();
  printAliveStatus();
  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    handleSerialCommand(static_cast<char>(Serial.read()));
  }

  const uint32_t now = millis();

  if (now - lastAlivePrintAt >= ALIVE_PRINT_MS) {
    lastAlivePrintAt = now;
    printAliveStatus();
  }

  if (liveReadingsEnabled && now - lastLivePrintAt >= LIVE_PRINT_MS) {
    lastLivePrintAt = now;
    printSensorReading();
  }

  detectEvent();
  serviceBuzzer();
  delay(20);
}
