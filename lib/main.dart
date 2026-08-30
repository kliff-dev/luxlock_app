import 'package:flutter/material.dart';
import 'package:firebase_core/firebase_core.dart';
import 'firebase_options.dart';
import 'package:firebase_auth/firebase_auth.dart';
import 'screens/pin_lock_screen.dart';
import 'services/luxlock_connection_service.dart';
import 'services/luxlock_notification_service.dart';
import 'services/luxlock_pin_service.dart';
import 'theme/luxlock_theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  registerLuxLockNotificationBackgroundHandler();

  await Firebase.initializeApp(options: DefaultFirebaseOptions.currentPlatform);

  try {
    await FirebaseAuth.instance.signInAnonymously();
  } catch (e) {
    debugPrint('Anonymous sign-in deferred: $e');
  }

  await LuxLockConnectionService.instance.initialize();
  await LuxLockPinService.instance.initialize();
  await LuxLockNotificationService.instance.initialize();

  runApp(const LuxLockApp());
}

class LuxLockApp extends StatelessWidget {
  const LuxLockApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: LuxLockTheme.build(),
      home: const PinLockScreen(),
    );
  }
}
