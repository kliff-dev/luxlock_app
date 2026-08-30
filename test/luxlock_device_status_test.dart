import 'package:flutter_test/flutter_test.dart';
import 'package:luxlock_app/services/luxlock_connection_service.dart';

void main() {
  group('LuxLockDeviceStatus parsing', () {
    test('parses Firebase, enrollment, actuator, and access state', () {
      final now = DateTime.now();
      final status = LuxLockDeviceStatus.fromRaw({
        'wifiConnected': true,
        'firebaseReady': false,
        'fingerprintReady': true,
        'registeredAccessMask': 3,
        'enrollInProgress': true,
        'lastMessage': 'waiting for finger',
        'lastAuthorized': 'N/A',
        'lastCase': 'N/A',
        'lastSeenAtMs': 123456,
        'lastSeenAtEpoch': now.millisecondsSinceEpoch ~/ 1000,
        'pendingHistoryCount': 2,
        'enrollment': {
          'state': 'waiting_for_finger',
          'message': 'Please place your finger on the sensor.',
          'access': '1,2',
          'nonce': '987654321',
          'templateId': -1,
        },
        'ack': {
          'lastProcessedOverrideNonce': '111',
          'lastOverrideResult': 'accepted',
        },
        'actuators': {
          'case1': {
            'active': false,
            'state': 'closed',
            'positionOpen': false,
            'autoCloseArmed': false,
            'cooldownActive': false,
          },
          'case2': {
            'active': true,
            'state': 'open',
            'positionOpen': false,
            'autoCloseArmed': false,
            'cooldownActive': false,
          },
        },
      });

      expect(status.hasSnapshot, isTrue);
      expect(status.wifiConnected, isTrue);
      expect(status.firebaseReady, isFalse);
      expect(status.fingerprintReady, isTrue);
      expect(status.enrollInProgress, isTrue);
      expect(status.enrollmentState, 'waiting_for_finger');
      expect(status.enrollmentNonce, 987654321);
      expect(status.hasFingerprintAccessForCase(1), isTrue);
      expect(status.hasFingerprintAccessForCase(2), isTrue);
      expect(status.hasFingerprintAccessForCase(3), isFalse);
      expect(status.anyActuatorActive, isTrue);
      expect(status.lastProcessedOverrideNonce, '111');
      expect(status.lastOverrideResult, 'accepted');
      expect(status.pendingHistoryCount, 2);
      expect(status.isFreshAt(now), isTrue);
    });

    test('treats finished enrollment states as not in progress', () {
      for (final state in ['success', 'failed', 'canceled', 'wipe_success']) {
        final status = LuxLockDeviceStatus.fromRaw({
          'wifiConnected': true,
          'firebaseReady': true,
          'fingerprintReady': true,
          'enrollInProgress': true,
          'lastSeenAtMs': 1,
          'lastSeenAtEpoch': DateTime.now().millisecondsSinceEpoch ~/ 1000,
          'enrollment': {'state': state},
        });

        expect(
          status.enrollInProgress,
          isFalse,
          reason: '$state must not block the next registration',
        );
      }
    });

    test('detects stale status by epoch time', () {
      final now = DateTime(2026, 4, 26, 12);
      final status = LuxLockDeviceStatus.fromRaw({
        'wifiConnected': true,
        'firebaseReady': true,
        'fingerprintReady': true,
        'lastSeenAtMs': 1,
        'lastSeenAtEpoch':
            now.subtract(const Duration(minutes: 2)).millisecondsSinceEpoch ~/
            1000,
      });

      expect(status.isFreshAt(now), isFalse);
      expect(status.isResponsiveAt(now), isFalse);
    });

    test('falls back safely for invalid data', () {
      final status = LuxLockDeviceStatus.fromRaw('not a map');

      expect(status.hasSnapshot, isFalse);
      expect(status.enrollmentState, 'idle');
      expect(status.anyActuatorActive, isFalse);
    });
  });
}
