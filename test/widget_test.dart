import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:luxlock_app/screens/home_screen.dart';
import 'package:luxlock_app/theme/luxlock_theme.dart';

void main() {
  testWidgets('Home screen builds', (WidgetTester tester) async {
    await tester.pumpWidget(
      MaterialApp(theme: LuxLockTheme.build(), home: const HomeScreen()),
    );

    expect(find.text('LuxLock Control'), findsOneWidget);
    expect(find.text('Register Access'), findsOneWidget);

    await tester.drag(find.byType(ListView), const Offset(0, -500));
    await tester.pumpAndSettle();

    expect(find.text('Change PIN'), findsOneWidget);
  });
}
