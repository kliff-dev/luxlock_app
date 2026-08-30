import 'dart:async';

import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:firebase_messaging/firebase_messaging.dart';
import 'package:flutter/foundation.dart';

import '../firebase_options.dart';
import 'luxlock_connection_service.dart';

@pragma('vm:entry-point')
Future<void> luxLockFirebaseMessagingBackgroundHandler(
  RemoteMessage message,
) async {
  await Firebase.initializeApp(options: DefaultFirebaseOptions.currentPlatform);
}

void registerLuxLockNotificationBackgroundHandler() {
  FirebaseMessaging.onBackgroundMessage(
    luxLockFirebaseMessagingBackgroundHandler,
  );
}

class LuxLockNotificationService {
  LuxLockNotificationService._();

  static final LuxLockNotificationService instance =
      LuxLockNotificationService._();

  bool _initialized = false;
  StreamSubscription<String>? _tokenRefreshSubscription;

  Future<void> initialize() async {
    if (_initialized) {
      return;
    }

    _initialized = true;
    await FirebaseMessaging.instance.setAutoInitEnabled(true);

    final settings = await FirebaseMessaging.instance.requestPermission(
      alert: true,
      badge: true,
      sound: true,
    );

    final authorized =
        settings.authorizationStatus == AuthorizationStatus.authorized ||
        settings.authorizationStatus == AuthorizationStatus.provisional;

    if (!authorized) {
      debugPrint('LuxLock notifications are not authorized yet.');
      return;
    }

    final token = await FirebaseMessaging.instance.getToken();
    if (token != null && token.isNotEmpty) {
      await _saveToken(token);
    }

    await _tokenRefreshSubscription?.cancel();
    _tokenRefreshSubscription = FirebaseMessaging.instance.onTokenRefresh
        .listen((nextToken) {
          unawaited(_saveToken(nextToken));
        });

    FirebaseMessaging.onMessage.listen((message) {
      debugPrint(
        'LuxLock foreground notification: ${message.notification?.title ?? message.data['title'] ?? 'Alert'}',
      );
    });
  }

  Future<void> _saveToken(String token) async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) {
      return;
    }

    final tokenKey = _databaseSafeKey(token);
    await luxLockDb
        .child('devices/esp32_1/notificationRecipients/${user.uid}/$tokenKey')
        .set({
          'token': token,
          'enabled': true,
          'platform': _platformLabel,
          'updatedAt': ServerValue.timestamp,
        });
  }

  String get _platformLabel {
    if (kIsWeb) {
      return 'web';
    }

    return defaultTargetPlatform.name;
  }

  String _databaseSafeKey(String value) {
    return value.replaceAll(RegExp(r'[.#$/\[\]]'), '_');
  }
}
