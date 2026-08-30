import 'dart:async';

import 'package:connectivity_plus/connectivity_plus.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/foundation.dart';

const String luxLockDatabaseUrl =
    'https://luxlock-3a53f-default-rtdb.asia-southeast1.firebasedatabase.app';
const Duration luxLockDeviceStaleThreshold = Duration(seconds: 60);

final FirebaseDatabase luxLockRtdb = FirebaseDatabase.instanceFor(
  app: Firebase.app(),
  databaseURL: luxLockDatabaseUrl,
);

final DatabaseReference luxLockDb = luxLockRtdb.ref();

class LuxLockActuatorStatus {
  final bool active;
  final bool positionOpen;
  final bool autoCloseArmed;
  final bool cooldownActive;
  final String state;

  const LuxLockActuatorStatus({
    required this.active,
    required this.positionOpen,
    required this.autoCloseArmed,
    required this.cooldownActive,
    required this.state,
  });

  const LuxLockActuatorStatus.unknown()
    : active = false,
      positionOpen = false,
      autoCloseArmed = false,
      cooldownActive = false,
      state = 'unknown';

  factory LuxLockActuatorStatus.fromRaw(Object? raw) {
    if (raw is! Map) {
      return const LuxLockActuatorStatus.unknown();
    }

    final data = Map<dynamic, dynamic>.from(raw);
    return LuxLockActuatorStatus(
      active: data['active'] == true,
      positionOpen: data['positionOpen'] == true,
      autoCloseArmed: data['autoCloseArmed'] == true,
      cooldownActive: data['cooldownActive'] == true,
      state: data['state']?.toString() ?? 'unknown',
    );
  }
}

class LuxLockDeviceStatus {
  final bool hasSnapshot;
  final bool wifiConnected;
  final bool firebaseReady;
  final bool rtcReady;
  final bool fingerprintReady;
  final int registeredAccessMask;
  final bool enrollInProgress;
  final String enrollmentState;
  final String enrollmentMessage;
  final String enrollmentAccess;
  final int enrollmentNonce;
  final int enrollmentTemplateId;
  final String lastMessage;
  final String lastAuthorized;
  final String lastCase;
  final String lastCommandError;
  final String lastProcessedOverrideNonce;
  final String lastOverrideTarget;
  final String lastOverrideAction;
  final String lastOverrideResult;
  final String lastOverrideError;
  final int lastSeenAtMs;
  final DateTime? lastSeenAt;
  final int pendingHistoryCount;
  final Map<String, LuxLockActuatorStatus> actuators;

  const LuxLockDeviceStatus({
    required this.hasSnapshot,
    required this.wifiConnected,
    required this.firebaseReady,
    required this.rtcReady,
    required this.fingerprintReady,
    required this.registeredAccessMask,
    required this.enrollInProgress,
    required this.enrollmentState,
    required this.enrollmentMessage,
    required this.enrollmentAccess,
    required this.enrollmentNonce,
    required this.enrollmentTemplateId,
    required this.lastMessage,
    required this.lastAuthorized,
    required this.lastCase,
    required this.lastCommandError,
    required this.lastProcessedOverrideNonce,
    required this.lastOverrideTarget,
    required this.lastOverrideAction,
    required this.lastOverrideResult,
    required this.lastOverrideError,
    required this.lastSeenAtMs,
    required this.lastSeenAt,
    required this.pendingHistoryCount,
    required this.actuators,
  });

  const LuxLockDeviceStatus.empty()
    : hasSnapshot = false,
      wifiConnected = false,
      firebaseReady = false,
      rtcReady = false,
      fingerprintReady = false,
      registeredAccessMask = 0,
      enrollInProgress = false,
      enrollmentState = 'idle',
      enrollmentMessage = '',
      enrollmentAccess = '',
      enrollmentNonce = 0,
      enrollmentTemplateId = -1,
      lastMessage = '',
      lastAuthorized = 'N/A',
      lastCase = 'N/A',
      lastCommandError = '',
      lastProcessedOverrideNonce = '',
      lastOverrideTarget = '',
      lastOverrideAction = '',
      lastOverrideResult = '',
      lastOverrideError = '',
      lastSeenAtMs = 0,
      lastSeenAt = null,
      pendingHistoryCount = 0,
      actuators = const {};

  factory LuxLockDeviceStatus.fromRaw(Object? raw) {
    if (raw is! Map) {
      return const LuxLockDeviceStatus.empty();
    }

    final data = Map<dynamic, dynamic>.from(raw);
    final ack = data['ack'] is Map
        ? Map<dynamic, dynamic>.from(data['ack'] as Map)
        : const <dynamic, dynamic>{};
    final enrollment = data['enrollment'] is Map
        ? Map<dynamic, dynamic>.from(data['enrollment'] as Map)
        : const <dynamic, dynamic>{};
    final actuatorsRaw = data['actuators'] is Map
        ? Map<dynamic, dynamic>.from(data['actuators'] as Map)
        : const <dynamic, dynamic>{};

    final actuators = <String, LuxLockActuatorStatus>{};
    for (final entry in actuatorsRaw.entries) {
      final key = entry.key?.toString();
      if (key == null || key.isEmpty) {
        continue;
      }
      actuators[key] = LuxLockActuatorStatus.fromRaw(entry.value);
    }

    final epochValue =
        int.tryParse(data['lastSeenAtEpoch']?.toString() ?? '') ?? 0;
    final lastSeenAtMs =
        int.tryParse(data['lastSeenAtMs']?.toString() ?? '') ?? 0;
    final lastSeenAt = epochValue > 0
        ? DateTime.fromMillisecondsSinceEpoch(
            epochValue * 1000,
            isUtc: true,
          ).toLocal()
        : null;
    final enrollmentState = enrollment['state']?.toString() ?? '';
    final enrollmentFinished = const {
      'success',
      'failed',
      'canceled',
      'wipe_success',
    }.contains(enrollmentState);
    final enrollInProgress =
        data['enrollInProgress'] == true && !enrollmentFinished;

    return LuxLockDeviceStatus(
      hasSnapshot: true,
      wifiConnected: data['wifiConnected'] == true,
      firebaseReady: data['firebaseReady'] == true,
      rtcReady: data['rtcReady'] == true,
      fingerprintReady: data['fingerprintReady'] == true,
      registeredAccessMask:
          int.tryParse(data['registeredAccessMask']?.toString() ?? '') ?? 0,
      enrollInProgress: enrollInProgress,
      enrollmentState: enrollmentState,
      enrollmentMessage: enrollment['message']?.toString() ?? '',
      enrollmentAccess: enrollment['access']?.toString() ?? '',
      enrollmentNonce: int.tryParse(enrollment['nonce']?.toString() ?? '') ?? 0,
      enrollmentTemplateId:
          int.tryParse(enrollment['templateId']?.toString() ?? '') ?? -1,
      lastMessage: data['lastMessage']?.toString() ?? '',
      lastAuthorized: data['lastAuthorized']?.toString() ?? 'N/A',
      lastCase: data['lastCase']?.toString() ?? 'N/A',
      lastCommandError: ack['lastCommandError']?.toString() ?? '',
      lastProcessedOverrideNonce:
          ack['lastProcessedOverrideNonce']?.toString() ?? '',
      lastOverrideTarget: ack['lastOverrideTarget']?.toString() ?? '',
      lastOverrideAction: ack['lastOverrideAction']?.toString() ?? '',
      lastOverrideResult: ack['lastOverrideResult']?.toString() ?? '',
      lastOverrideError: ack['lastOverrideError']?.toString() ?? '',
      lastSeenAtMs: lastSeenAtMs,
      lastSeenAt: lastSeenAt,
      pendingHistoryCount:
          int.tryParse(data['pendingHistoryCount']?.toString() ?? '') ?? 0,
      actuators: actuators,
    );
  }

  bool isFreshAt(
    DateTime now, {
    Duration threshold = luxLockDeviceStaleThreshold,
  }) {
    if (lastSeenAt == null) {
      return false;
    }

    final age = now.difference(lastSeenAt!);
    return !age.isNegative && age <= threshold;
  }

  bool isResponsiveAt(DateTime now) =>
      hasSnapshot && isFreshAt(now) && wifiConnected && firebaseReady;

  bool get anyActuatorActive => actuators.values.any((status) => status.active);

  bool get anyActuatorCoolingDown =>
      actuators.values.any((status) => status.cooldownActive);

  bool get anyCaseOpen => actuators.values.any((status) => status.positionOpen);

  bool get hasRegisteredFingerprints => registeredAccessMask != 0;

  bool hasFingerprintAccessForCase(int caseNumber) {
    if (caseNumber < 1 || caseNumber > 3) {
      return false;
    }

    return (registeredAccessMask & (1 << (caseNumber - 1))) != 0;
  }
}

class LuxLockOverrideCommandResult {
  final int nonce;

  const LuxLockOverrideCommandResult({required this.nonce});

  bool get confirmedImmediately => false;

  String get deliveryLabel => 'Firebase';
}

class LuxLockConnectionService extends ChangeNotifier {
  LuxLockConnectionService._();

  static final LuxLockConnectionService instance = LuxLockConnectionService._();

  bool _initialized = false;
  bool _isConnected = false;
  bool _isRefreshing = false;
  bool _isAuthenticated = false;
  LuxLockDeviceStatus _deviceStatus = const LuxLockDeviceStatus.empty();
  Timer? _deviceFreshnessTicker;
  int? _lastSeenHeartbeatMs;
  DateTime? _lastLiveHeartbeatAt;

  bool get isConnected => _isConnected;
  bool get isRefreshing => _isRefreshing;
  bool get isAuthenticated => _isAuthenticated;
  LuxLockDeviceStatus get deviceStatus => _deviceStatus;
  bool get isDeviceFresh => _isDeviceFreshAt(DateTime.now());
  bool get isDeviceResponsive => _isDeviceResponsiveAt(DateTime.now());
  bool get hasLiveDeviceHeartbeat => _isDeviceFreshAt(DateTime.now());
  bool get canReachCommandPath => _isConnected && _isAuthenticated;
  bool get canSendCommands => canReachCommandPath && isDeviceResponsive;
  bool get canSendOverrideCommands => canSendCommands;
  bool get canSendEnrollmentCommands => canSendCommands;
  bool get canRegisterFingerprints =>
      canSendEnrollmentCommands &&
      _deviceStatus.fingerprintReady &&
      !_deviceStatus.enrollInProgress &&
      !_deviceStatus.anyActuatorActive &&
      !_deviceStatus.anyCaseOpen;

  String get statusLabel {
    if (_isRefreshing) {
      return 'Refreshing connection...';
    }

    if (!_isConnected) {
      return 'No internet connection';
    }

    if (!_isAuthenticated) {
      return 'Reconnecting to Firebase...';
    }

    if (!_deviceStatus.hasSnapshot) {
      return 'Waiting for ESP32 status...';
    }

    if (!isDeviceFresh) {
      return 'ESP32 status is stale';
    }

    if (!_deviceStatus.wifiConnected) {
      return 'ESP32 is offline';
    }

    if (!_deviceStatus.firebaseReady) {
      return 'ESP32 is reconnecting to Firebase...';
    }

    if (_deviceStatus.enrollInProgress) {
      return 'ESP32 is enrolling a fingerprint...';
    }

    if (_deviceStatus.anyActuatorActive) {
      return 'Case movement in progress';
    }

    if (_deviceStatus.anyActuatorCoolingDown) {
      return 'Actuator cooldown in progress';
    }

    return 'ESP32 ready';
  }

  String get blockedOverrideCommandMessage => blockedCommandMessage;

  String get blockedCommandMessage {
    if (!_isConnected) {
      return 'No internet connection. Reconnect and pull down to refresh.';
    }

    if (!_isAuthenticated) {
      return 'Connecting to Firebase. Please try again in a moment.';
    }

    if (!_deviceStatus.hasSnapshot) {
      return 'Waiting for the ESP32 to publish its status. Please try again in a moment.';
    }

    if (!isDeviceFresh) {
      return 'The ESP32 has not reported in recently. Please refresh or power-cycle the device.';
    }

    if (!_deviceStatus.wifiConnected || !_deviceStatus.firebaseReady) {
      return 'The ESP32 is online but not fully ready yet. Wait for it to reconnect, then try again.';
    }

    if (_deviceStatus.enrollInProgress) {
      return 'The ESP32 is currently enrolling a fingerprint. Please wait for it to finish.';
    }

    if (_deviceStatus.anyActuatorActive) {
      return 'A case is still moving. Please wait for the movement to finish.';
    }

    if (_deviceStatus.anyActuatorCoolingDown) {
      return 'A case just finished moving. Please wait a moment before sending another command.';
    }

    return 'The ESP32 is still synchronizing. Please try again in a moment.';
  }

  Future<void> initialize() async {
    if (_initialized) {
      return;
    }

    _initialized = true;
    _isAuthenticated = FirebaseAuth.instance.currentUser != null;
    _isConnected = await _hasUsableNetwork();
    _deviceFreshnessTicker?.cancel();
    _deviceFreshnessTicker = Timer.periodic(
      const Duration(seconds: 1),
      (_) => notifyListeners(),
    );
    FirebaseAuth.instance.authStateChanges().listen((user) {
      _isAuthenticated = user != null;
      notifyListeners();
    });

    Connectivity().onConnectivityChanged.listen((results) {
      final hasNetwork = _containsUsableNetwork(results);
      if (_isConnected != hasNetwork) {
        _isConnected = hasNetwork;
        notifyListeners();
      }

      if (hasNetwork) {
        unawaited(refresh());
      }
    });

    luxLockDb
        .child('devices/esp32_1/status')
        .onValue
        .listen(
          (event) {
            final nextStatus = LuxLockDeviceStatus.fromRaw(
              event.snapshot.value,
            );
            _applyDeviceStatus(nextStatus);
          },
          onError: (_) {
            _deviceStatus = const LuxLockDeviceStatus.empty();
            _lastSeenHeartbeatMs = null;
            _lastLiveHeartbeatAt = null;
            notifyListeners();
          },
        );

    await refresh();
  }

  Future<void> refresh() async {
    if (_isRefreshing) {
      return;
    }

    _isRefreshing = true;
    notifyListeners();

    try {
      _isConnected = await _hasUsableNetwork();
      if (!_isConnected) {
        return;
      }

      await _ensureSignedIn();
    } catch (_) {
      _isAuthenticated = FirebaseAuth.instance.currentUser != null;
    } finally {
      _isRefreshing = false;
      notifyListeners();
    }
  }

  Future<bool> ensureReadyForCommand() async {
    if (canSendCommands) {
      return true;
    }

    await refresh();
    return canSendCommands;
  }

  Future<bool> ensureReadyForOverrideCommand() async {
    if (canSendOverrideCommands) {
      return true;
    }

    await refresh();
    return canSendOverrideCommands;
  }

  Future<LuxLockOverrideCommandResult> sendOverrideCommand({
    required String target,
    required String action,
  }) async {
    if (!await ensureReadyForOverrideCommand()) {
      throw StateError(blockedOverrideCommandMessage);
    }

    final int nonce = DateTime.now().millisecondsSinceEpoch;

    if (!await ensureReadyForCommand()) {
      throw StateError(blockedCommandMessage);
    }

    final cmdRef = luxLockDb.child('devices/esp32_1/commands/override');
    await cmdRef.update({
      'target': target,
      'action': action,
      'nonce': nonce,
      'requestedAt': nonce,
      'requestBy': 'app',
    });

    return LuxLockOverrideCommandResult(nonce: nonce);
  }

  Future<int> sendEnrollmentCommand({required String access}) {
    return _sendEnrollmentCommand(action: 'register', access: access);
  }

  Future<int> sendDeleteAllFingerprintsCommand() {
    return _sendEnrollmentCommand(action: 'delete_all');
  }

  Future<int> sendDeleteFingerprintAccessCommand({
    required String deleteAccess,
  }) {
    return _sendEnrollmentCommand(
      action: 'delete_access',
      deleteAccess: deleteAccess,
    );
  }

  Future<void> sendEnrollmentCancel({required int nonce}) async {
    if (!await ensureReadyForCommand()) {
      return;
    }

    final enrollRef = luxLockDb.child('devices/esp32_1/commands/enroll');
    final int requestedAt = DateTime.now().millisecondsSinceEpoch;

    await enrollRef.update({
      'cancelNonce': nonce,
      'cancelRequestedAt': requestedAt,
      'cancelBy': 'app',
    });
  }

  Future<int> _sendEnrollmentCommand({
    required String action,
    String? access,
    String? deleteAccess,
  }) async {
    if (!await ensureReadyForEnrollmentCommand()) {
      throw StateError(blockedEnrollmentCommandMessage);
    }

    final status = deviceStatus;
    if (!status.fingerprintReady) {
      throw StateError(
        'Fingerprint sensor is not ready yet. Wait for the ESP32 to detect it, then try again.',
      );
    }

    final int nonce = DateTime.now().millisecondsSinceEpoch;

    if (!await ensureReadyForCommand()) {
      throw StateError(blockedCommandMessage);
    }

    final enrollRef = luxLockDb.child('devices/esp32_1/commands/enroll');
    final payload = <String, Object>{
      'action': action,
      'nonce': nonce,
      'requestedAt': nonce,
      'requestBy': 'app',
    };

    if (access != null) {
      payload['access'] = access;
    }
    if (deleteAccess != null) {
      payload['deleteAccess'] = deleteAccess;
    }

    await enrollRef.set(payload);
    return nonce;
  }

  Future<bool> ensureReadyForEnrollmentCommand() async {
    if (canSendEnrollmentCommands) {
      return true;
    }

    await refresh();
    return canSendEnrollmentCommands;
  }

  String get blockedEnrollmentCommandMessage => blockedCommandMessage;

  bool _isDeviceFreshAt(DateTime now) {
    // Firebase may deliver an old status snapshot when the app first opens.
    // Only a changing ESP32 heartbeat proves the device is actually alive.
    final liveHeartbeatAt = _lastLiveHeartbeatAt;
    if (liveHeartbeatAt == null) {
      return false;
    }

    final age = now.difference(liveHeartbeatAt);
    return !age.isNegative && age <= luxLockDeviceStaleThreshold;
  }

  bool _isDeviceResponsiveAt(DateTime now) =>
      _deviceStatus.hasSnapshot &&
      _isDeviceFreshAt(now) &&
      _deviceStatus.wifiConnected &&
      _deviceStatus.firebaseReady;

  void _applyDeviceStatus(LuxLockDeviceStatus nextStatus) {
    if (nextStatus.hasSnapshot) {
      final heartbeatMs = nextStatus.lastSeenAtMs;
      final currentHeartbeatMs = _deviceStatus.lastSeenAtMs;
      if (heartbeatMs > 0 &&
          currentHeartbeatMs > 0 &&
          heartbeatMs < currentHeartbeatMs &&
          _isDeviceFreshAt(DateTime.now())) {
        return;
      }

      if (heartbeatMs > 0 && heartbeatMs != _lastSeenHeartbeatMs) {
        if (_lastSeenHeartbeatMs != null) {
          _lastLiveHeartbeatAt = DateTime.now();
        }
        _lastSeenHeartbeatMs = heartbeatMs;
      }
    }

    _deviceStatus = nextStatus;
    notifyListeners();
  }

  Future<void> _ensureSignedIn() async {
    if (FirebaseAuth.instance.currentUser != null) {
      _isAuthenticated = true;
      notifyListeners();
      return;
    }

    try {
      await FirebaseAuth.instance.signInAnonymously();
      _isAuthenticated = FirebaseAuth.instance.currentUser != null;
    } catch (_) {
      _isAuthenticated = FirebaseAuth.instance.currentUser != null;
    }

    notifyListeners();
  }

  Future<bool> _hasUsableNetwork() async {
    final results = await Connectivity().checkConnectivity();
    return _containsUsableNetwork(results);
  }

  bool _containsUsableNetwork(List<ConnectivityResult> results) {
    return results.any((result) => result != ConnectivityResult.none);
  }
}
