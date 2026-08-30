import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:luxlock_app/screens/change_pin_screen.dart';
import 'package:luxlock_app/screens/history_logs_screen.dart';
import 'package:luxlock_app/screens/home_screen.dart';
import 'package:luxlock_app/screens/override_screen.dart';
import 'package:luxlock_app/screens/pin_lock_screen.dart';
import 'package:luxlock_app/screens/register_screen.dart';
import 'package:luxlock_app/theme/luxlock_theme.dart';

Widget _testApp(Widget child) {
  return MaterialApp(theme: LuxLockTheme.build(), home: child);
}

Future<void> _pumpPhoneFlow(WidgetTester tester, Widget child) async {
  tester.view.physicalSize = const Size(420, 900);
  tester.view.devicePixelRatio = 1;
  addTearDown(() {
    tester.view.resetPhysicalSize();
    tester.view.resetDevicePixelRatio();
  });

  await tester.pumpWidget(_testApp(child));
}

Future<void> _tapPin(WidgetTester tester, String pin) async {
  for (final digit in pin.split('')) {
    await tester.tap(find.text(digit).last);
    await tester.pump();
  }
}

void main() {
  group('software flow without hardware', () {
    testWidgets('accepts the default PIN and opens Home', (tester) async {
      await _pumpPhoneFlow(tester, const PinLockScreen());

      await _tapPin(tester, '1234');
      await tester.pumpAndSettle();

      expect(find.text('LuxLock Control'), findsOneWidget);
      expect(find.text('Register Access'), findsOneWidget);
    });

    testWidgets('rejects an incorrect PIN without leaving the PIN screen', (
      tester,
    ) async {
      await _pumpPhoneFlow(tester, const PinLockScreen());

      await _tapPin(tester, '9999');
      await tester.pumpAndSettle();

      expect(find.text('Incorrect PIN.'), findsOneWidget);
      expect(find.text('Enter PIN'), findsOneWidget);
      expect(find.text('LuxLock Control'), findsNothing);
    });

    testWidgets('Home can navigate to core screens without hardware', (
      tester,
    ) async {
      await _pumpPhoneFlow(tester, const HomeScreen());

      await tester.tap(find.text('Register Access'));
      await tester.pumpAndSettle();
      expect(find.text('Fingerprint Access'), findsOneWidget);

      Navigator.of(tester.element(find.byType(RegisterScreen))).pop();
      await tester.pumpAndSettle();
      await tester.tap(find.text('Master Key Override'));
      await tester.pumpAndSettle();
      expect(find.text('Master Key'), findsOneWidget);

      Navigator.of(tester.element(find.byType(OverrideScreen))).pop();
      await tester.pumpAndSettle();
      await tester.drag(find.byType(ListView), const Offset(0, -500));
      await tester.pumpAndSettle();
      await tester.tap(find.text('History Logs'));
      await tester.pumpAndSettle();
      expect(find.text('History Offline'), findsOneWidget);
    });

    testWidgets('Register checklist builds shared access labels', (
      tester,
    ) async {
      await _pumpPhoneFlow(tester, const RegisterScreen());

      await tester.tap(find.text('Case 1').first);
      await tester.pumpAndSettle();
      await tester.tap(find.text('Case 2').first);
      await tester.pumpAndSettle();

      expect(find.text('Case 1 & 2'), findsOneWidget);
      await tester.drag(find.byType(ListView), const Offset(0, -500));
      await tester.pumpAndSettle();
      expect(find.text('REGISTER'), findsWidgets);
      expect(find.text('DELETE'), findsOneWidget);
    });

    testWidgets('Master Key checklist builds combined override selection', (
      tester,
    ) async {
      await _pumpPhoneFlow(tester, const OverrideScreen());

      await tester.tap(find.text('Case 1').first);
      await tester.pumpAndSettle();
      await tester.tap(find.text('Case 3').first);
      await tester.pumpAndSettle();

      expect(find.text('Case 1 & 3'), findsOneWidget);
      expect(find.text('OPEN'), findsOneWidget);
      expect(find.text('CLOSE'), findsOneWidget);
      expect(find.text('STOP'), findsOneWidget);
    });

    testWidgets('Change PIN clearly disables saving while offline', (
      tester,
    ) async {
      await _pumpPhoneFlow(tester, const ChangePinScreen());

      expect(find.textContaining('Offline mode'), findsOneWidget);
      await tester.drag(find.byType(ListView), const Offset(0, -900));
      await tester.pumpAndSettle();

      final saveButton = tester.widget<ButtonStyleButton>(
        find.ancestor(
          of: find.text('SAVE PIN'),
          matching: find.bySubtype<ButtonStyleButton>(),
        ),
      );
      expect(saveButton.onPressed, isNull);
    });

    testWidgets(
      'History explains Firebase is offline without opening Firebase',
      (tester) async {
        await _pumpPhoneFlow(tester, const HistoryLogsScreen());

        expect(find.text('History Offline'), findsOneWidget);
        expect(
          find.textContaining('cloud history will update after reconnection'),
          findsOneWidget,
        );
      },
    );
  });
}
