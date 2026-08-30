const assert = require("node:assert/strict");
const test = require("node:test");

process.env.NODE_ENV = "test";

const {
  ALERT_COOLDOWN_MS,
  buildAlert,
  isFingerprintFailure,
  shouldClaimCooldown,
  tamperLevelFrom,
} = require("../index")._test;

test("tamper level 1 creates a buzzer notification", () => {
  const alert = buildAlert("esp32_1", "log_a", {
    case: "2",
    authorized: "Tamper L1",
  });

  assert.equal(alert.title, "LuxLock buzzer activated");
  assert.equal(alert.body, "Case 2 detected vibration level 1.");
  assert.equal(alert.cooldownKey, "buzzer_case_2");
  assert.deepEqual(alert.data, {
    type: "buzzer",
    deviceId: "esp32_1",
    logId: "log_a",
    case: "2",
    level: "1",
  });
});

test("tamper events below the configured level are ignored", () => {
  assert.equal(
    buildAlert("esp32_1", "log_a", {
      case: "1",
      authorized: "Tamper L0",
    }),
    null,
  );
});

test("fingerprint failures create a denied notification", () => {
  for (const authorized of [
    "No",
    "Fingerprint Failed",
    "fingerprint failed before match",
  ]) {
    const alert = buildAlert("esp32_1", "log_b", {
      case: "N/A",
      authorized,
    });

    assert.equal(alert.title, "LuxLock fingerprint denied");
    assert.equal(alert.cooldownKey, "fingerprint_failed_unknown_case");
    assert.equal(alert.data.type, "fingerprint_failed");
  }
});

test("normal successful access logs do not create alerts", () => {
  for (const authorized of [
    "Authorized",
    "Override Open",
    "Override Close",
    "Case 1 Open",
  ]) {
    assert.equal(
      buildAlert("esp32_1", "log_c", {case: "1", authorized}),
      null,
      `${authorized} should not send a notification`,
    );
  }
});

test("tamper level parser accepts both short and long labels", () => {
  assert.equal(tamperLevelFrom("Tamper L3"), 3);
  assert.equal(tamperLevelFrom("Tamper Level 2"), 2);
  assert.equal(tamperLevelFrom("Tamper"), 0);
});

test("fingerprint failure matcher is specific enough", () => {
  assert.equal(isFingerprintFailure("no"), true);
  assert.equal(isFingerprintFailure("fingerprint failed"), true);
  assert.equal(isFingerprintFailure("fingerprint success"), false);
  assert.equal(isFingerprintFailure("override open"), false);
});

test("notification cooldown blocks repeat alerts only inside the window", () => {
  const now = 100000;

  assert.equal(shouldClaimCooldown(0, now), true);
  assert.equal(shouldClaimCooldown(now - ALERT_COOLDOWN_MS + 1, now), false);
  assert.equal(shouldClaimCooldown(now - ALERT_COOLDOWN_MS, now), true);
  assert.equal(shouldClaimCooldown(now - ALERT_COOLDOWN_MS - 1, now), true);
});

test("daily history reset does not depend on log age or epoch", () => {
  assert.equal(
    Object.hasOwn(require("../index")._test, "historyCutoffEpoch"),
    false,
  );
  assert.equal(
    Object.hasOwn(require("../index")._test, "shouldDeleteHistoryLog"),
    false,
  );
});
