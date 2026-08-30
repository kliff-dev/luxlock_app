import 'dart:convert';

import 'package:crypto/crypto.dart';

class LuxLockPinPolicy {
  const LuxLockPinPolicy._();

  static const String defaultPin = '1234';
  static const String _pinSalt = 'luxlock-local-pin-v1';

  static bool isValidPin(String pin) {
    return RegExp(r'^\d{4}$').hasMatch(pin);
  }

  static String hashPin(String pin) {
    return sha256.convert(utf8.encode('$_pinSalt:$pin')).toString();
  }

  static bool isValidHash(String hash) {
    return RegExp(r'^[a-f0-9]{64}$').hasMatch(hash);
  }
}
