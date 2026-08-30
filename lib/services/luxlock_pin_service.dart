import 'dart:async';

import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../models/pin_policy.dart';
import 'luxlock_connection_service.dart';

class LuxLockPinService extends ChangeNotifier {
  LuxLockPinService._();

  static final LuxLockPinService instance = LuxLockPinService._();

  static const String _pinHashKey = 'luxlock_pin_hash';

  DatabaseReference get _pinRef => luxLockDb.child('app/security/pin');

  SharedPreferences? _preferences;
  String _localPinHash = LuxLockPinPolicy.hashPin(LuxLockPinPolicy.defaultPin);
  bool _initialized = false;
  bool _syncInProgress = false;
  bool _hasRemotePin = false;
  bool _wasOnline = false;
  String _lastSyncLabel = 'Local PIN ready';

  bool get isInitialized => _initialized;
  bool get syncInProgress => _syncInProgress;
  bool get hasRemotePin => _hasRemotePin;
  String get lastSyncLabel => _lastSyncLabel;

  bool get canChangePin =>
      LuxLockConnectionService.instance.canReachCommandPath;

  Future<void> initialize() async {
    if (_initialized) {
      return;
    }

    _preferences = await SharedPreferences.getInstance();
    final savedHash = _preferences?.getString(_pinHashKey);
    if (savedHash == null || savedHash.isEmpty) {
      await _saveLocalHash(_localPinHash);
    } else {
      _localPinHash = savedHash;
    }

    _initialized = true;
    LuxLockConnectionService.instance.addListener(_handleConnectionChange);
    _wasOnline = canChangePin;
    await syncFromFirebase();
    notifyListeners();
  }

  bool verifyPin(String pin) {
    if (!LuxLockPinPolicy.isValidPin(pin)) {
      return false;
    }

    return LuxLockPinPolicy.hashPin(pin) == _localPinHash;
  }

  Future<void> changePin({
    required String currentPin,
    required String newPin,
  }) async {
    if (!LuxLockPinPolicy.isValidPin(currentPin) ||
        !LuxLockPinPolicy.isValidPin(newPin)) {
      throw StateError('PIN must be exactly 4 digits.');
    }

    if (!verifyPin(currentPin)) {
      throw StateError('Current PIN is incorrect.');
    }

    if (!canChangePin) {
      throw StateError(
        'PIN changes require Firebase. Reconnect to the internet first.',
      );
    }

    final nextHash = LuxLockPinPolicy.hashPin(newPin);
    await _pinRef
        .set({
          'hash': nextHash,
          'updatedAt': ServerValue.timestamp,
          'updatedBy': 'app',
        })
        .timeout(const Duration(seconds: 8));

    _localPinHash = nextHash;
    _hasRemotePin = true;
    _lastSyncLabel = 'PIN updated online';
    await _saveLocalHash(nextHash);
    notifyListeners();
  }

  Future<void> syncFromFirebase() async {
    if (!_initialized || _syncInProgress || !canChangePin) {
      return;
    }

    _syncInProgress = true;
    notifyListeners();

    try {
      final snapshot = await _pinRef.get().timeout(const Duration(seconds: 5));
      final remoteHash = _hashFromSnapshot(snapshot);

      if (remoteHash == null || remoteHash.isEmpty) {
        await _pinRef
            .set({
              'hash': _localPinHash,
              'updatedAt': ServerValue.timestamp,
              'updatedBy': 'app_default',
            })
            .timeout(const Duration(seconds: 5));
        _hasRemotePin = true;
        _lastSyncLabel = 'Default PIN saved online';
      } else {
        _localPinHash = remoteHash;
        await _saveLocalHash(remoteHash);
        _hasRemotePin = true;
        _lastSyncLabel = 'PIN synced from Firebase';
      }
    } catch (_) {
      _lastSyncLabel = 'Using saved offline PIN';
    } finally {
      _syncInProgress = false;
      notifyListeners();
    }
  }

  void _handleConnectionChange() {
    final online = canChangePin;
    if (online == _wasOnline) {
      return;
    }

    if (online && !_wasOnline) {
      unawaited(syncFromFirebase());
    }

    _wasOnline = online;
    notifyListeners();
  }

  Future<void> _saveLocalHash(String hash) async {
    await _preferences?.setString(_pinHashKey, hash);
  }

  String? _hashFromSnapshot(DataSnapshot snapshot) {
    final raw = snapshot.value;
    if (raw is Map) {
      final data = Map<dynamic, dynamic>.from(raw);
      return data['hash']?.toString();
    }

    if (raw is String) {
      return raw;
    }

    return null;
  }
}
