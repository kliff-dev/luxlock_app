#include <Wire.h>
#include <math.h>

// ---------------------------------------------------------------------------
// LuxLock Tamper Hardware Test
// ---------------------------------------------------------------------------
//
// Upload this only to the tamper ESP32 while testing the ADXL345 vibration
// sensors and buzzer. This sketch does not use Wi-Fi or Firebase.
//
// Serial Monitor:
//   Baud: 115200
//   Line ending: New Line
//
// Commands:
//   h = show help
//   i = sensor info
//   s = scan I2C buses
//   d = detect sensors again
//   r = read sensors once
//   l = toggle live readings
//   c = recalibrate baselines
//   b = beep buzzer once
//   0 = buzzer off
//   1 = buzzer on
//
// ADXL345 wiring plan:
//   Case 1: SDA GPIO21, SCL GPIO22, address 0x53, SDO/ALT -> GND
//   Case 2: SDA GPIO25, SCL GPIO26, address 0x53, SDO/ALT -> GND
//   Case 3: SDA GPIO32, SCL GPIO33, address 0x53, SDO/ALT -> GND
//
// Buzzer:
//   Signal -> GPIO27
//   GND    -> GND

static const uint32_t SERIAL_BAUD = 115200;

static const int BUS_A_SDA_PIN = 21;
static const int BUS_A_SCL_PIN = 22;
static const int BUS_B_SDA_PIN = 25;
static const int BUS_B_SCL_PIN = 26;
static const int BUS_C_SDA_PIN = 32;
static const int BUS_C_SCL_PIN = 33;

static const int BUZZER_PIN = 27;
static const bool BUZZER_ACTIVE_HIGH = true;

static const uint32_t LIVE_PRINT_MS = 500;
static const uint32_t ALIVE_PRINT_MS = 3000;
static const uint32_t SENSOR_BASELINE_SAMPLES = 80;
static const uint32_t EVENT_COOLDOWN_MS = 1500;
static const uint32_t BUZZER_BEEP_MS = 500;
static const uint8_t BUZZER_TRIGGER_LEVEL = 1;

// These are test thresholds. Tune after the sensors are mounted on the cases.
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

TwoWire busA = TwoWire(0);
TwoWire busB = TwoWire(1);

enum SensorBusType {
  SENSOR_BUS_HARDWARE,
  SENSOR_BUS_SOFTWARE,
};

struct SensorConfig {
  const char *name;
  uint8_t caseNumber;
  SensorBusType busType;
  TwoWire *bus;
  int sdaPin;
  int sclPin;
  uint8_t address;
  bool ready;
  float baselineG;
  uint8_t lastEventLevel;
  uint32_t lastEventAt;
};

SensorConfig sensors[] = {
    {"Case 1", 1, SENSOR_BUS_HARDWARE, &busA, BUS_A_SDA_PIN, BUS_A_SCL_PIN, 0x53, false, 1.0f, 0, 0},
    {"Case 2", 2, SENSOR_BUS_HARDWARE, &busB, BUS_B_SDA_PIN, BUS_B_SCL_PIN, 0x53, false, 1.0f, 0, 0},
    {"Case 3", 3, SENSOR_BUS_SOFTWARE, nullptr, BUS_C_SDA_PIN, BUS_C_SCL_PIN, 0x53, false, 1.0f, 0, 0},
};

bool liveReadingsEnabled = false;
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

void softI2cDelay() {
  delayMicroseconds(5);
}

void softI2cRelease(int pin) {
  pinMode(pin, INPUT_PULLUP);
}

void softI2cLow(int pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void softI2cStart(const SensorConfig &sensor) {
  softI2cRelease(sensor.sdaPin);
  softI2cRelease(sensor.sclPin);
  softI2cDelay();
  softI2cLow(sensor.sdaPin);
  softI2cDelay();
  softI2cLow(sensor.sclPin);
}

void softI2cStop(const SensorConfig &sensor) {
  softI2cLow(sensor.sdaPin);
  softI2cRelease(sensor.sclPin);
  softI2cDelay();
  softI2cRelease(sensor.sdaPin);
  softI2cDelay();
}

bool softI2cWriteByte(const SensorConfig &sensor, uint8_t value) {
  for (uint8_t bit = 0; bit < 8; bit++) {
    if ((value & 0x80) != 0) {
      softI2cRelease(sensor.sdaPin);
    } else {
      softI2cLow(sensor.sdaPin);
    }

    softI2cRelease(sensor.sclPin);
    softI2cDelay();
    softI2cLow(sensor.sclPin);
    softI2cDelay();
    value <<= 1;
  }

  softI2cRelease(sensor.sdaPin);
  softI2cRelease(sensor.sclPin);
  softI2cDelay();
  const bool ack = digitalRead(sensor.sdaPin) == LOW;
  softI2cLow(sensor.sclPin);
  softI2cDelay();
  return ack;
}

uint8_t softI2cReadByte(const SensorConfig &sensor, bool ack) {
  uint8_t value = 0;
  softI2cRelease(sensor.sdaPin);

  for (uint8_t bit = 0; bit < 8; bit++) {
    value <<= 1;
    softI2cRelease(sensor.sclPin);
    softI2cDelay();
    if (digitalRead(sensor.sdaPin) == HIGH) {
      value |= 0x01;
    }
    softI2cLow(sensor.sclPin);
    softI2cDelay();
  }

  if (ack) {
    softI2cLow(sensor.sdaPin);
  } else {
    softI2cRelease(sensor.sdaPin);
  }
  softI2cRelease(sensor.sclPin);
  softI2cDelay();
  softI2cLow(sensor.sclPin);
  softI2cRelease(sensor.sdaPin);
  softI2cDelay();

  return value;
}

bool writeRegister(TwoWire &bus, uint8_t address, uint8_t reg, uint8_t value) {
  bus.beginTransmission(address);
  bus.write(reg);
  bus.write(value);
  return bus.endTransmission() == 0;
}

bool writeRegister(SensorConfig &sensor, uint8_t reg, uint8_t value) {
  if (sensor.busType == SENSOR_BUS_HARDWARE) {
    return writeRegister(*sensor.bus, sensor.address, reg, value);
  }

  softI2cStart(sensor);
  bool ok = softI2cWriteByte(sensor, (sensor.address << 1) | 0);
  ok = ok && softI2cWriteByte(sensor, reg);
  ok = ok && softI2cWriteByte(sensor, value);
  softI2cStop(sensor);
  return ok;
}

bool readRegisters(
    TwoWire &bus,
    uint8_t address,
    uint8_t startRegister,
    uint8_t *buffer,
    size_t length) {
  bus.beginTransmission(address);
  bus.write(startRegister);
  if (bus.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = bus.requestFrom(
      static_cast<int>(address),
      static_cast<int>(length));
  if (received != length) {
    return false;
  }

  for (size_t i = 0; i < length; i++) {
    buffer[i] = bus.read();
  }

  return true;
}

bool readRegisters(
    SensorConfig &sensor,
    uint8_t startRegister,
    uint8_t *buffer,
    size_t length) {
  if (sensor.busType == SENSOR_BUS_HARDWARE) {
    return readRegisters(*sensor.bus, sensor.address, startRegister, buffer, length);
  }

  softI2cStart(sensor);
  bool ok = softI2cWriteByte(sensor, (sensor.address << 1) | 0);
  ok = ok && softI2cWriteByte(sensor, startRegister);
  if (!ok) {
    softI2cStop(sensor);
    return false;
  }

  softI2cStart(sensor);
  ok = softI2cWriteByte(sensor, (sensor.address << 1) | 1);
  if (!ok) {
    softI2cStop(sensor);
    return false;
  }

  for (size_t i = 0; i < length; i++) {
    buffer[i] = softI2cReadByte(sensor, i + 1 < length);
  }

  softI2cStop(sensor);
  return true;
}

bool readAccelerationG(SensorConfig &sensor, float &x, float &y, float &z) {
  uint8_t buffer[6] = {};
  if (!readRegisters(sensor, ADXL345_DATAX0, buffer, 6)) {
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

bool initAdxl345(SensorConfig &sensor) {
  uint8_t deviceId = 0;
  if (!readRegisters(sensor, ADXL345_DEVID, &deviceId, 1)) {
    Serial.printf(
        "%s: no response at address 0x%02X\n",
        sensor.name,
        sensor.address);
    return false;
  }

  if (deviceId != ADXL345_EXPECTED_DEVID) {
    Serial.printf(
        "%s: unexpected device ID 0x%02X at 0x%02X\n",
        sensor.name,
        deviceId,
        sensor.address);
    return false;
  }

  if (!writeRegister(sensor, ADXL345_BW_RATE, 0x0B)) {
    Serial.printf("%s: failed to set data rate\n", sensor.name);
    return false;
  }

  if (!writeRegister(sensor, ADXL345_DATA_FORMAT, 0x0B)) {
    Serial.printf("%s: failed to set data format\n", sensor.name);
    return false;
  }

  if (!writeRegister(sensor, ADXL345_POWER_CTL, 0x08)) {
    Serial.printf("%s: failed to enter measurement mode\n", sensor.name);
    return false;
  }

  return true;
}

void calibrateSensor(SensorConfig &sensor) {
  if (!sensor.ready) {
    Serial.printf("%s: skipped calibration because sensor is offline\n", sensor.name);
    return;
  }

  float totalMagnitude = 0.0f;
  uint32_t samples = 0;

  Serial.printf("%s: calibrating, keep the sensor still...\n", sensor.name);
  for (uint32_t i = 0; i < SENSOR_BASELINE_SAMPLES; i++) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (readAccelerationG(sensor, x, y, z)) {
      totalMagnitude += accelerationMagnitude(x, y, z);
      samples++;
    }
    delay(10);
  }

  if (samples > 0) {
    sensor.baselineG = totalMagnitude / samples;
  }

  Serial.printf("%s: baseline %.3fg using %lu samples\n",
                sensor.name,
                sensor.baselineG,
                static_cast<unsigned long>(samples));
}

void calibrateAllSensors() {
  Serial.println();
  Serial.println("Recalibrating all ready sensors.");
  Serial.println("Keep the sensors still until calibration finishes.");
  for (SensorConfig &sensor : sensors) {
    calibrateSensor(sensor);
  }
  Serial.println("Calibration finished.");
}

void printHelp() {
  Serial.println();
  Serial.println("LuxLock Tamper Test Commands");
  Serial.println("  h = show this help");
  Serial.println("  i = sensor info");
  Serial.println("  s = scan I2C buses");
  Serial.println("  d = detect sensors again");
  Serial.println("  r = read sensors once");
  Serial.println("  l = toggle live readings");
  Serial.println("  c = recalibrate baselines");
  Serial.println("  b = beep buzzer once");
  Serial.println("  0 = buzzer off");
  Serial.println("  1 = buzzer on");
  Serial.println();
}

void printSensorInfo() {
  Serial.println();
  Serial.println("Sensor status:");
  for (const SensorConfig &sensor : sensors) {
    Serial.printf(
        "  %s: %s, SDA=GPIO%d, SCL=GPIO%d, address=0x%02X, baseline=%.3fg\n",
        sensor.name,
        sensor.ready ? "READY" : "OFFLINE",
        sensor.sdaPin,
        sensor.sclPin,
        sensor.address,
        sensor.baselineG);
  }
}

void scanI2CBus(TwoWire &bus, const char *busName) {
  uint8_t found = 0;

  Serial.printf("%s scan:", busName);
  for (uint8_t address = 1; address < 127; address++) {
    bus.beginTransmission(address);
    if (bus.endTransmission() == 0) {
      Serial.printf(" 0x%02X", address);
      found++;
    }
  }

  if (found == 0) {
    Serial.print(" no devices found");
  }

  Serial.println();
}

void scanI2CBuses() {
  Serial.println();
  Serial.println("Scanning I2C buses...");
  scanI2CBus(busA, "Bus A GPIO21/GPIO22");
  scanI2CBus(busB, "Bus B GPIO25/GPIO26");
  Serial.println("Bus C GPIO32/GPIO33 is software I2C and is tested during sensor detection.");
  Serial.println("Expected ADXL345 address on each sensor: 0x53.");
}

void detectAllSensors(bool calibrateReady) {
  Serial.println();
  Serial.println("Detecting ADXL345 sensors...");
  for (SensorConfig &sensor : sensors) {
    sensor.ready = initAdxl345(sensor);
    sensor.lastEventLevel = 0;
    sensor.lastEventAt = 0;
    Serial.printf(
        "%s at 0x%02X: %s\n",
        sensor.name,
        sensor.address,
        sensor.ready ? "READY" : "OFFLINE");

    if (calibrateReady) {
      calibrateSensor(sensor);
    }
  }
}

uint8_t readySensorCount() {
  uint8_t count = 0;
  for (const SensorConfig &sensor : sensors) {
    if (sensor.ready) {
      count++;
    }
  }
  return count;
}

void printAliveStatus() {
  Serial.printf(
      "Tamper test alive. Ready sensors: %u/3. Type h then press Enter for commands.\n",
      readySensorCount());
}

void printSensorReading(SensorConfig &sensor) {
  if (!sensor.ready) {
    Serial.printf("%s: OFFLINE\n", sensor.name);
    return;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!readAccelerationG(sensor, x, y, z)) {
    Serial.printf("%s: read failed\n", sensor.name);
    return;
  }

  const float magnitude = accelerationMagnitude(x, y, z);
  const float deltaG = fabsf(magnitude - sensor.baselineG);
  const uint8_t level = shockLevelForDelta(deltaG);

  Serial.printf(
      "%s: x=% .2fg y=% .2fg z=% .2fg mag=%.2fg delta=%.2fg level=%u\n",
      sensor.name,
      x,
      y,
      z,
      magnitude,
      deltaG,
      level);
}

void printAllReadings() {
  Serial.println();
  for (SensorConfig &sensor : sensors) {
    printSensorReading(sensor);
  }
}

void detectEvents() {
  const uint32_t now = millis();
  for (SensorConfig &sensor : sensors) {
    if (!sensor.ready) {
      continue;
    }

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!readAccelerationG(sensor, x, y, z)) {
      continue;
    }

    const float magnitude = accelerationMagnitude(x, y, z);
    const float deltaG = fabsf(magnitude - sensor.baselineG);
    const uint8_t level = shockLevelForDelta(deltaG);
    if (level == 0) {
      sensor.lastEventLevel = 0;
      continue;
    }

    const bool cooldownDone = now - sensor.lastEventAt >= EVENT_COOLDOWN_MS;
    const bool levelIncreased = level > sensor.lastEventLevel;
    if (!cooldownDone && !levelIncreased) {
      continue;
    }

    sensor.lastEventAt = now;
    sensor.lastEventLevel = level;

    Serial.printf(
        "EVENT: %s shock level %u, delta=%.2fg\n",
        sensor.name,
        level,
        deltaG);

    if (level >= BUZZER_TRIGGER_LEVEL) {
      Serial.printf("EVENT: level %u reached, buzzer beeping.\n", level);
      beepBuzzer(1000);
    }
  }
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
      scanI2CBuses();
      break;
    case 'd':
    case 'D':
      detectAllSensors(true);
      printSensorInfo();
      break;
    case 'r':
    case 'R':
      printAllReadings();
      break;
    case 'l':
    case 'L':
      liveReadingsEnabled = !liveReadingsEnabled;
      Serial.printf(
          "Live readings %s.\n",
          liveReadingsEnabled ? "ON" : "OFF");
      break;
    case 'c':
    case 'C':
      calibrateAllSensors();
      break;
    case 'b':
    case 'B':
      Serial.println("Buzzer beep.");
      beepBuzzer();
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
    default:
      Serial.printf("Unknown command '%c'. Type h for help.\n", command);
      break;
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1500);

  pinMode(BUZZER_PIN, OUTPUT);
  setBuzzer(false);

  Serial.println();
  Serial.println("LuxLock Tamper Hardware Test");
  Serial.println("No Wi-Fi. No Firebase. ADXL345 + buzzer only.");

  busA.begin(BUS_A_SDA_PIN, BUS_A_SCL_PIN);
  busB.begin(BUS_B_SDA_PIN, BUS_B_SCL_PIN);
  busA.setClock(400000);
  busB.setClock(400000);
  softI2cRelease(BUS_C_SDA_PIN);
  softI2cRelease(BUS_C_SCL_PIN);

  scanI2CBuses();
  detectAllSensors(true);

  printHelp();
  printSensorInfo();
  printAliveStatus();
}

void loop() {
  const uint32_t now = millis();

  while (Serial.available() > 0) {
    handleSerialCommand(static_cast<char>(Serial.read()));
  }

  if (buzzerOffAt != 0 && now >= buzzerOffAt) {
    buzzerOffAt = 0;
    setBuzzer(false);
    Serial.println("Buzzer auto OFF.");
  }

  detectEvents();

  if (now - lastAlivePrintAt >= ALIVE_PRINT_MS) {
    lastAlivePrintAt = now;
    printAliveStatus();
  }

  if (liveReadingsEnabled && now - lastLivePrintAt >= LIVE_PRINT_MS) {
    lastLivePrintAt = now;
    printAllReadings();
  }
}
