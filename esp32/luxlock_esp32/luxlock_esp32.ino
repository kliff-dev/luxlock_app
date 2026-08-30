#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Preferences.h>
#include <stdlib.h>
#include <stdarg.h>
#include <Adafruit_Fingerprint.h>
#include <FirebaseClient.h>
#include "AccessCleanupResult.h"
#include "secrets.h"

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------

static const char *DEVICE_ID = "esp32_1";
// Keep false for the final build. Turn true only when using Serial Monitor
// because USB logging adds extra work that is not useful after installation.
static const bool ENABLE_SERIAL_DEBUG = false;

// ---------------------------------------------------------------------------
// LuxLock timing
// ---------------------------------------------------------------------------

static const uint32_t WIFI_RETRY_MS = 5000;
static const uint32_t FIREBASE_RETRY_MS = 5000;
static const uint32_t FIREBASE_AUTH_TIMEOUT_MS = 30000;
static const uint32_t FIREBASE_ERROR_WINDOW_MS = 30000;
static const uint32_t OVERRIDE_POLL_MS = 750;
static const uint32_t ENROLL_POLL_MS = 1500;
static const uint32_t HISTORY_FLUSH_MS = 5000;
static const uint32_t STATUS_FAST_PUSH_MS = 2000;
static const uint32_t STATUS_NORMAL_PUSH_MS = 15000;
static const uint32_t STATUS_FAST_WINDOW_MS = 15000;
static const uint32_t CASE1_RUN_MS = 7200;
static const uint32_t CASE2_RUN_MS = 7200;
static const uint32_t CASE3_RUN_MS = 7200;
static const uint32_t AUTO_CLOSE_DELAY_MS = 20000;
static const uint32_t ACTUATOR_DIRECTION_COOLDOWN_MS = 0;
static const uint32_t FINGER_COOLDOWN_MS = 2500;
static const uint32_t FINGERPRINT_RETRY_MS = 10000;
static const uint32_t NTP_SYNC_RETRY_MS = 30000;
static const uint32_t NTP_RESYNC_MS = 21600000;
static const uint32_t ENROLL_TIMEOUT_MS = 30000;
static const uint32_t ENROLL_CANCEL_POLL_MS = 150;
static const uint8_t ENROLL_SCAN_PAIR_ATTEMPTS = 1;
static const uint8_t MOTOR_PWM_VALUE = 220;
static const uint16_t FINGERPRINT_MAX_TEMPLATES = 127;
static const uint8_t MAX_PENDING_HISTORY_LOGS = 50;
static const uint8_t MAX_HISTORY_FLUSH_PER_PASS = 5;
static const int ENROLL_WAIT_TIMEOUT = -1000;
static const int ENROLL_CANCELLED = -1001;
static const wifi_power_t WIFI_TX_POWER = WIFI_POWER_19_5dBm;
static const uint8_t FIREBASE_MAX_CONSECUTIVE_ERRORS = 3;
static const uint8_t FINGERPRINT_SECURITY_LEVEL = FINGERPRINT_SECURITY_LEVEL_2;

static const int FP_RX_PIN = 16;
static const int FP_TX_PIN = 17;
static const int FP_TOUCH_PIN = 21;
static const bool USE_FINGER_TOUCH_PIN = false;
static const bool FP_TOUCH_ACTIVE_LOW = true;
static const int TAMPER_RX_PIN = 21;
static const int TAMPER_TX_PIN = 22;
static const uint32_t TAMPER_UART_BAUD = 115200;
static const uint32_t TAMPER_STALE_MS = 15000;
static const uint32_t TAMPER_LOG_COOLDOWN_MS = 5000;
static const int8_t UNUSED_PIN = -1;

struct ActuatorConfig {
  const char *name;
  int8_t rpwmPin;
  int8_t lpwmPin;
  int8_t renPin;
  int8_t lenPin;
  bool openUsesRpwm;
  uint32_t runMs;
  bool isActive;
  bool isOpenPosition;
  bool targetOpenPosition;
  bool autoCloseArmed;
  uint32_t autoCloseAt;
  uint32_t stopAt;
  uint32_t cooldownUntil;
  String lastAction;
};

ActuatorConfig actuators[3] = {
    {"case1", 25, 26, UNUSED_PIN, UNUSED_PIN, false, CASE1_RUN_MS, false, false, false, false, 0, 0, 0, "stopped"},
    {"case2", 32, 33, UNUSED_PIN, UNUSED_PIN, false, CASE2_RUN_MS, false, false, false, false, 0, 0, 0, "stopped"},
    {"case3", 18, 19, UNUSED_PIN, UNUSED_PIN, false, CASE3_RUN_MS, false, false, false, false, 0, 0, 0, "stopped"},
};
    
// ---------------------------------------------------------------------------
// Firebase + peripherals
// ---------------------------------------------------------------------------

FirebaseApp app;
UserAccount firebaseUser(FIREBASE_API_KEY);
RealtimeDatabase Database;
WiFiClientSecure sslClient;
using AsyncClient = AsyncClientClass;
AsyncClient firebaseClient(sslClient);
bool firebaseSignupOk = false;
bool firebaseInitialized = false;
bool firebaseDatabaseBound = false;
bool wifiRadioConfigured = false;
uint32_t firebaseAuthStartedAt = 0;
uint32_t firebaseReadySinceAt = 0;

Preferences prefs;

HardwareSerial fingerprintSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerprintSerial);
HardwareSerial tamperSerial(1);
bool fingerprintOk = false;
bool clockTimeSynced = false;

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------

uint32_t lastOverridePollAt = 0;
uint32_t lastEnrollPollAt = 0;
uint32_t lastHistoryFlushAt = 0;
uint32_t lastStatusPushAt = 0;
uint32_t fastStatusPushUntil = 0;
uint32_t lastWifiRetryAt = 0;
uint32_t lastFirebaseRetryAt = 0;
uint32_t lastFirebaseErrorAt = 0;
uint32_t fingerCooldownUntil = 0;
uint32_t lastEnrollCancelCheckAt = 0;
uint32_t lastFingerprintRetryAt = 0;
uint32_t lastNtpSyncAttemptAt = 0;
uint32_t lastTamperHeartbeatAt = 0;
uint32_t lastTamperEventAt = 0;
uint32_t lastTamperLogAt[3] = {0, 0, 0};

String lastSystemMessage = "booting";
String lastAuthorizedValue = "N/A";
String lastCaseValue = "N/A";
String tamperLineBuffer = "";
String lastTamperPeak = "0.00";
uint8_t tamperSensorCount = 0;
uint8_t lastTamperCase = 0;
uint8_t lastTamperLevel = 0;
uint8_t lastTamperLoggedLevel[3] = {0, 0, 0};

uint64_t lastEnrollNonce = 0;
uint64_t lastOverrideNonce = 0;
uint64_t lastSeenEnrollNonce = 0;
uint64_t lastSeenOverrideNonce = 0;
uint64_t lastProcessedEnrollNonce = 0;
uint64_t lastProcessedOverrideNonce = 0;

volatile bool enrollInProgress = false;
String pendingEnrollAccess = "";
String enrollmentState = "idle";
String enrollmentMessage = "Waiting for enrollment command.";
String enrollmentAccess = "";
uint64_t enrollmentNonce = 0;
int32_t enrollmentTemplateId = -1;
bool enrollCancelRequested = false;
String lastCommandError = "";
String lastOverrideTarget = "";
String lastOverrideAction = "";
String lastEnrollAccessSeen = "";
String lastOverrideResult = "idle";
String lastOverrideError = "";
uint8_t registeredAccessMaskCache = 0;
bool registeredAccessMaskDirty = true;
uint8_t consecutiveFirebaseErrors = 0;

struct ClockSnapshot {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

struct EnrollmentResult {
  bool ok;
  bool reusedExistingTemplate;
  uint16_t templateId;
  String savedAccess;
  String previousAccess;
};

// ---------------------------------------------------------------------------
// Firebase paths
// ---------------------------------------------------------------------------

String rootPath() {
  return String("/devices/") + DEVICE_ID;
}

String enrollBasePath() {
  return rootPath() + "/commands/enroll";
}

String overrideBasePath() {
  return rootPath() + "/commands/override";
}

String historyPath() {
  return rootPath() + "/history";
}

String statusPath() {
  return rootPath() + "/status";
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

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

void setSystemMessage(const String &message) {
  lastSystemMessage = message;
  debugPrintln("[STATUS] " + message);
}

void pushStatus();
bool firebaseReadyForUse();
void startFastStatusPushWindow();
uint32_t currentStatusPushIntervalMs();
void bindDatabase();
void resetFirebaseSession();
void syncFirebaseAuthState();
void syncClockWithNtpIfNeeded();
String firebaseLastError();
void configureFirebaseSslClient();
void authDebugPrint(AsyncResult &aResult);
String escapeJsonString(const String &value);
bool fingerTouchDetected();

uint64_t parseNonceString(const String &raw) {
  if (raw.isEmpty()) {
    return 0;
  }

  char *end = nullptr;
  const unsigned long long parsed = strtoull(raw.c_str(), &end, 10);
  if (end == raw.c_str()) {
    return 0;
  }

  return static_cast<uint64_t>(parsed);
}

String formatNonce(uint64_t nonce) {
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(nonce));
  return String(buffer);
}

void updateEnrollmentStatus(
    const String &state,
    const String &message,
    const String &access = "",
    uint64_t nonce = 0,
    int32_t templateId = -1) {
  enrollmentState = state;
  enrollmentMessage = message;
  enrollmentAccess = access;
  enrollmentNonce = nonce;
  enrollmentTemplateId = templateId;

  debugPrintln(
      "[ENROLL] state=" + enrollmentState +
      " nonce=" + formatNonce(enrollmentNonce) +
      " access=" + enrollmentAccess +
      " message=" + enrollmentMessage);

  if (firebaseReadyForUse()) {
    pushStatus();
    lastStatusPushAt = millis();
  }
}

String prefsKeyForFinger(uint16_t fingerId) {
  return "fp_" + String(fingerId);
}

String prefsKeyForActuatorPosition(const char *actuatorName) {
  return String("pos_") + actuatorName;
}

String prefsKeyForPendingHistoryIndex(uint8_t index) {
  return "hist_" + String(index);
}

uint8_t pendingHistoryLogCount() {
  uint8_t count = static_cast<uint8_t>(prefs.getUChar("hist_count", 0));
  if (count <= MAX_PENDING_HISTORY_LOGS) {
    return count;
  }

  prefs.putUChar("hist_count", MAX_PENDING_HISTORY_LOGS);
  return MAX_PENDING_HISTORY_LOGS;
}

void scheduleHistoryFlushSoon() {
  lastHistoryFlushAt = 0;
}

uint8_t monthFromShortName(const char *name) {
  if (strncmp(name, "Jan", 3) == 0) return 1;
  if (strncmp(name, "Feb", 3) == 0) return 2;
  if (strncmp(name, "Mar", 3) == 0) return 3;
  if (strncmp(name, "Apr", 3) == 0) return 4;
  if (strncmp(name, "May", 3) == 0) return 5;
  if (strncmp(name, "Jun", 3) == 0) return 6;
  if (strncmp(name, "Jul", 3) == 0) return 7;
  if (strncmp(name, "Aug", 3) == 0) return 8;
  if (strncmp(name, "Sep", 3) == 0) return 9;
  if (strncmp(name, "Oct", 3) == 0) return 10;
  if (strncmp(name, "Nov", 3) == 0) return 11;
  if (strncmp(name, "Dec", 3) == 0) return 12;
  return 1;
}

ClockSnapshot compileTimeSnapshot() {
  char monthName[4] = {0};
  int day = 1;
  int year = 1970;
  int hour = 0;
  int minute = 0;
  int second = 0;

  sscanf(__DATE__, "%3s %d %d", monthName, &day, &year);
  sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

  return ClockSnapshot{
      static_cast<uint16_t>(year),
      monthFromShortName(monthName),
      static_cast<uint8_t>(day),
      static_cast<uint8_t>(hour),
      static_cast<uint8_t>(minute),
      static_cast<uint8_t>(second)};
}

String formatDate(const ClockSnapshot &dt) {
  char buf[11];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u", dt.year, dt.month, dt.day);
  return String(buf);
}

String formatTime12h(const ClockSnapshot &dt) {
  char buf[12];
  uint8_t hour = dt.hour;
  const char *meridiem = hour >= 12 ? "PM" : "AM";
  hour = hour % 12;
  if (hour == 0) {
    hour = 12;
  }
  snprintf(buf, sizeof(buf), "%02u:%02u %s", hour, dt.minute, meridiem);
  return String(buf);
}

bool hasNetworkTime() {
  time_t now = time(nullptr);
  return now >= 1700000000;
}

bool fingerTouchDetected() {
  if (!USE_FINGER_TOUCH_PIN) {
    return true;
  }

  const int level = digitalRead(FP_TOUCH_PIN);
  return FP_TOUCH_ACTIVE_LOW ? level == LOW : level == HIGH;
}

ClockSnapshot currentBestDateTime() {
  if (hasNetworkTime()) {
    time_t now = time(nullptr);
    struct tm timeInfo;
    localtime_r(&now, &timeInfo);
    return ClockSnapshot{
        static_cast<uint16_t>(timeInfo.tm_year + 1900),
        static_cast<uint8_t>(timeInfo.tm_mon + 1),
        static_cast<uint8_t>(timeInfo.tm_mday),
        static_cast<uint8_t>(timeInfo.tm_hour),
        static_cast<uint8_t>(timeInfo.tm_min),
        static_cast<uint8_t>(timeInfo.tm_sec)};
  }

  return compileTimeSnapshot();
}

bool pushHistoryLogPayload(
    const String &dateValue,
    const String &timeValue,
    uint32_t epochValue,
    const String &caseValue,
    const String &authorized) {
  if (!firebaseReadyForUse()) {
    return false;
  }

  const String payload =
      String("{\"time\":\"") + escapeJsonString(timeValue) +
      "\",\"date\":\"" + escapeJsonString(dateValue) +
      "\",\"epoch\":" + String(epochValue) +
      ",\"case\":\"" + escapeJsonString(caseValue) +
      "\",\"authorized\":\"" + escapeJsonString(authorized) + "\"}";

  String pushName =
      Database.push<object_t>(firebaseClient, historyPath(), object_t(payload));
  if (pushName.isEmpty() && firebaseClient.lastError().code() != 0) {
    debugPrintf("Failed to push history: %s\n", firebaseLastError().c_str());
    setCommandError(String("history push failed: ") + firebaseLastError());
    noteFirebaseOperationFailure("history push");
    return false;
  }

  noteFirebaseOperationSuccess();
  lastCaseValue = caseValue;
  lastAuthorizedValue = authorized;
  return true;
}

void queuePendingHistoryLog(
    const String &dateValue,
    const String &timeValue,
    uint32_t epochValue,
    const String &caseValue,
    const String &authorized) {
  uint8_t count = pendingHistoryLogCount();
  if (count >= MAX_PENDING_HISTORY_LOGS) {
    for (uint8_t i = 1; i < count; i++) {
      prefs.putString(
          prefsKeyForPendingHistoryIndex(i - 1).c_str(),
          prefs.getString(prefsKeyForPendingHistoryIndex(i).c_str(), ""));
    }
    debugPrintln("Pending history queue full; dropping oldest log.");
    count = MAX_PENDING_HISTORY_LOGS - 1;
  }

  const String storedValue =
      dateValue + "\n" + timeValue + "\n" + String(epochValue) +
      "\n" + caseValue + "\n" + authorized;
  prefs.putString(prefsKeyForPendingHistoryIndex(count).c_str(), storedValue);
  prefs.putUChar("hist_count", count + 1);
  scheduleHistoryFlushSoon();
}

void queueHistoryLogForLater(const String &caseValue, const String &authorized) {
  ClockSnapshot now = currentBestDateTime();
  String dateValue = formatDate(now);
  String timeValue = formatTime12h(now);
  queuePendingHistoryLog(
      dateValue,
      timeValue,
      currentRtcEpoch(),
      caseValue,
      authorized);
  lastCaseValue = caseValue;
  lastAuthorizedValue = authorized;
  pushStatusSoon();
}

void flushPendingHistoryLogs() {
  if (!firebaseReadyForUse()) {
    return;
  }

  uint8_t count = pendingHistoryLogCount();
  if (count == 0) {
    return;
  }

  uint8_t flushed = 0;
  while (flushed < count && flushed < MAX_HISTORY_FLUSH_PER_PASS) {
    String storedValue =
        prefs.getString(prefsKeyForPendingHistoryIndex(flushed).c_str(), "");
    if (storedValue.isEmpty()) {
      flushed++;
      continue;
    }

    int firstBreak = storedValue.indexOf('\n');
    int secondBreak =
        firstBreak >= 0 ? storedValue.indexOf('\n', firstBreak + 1) : -1;
    int thirdBreak =
        secondBreak >= 0 ? storedValue.indexOf('\n', secondBreak + 1) : -1;
    int fourthBreak =
        thirdBreak >= 0 ? storedValue.indexOf('\n', thirdBreak + 1) : -1;

    if (firstBreak < 0 || secondBreak < 0 || thirdBreak < 0) {
      flushed++;
      continue;
    }

    String dateValue = storedValue.substring(0, firstBreak);
    String timeValue = storedValue.substring(firstBreak + 1, secondBreak);
    uint32_t epochValue = 0;
    String caseValue;
    String authorized;

    if (fourthBreak >= 0) {
      epochValue =
          static_cast<uint32_t>(storedValue.substring(secondBreak + 1, thirdBreak).toInt());
      caseValue = storedValue.substring(thirdBreak + 1, fourthBreak);
      authorized = storedValue.substring(fourthBreak + 1);
    } else {
      caseValue = storedValue.substring(secondBreak + 1, thirdBreak);
      authorized = storedValue.substring(thirdBreak + 1);
    }

    if (!pushHistoryLogPayload(
            dateValue,
            timeValue,
            epochValue,
            caseValue,
            authorized)) {
      break;
    }

    flushed++;
  }

  if (flushed == 0) {
    return;
  }

  uint8_t remaining = count - flushed;
  for (uint8_t i = 0; i < remaining; i++) {
    prefs.putString(
        prefsKeyForPendingHistoryIndex(i).c_str(),
        prefs.getString(prefsKeyForPendingHistoryIndex(i + flushed).c_str(), ""));
  }

  for (uint8_t i = remaining; i < count; i++) {
    prefs.remove(prefsKeyForPendingHistoryIndex(i).c_str());
  }

  prefs.putUChar("hist_count", remaining);
}

uint32_t currentRtcEpoch() {
  if (hasNetworkTime()) {
    return static_cast<uint32_t>(time(nullptr));
  }

  return 0;
}

void setCommandError(const String &message) {
  lastCommandError = message;
  if (!message.isEmpty()) {
    debugPrintln("[COMMAND ERROR] " + message);
  }
}

void clearCommandError() {
  lastCommandError = "";
}

void noteFirebaseOperationSuccess() {
  consecutiveFirebaseErrors = 0;
  lastFirebaseErrorAt = 0;
}

void noteFirebaseOperationFailure(const String &context) {
  if (firebaseClient.lastError().code() == 0) {
    return;
  }

  const uint32_t now = millis();
  if (lastFirebaseErrorAt == 0 ||
      now - lastFirebaseErrorAt > FIREBASE_ERROR_WINDOW_MS) {
    consecutiveFirebaseErrors = 0;
  }

  lastFirebaseErrorAt = now;
  consecutiveFirebaseErrors++;

  debugPrintf(
      "Firebase %s failed (%u/%u): %s\n",
      context.c_str(),
      consecutiveFirebaseErrors,
      FIREBASE_MAX_CONSECUTIVE_ERRORS,
      firebaseLastError().c_str());

}

void pushStatusSoon() {
  if (!firebaseReadyForUse()) {
    return;
  }

  pushStatus();
  lastStatusPushAt = millis();
}

void recordOverrideResult(
    uint64_t nonce,
    const String &target,
    const String &action,
    bool accepted,
    const String &error = "") {
  lastProcessedOverrideNonce = nonce;
  lastOverrideTarget = target;
  lastOverrideAction = action;
  lastOverrideResult = accepted ? "accepted" : "rejected";
  lastOverrideError = error;
}

void loadSavedNonces() {
  lastEnrollNonce = parseNonceString(prefs.getString("enroll_nonce", ""));
  lastOverrideNonce = parseNonceString(prefs.getString("override_nonce", ""));
}

void saveEnrollNonce(uint64_t nonce) {
  lastEnrollNonce = nonce;
  prefs.putString("enroll_nonce", formatNonce(nonce));
}

void saveOverrideNonce(uint64_t nonce) {
  lastOverrideNonce = nonce;
  prefs.putString("override_nonce", formatNonce(nonce));
}

String accessForFingerprint(uint16_t fingerId) {
  return prefs.getString(prefsKeyForFinger(fingerId).c_str(), "");
}

uint8_t accessMaskFromString(const String &access) {
  if (access == "ALL") {
    return 0b111;
  }

  uint8_t mask = 0;
  if (access.indexOf('1') >= 0) {
    mask |= 0b001;
  }
  if (access.indexOf('2') >= 0) {
    mask |= 0b010;
  }
  if (access.indexOf('3') >= 0) {
    mask |= 0b100;
  }

  return mask;
}

String accessStringFromMask(uint8_t mask) {
  if (mask == 0) {
    return "";
  }

  if (mask == 0b111) {
    return "ALL";
  }

  String access = "";
  if (mask & 0b001) {
    access += "1";
  }
  if (mask & 0b010) {
    if (!access.isEmpty()) {
      access += ",";
    }
    access += "2";
  }
  if (mask & 0b100) {
    if (!access.isEmpty()) {
      access += ",";
    }
    access += "3";
  }

  return access;
}

String accessLabel(const String &access) {
  if (access == "1") {
    return "Case 1";
  }
  if (access == "2") {
    return "Case 2";
  }
  if (access == "3") {
    return "Case 3";
  }
  if (access == "ALL") {
    return "all cases";
  }

  return access;
}

void clearAccessForFingerprint(uint16_t fingerId) {
  prefs.remove(prefsKeyForFinger(fingerId).c_str());
  registeredAccessMaskDirty = true;
}

void saveAccessForFingerprint(uint16_t fingerId, const String &access) {
  prefs.putString(prefsKeyForFinger(fingerId).c_str(), access);
  registeredAccessMaskDirty = true;
}

uint8_t savedFingerprintAccessMask() {
  uint8_t mask = 0;

  for (uint16_t id = 1; id <= FINGERPRINT_MAX_TEMPLATES; id++) {
    const String access = accessForFingerprint(id);
    if (access.isEmpty()) {
      continue;
    }

    mask |= accessMaskFromString(access);
  }

  return mask;
}

uint8_t currentRegisteredAccessMask() {
  if (registeredAccessMaskDirty) {
    registeredAccessMaskCache = savedFingerprintAccessMask();
    registeredAccessMaskDirty = false;
  }

  return registeredAccessMaskCache;
}

void loadActuatorPositions() {
  for (uint8_t i = 0; i < 3; i++) {
    auto &act = actuators[i];
    act.isOpenPosition =
        prefs.getBool(prefsKeyForActuatorPosition(act.name).c_str(), false);
    act.targetOpenPosition = act.isOpenPosition;
    act.autoCloseArmed = false;
    act.autoCloseAt = 0;
  }
}

void saveActuatorPosition(uint8_t index, bool isOpen) {
  auto &act = actuators[index];
  act.isOpenPosition = isOpen;
  prefs.putBool(prefsKeyForActuatorPosition(act.name).c_str(), isOpen);
}

bool anyActuatorActive() {
  for (uint8_t i = 0; i < 3; i++) {
    if (actuators[i].isActive) {
      return true;
    }
  }

  return false;
}

bool anyCaseOpen() {
  for (uint8_t i = 0; i < 3; i++) {
    if (actuators[i].isOpenPosition) {
      return true;
    }
  }

  return false;
}

bool anyActuatorCoolingDown() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < 3; i++) {
    if (now < actuators[i].cooldownUntil) {
      return true;
    }
  }

  return false;
}

bool allCasesOpen() {
  for (uint8_t i = 0; i < 3; i++) {
    if (!actuators[i].isOpenPosition) {
      return false;
    }
  }

  return true;
}

bool allCasesClosed() {
  for (uint8_t i = 0; i < 3; i++) {
    if (actuators[i].isOpenPosition) {
      return false;
    }
  }

  return true;
}

bool actuatorCoolingDown(uint8_t index) {
  return millis() < actuators[index].cooldownUntil;
}

bool accessMaskCoolingDown(uint8_t mask) {
  for (uint8_t i = 0; i < 3; i++) {
    if (((mask >> i) & 0x01) != 0 && actuatorCoolingDown(i)) {
      return true;
    }
  }

  return false;
}

void armAutoClose(uint8_t index) {
  auto &act = actuators[index];
  act.autoCloseArmed = true;
  act.autoCloseAt = 0;
}

void clearAutoClose(uint8_t index) {
  auto &act = actuators[index];
  act.autoCloseArmed = false;
  act.autoCloseAt = 0;
}

AccessCleanupResult reassignOverlappingFingerprintAccess(
    uint16_t newFingerId,
    const String &newAccess) {
  AccessCleanupResult result = {0, 0};
  const uint8_t newMask = accessMaskFromString(newAccess);

  if (!fingerprintOk || newMask == 0) {
    return result;
  }

  for (uint16_t id = 1; id <= FINGERPRINT_MAX_TEMPLATES; id++) {
    if (id == newFingerId) {
      continue;
    }

    const String existingAccess = accessForFingerprint(id);
    if (existingAccess.isEmpty()) {
      continue;
    }

    const uint8_t existingMask = accessMaskFromString(existingAccess);
    if (existingMask == 0 || (existingMask & newMask) == 0) {
      continue;
    }

    const uint8_t remainingMask = existingMask & ~newMask;

    if (remainingMask == 0) {
      uint8_t loadResult = finger.loadModel(id);
      if (loadResult == FINGERPRINT_OK) {
        uint8_t deleteResult = finger.deleteModel(id);
        if (deleteResult != FINGERPRINT_OK) {
          debugPrintf(
              "Failed to delete overlapping fingerprint %u for access %s: %d\n",
              id,
              existingAccess.c_str(),
              deleteResult);
          continue;
        }
      }

      clearAccessForFingerprint(id);
      result.removedCount++;
      continue;
    }

    const String updatedAccess = accessStringFromMask(remainingMask);
    if (updatedAccess.isEmpty()) {
      uint8_t loadResult = finger.loadModel(id);
      if (loadResult == FINGERPRINT_OK) {
        uint8_t deleteResult = finger.deleteModel(id);
        if (deleteResult != FINGERPRINT_OK) {
          debugPrintf(
              "Failed to delete fingerprint %u after overlap update for access %s: %d\n",
              id,
              existingAccess.c_str(),
              deleteResult);
          continue;
        }
      }

      clearAccessForFingerprint(id);
      result.removedCount++;
    } else if (updatedAccess != existingAccess) {
      saveAccessForFingerprint(id, updatedAccess);
      result.updatedCount++;
      debugPrintf(
          "Updated fingerprint %u access from %s to %s\n",
          id,
          existingAccess.c_str(),
          updatedAccess.c_str());
    }
  }

  return result;
}

void bindDatabase() {
  if (firebaseDatabaseBound) {
    return;
  }

  app.getApp<RealtimeDatabase>(Database);
  Database.url(FIREBASE_DATABASE_URL);
  firebaseDatabaseBound = true;
}

void resetFirebaseSession() {
  if (firebaseDatabaseBound) {
    Database.resetApp();
    firebaseDatabaseBound = false;
  }

  deinitializeApp(app);
  firebaseInitialized = false;
  firebaseSignupOk = false;
  firebaseAuthStartedAt = 0;
  firebaseReadySinceAt = 0;
  consecutiveFirebaseErrors = 0;
  lastFirebaseErrorAt = 0;
}

void syncFirebaseAuthState() {
  const bool readyNow = WiFi.status() == WL_CONNECTED && app.ready();
  if (readyNow == firebaseSignupOk) {
    return;
  }

  firebaseSignupOk = readyNow;

  if (firebaseSignupOk) {
    if (firebaseReadySinceAt == 0) {
      firebaseReadySinceAt = millis();
    }
    bindDatabase();
    startFastStatusPushWindow();
    setSystemMessage("firebase anonymous auth ready");
    pushStatusSoon();
  } else if (firebaseInitialized && WiFi.status() == WL_CONNECTED) {
    firebaseReadySinceAt = 0;
    setSystemMessage("firebase authenticating");
  }
}

String firebaseLastError() {
  if (firebaseClient.lastError().code() == 0) {
    return "";
  }

  return firebaseClient.lastError().message();
}

void configureFirebaseSslClient() {
  sslClient.setInsecure();
}

void authDebugPrint(AsyncResult &aResult) {
  if (!ENABLE_SERIAL_DEBUG) {
    return;
  }

  if (aResult.isEvent()) {
    Firebase.printf(
        "Event task: %s, msg: %s, code: %d\n",
        aResult.uid().c_str(),
        aResult.eventLog().message().c_str(),
        aResult.eventLog().code());
  }

  if (aResult.isDebug()) {
    Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
  }

  if (aResult.isError()) {
    Firebase.printf(
        "Error task: %s, msg: %s, code: %d\n",
        aResult.uid().c_str(),
        aResult.error().message().c_str(),
        aResult.error().code());
  }
}

String escapeJsonString(const String &value) {
  String escaped = "";
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); i++) {
    const char ch = value.charAt(i);
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }

  return escaped;
}

bool firebaseReadyForUse() {
  return WiFi.status() == WL_CONNECTED && app.ready() && firebaseSignupOk;
}

void startFastStatusPushWindow() {
  fastStatusPushUntil = millis() + STATUS_FAST_WINDOW_MS;
}

uint32_t currentStatusPushIntervalMs() {
  if (fastStatusPushUntil != 0 &&
      static_cast<int32_t>(fastStatusPushUntil - millis()) > 0) {
    return STATUS_FAST_PUSH_MS;
  }

  return STATUS_NORMAL_PUSH_MS;
}

void configureWifiRadio() {
  if (wifiRadioConfigured) {
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_TX_POWER);
  WiFi.setHostname("LuxLock-ESP32");
  wifiRadioConfigured = true;
}

// ---------------------------------------------------------------------------
// WiFi + Firebase setup
// ---------------------------------------------------------------------------

void connectWifiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (firebaseInitialized || firebaseSignupOk || firebaseDatabaseBound) {
    setSystemMessage("wifi disconnected");
    resetFirebaseSession();
  }

  uint32_t now = millis();
  if (lastWifiRetryAt != 0 && now - lastWifiRetryAt < WIFI_RETRY_MS) {
    return;
  }

  lastWifiRetryAt = now;
  setSystemMessage("connecting wifi");
  debugPrintf("Connecting to WiFi SSID: %s\n", WIFI_SSID);

  configureWifiRadio();
  WiFi.disconnect(false, false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void syncClockWithNtpIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  uint32_t now = millis();
  if (lastNtpSyncAttemptAt != 0 &&
      now - lastNtpSyncAttemptAt <
          (clockTimeSynced ? NTP_RESYNC_MS : NTP_SYNC_RETRY_MS)) {
    return;
  }

  lastNtpSyncAttemptAt = now;

  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 2000)) {
    debugPrintln("NTP time sync not ready yet.");
    if (!clockTimeSynced) {
      setSystemMessage("waiting for network time");
    }
    return;
  }

  clockTimeSynced = true;
  debugPrintln("Clock synchronized from NTP.");
  setSystemMessage("time synced from network");
  pushStatusSoon();
}

void initFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    if (firebaseInitialized || firebaseSignupOk || firebaseDatabaseBound) {
      resetFirebaseSession();
    }
    return;
  }

  uint32_t now = millis();

  if (firebaseInitialized) {
    if (firebaseSignupOk) {
      return;
    }

    const bool hasAuthError = firebaseClient.lastError().code() != 0;
    const bool retryAfterError =
        hasAuthError && now - firebaseAuthStartedAt >= FIREBASE_RETRY_MS;
    const bool authTimedOut =
        now - firebaseAuthStartedAt >= FIREBASE_AUTH_TIMEOUT_MS;

    if (retryAfterError || authTimedOut) {
      if (hasAuthError) {
        debugPrintf("Restarting Firebase auth after error: %s\n", firebaseLastError().c_str());
      } else {
        debugPrintln("Restarting Firebase auth after timeout.");
      }
      resetFirebaseSession();
      lastFirebaseRetryAt = 0;
    }
    return;
  }

  if (lastFirebaseRetryAt != 0 && now - lastFirebaseRetryAt < FIREBASE_RETRY_MS) {
    return;
  }

  lastFirebaseRetryAt = now;
  configureFirebaseSslClient();
  bindDatabase();
  setSystemMessage("firebase auth starting");
  signup(firebaseClient, app, getAuth(firebaseUser), authDebugPrint, "luxlockAuth");
  firebaseInitialized = true;
  firebaseAuthStartedAt = now;
  firebaseSignupOk = false;
}

// ---------------------------------------------------------------------------
// Fingerprint setup
// ---------------------------------------------------------------------------

void initFingerprint() {
  if (USE_FINGER_TOUCH_PIN) {
    pinMode(FP_TOUCH_PIN, FP_TOUCH_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  }

  fingerprintSerial.begin(57600, SERIAL_8N1, FP_RX_PIN, FP_TX_PIN);
  finger.begin(57600);
  delay(50);

  if (finger.verifyPassword()) {
    fingerprintOk = true;
    finger.getParameters();
    if (finger.security_level != FINGERPRINT_SECURITY_LEVEL) {
      uint8_t securityResult = finger.setSecurityLevel(FINGERPRINT_SECURITY_LEVEL);
      if (securityResult == FINGERPRINT_OK) {
        debugPrintf("Fingerprint security level set to %u\n", FINGERPRINT_SECURITY_LEVEL);
      } else {
        debugPrintf(
            "Failed to set fingerprint security level to %u, code=%u\n",
            FINGERPRINT_SECURITY_LEVEL,
            securityResult);
      }
      finger.getParameters();
    }
    finger.getTemplateCount();
    debugPrintf(
        "Fingerprint sensor ready, stored templates: %u, security level: %u\n",
        finger.templateCount,
        finger.security_level);
    setSystemMessage("fingerprint ready");
  } else {
    fingerprintOk = false;
    debugPrintln("Fingerprint sensor not detected or password mismatch.");
    setSystemMessage("fingerprint unavailable");
  }
}

void retryFingerprintIfNeeded() {
  if (fingerprintOk || enrollInProgress) {
    return;
  }

  uint32_t now = millis();
  if (lastFingerprintRetryAt != 0 &&
      now - lastFingerprintRetryAt < FINGERPRINT_RETRY_MS) {
    return;
  }

  lastFingerprintRetryAt = now;
  initFingerprint();
  pushStatusSoon();
}

// ---------------------------------------------------------------------------
// Actuator control
// ---------------------------------------------------------------------------

bool pinIsUsed(int8_t pin) {
  return pin >= 0;
}

void pinModeIfUsed(int8_t pin, uint8_t mode) {
  if (pinIsUsed(pin)) {
    pinMode(pin, mode);
  }
}

void digitalWriteIfUsed(int8_t pin, uint8_t value) {
  if (pinIsUsed(pin)) {
    digitalWrite(pin, value);
  }
}

void stopActuator(
    uint8_t index,
    bool commitTargetPosition = false,
    bool pushStopStatus = true) {
  auto &act = actuators[index];
  ledcWrite(act.rpwmPin, 0);
  ledcWrite(act.lpwmPin, 0);
  digitalWriteIfUsed(act.renPin, LOW);
  digitalWriteIfUsed(act.lenPin, LOW);
  act.isActive = false;
  act.stopAt = 0;
  if (commitTargetPosition) {
    saveActuatorPosition(index, act.targetOpenPosition);
  }
  act.targetOpenPosition = act.isOpenPosition;
  act.cooldownUntil = ACTUATOR_DIRECTION_COOLDOWN_MS == 0
      ? 0
      : millis() + ACTUATOR_DIRECTION_COOLDOWN_MS;
  act.lastAction = "stopped";
  if (pushStopStatus) {
    pushStatusSoon();
  }
}

bool runActuatorDirection(
    uint8_t index,
    bool openDirection,
    bool pushStartStatus = true) {
  auto &act = actuators[index];
  if (act.isActive) {
    return false;
  }

  if (actuatorCoolingDown(index)) {
    return false;
  }

  bool useRpwm = openDirection ? act.openUsesRpwm : !act.openUsesRpwm;

  digitalWriteIfUsed(act.renPin, HIGH);
  digitalWriteIfUsed(act.lenPin, HIGH);

  if (useRpwm) {
    ledcWrite(act.rpwmPin, MOTOR_PWM_VALUE);
    ledcWrite(act.lpwmPin, 0);
  } else {
    ledcWrite(act.rpwmPin, 0);
    ledcWrite(act.lpwmPin, MOTOR_PWM_VALUE);
  }

  act.isActive = true;
  act.targetOpenPosition = openDirection;
  act.stopAt = millis() + act.runMs;
  act.cooldownUntil = 0;
  act.lastAction = openDirection ? "open" : "close";
  if (pushStartStatus) {
    pushStatusSoon();
  }
  return true;
}

bool openCase(
    uint8_t index,
    bool armAutoCloseTimer = false,
    bool pushStartStatus = true) {
  if (armAutoCloseTimer) {
    armAutoClose(index);
  } else {
    clearAutoClose(index);
  }
  return runActuatorDirection(index, true, pushStartStatus);
}

bool closeCase(uint8_t index, bool pushStartStatus = true) {
  clearAutoClose(index);
  return runActuatorDirection(index, false, pushStartStatus);
}

void stopAllCases(bool pushStopStatus = true) {
  for (uint8_t i = 0; i < 3; i++) {
    clearAutoClose(i);
    stopActuator(i, false, pushStopStatus);
  }
}

bool openAllCases(bool pushStartStatus = true) {
  bool startedAny = false;

  for (uint8_t i = 0; i < 3; i++) {
    if (actuators[i].isOpenPosition) {
      continue;
    }

    if (!openCase(i, false, pushStartStatus)) {
      return false;
    }

    startedAny = true;
  }

  return startedAny;
}

bool closeAllCases(bool pushStartStatus = true) {
  bool startedAny = false;

  for (uint8_t i = 0; i < 3; i++) {
    if (!actuators[i].isOpenPosition) {
      continue;
    }

    if (!closeCase(i, pushStartStatus)) {
      return false;
    }

    startedAny = true;
  }

  return startedAny;
}

void setupActuators() {
  const uint32_t pwmFreq = 20000;
  const uint8_t pwmResolution = 8;

  for (uint8_t i = 0; i < 3; i++) {
    auto &act = actuators[i];

    pinModeIfUsed(act.renPin, OUTPUT);
    pinModeIfUsed(act.lenPin, OUTPUT);
    digitalWriteIfUsed(act.renPin, LOW);
    digitalWriteIfUsed(act.lenPin, LOW);

    ledcAttach(act.rpwmPin, pwmFreq, pwmResolution);
    ledcAttach(act.lpwmPin, pwmFreq, pwmResolution);

    stopActuator(i);
  }
}

void updateActuatorTimeouts() {
  uint32_t now = millis();
  bool stoppedAny = false;
  for (uint8_t i = 0; i < 3; i++) {
    if (actuators[i].isActive && now >= actuators[i].stopAt) {
      const bool reachedOpenPosition = actuators[i].targetOpenPosition;
      const bool shouldScheduleAutoClose =
          actuators[i].autoCloseArmed && reachedOpenPosition;

      stopActuator(i, true, false);
      stoppedAny = true;

      if (shouldScheduleAutoClose) {
        actuators[i].autoCloseAt = now + AUTO_CLOSE_DELAY_MS;
      }
    }

    if (actuators[i].autoCloseArmed &&
        !actuators[i].isActive &&
        actuators[i].isOpenPosition &&
        actuators[i].autoCloseAt != 0 &&
        now >= actuators[i].autoCloseAt) {
      if (closeCase(i, false)) {
        pushHistoryLog(String(i + 1), "Auto Close");
      }
      setSystemMessage(String("auto close ") + actuators[i].name);
    }
  }

  if (stoppedAny) {
    pushStatusSoon();
  }
}

// ---------------------------------------------------------------------------
// History + status
// ---------------------------------------------------------------------------

void pushHistoryLog(const String &caseValue, const String &authorized) {
  ClockSnapshot now = currentBestDateTime();
  String dateValue = formatDate(now);
  String timeValue = formatTime12h(now);

  queuePendingHistoryLog(
      dateValue,
      timeValue,
      currentRtcEpoch(),
      caseValue,
      authorized);
  lastCaseValue = caseValue;
  lastAuthorizedValue = authorized;

  pushStatusSoon();
}

bool tamperHelperReady() {
  if (lastTamperHeartbeatAt == 0) {
    return false;
  }

  const uint32_t now = millis();
  return now - lastTamperHeartbeatAt <= TAMPER_STALE_MS;
}

String tamperFieldValue(const String &line, const String &fieldName) {
  const String token = fieldName + "=";
  int start = line.indexOf(token);
  if (start < 0) {
    return "";
  }

  start += token.length();
  int end = line.indexOf(',', start);
  if (end < 0) {
    end = line.length();
  }

  String value = line.substring(start, end);
  value.trim();
  return value;
}

void handleTamperReadyMessage(const String &line) {
  const String sensorsValue = tamperFieldValue(line, "sensors");
  if (!sensorsValue.isEmpty()) {
    tamperSensorCount = static_cast<uint8_t>(sensorsValue.toInt());
  }

  lastTamperHeartbeatAt = millis();
}

void handleTamperEventMessage(const String &line) {
  const int caseNumber = tamperFieldValue(line, "case").toInt();
  const int level = tamperFieldValue(line, "level").toInt();
  const String peakValue = tamperFieldValue(line, "peak");

  if (caseNumber < 1 || caseNumber > 3 || level < 1 || level > 5) {
    setSystemMessage("invalid tamper message");
    return;
  }

  const uint8_t caseIndex = static_cast<uint8_t>(caseNumber - 1);
  const uint32_t now = millis();
  const bool cooldownDone =
      now - lastTamperLogAt[caseIndex] >= TAMPER_LOG_COOLDOWN_MS;
  const bool moreSevere =
      static_cast<uint8_t>(level) > lastTamperLoggedLevel[caseIndex];

  lastTamperEventAt = now;
  lastTamperCase = static_cast<uint8_t>(caseNumber);
  lastTamperLevel = static_cast<uint8_t>(level);
  lastTamperPeak = peakValue.isEmpty() ? "0.00" : peakValue;
  lastTamperHeartbeatAt = now;

  if (cooldownDone || moreSevere) {
    lastTamperLogAt[caseIndex] = now;
    lastTamperLoggedLevel[caseIndex] = static_cast<uint8_t>(level);
    pushHistoryLog(String(caseNumber), String("Tamper L") + String(level));
  }

  setSystemMessage(
      String("tamper case ") + caseNumber +
      " level " + level +
      " peak " + lastTamperPeak + "g");
  pushStatusSoon();
}

void handleTamperLine(String line) {
  line.trim();
  if (line.isEmpty()) {
    return;
  }

  debugPrintln("[TAMPER] " + line);

  if (line.startsWith("READY")) {
    handleTamperReadyMessage(line);
    return;
  }

  if (line.startsWith("TAMPER")) {
    handleTamperEventMessage(line);
  }
}

void initTamperLink() {
  tamperSerial.begin(TAMPER_UART_BAUD, SERIAL_8N1, TAMPER_RX_PIN, TAMPER_TX_PIN);
  tamperLineBuffer.reserve(128);
  debugPrintf(
      "Tamper UART ready: RX GPIO %d, TX GPIO %d, baud %lu\n",
      TAMPER_RX_PIN,
      TAMPER_TX_PIN,
      static_cast<unsigned long>(TAMPER_UART_BAUD));
}

void pollTamperLink() {
  while (tamperSerial.available() > 0) {
    const char c = static_cast<char>(tamperSerial.read());
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      handleTamperLine(tamperLineBuffer);
      tamperLineBuffer = "";
      continue;
    }

    if (tamperLineBuffer.length() < 120) {
      tamperLineBuffer += c;
    } else {
      tamperLineBuffer = "";
      setSystemMessage("tamper message too long");
    }
  }
}

String buildStatusPayload() {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;

  String payload =
      String("{\"wifiConnected\":") + (wifiConnected ? "true" : "false") +
      ",\"firebaseReady\":" + (app.ready() ? "true" : "false") +
      ",\"rtcReady\":false" +
      ",\"rtcTimeSynced\":" + (clockTimeSynced ? "true" : "false") +
      ",\"timeReady\":" + ((hasNetworkTime() || clockTimeSynced) ? "true" : "false") +
      ",\"timeSource\":\"" + escapeJsonString(hasNetworkTime() ? "ntp" : "compile") + "\"" +
      ",\"fingerprintReady\":" + (fingerprintOk ? "true" : "false") +
      ",\"registeredAccessMask\":" + String(currentRegisteredAccessMask()) +
      ",\"enrollInProgress\":" + (enrollInProgress ? "true" : "false") +
      ",\"lastMessage\":\"" + escapeJsonString(lastSystemMessage) +
      "\",\"lastAuthorized\":\"" + escapeJsonString(lastAuthorizedValue) +
      "\",\"lastCase\":\"" + escapeJsonString(lastCaseValue) +
      "\",\"lastSeenAtMs\":" + String(millis()) +
      ",\"lastSeenAtEpoch\":" + String(currentRtcEpoch()) +
      ",\"pendingHistoryCount\":" + String(pendingHistoryLogCount()) +
      ",\"enrollment\":{\"state\":\"" + escapeJsonString(enrollmentState) +
      "\",\"message\":\"" + escapeJsonString(enrollmentMessage) +
      "\",\"access\":\"" + escapeJsonString(enrollmentAccess) +
      "\",\"nonce\":\"" + escapeJsonString(formatNonce(enrollmentNonce)) +
      "\",\"templateId\":" + String(enrollmentTemplateId) +
      "},\"tamper\":{\"ready\":" + (tamperHelperReady() ? "true" : "false") +
      ",\"sensorCount\":" + String(tamperSensorCount) +
      ",\"lastCase\":" + String(lastTamperCase) +
      ",\"lastLevel\":" + String(lastTamperLevel) +
      ",\"lastPeakG\":\"" + escapeJsonString(lastTamperPeak) +
      "\",\"lastSeenAtMs\":" + String(lastTamperEventAt) +
      "},\"ack\":{\"lastCommandError\":\"" + escapeJsonString(lastCommandError) +
      "\",\"lastProcessedOverrideNonce\":\"" + escapeJsonString(formatNonce(lastProcessedOverrideNonce)) +
      "\",\"lastOverrideTarget\":\"" + escapeJsonString(lastOverrideTarget) +
      "\",\"lastOverrideAction\":\"" + escapeJsonString(lastOverrideAction) +
      "\",\"lastOverrideResult\":\"" + escapeJsonString(lastOverrideResult) +
      "\",\"lastOverrideError\":\"" + escapeJsonString(lastOverrideError) +
      "\"},\"actuators\":{";

  for (uint8_t i = 0; i < 3; i++) {
    if (i > 0) {
      payload += ",";
    }

    payload +=
        String("\"") + actuators[i].name +
        "\":{\"active\":" + (actuators[i].isActive ? "true" : "false") +
        ",\"state\":\"" + escapeJsonString(actuators[i].lastAction) +
        "\",\"positionOpen\":" + (actuators[i].isOpenPosition ? "true" : "false") +
        ",\"autoCloseArmed\":" + (actuators[i].autoCloseArmed ? "true" : "false") +
        ",\"cooldownActive\":" + (actuatorCoolingDown(i) ? "true" : "false") +
        "}";
  }

  payload += "}}";
  return payload;
}

void pushStatus() {
  if (!firebaseReadyForUse()) {
    return;
  }

  const String payload = buildStatusPayload();

  if (!Database.update<object_t>(firebaseClient, statusPath(), object_t(payload))) {
    debugPrintf("Failed to push status: %s\n", firebaseLastError().c_str());
    noteFirebaseOperationFailure("status update");
  } else {
    noteFirebaseOperationSuccess();
  }
}

// ---------------------------------------------------------------------------
// Command reading
// ---------------------------------------------------------------------------

uint64_t readNonceAt(const String &path) {
  double numericValue = Database.get<double>(firebaseClient, path);
  if (firebaseClient.lastError().code() == 0) {
    noteFirebaseOperationSuccess();
    return static_cast<uint64_t>(numericValue);
  }

  String stringValue = Database.get<String>(firebaseClient, path);
  if (firebaseClient.lastError().code() == 0) {
    noteFirebaseOperationSuccess();
    return parseNonceString(stringValue);
  }

  noteFirebaseOperationFailure("nonce read");
  return 0;
}

String readStringAt(const String &path) {
  String value = Database.get<String>(firebaseClient, path);
  if (firebaseClient.lastError().code() != 0) {
    noteFirebaseOperationFailure("string read");
    return "";
  }
  noteFirebaseOperationSuccess();
  return value;
}

String readJsonNodeAt(const String &path) {
  String value = Database.get<String>(firebaseClient, path);
  if (firebaseClient.lastError().code() != 0) {
    noteFirebaseOperationFailure("json read");
    return "";
  }

  noteFirebaseOperationSuccess();
  value.trim();
  if (value.isEmpty() || value == "null") {
    return "";
  }

  return value;
}

int findJsonValueStart(const String &json, const String &key) {
  const String needle = "\"" + key + "\"";
  int index = json.indexOf(needle);
  if (index < 0) {
    return -1;
  }

  index = json.indexOf(':', index + needle.length());
  if (index < 0) {
    return -1;
  }

  index++;
  while (index < json.length()) {
    const char ch = json.charAt(index);
    if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
      return index;
    }
    index++;
  }

  return -1;
}

String extractJsonStringField(const String &json, const String &key) {
  int index = findJsonValueStart(json, key);
  if (index < 0) {
    return "";
  }

  if (json.charAt(index) != '"') {
    return "";
  }

  index++;
  String value = "";
  bool escaped = false;

  while (index < json.length()) {
    const char ch = json.charAt(index++);
    if (escaped) {
      switch (ch) {
        case '"':
        case '\\':
        case '/':
          value += ch;
          break;
        case 'b':
          value += '\b';
          break;
        case 'f':
          value += '\f';
          break;
        case 'n':
          value += '\n';
          break;
        case 'r':
          value += '\r';
          break;
        case 't':
          value += '\t';
          break;
        default:
          value += ch;
          break;
      }
      escaped = false;
      continue;
    }

    if (ch == '\\') {
      escaped = true;
      continue;
    }

    if (ch == '"') {
      return value;
    }

    value += ch;
  }

  return "";
}

uint64_t extractJsonNonceField(const String &json, const String &key) {
  int index = findJsonValueStart(json, key);
  if (index < 0) {
    return 0;
  }

  if (json.charAt(index) == '"') {
    return parseNonceString(extractJsonStringField(json, key));
  }

  int end = index;
  while (end < json.length()) {
    const char ch = json.charAt(end);
    if ((ch >= '0' && ch <= '9') || ch == '-') {
      end++;
      continue;
    }
    break;
  }

  if (end <= index) {
    return 0;
  }

  return parseNonceString(json.substring(index, end));
}

bool isEnrollCancellationRequested() {
  if (!enrollInProgress || enrollmentNonce == 0) {
    return false;
  }

  if (enrollCancelRequested) {
    return true;
  }

  if (!firebaseReadyForUse()) {
    return false;
  }

  uint32_t now = millis();
  if (now - lastEnrollCancelCheckAt < ENROLL_CANCEL_POLL_MS) {
    return false;
  }

  lastEnrollCancelCheckAt = now;
  uint64_t cancelNonce = readNonceAt(enrollBasePath() + "/cancelNonce");

  if (cancelNonce == enrollmentNonce && cancelNonce != 0) {
    enrollCancelRequested = true;
    return true;
  }

  return false;
}

void markEnrollmentCanceled(int32_t templateId = -1) {
  setSystemMessage("enroll canceled");
  updateEnrollmentStatus(
      "canceled",
      "Fingerprint registration canceled.",
      pendingEnrollAccess,
      enrollmentNonce,
      templateId);
  saveEnrollNonce(enrollmentNonce);
}

void finishEnrollmentSession() {
  enrollInProgress = false;
  pendingEnrollAccess = "";
  enrollCancelRequested = false;
  pushStatusSoon();
}

void clearAllSavedFingerprintAccess() {
  for (uint16_t id = 1; id <= FINGERPRINT_MAX_TEMPLATES; id++) {
    clearAccessForFingerprint(id);
  }
}

AccessCleanupResult deleteFingerprintAccess(uint8_t deleteMask) {
  AccessCleanupResult result = {0, 0};

  if (!fingerprintOk || deleteMask == 0) {
    return result;
  }

  for (uint16_t id = 1; id <= FINGERPRINT_MAX_TEMPLATES; id++) {
    const String existingAccess = accessForFingerprint(id);
    if (existingAccess.isEmpty()) {
      continue;
    }

    const uint8_t existingMask = accessMaskFromString(existingAccess);
    if (existingMask == 0 || (existingMask & deleteMask) == 0) {
      continue;
    }

    const uint8_t remainingMask = existingMask & ~deleteMask;
    if (remainingMask == 0) {
      bool canClearMapping = true;
      uint8_t loadResult = finger.loadModel(id);
      if (loadResult == FINGERPRINT_OK) {
        uint8_t deleteResult = finger.deleteModel(id);
        if (deleteResult != FINGERPRINT_OK) {
          debugPrintf(
              "Failed to delete fingerprint %u for access %s: %d\n",
              id,
              existingAccess.c_str(),
              deleteResult);
          canClearMapping = false;
        }
      }

      if (!canClearMapping) {
        continue;
      }

      clearAccessForFingerprint(id);
      result.removedCount++;
      continue;
    }

    const String updatedAccess = accessStringFromMask(remainingMask);
    if (updatedAccess != existingAccess) {
      saveAccessForFingerprint(id, updatedAccess);
      result.updatedCount++;
      debugPrintf(
          "Updated fingerprint %u access from %s to %s during delete\n",
          id,
          existingAccess.c_str(),
          updatedAccess.c_str());
    }
  }

  return result;
}

void processDeleteAllFingerprintsCommand(uint64_t nonce) {
  updateEnrollmentStatus(
      "deleting_all",
      "Deleting all stored fingerprints.",
      "",
      nonce,
      -1);

  if (!fingerprintOk) {
    setSystemMessage("fingerprint sensor unavailable");
    setCommandError("fingerprint sensor unavailable");
    updateEnrollmentStatus(
        "failed",
        "Fingerprint sensor is not ready.",
        "",
        nonce,
        -1);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  if (anyActuatorActive()) {
    setSystemMessage("delete fingerprints blocked while actuator is moving");
    setCommandError("delete fingerprints blocked while actuator is moving");
    updateEnrollmentStatus(
        "failed",
        "Wait for all case movement to finish before deleting fingerprints.",
        "",
        nonce,
        -1);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  const uint8_t result = finger.emptyDatabase();
  if (result != FINGERPRINT_OK) {
    debugPrintf("emptyDatabase failed: %u\n", result);
    setSystemMessage("delete fingerprints failed");
    setCommandError(String("delete fingerprints failed code=") + result);
    updateEnrollmentStatus(
        "failed",
        "The fingerprint sensor could not clear its database.",
        "",
        nonce,
        -1);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  clearAllSavedFingerprintAccess();
  finger.getTemplateCount();
  clearCommandError();
  saveEnrollNonce(nonce);
  lastProcessedEnrollNonce = nonce;
  fingerCooldownUntil = millis() + FINGER_COOLDOWN_MS;
  setSystemMessage("all fingerprints deleted");
  updateEnrollmentStatus(
      "wipe_success",
      "All fingerprints have been deleted.",
      "",
      nonce,
      -1);
}

void processDeleteFingerprintAccessCommand(
    uint64_t nonce,
    const String &deleteAccess) {
  updateEnrollmentStatus(
      "deleting_access",
      "Deleting selected fingerprint access.",
      deleteAccess,
      nonce,
      -1);

  const uint8_t deleteMask = accessMaskFromString(deleteAccess);
  if (deleteMask == 0) {
    setSystemMessage("delete fingerprint access missing target");
    setCommandError("delete fingerprint access missing target");
    updateEnrollmentStatus(
        "failed",
        "Delete request is missing a valid case selection.",
        deleteAccess,
        nonce,
        -1);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  if (!fingerprintOk) {
    setSystemMessage("fingerprint sensor unavailable");
    setCommandError("fingerprint sensor unavailable");
    updateEnrollmentStatus(
        "failed",
        "Fingerprint sensor is not ready.",
        deleteAccess,
        nonce,
        -1);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  if (anyActuatorActive()) {
    setSystemMessage("delete fingerprints blocked while actuator is moving");
    setCommandError("delete fingerprints blocked while actuator is moving");
    updateEnrollmentStatus(
        "failed",
        "Wait for all case movement to finish before deleting fingerprints.",
        deleteAccess,
        nonce,
        -1);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  AccessCleanupResult cleanup = deleteFingerprintAccess(deleteMask);
  const int changedCount = cleanup.removedCount + cleanup.updatedCount;
  if (changedCount == 0) {
    setSystemMessage("no fingerprint access found to delete");
    setCommandError("no fingerprint access found to delete");
    updateEnrollmentStatus(
        "failed",
        "No stored fingerprint access matched that case.",
        deleteAccess,
        nonce,
        -1);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  finger.getTemplateCount();
  clearCommandError();
  saveEnrollNonce(nonce);
  lastProcessedEnrollNonce = nonce;
  fingerCooldownUntil = millis() + FINGER_COOLDOWN_MS;
  setSystemMessage(String("fingerprint access deleted ") + deleteAccess);
  updateEnrollmentStatus(
      "wipe_success",
      String("Deleted fingerprint access for ") + accessLabel(deleteAccess) + ".",
      deleteAccess,
      nonce,
      -1);
}

String historyCaseValueForTarget(const String &target) {
  if (target == "case1") {
    return "1";
  }

  if (target == "case2") {
    return "2";
  }

  if (target == "case3") {
    return "3";
  }

  if (target == "case1_case2" || target == "case2_case1") {
    return "1,2";
  }

  if (target == "case1_case3" || target == "case3_case1") {
    return "1,3";
  }

  if (target == "case2_case3" || target == "case3_case2") {
    return "2,3";
  }

  if (target == "all") {
    return "ALL";
  }

  return "N/A";
}

uint8_t overrideMaskFromTarget(const String &target) {
  if (target == "case1") return 0b001;
  if (target == "case2") return 0b010;
  if (target == "case3") return 0b100;
  if (target == "case1_case2" || target == "case2_case1") return 0b011;
  if (target == "case1_case3" || target == "case3_case1") return 0b101;
  if (target == "case2_case3" || target == "case3_case2") return 0b110;
  if (target == "all") return 0b111;
  return 0;
}

bool selectedCasesAllOpen(uint8_t mask) {
  if (mask == 0) {
    return false;
  }

  for (int index = 0; index < 3; index++) {
    if ((mask & (1 << index)) != 0 && !actuators[index].isOpenPosition) {
      return false;
    }
  }

  return true;
}

bool selectedCasesAllClosed(uint8_t mask) {
  if (mask == 0) {
    return false;
  }

  for (int index = 0; index < 3; index++) {
    if ((mask & (1 << index)) != 0 && actuators[index].isOpenPosition) {
      return false;
    }
  }

  return true;
}

bool openCasesByMask(uint8_t mask, bool pushStartStatus) {
  bool started = false;

  for (int index = 0; index < 3; index++) {
    if ((mask & (1 << index)) == 0 || actuators[index].isOpenPosition) {
      continue;
    }

    if (openCase(index, false, pushStartStatus)) {
      started = true;
    }
  }

  return started;
}

bool closeCasesByMask(uint8_t mask, bool pushStartStatus) {
  bool started = false;

  for (int index = 0; index < 3; index++) {
    if ((mask & (1 << index)) == 0 || !actuators[index].isOpenPosition) {
      continue;
    }

    if (closeCase(index, pushStartStatus)) {
      started = true;
    }
  }

  return started;
}

void stopCasesByMask(uint8_t mask, bool pushStopStatus) {
  for (int index = 0; index < 3; index++) {
    if ((mask & (1 << index)) == 0 || !actuators[index].isActive) {
      continue;
    }

    clearAutoClose(index);
    stopActuator(index, false, pushStopStatus);
  }
}

bool handleOverride(const String &target, const String &action, String &errorOut) {
  debugPrintf("Override: target=%s action=%s\n", target.c_str(), action.c_str());
  errorOut = "";

  const uint8_t targetMask = overrideMaskFromTarget(target);
  const String targetLabel = historyCaseValueForTarget(target);

  if (targetMask == 0) {
    setSystemMessage("override target invalid");
    errorOut = String("override target invalid: ") + target;
    setCommandError(errorOut);
    return false;
  }

  if (enrollInProgress) {
    setSystemMessage("override blocked during enrollment");
    errorOut = "override blocked during enrollment";
    setCommandError(errorOut);
    return false;
  }

  if (action == "stop") {
    stopCasesByMask(targetMask, false);
    setSystemMessage(String("override stop ") + targetLabel);
    clearCommandError();
    return true;
  }

  if (anyActuatorActive()) {
    setSystemMessage("override blocked while actuator is moving");
    errorOut = "override blocked while actuator is moving";
    setCommandError(errorOut);
    return false;
  }

  if (anyActuatorCoolingDown()) {
    setSystemMessage("override blocked during actuator cooldown");
    errorOut = "override blocked during actuator cooldown";
    setCommandError(errorOut);
    return false;
  }

  if (action == "open") {
    if (selectedCasesAllOpen(targetMask)) {
      setSystemMessage(String("override open skipped ") + targetLabel);
      errorOut = targetLabel + " already open";
      setCommandError(errorOut);
      return false;
    }

    if (!openCasesByMask(targetMask, false)) {
      setSystemMessage(String("override open failed to start ") + targetLabel);
      errorOut = targetLabel + " could not start opening";
      setCommandError(errorOut);
      return false;
    }
    setSystemMessage(String("override open ") + targetLabel);
    clearCommandError();
    return true;
  } else if (action == "close") {
    if (selectedCasesAllClosed(targetMask)) {
      setSystemMessage(String("override close skipped ") + targetLabel);
      errorOut = targetLabel + " already closed";
      setCommandError(errorOut);
      return false;
    }

    if (!closeCasesByMask(targetMask, false)) {
      setSystemMessage(String("override close failed to start ") + targetLabel);
      errorOut = targetLabel + " could not start closing";
      setCommandError(errorOut);
      return false;
    }
    setSystemMessage(String("override close ") + targetLabel);
    clearCommandError();
    return true;
  } else {
    setSystemMessage("override action invalid");
    errorOut = String("override action invalid: ") + action;
    setCommandError(errorOut);
    return false;
  }
}

String overrideHistoryLabelForAction(const String &action) {
  if (action == "open") {
    return "Override Open";
  }

  if (action == "close") {
    return "Override Close";
  }

  if (action == "stop") {
    return "Override Stop";
  }

  return "";
}

bool processOverrideCommandPayload(
    uint64_t nonce,
    const String &target,
    const String &action,
    bool &acceptedOut,
    String &errorOut) {
  acceptedOut = false;
  errorOut = "";
  lastOverrideTarget = target;
  lastOverrideAction = action;

  if (nonce == 0) {
    setSystemMessage("override nonce missing");
    errorOut = "override nonce missing";
    setCommandError(errorOut);
    return false;
  }

  if (target.isEmpty() || action.isEmpty()) {
    setSystemMessage("override payload incomplete");
    errorOut =
        String("override payload incomplete target='") + target +
        "' action='" + action + "'";
    setCommandError(errorOut);
    saveOverrideNonce(nonce);
    recordOverrideResult(nonce, target, action, false, errorOut);
    pushStatusSoon();
    return false;
  }

  String errorMessage;
  const bool handled = handleOverride(target, action, errorMessage);
  saveOverrideNonce(nonce);
  recordOverrideResult(nonce, target, action, handled, errorMessage);
  pushStatusSoon();

  if (handled) {
    const String historyLabel = overrideHistoryLabelForAction(action);
    if (!historyLabel.isEmpty()) {
      pushHistoryLog(historyCaseValueForTarget(target), historyLabel);
    }
  }

  acceptedOut = handled;
  errorOut = errorMessage;
  return handled;
}

void pollOverrideCommand() {
  const String commandJson = readJsonNodeAt(overrideBasePath());
  uint64_t nonce = extractJsonNonceField(commandJson, "nonce");
  if (nonce != 0) {
    lastSeenOverrideNonce = nonce;
  }

  if (nonce == 0 || nonce == lastOverrideNonce) {
    return;
  }

  String target = extractJsonStringField(commandJson, "target");
  String action = extractJsonStringField(commandJson, "action");
  String errorMessage;
  bool accepted = false;
  processOverrideCommandPayload(nonce, target, action, accepted, errorMessage);
}

// ---------------------------------------------------------------------------
// Enrollment
// ---------------------------------------------------------------------------

int waitForFingerImage(uint32_t timeoutMs, const char *prompt) {
  debugPrintln(prompt);
  uint32_t startedAt = millis();

  while (millis() - startedAt < timeoutMs) {
    if (isEnrollCancellationRequested()) {
      return ENROLL_CANCELLED;
    }

    uint8_t result = finger.getImage();

    if (result == FINGERPRINT_OK) {
      return FINGERPRINT_OK;
    }

    if (result != FINGERPRINT_NOFINGER) {
      return result;
    }

    delay(50);
  }

  return ENROLL_WAIT_TIMEOUT;
}

bool waitFingerRemoved(uint32_t timeoutMs) {
  uint32_t startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    if (isEnrollCancellationRequested()) {
      return false;
    }

    if (finger.getImage() == FINGERPRINT_NOFINGER) {
      return true;
    }
    delay(50);
  }
  return false;
}

void armPostEnrollmentCooldown() {
  const uint32_t startedAt = millis();
  bool fingerRemoved = false;

  while (millis() - startedAt < 5000) {
    if (finger.getImage() == FINGERPRINT_NOFINGER) {
      fingerRemoved = true;
      break;
    }
    delay(50);
  }

  fingerCooldownUntil = millis() + (fingerRemoved ? FINGER_COOLDOWN_MS : 8000);
}

uint16_t findNextFreeTemplateId() {
  if (!fingerprintOk) {
    return 0;
  }

  finger.getTemplateCount();

  for (uint16_t id = 1; id <= FINGERPRINT_MAX_TEMPLATES; id++) {
    uint8_t loadResult = finger.loadModel(id);
    if (loadResult != FINGERPRINT_OK) {
      return id;
    }
  }

  return 0;
}

EnrollmentResult enrollFingerprint(const String &requestedAccess) {
  EnrollmentResult outcome = {false, false, 0, requestedAccess, ""};
  const uint16_t nextFreeTemplateId = findNextFreeTemplateId();

  for (uint8_t attempt = 1; attempt <= ENROLL_SCAN_PAIR_ATTEMPTS; attempt++) {
    uint16_t confirmedExistingTemplateId = 0;
    String confirmedExistingAccess = "";

    updateEnrollmentStatus(
        "waiting_for_finger",
        "Please place your finger on the sensor.",
        pendingEnrollAccess,
        enrollmentNonce,
        -1);

    int result = waitForFingerImage(ENROLL_TIMEOUT_MS, "Place finger for first scan...");
    if (result == ENROLL_CANCELLED) {
      debugPrintln("Enrollment canceled before first scan.");
      markEnrollmentCanceled();
      return outcome;
    }

    if (result == ENROLL_WAIT_TIMEOUT) {
      debugPrintln("First scan timed out.");
      updateEnrollmentStatus(
          "failed",
          "Timed out waiting for the first fingerprint scan.",
          pendingEnrollAccess,
          enrollmentNonce,
          -1);
      return outcome;
    }

    if (result != FINGERPRINT_OK) {
      debugPrintf("First scan failed: %d\n", result);
      updateEnrollmentStatus(
          "failed",
          "The first fingerprint scan could not be captured.",
          pendingEnrollAccess,
          enrollmentNonce,
          -1);
      return outcome;
    }

    result = finger.image2Tz(1);
    if (isEnrollCancellationRequested()) {
      debugPrintln("Enrollment canceled after first scan.");
      markEnrollmentCanceled();
      return outcome;
    }

    if (result != FINGERPRINT_OK) {
      debugPrintf("First image conversion failed: %d\n", result);
      if (attempt < ENROLL_SCAN_PAIR_ATTEMPTS) {
        updateEnrollmentStatus(
            "remove_finger",
            "The first scan was unclear. Remove your finger, then try again.",
            pendingEnrollAccess,
            enrollmentNonce,
            -1);
        if (waitFingerRemoved(ENROLL_TIMEOUT_MS)) {
          continue;
        }
      }

      updateEnrollmentStatus(
          "failed",
          "The first fingerprint scan could not be processed.",
          pendingEnrollAccess,
          enrollmentNonce,
          -1);
      return outcome;
    }

    uint8_t searchResult = finger.fingerFastSearch();
    if (searchResult == FINGERPRINT_OK) {
      confirmedExistingTemplateId = finger.fingerID;
      confirmedExistingAccess = accessForFingerprint(confirmedExistingTemplateId);
      debugPrintf(
          "First scan matched existing fingerprint template %u during enrollment. Previous access=%s confidence=%u\n",
          confirmedExistingTemplateId,
          confirmedExistingAccess.c_str(),
          finger.confidence);
    } else if (searchResult != FINGERPRINT_NOTFOUND) {
      debugPrintf(
          "Existing fingerprint pre-check on first scan failed: %u. Continuing with confirmation flow.\n",
          searchResult);
    } else {
      debugPrintln("First scan did not match an existing fingerprint template.");
    }

    updateEnrollmentStatus(
        "remove_finger",
        "Remove your finger from the sensor.",
        pendingEnrollAccess,
        enrollmentNonce,
        -1);

    if (!waitFingerRemoved(ENROLL_TIMEOUT_MS)) {
      if (enrollCancelRequested) {
        debugPrintln("Enrollment canceled while waiting for finger removal.");
        markEnrollmentCanceled();
        return outcome;
      }

      debugPrintln("Finger was not removed in time.");
      updateEnrollmentStatus(
          "failed",
          "Please remove your finger and try again.",
          pendingEnrollAccess,
          enrollmentNonce,
          -1);
      return outcome;
    }

    const uint16_t firstMatchedTemplateId = confirmedExistingTemplateId;
    const String firstMatchedAccess = confirmedExistingAccess;

    updateEnrollmentStatus(
        "scan_second_time",
        "Place the same finger again to finish registration.",
        pendingEnrollAccess,
        enrollmentNonce,
        -1);

    result = waitForFingerImage(ENROLL_TIMEOUT_MS, "Place same finger again...");
    if (result == ENROLL_CANCELLED) {
      debugPrintln("Enrollment canceled before second scan.");
      markEnrollmentCanceled();
      return outcome;
    }

    if (result == ENROLL_WAIT_TIMEOUT) {
      debugPrintln("Second scan timed out.");
      updateEnrollmentStatus(
          "failed",
          "Timed out waiting for the second fingerprint scan.",
          pendingEnrollAccess,
          enrollmentNonce,
          -1);
      return outcome;
    }

    if (result != FINGERPRINT_OK) {
      debugPrintf("Second scan failed: %d\n", result);
      updateEnrollmentStatus(
          "failed",
          "The second fingerprint scan could not be captured.",
          pendingEnrollAccess,
          enrollmentNonce,
          -1);
      return outcome;
    }

    result = finger.image2Tz(2);
    if (isEnrollCancellationRequested()) {
      debugPrintln("Enrollment canceled after second scan.");
      markEnrollmentCanceled();
      return outcome;
    }

    if (result != FINGERPRINT_OK) {
      debugPrintf("Second image conversion failed: %d\n", result);
      if (attempt < ENROLL_SCAN_PAIR_ATTEMPTS) {
        updateEnrollmentStatus(
            "remove_finger",
            "The second scan was unclear. Remove your finger, then try again.",
            pendingEnrollAccess,
            enrollmentNonce,
            -1);
        if (waitFingerRemoved(ENROLL_TIMEOUT_MS)) {
          continue;
        }
      }

      updateEnrollmentStatus(
          "failed",
          "The second fingerprint scan could not be processed.",
          pendingEnrollAccess,
          enrollmentNonce,
          -1);
      return outcome;
    }

    confirmedExistingTemplateId = firstMatchedTemplateId;
    confirmedExistingAccess = firstMatchedAccess;

    if (nextFreeTemplateId == 0 && confirmedExistingTemplateId == 0) {
      setSystemMessage("no free fingerprint slots");
      setCommandError("no free fingerprint slots");
      updateEnrollmentStatus(
          "failed",
          "No free fingerprint slots are available.",
          pendingEnrollAccess,
          enrollmentNonce,
          -1);
      return outcome;
    }

    outcome.templateId = confirmedExistingTemplateId != 0
        ? confirmedExistingTemplateId
        : nextFreeTemplateId;

    updateEnrollmentStatus(
        "create_new_template",
        "Checking whether the two scans match before saving.",
        pendingEnrollAccess,
        enrollmentNonce,
        outcome.templateId);

    result = finger.createModel();
    if (isEnrollCancellationRequested()) {
      debugPrintln("Enrollment canceled before model creation.");
      markEnrollmentCanceled(outcome.templateId);
      return outcome;
    }

    if (result != FINGERPRINT_OK) {
      debugPrintf("createModel failed: %d\n", result);
      if (attempt < ENROLL_SCAN_PAIR_ATTEMPTS) {
        updateEnrollmentStatus(
            "remove_finger",
            "The scans did not match. Remove your finger, then restart with a fresh first scan.",
            pendingEnrollAccess,
            enrollmentNonce,
            outcome.templateId);
        if (waitFingerRemoved(ENROLL_TIMEOUT_MS)) {
          continue;
        }
      }

      updateEnrollmentStatus(
          "failed",
          "The two scans did not match. Restart enrollment and place the same finger in the same position.",
          pendingEnrollAccess,
          enrollmentNonce,
          outcome.templateId);
      return outcome;
    }

    if (confirmedExistingTemplateId != 0) {
      outcome.ok = true;
      outcome.reusedExistingTemplate = true;
      outcome.templateId = confirmedExistingTemplateId;
      outcome.previousAccess = confirmedExistingAccess;

      const uint8_t mergedMask =
          accessMaskFromString(outcome.previousAccess) |
          accessMaskFromString(requestedAccess);
      outcome.savedAccess =
          mergedMask == 0 ? requestedAccess : accessStringFromMask(mergedMask);

      debugPrintf(
          "Confirmed existing fingerprint template %u during enrollment. Previous access=%s merged access=%s\n",
          outcome.templateId,
          outcome.previousAccess.c_str(),
          outcome.savedAccess.c_str());
      return outcome;
    }

    result = finger.storeModel(outcome.templateId);
    if (isEnrollCancellationRequested()) {
      debugPrintln("Enrollment canceled before storing model.");
      markEnrollmentCanceled(outcome.templateId);
      return outcome;
    }

    if (result != FINGERPRINT_OK) {
      debugPrintf("storeModel failed: %d\n", result);
      updateEnrollmentStatus(
          "failed",
          "The fingerprint could not be stored in memory.",
          pendingEnrollAccess,
          enrollmentNonce,
          outcome.templateId);
      return outcome;
    }

    outcome.ok = true;
    outcome.reusedExistingTemplate = false;
    outcome.savedAccess = requestedAccess;
    return outcome;
  }

  updateEnrollmentStatus(
      "failed",
      "Fingerprint registration could not get two matching scans.",
      pendingEnrollAccess,
      enrollmentNonce,
      -1);
  return outcome;
}

void processEnrollCommandPayload(
    uint64_t nonce,
    String action,
    const String &access,
    const String &deleteAccess) {
  lastEnrollAccessSeen = access;
  if (nonce != 0) {
    lastSeenEnrollNonce = nonce;
  }

  if (nonce == 0) {
    setSystemMessage("enroll nonce missing");
    setCommandError("enroll nonce missing");
    return;
  }

  if (action.isEmpty()) {
    action = "register";
  }

  if (action == "delete_all") {
    processDeleteAllFingerprintsCommand(nonce);
    return;
  }

  if (action == "delete_access") {
    processDeleteFingerprintAccessCommand(nonce, deleteAccess);
    return;
  }

  if (!action.isEmpty() && action != "register") {
    setSystemMessage("unknown enrollment command");
    setCommandError(String("unknown enrollment action=") + action);
    updateEnrollmentStatus(
        "failed",
        "Unknown enrollment command.",
        access,
        nonce);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  if (access.isEmpty()) {
    setSystemMessage("enroll payload missing access");
    setCommandError("enroll payload missing access");
    updateEnrollmentStatus(
        "failed",
        "Enrollment request is missing an access scope.",
        "",
        nonce);
    saveEnrollNonce(nonce);
    return;
  }

  if (!fingerprintOk) {
    setSystemMessage("fingerprint sensor unavailable");
    setCommandError("fingerprint sensor unavailable");
    updateEnrollmentStatus(
        "failed",
        "Fingerprint sensor is not ready.",
        access,
        nonce);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  if (anyActuatorActive()) {
    setSystemMessage("close all cases before enrollment");
    setCommandError("enroll blocked while actuator is moving");
    updateEnrollmentStatus(
        "failed",
        "Wait for all case movement to finish before enrolling.",
        access,
        nonce);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  if (anyActuatorCoolingDown()) {
    setSystemMessage("wait for actuator cooldown before enrollment");
    setCommandError("enroll blocked during actuator cooldown");
    updateEnrollmentStatus(
        "failed",
        "Please wait a moment for the case hardware to settle before enrolling.",
        access,
        nonce);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  if (anyCaseOpen()) {
    setSystemMessage("close all cases before enrollment");
    setCommandError("enroll blocked while cases are open");
    updateEnrollmentStatus(
        "failed",
        "Close all cases before registering a fingerprint.",
        access,
        nonce);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    return;
  }

  enrollInProgress = true;
  pendingEnrollAccess = access;
  enrollmentNonce = nonce;
  enrollCancelRequested = false;
  lastEnrollCancelCheckAt = 0;
  setSystemMessage(String("enrolling access ") + access);
  updateEnrollmentStatus(
      "waiting_for_finger",
      "Please place your finger on the sensor.",
      access,
      nonce);

  if (isEnrollCancellationRequested()) {
    markEnrollmentCanceled();
    lastProcessedEnrollNonce = nonce;
    finishEnrollmentSession();
    return;
  }

  EnrollmentResult enrollmentResult = enrollFingerprint(access);
  if (enrollmentResult.ok) {
    AccessCleanupResult cleanupResult =
        reassignOverlappingFingerprintAccess(
            enrollmentResult.templateId,
            enrollmentResult.savedAccess);
    saveAccessForFingerprint(
        enrollmentResult.templateId,
        enrollmentResult.savedAccess);
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    clearCommandError();
    finger.getTemplateCount();
    if (enrollmentResult.reusedExistingTemplate) {
      const bool accessChanged =
          enrollmentResult.previousAccess != enrollmentResult.savedAccess;
      if (cleanupResult.removedCount > 0 || cleanupResult.updatedCount > 0) {
        setSystemMessage(
            String("enroll updated existing id=") + enrollmentResult.templateId +
            " access=" + enrollmentResult.savedAccess +
            " removed=" + cleanupResult.removedCount +
            " updated=" + cleanupResult.updatedCount);
      } else if (accessChanged) {
        setSystemMessage(
            String("enroll merged existing id=") + enrollmentResult.templateId +
            " access=" + enrollmentResult.savedAccess);
      } else {
        setSystemMessage(
            String("enroll reused existing id=") + enrollmentResult.templateId +
            " access=" + enrollmentResult.savedAccess);
      }

      updateEnrollmentStatus(
          "success",
          accessChanged
              ? "Existing fingerprint matched. Access has been updated."
              : "Existing fingerprint matched. Access was already assigned.",
          enrollmentResult.savedAccess,
          nonce,
          enrollmentResult.templateId);
    } else if (cleanupResult.removedCount > 0 || cleanupResult.updatedCount > 0) {
      setSystemMessage(
          String("enroll reassigned access ") + enrollmentResult.savedAccess +
          " removed=" + cleanupResult.removedCount +
          " updated=" + cleanupResult.updatedCount);
      updateEnrollmentStatus(
          "success",
          "Fingerprint registered successfully.",
          enrollmentResult.savedAccess,
          nonce,
          enrollmentResult.templateId);
    } else {
      setSystemMessage(
          String("enroll success id=") + enrollmentResult.templateId +
          " access=" + enrollmentResult.savedAccess);
      updateEnrollmentStatus(
          "success",
          "Fingerprint registered successfully.",
          enrollmentResult.savedAccess,
          nonce,
          enrollmentResult.templateId);
    }
    armPostEnrollmentCooldown();
  } else if (!enrollCancelRequested && enrollmentState != "canceled") {
    setSystemMessage("enroll failed");
    saveEnrollNonce(nonce);
    lastProcessedEnrollNonce = nonce;
    setCommandError("enroll failed before completion");
    armPostEnrollmentCooldown();
  }

  finishEnrollmentSession();
}

void processEnrollCommand() {
  if (enrollInProgress) {
    return;
  }

  const String commandJson = readJsonNodeAt(enrollBasePath());
  uint64_t nonce = extractJsonNonceField(commandJson, "nonce");
  if (nonce != 0) {
    lastSeenEnrollNonce = nonce;
  }
  if (nonce == 0 || nonce == lastEnrollNonce) {
    return;
  }

  String access = extractJsonStringField(commandJson, "access");
  String action = extractJsonStringField(commandJson, "action");
  String deleteAccess = extractJsonStringField(commandJson, "deleteAccess");
  processEnrollCommandPayload(nonce, action, access, deleteAccess);
}

// ---------------------------------------------------------------------------
// Fingerprint access
// ---------------------------------------------------------------------------

void authorizeAccess(const String &access) {
  const uint8_t accessMask = accessMaskFromString(access);
  if (accessMask == 0) {
    pushHistoryLog("N/A", "Fingerprint Failed");
    setSystemMessage("authorized finger without mapped cases");
    return;
  }

  if (anyActuatorActive()) {
    setSystemMessage("fingerprint access blocked while actuator is moving");
    return;
  }

  if (accessMaskCoolingDown(accessMask)) {
    setSystemMessage("fingerprint access blocked during actuator cooldown");
    return;
  }

  bool allAssignedCasesOpen = true;

  for (uint8_t i = 0; i < 3; i++) {
    if (((accessMask >> i) & 0x01) == 0) {
      continue;
    }

    if (!actuators[i].isOpenPosition) {
      allAssignedCasesOpen = false;
      break;
    }
  }

  for (uint8_t i = 0; i < 3; i++) {
    if (((accessMask >> i) & 0x01) == 0) {
      continue;
    }

    if (allAssignedCasesOpen) {
      closeCase(i, false);
    } else if (!actuators[i].isOpenPosition) {
      openCase(i, true, false);
    }
  }

  pushHistoryLog(access, "Yes");
  setSystemMessage(
      String("authorized access ") + access +
      (allAssignedCasesOpen ? " -> close" : " -> open"));
}

void processFingerprintScan() {
  if (!fingerprintOk || enrollInProgress) {
    return;
  }

  if (!fingerTouchDetected()) {
    return;
  }

  if (anyActuatorActive() || anyActuatorCoolingDown()) {
    return;
  }

  if (millis() < fingerCooldownUntil) {
    return;
  }

  uint8_t result = finger.getImage();
  if (result == FINGERPRINT_NOFINGER) {
    return;
  }

  if (result != FINGERPRINT_OK) {
    return;
  }

  result = finger.image2Tz();
  if (result != FINGERPRINT_OK) {
    fingerCooldownUntil = millis() + FINGER_COOLDOWN_MS;
    pushHistoryLog("N/A", "Fingerprint Failed");
    setSystemMessage("fingerprint image conversion failed");
    return;
  }

  result = finger.fingerFastSearch();
  if (result == FINGERPRINT_OK) {
    uint16_t fingerId = finger.fingerID;
    String access = accessForFingerprint(fingerId);

    if (access.isEmpty()) {
      pushHistoryLog("N/A", "Fingerprint Failed");
      setSystemMessage(String("unmapped fingerprint id=") + fingerId);
    } else {
      authorizeAccess(access);
    }
  } else {
    pushHistoryLog("N/A", "Fingerprint Failed");
    setSystemMessage("unregistered fingerprint attempt");
  }

  fingerCooldownUntil = millis() + FINGER_COOLDOWN_MS;
}

void serviceFirebaseTasks(uint32_t now) {
  if (!firebaseReadyForUse()) {
    return;
  }

  // Do only one Firebase transaction per loop pass. Firebase calls can block
  // briefly, so spreading them out keeps fingerprint polling responsive.
  if (now - lastOverridePollAt >= OVERRIDE_POLL_MS) {
    lastOverridePollAt = now;
    pollOverrideCommand();
    return;
  }

  if (now - lastEnrollPollAt >= ENROLL_POLL_MS) {
    lastEnrollPollAt = now;
    processEnrollCommand();
    return;
  }

  if (now - lastStatusPushAt >= currentStatusPushIntervalMs()) {
    lastStatusPushAt = now;
    pushStatus();
    return;
  }

  if (pendingHistoryLogCount() > 0 &&
      (lastHistoryFlushAt == 0 || now - lastHistoryFlushAt >= HISTORY_FLUSH_MS)) {
    lastHistoryFlushAt = now;
    flushPendingHistoryLogs();
  }
}

// ---------------------------------------------------------------------------
// Arduino setup / loop
// ---------------------------------------------------------------------------

void setup() {
  debugBegin(115200);
  debugPrintln("\nLuxLock ESP32 booting...");

  prefs.begin("luxlock", false);
  loadSavedNonces();
  loadActuatorPositions();

  setupActuators();
  initFingerprint();
  initTamperLink();
  configureFirebaseSslClient();
  connectWifiIfNeeded();

  setSystemMessage("system ready");
}

void loop() {
  connectWifiIfNeeded();
  app.loop();
  syncFirebaseAuthState();
  initFirebase();
  syncFirebaseAuthState();
  syncClockWithNtpIfNeeded();

  pollTamperLink();
  updateActuatorTimeouts();
  retryFingerprintIfNeeded();
  processFingerprintScan();
  serviceFirebaseTasks(millis());
  delay(5);
}
