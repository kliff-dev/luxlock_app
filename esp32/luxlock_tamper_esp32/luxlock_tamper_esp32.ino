#include <Wire.h>
#include <math.h>
#include <stdarg.h>

// ---------------------------------------------------------------------------
// LuxLock Tamper ESP32
// ---------------------------------------------------------------------------
//
// This helper ESP32 reads three ADXL345 vibration sensors and sends tamper
// events to the main LuxLock ESP32 over UART. The main ESP32 remains the only
// board that talks to Wi-Fi/Firebase/app.
//
// ADXL345 wiring plan:
//   Case 1 sensor: I2C bus A, SDA GPIO21, SCL GPIO22, SDO/ALT -> GND
//   Case 2 sensor: I2C bus B, SDA GPIO25, SCL GPIO26, SDO/ALT -> GND
//   Case 3 sensor: software I2C, SDA GPIO32, SCL GPIO33, SDO/ALT -> GND
//
// UART wiring to main ESP32:
//   Tamper TX GPIO17 -> Main RX GPIO21
//   Tamper RX GPIO16 -> Main TX GPIO22 (optional for now)
//   Tamper GND       -> Main GND
//
// Active buzzer with 2N2222 transistor driver:
//   12V PSU + -> active buzzer +
//   active buzzer - -> 2N2222 collector
//   2N2222 emitter -> common GND
//   Tamper GPIO27 -> 1k resistor -> 2N2222 base
//   Tamper GND, 12V PSU -, and emitter GND must be common
//
// GPIO27 HIGH turns on the 2N2222 and powers the buzzer.

static const uint32_t SERIAL_BAUD = 115200;
static const uint32_t TAMPER_UART_BAUD = 115200;
// Keep false for the final build. Turn true only when using Serial Monitor
// because USB logging adds extra work that is not useful after installation.
static const bool ENABLE_SERIAL_DEBUG = false;

static const int TAMPER_UART_RX_PIN = 16;
static const int TAMPER_UART_TX_PIN = 17;

static const int BUS_A_SDA_PIN = 21;
static const int BUS_A_SCL_PIN = 22;
static const int BUS_B_SDA_PIN = 25;
static const int BUS_B_SCL_PIN = 26;
static const int BUS_C_SDA_PIN = 32;
static const int BUS_C_SCL_PIN = 33;

static const int BUZZER_PIN = 27;
static const bool BUZZER_ACTIVE_HIGH = true;

static const uint32_t SENSOR_POLL_MS = 50;
static const uint32_t SENSOR_BASELINE_SAMPLES = 80;
static const uint32_t EVENT_COOLDOWN_MS = 8000;
static const uint32_t HEARTBEAT_MS = 5000;
static const uint32_t BUZZER_ALARM_MS = 5000;
static const uint8_t BUZZER_TRIGGER_LEVEL = 1;

// Tune these after mounting sensors on the real cases.
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
HardwareSerial mainSerial(2);

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
  uint8_t lastSentLevel;
  uint32_t lastSentAt;
};

SensorConfig sensors[] = {
    {"case1", 1, SENSOR_BUS_HARDWARE, &busA, BUS_A_SDA_PIN, BUS_A_SCL_PIN, 0x53, false, 1.0f, 0, 0},
    {"case2", 2, SENSOR_BUS_HARDWARE, &busB, BUS_B_SDA_PIN, BUS_B_SCL_PIN, 0x53, false, 1.0f, 0, 0},
    {"case3", 3, SENSOR_BUS_SOFTWARE, nullptr, BUS_C_SDA_PIN, BUS_C_SCL_PIN, 0x53, false, 1.0f, 0, 0},
};


uint32_t lastPollAt = 0;
uint32_t lastHeartbeatAt = 0;
uint32_t buzzerUntil = 0;

void debugBegin(uint32_t baudRate) {
  if (ENABLE_SERIAL_DEBUG) {
    Serial.begin(baudRate);
    delay(300);
  }
}

void debugPrintln() {
  if (ENABLE_SERIAL_DEBUG) {
    Serial.println();
  }
}

template <typename T>
void debugPrintln(const T &value) {
  if (ENABLE_SERIAL_DEBUG) {
    Serial.println(value);
  }
}

void debugPrintf(const char *format, ...) {
  if (!ENABLE_SERIAL_DEBUG) {
    return;
  }

  va_list args;
  va_start(args, format);
  Serial.vprintf(format, args);
  va_end(args);
}

void setBuzzer(bool on) {
  digitalWrite(
      BUZZER_PIN,
      on == BUZZER_ACTIVE_HIGH ? HIGH : LOW);
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
    debugPrintf("%s ADXL345 not responding at 0x%02X\n",
                sensor.name,
                sensor.address);
    return false;
  }

  if (deviceId != ADXL345_EXPECTED_DEVID) {
    debugPrintf("%s unexpected device ID 0x%02X at 0x%02X\n",
                sensor.name,
                deviceId,
                sensor.address);
    return false;
  }

  // 200 Hz output data rate.
  if (!writeRegister(sensor, ADXL345_BW_RATE, 0x0B)) {
    return false;
  }

  // Full-resolution mode, +/-16g range.
  if (!writeRegister(sensor, ADXL345_DATA_FORMAT, 0x0B)) {
    return false;
  }

  // Measurement mode.
  if (!writeRegister(sensor, ADXL345_POWER_CTL, 0x08)) {
    return false;
  }

  return true;
}

void calibrateSensor(SensorConfig &sensor) {
  if (!sensor.ready) {
    return;
  }

  float totalMagnitude = 0.0f;
  uint32_t samples = 0;

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

  debugPrintf("%s baseline: %.3fg\n", sensor.name, sensor.baselineG);
}

uint8_t readySensorCount() {
  uint8_t count = 0;
  for (SensorConfig &sensor : sensors) {
    if (sensor.ready) {
      count++;
    }
  }
  return count;
}

void sendHeartbeat() {
  const uint8_t count = readySensorCount();
  mainSerial.printf("READY,sensors=%u\n", count);
  debugPrintf("READY,sensors=%u\n", count);
}

void sendTamperEvent(
    const SensorConfig &sensor,
    uint8_t level,
    float peakDeltaG) {
  mainSerial.printf(
      "TAMPER,case=%u,level=%u,peak=%.2f\n",
      sensor.caseNumber,
      level,
      peakDeltaG);
  debugPrintf(
      "TAMPER %s level=%u peak=%.2fg\n",
      sensor.name,
      level,
      peakDeltaG);
}

void pollSensor(SensorConfig &sensor) {
  if (!sensor.ready) {
    return;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!readAccelerationG(sensor, x, y, z)) {
    debugPrintf("%s read failed\n", sensor.name);
    return;
  }

  const float magnitude = accelerationMagnitude(x, y, z);
  const float deltaG = fabsf(magnitude - sensor.baselineG);
  const uint8_t level = shockLevelForDelta(deltaG);
  if (level == 0) {
    sensor.lastSentLevel = 0;
    return;
  }

  const uint32_t now = millis();
  const bool cooldownDone = now - sensor.lastSentAt >= EVENT_COOLDOWN_MS;
  const bool levelIncreased = level > sensor.lastSentLevel;
  if (!cooldownDone && !levelIncreased) {
    return;
  }

  sensor.lastSentAt = now;
  sensor.lastSentLevel = level;
  sendTamperEvent(sensor, level, deltaG);

  if (level >= BUZZER_TRIGGER_LEVEL) {
    buzzerUntil = now + BUZZER_ALARM_MS;
    setBuzzer(true);
  }
}

void setup() {
  debugBegin(SERIAL_BAUD);
  debugPrintln();
  debugPrintln("LuxLock tamper ESP32 booting...");

  pinMode(BUZZER_PIN, OUTPUT);
  setBuzzer(false);

  mainSerial.begin(
      TAMPER_UART_BAUD,
      SERIAL_8N1,
      TAMPER_UART_RX_PIN,
      TAMPER_UART_TX_PIN);

  busA.begin(BUS_A_SDA_PIN, BUS_A_SCL_PIN);
  busB.begin(BUS_B_SDA_PIN, BUS_B_SCL_PIN);
  busA.setClock(400000);
  busB.setClock(400000);
  softI2cRelease(BUS_C_SDA_PIN);
  softI2cRelease(BUS_C_SCL_PIN);

  for (SensorConfig &sensor : sensors) {
    sensor.ready = initAdxl345(sensor);
    debugPrintf(
        "%s sensor %s at 0x%02X\n",
        sensor.name,
        sensor.ready ? "ready" : "offline",
        sensor.address);
    calibrateSensor(sensor);
  }

  sendHeartbeat();
}

void loop() {
  const uint32_t now = millis();

  if (now - lastPollAt >= SENSOR_POLL_MS) {
    lastPollAt = now;
    for (SensorConfig &sensor : sensors) {
      pollSensor(sensor);
    }
  }

  if (buzzerUntil != 0 && now >= buzzerUntil) {
    buzzerUntil = 0;
    setBuzzer(false);
  }

  if (now - lastHeartbeatAt >= HEARTBEAT_MS) {
    lastHeartbeatAt = now;
    sendHeartbeat();
  }
}
