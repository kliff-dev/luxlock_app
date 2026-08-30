import 'package:flutter/material.dart';

class LuxLockAssets {
  static const String logo = 'assets/branding/luxlock_logo.png';
}

class LuxLockPalette {
  static const Color midnight = Color(0xFF030712);
  static const Color obsidian = Color(0xFF070B14);
  static const Color steel = Color(0xFF101827);
  static const Color slate = Color(0xFF1A2638);
  static const Color panel = Color(0xDD08111F);
  static const Color panelElevated = Color(0xEE0D1728);
  static const Color panelBorder = Color(0x4DF7D778);
  static const Color gold = Color(0xFFF7D778);
  static const Color goldSoft = Color(0xFFFFF1B8);
  static const Color goldDeep = Color(0xFFC28A2B);
  static const Color amber = Color(0xFFE2B447);
  static const Color mint = Color(0xFF7EE7DA);
  static const Color cyanGlow = Color(0xFF53E7FF);
  static const Color coral = Color(0xFFFF8B73);
  static const Color textPrimary = Color(0xFFF7F3E8);
  static const Color textMuted = Color(0xFFB6C0CE);

  static const LinearGradient backgroundGradient = LinearGradient(
    begin: Alignment.topLeft,
    end: Alignment.bottomRight,
    colors: [midnight, obsidian, steel],
    stops: [0, 0.48, 1],
  );

  static const LinearGradient goldGradient = LinearGradient(
    begin: Alignment.topLeft,
    end: Alignment.bottomRight,
    colors: [goldSoft, gold, goldDeep],
  );
}

class LuxLockTheme {
  static ThemeData build() {
    const colorScheme = ColorScheme.dark(
      primary: LuxLockPalette.gold,
      secondary: LuxLockPalette.mint,
      surface: Color(0xFF10243D),
      error: LuxLockPalette.coral,
      onPrimary: Color(0xFF08111F),
      onSecondary: Color(0xFF08111F),
      onSurface: LuxLockPalette.textPrimary,
      onError: Colors.white,
    );

    return ThemeData(
      useMaterial3: true,
      colorScheme: colorScheme,
      scaffoldBackgroundColor: LuxLockPalette.midnight,
      textTheme: const TextTheme(
        headlineMedium: TextStyle(
          fontSize: 32,
          fontWeight: FontWeight.w900,
          color: LuxLockPalette.textPrimary,
          height: 1.05,
          letterSpacing: -0.7,
        ),
        titleLarge: TextStyle(
          fontSize: 20,
          fontWeight: FontWeight.w800,
          color: LuxLockPalette.textPrimary,
          letterSpacing: -0.2,
        ),
        titleMedium: TextStyle(
          fontSize: 16,
          fontWeight: FontWeight.w800,
          color: LuxLockPalette.textPrimary,
        ),
        bodyLarge: TextStyle(
          fontSize: 16,
          color: LuxLockPalette.textPrimary,
          height: 1.35,
        ),
        bodyMedium: TextStyle(
          fontSize: 14,
          color: LuxLockPalette.textMuted,
          height: 1.4,
        ),
        labelLarge: TextStyle(
          fontSize: 15,
          fontWeight: FontWeight.w800,
          letterSpacing: 0.1,
        ),
      ),
      appBarTheme: const AppBarTheme(
        backgroundColor: Colors.transparent,
        foregroundColor: LuxLockPalette.textPrimary,
        surfaceTintColor: Colors.transparent,
        elevation: 0,
        centerTitle: false,
      ),
      snackBarTheme: SnackBarThemeData(
        backgroundColor: LuxLockPalette.panelElevated,
        contentTextStyle: const TextStyle(
          color: LuxLockPalette.textPrimary,
          fontWeight: FontWeight.w600,
        ),
        behavior: SnackBarBehavior.floating,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(18)),
      ),
      dialogTheme: DialogThemeData(
        backgroundColor: const Color(0xFF0D1728),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(28)),
        titleTextStyle: const TextStyle(
          color: LuxLockPalette.textPrimary,
          fontSize: 22,
          fontWeight: FontWeight.w800,
        ),
        contentTextStyle: const TextStyle(
          color: LuxLockPalette.textMuted,
          fontSize: 15,
          height: 1.45,
        ),
      ),
      elevatedButtonTheme: ElevatedButtonThemeData(
        style: ElevatedButton.styleFrom(
          backgroundColor: LuxLockPalette.gold,
          foregroundColor: LuxLockPalette.midnight,
          minimumSize: const Size.fromHeight(58),
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 18),
          textStyle: const TextStyle(
            fontSize: 15,
            fontWeight: FontWeight.w800,
            letterSpacing: 0.1,
          ),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(22),
          ),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: LuxLockPalette.textPrimary,
          side: const BorderSide(color: LuxLockPalette.panelBorder),
          minimumSize: const Size.fromHeight(56),
          padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 18),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(20),
          ),
          textStyle: const TextStyle(fontSize: 14, fontWeight: FontWeight.w700),
        ),
      ),
      dividerTheme: const DividerThemeData(
        color: Color(0x1FFFFFFF),
        thickness: 1,
      ),
    );
  }
}
