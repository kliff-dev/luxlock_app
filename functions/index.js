const {onValueCreated} = require("firebase-functions/v2/database");
const {onSchedule} = require("firebase-functions/v2/scheduler");
const logger = require("firebase-functions/logger");
const {initializeApp} = require("firebase-admin/app");
const {getDatabase} = require("firebase-admin/database");
const {getMessaging} = require("firebase-admin/messaging");

initializeApp();

const DATABASE_INSTANCE = "luxlock-3a53f-default-rtdb";
const REGION = "asia-southeast1";
const ALERT_COOLDOWN_MS = 60 * 1000;
const MIN_TAMPER_LEVEL = 1;
const HISTORY_CLEANUP_SCHEDULE = "0 0 * * *";
const HISTORY_CLEANUP_TIME_ZONE = "Asia/Manila";
const HISTORY_CLEANUP_DEVICE_IDS = ["esp32_1"];

exports.notifyLuxLockAlerts = onValueCreated(
  {
    ref: "/devices/{deviceId}/history/{logId}",
    instance: DATABASE_INSTANCE,
    region: REGION,
  },
  async (event) => {
    const deviceId = event.params.deviceId;
    const logId = event.params.logId;
    const log = event.data.val();
    const alert = buildAlert(deviceId, logId, log);

    if (!alert) {
      return;
    }

    if (!(await claimCooldown(deviceId, alert.cooldownKey))) {
      logger.info("LuxLock notification cooldown skipped", {
        deviceId,
        logId,
        cooldownKey: alert.cooldownKey,
      });
      return;
    }

    const recipients = await loadRecipients(deviceId);
    if (recipients.length === 0) {
      logger.info("LuxLock notification skipped: no recipients", {
        deviceId,
        logId,
      });
      return;
    }

    await sendAlert(deviceId, alert, recipients);
  },
);

exports.cleanupLuxLockHistory = onSchedule(
  {
    schedule: HISTORY_CLEANUP_SCHEDULE,
    timeZone: HISTORY_CLEANUP_TIME_ZONE,
    region: REGION,
  },
  async () => {
    const cleanupResults = await Promise.all(
      HISTORY_CLEANUP_DEVICE_IDS.map((deviceId) =>
        resetHistoryForDevice(deviceId),
      ),
    );

    const deletedCount = cleanupResults.reduce(
      (total, result) => total + result.deletedCount,
      0,
    );

    logger.info("LuxLock daily history reset complete", {
      deletedCount,
      devices: cleanupResults,
    });
  },
);

function buildAlert(deviceId, logId, rawLog) {
  const log = rawLog || {};
  const authorized = String(log.authorized || "").trim();
  const normalized = authorized.toLowerCase();
  const caseValue = String(log.case || "N/A").trim();

  if (normalized.startsWith("tamper")) {
    const level = tamperLevelFrom(authorized);
    if (level < MIN_TAMPER_LEVEL) {
      return null;
    }

    const caseLabel = caseValue && caseValue !== "N/A"
      ? `Case ${caseValue}`
      : "A case";

    return {
      cooldownKey: `buzzer_case_${caseValue || "unknown"}`,
      title: "LuxLock buzzer activated",
      body: `${caseLabel} detected vibration level ${level}.`,
      data: {
        type: "buzzer",
        deviceId,
        logId,
        case: caseValue,
        level: String(level),
      },
    };
  }

  if (isFingerprintFailure(normalized)) {
    return {
      cooldownKey: "fingerprint_failed_unknown_case",
      title: "LuxLock fingerprint denied",
      body: "A fingerprint attempt failed to open a case.",
      data: {
        type: "fingerprint_failed",
        deviceId,
        logId,
        case: caseValue,
      },
    };
  }

  return null;
}

async function resetHistoryForDevice(deviceId) {
  const historyRef = getDatabase().ref(`/devices/${deviceId}/history`);
  const snapshot = await historyRef.get();

  if (!snapshot.exists()) {
    return {deviceId, deletedCount: 0};
  }

  const deletedCount = snapshot.numChildren();
  await historyRef.remove();
  return {deviceId, deletedCount};
}

function tamperLevelFrom(value) {
  const match = String(value).match(/(?:l|level)\s*(\d+)/i);
  if (!match) {
    return 0;
  }

  return Number.parseInt(match[1], 10) || 0;
}

function isFingerprintFailure(normalized) {
  return normalized === "no" ||
    normalized.startsWith("fingerprint failed") ||
    (normalized.includes("fingerprint") && normalized.includes("fail"));
}

async function claimCooldown(deviceId, cooldownKey) {
  const stateRef = getDatabase().ref(
    `/devices/${deviceId}/notificationState/${cooldownKey}`,
  );
  const now = Date.now();
  let claimed = false;

  await stateRef.transaction((current) => {
    const lastSentAt = Number(current && current.lastSentAt) || 0;
    if (!shouldClaimCooldown(lastSentAt, now)) {
      return;
    }

    claimed = true;
    return {lastSentAt: now};
  });

  return claimed;
}

function shouldClaimCooldown(lastSentAt, now) {
  const previous = Number(lastSentAt) || 0;
  return !(previous > 0 && now - previous < ALERT_COOLDOWN_MS);
}

async function loadRecipients(deviceId) {
  const snapshot = await getDatabase()
    .ref(`/devices/${deviceId}/notificationRecipients`)
    .get();
  const recipients = [];

  snapshot.forEach((userSnapshot) => {
    userSnapshot.forEach((tokenSnapshot) => {
      const value = tokenSnapshot.val();
      if (value && value.enabled === true && value.token) {
        recipients.push({
          token: String(value.token),
          ref: tokenSnapshot.ref,
        });
      }
    });
  });

  return recipients;
}

async function sendAlert(deviceId, alert, recipients) {
  const tokens = recipients.map((recipient) => recipient.token);
  const response = await getMessaging().sendEachForMulticast({
    tokens,
    notification: {
      title: alert.title,
      body: alert.body,
    },
    data: alert.data,
    android: {
      priority: "high",
      notification: {
        sound: "default",
      },
    },
  });

  const cleanup = [];
  response.responses.forEach((result, index) => {
    if (result.success) {
      return;
    }

    const code = result.error && result.error.code;
    logger.warn("LuxLock notification token failed", {
      deviceId,
      code,
      message: result.error && result.error.message,
    });

    if (
      code === "messaging/registration-token-not-registered" ||
      code === "messaging/invalid-registration-token"
    ) {
      cleanup.push(recipients[index].ref.remove());
    }
  });

  await Promise.all(cleanup);
  logger.info("LuxLock notification sent", {
    deviceId,
    successCount: response.successCount,
    failureCount: response.failureCount,
    type: alert.data.type,
  });
}

if (process.env.NODE_ENV === "test") {
  exports._test = {
    ALERT_COOLDOWN_MS,
    MIN_TAMPER_LEVEL,
    buildAlert,
    isFingerprintFailure,
    shouldClaimCooldown,
    tamperLevelFrom,
  };
}
