import 'package:flutter_test/flutter_test.dart';
import 'package:luxlock_app/models/pin_policy.dart';

void main() {
  group('LuxLockPinPolicy', () {
    test('uses 1234 as the first default PIN', () {
      expect(LuxLockPinPolicy.defaultPin, '1234');
      expect(LuxLockPinPolicy.isValidPin(LuxLockPinPolicy.defaultPin), isTrue);
    });

    test('accepts exactly four numeric digits only', () {
      for (final pin in ['0000', '1234', '9999']) {
        expect(LuxLockPinPolicy.isValidPin(pin), isTrue);
      }

      for (final pin in ['', '123', '12345', '12a4', '12 4', 'abcd']) {
        expect(LuxLockPinPolicy.isValidPin(pin), isFalse);
      }
    });

    test('hashes PINs consistently without storing the plain PIN', () {
      final firstHash = LuxLockPinPolicy.hashPin('1234');
      final secondHash = LuxLockPinPolicy.hashPin('1234');
      final differentHash = LuxLockPinPolicy.hashPin('4321');

      expect(firstHash, secondHash);
      expect(firstHash, isNot('1234'));
      expect(firstHash, isNot(differentHash));
      expect(LuxLockPinPolicy.isValidHash(firstHash), isTrue);
    });
  });
}
