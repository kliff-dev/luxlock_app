import 'dart:async';

import 'package:flutter/material.dart';

import '../services/luxlock_pin_service.dart';
import '../theme/luxlock_theme.dart';
import '../widgets/luxlock_ui.dart';
import 'home_screen.dart';

class PinLockScreen extends StatefulWidget {
  const PinLockScreen({super.key});

  @override
  State<PinLockScreen> createState() => _PinLockScreenState();
}

class _PinLockScreenState extends State<PinLockScreen> {
  String _pin = '';
  String? _error;

  @override
  void initState() {
    super.initState();
    unawaited(LuxLockPinService.instance.syncFromFirebase());
  }

  void _appendDigit(String digit) {
    if (_pin.length >= 4) {
      return;
    }

    setState(() {
      _pin += digit;
      _error = null;
    });

    if (_pin.length == 4) {
      _submit();
    }
  }

  void _removeDigit() {
    if (_pin.isEmpty) {
      return;
    }

    setState(() {
      _pin = _pin.substring(0, _pin.length - 1);
      _error = null;
    });
  }

  void _clearPin() {
    setState(() {
      _pin = '';
      _error = null;
    });
  }

  void _submit() {
    if (_pin.length != 4) {
      setState(() => _error = 'Enter your 4-digit PIN.');
      return;
    }

    if (LuxLockPinService.instance.verifyPin(_pin)) {
      Navigator.of(
        context,
      ).pushReplacement(MaterialPageRoute(builder: (_) => const HomeScreen()));
      return;
    }

    setState(() {
      _pin = '';
      _error = 'Incorrect PIN.';
    });
  }

  @override
  Widget build(BuildContext context) {
    return LuxLockScaffold(
      eyebrow: 'Secure Access',
      title: 'Enter PIN',
      subtitle: 'Unlock LuxLock with your 4-digit PIN.',
      showBackButton: false,
      onRefresh: LuxLockPinService.instance.syncFromFirebase,
      child: ListView(
        physics: const AlwaysScrollableScrollPhysics(),
        children: [
          LuxLockPanel(
            padding: const EdgeInsets.all(18),
            child: AnimatedBuilder(
              animation: LuxLockPinService.instance,
              builder: (context, _) {
                final pinService = LuxLockPinService.instance;
                return Column(
                  children: [
                    Container(
                      width: 72,
                      height: 72,
                      decoration: BoxDecoration(
                        gradient: LuxLockPalette.goldGradient,
                        borderRadius: BorderRadius.circular(24),
                        boxShadow: const [
                          BoxShadow(
                            color: Color(0x30F7D778),
                            blurRadius: 26,
                            offset: Offset(0, 12),
                          ),
                        ],
                      ),
                      child: const Icon(
                        Icons.pin_rounded,
                        color: LuxLockPalette.midnight,
                        size: 34,
                      ),
                    ),
                    const SizedBox(height: 20),
                    Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        for (var index = 0; index < 4; index++)
                          AnimatedContainer(
                            duration: const Duration(milliseconds: 160),
                            width: 18,
                            height: 18,
                            margin: const EdgeInsets.symmetric(horizontal: 8),
                            decoration: BoxDecoration(
                              shape: BoxShape.circle,
                              color: index < _pin.length
                                  ? LuxLockPalette.gold
                                  : Colors.white.withValues(alpha: 0.12),
                              border: Border.all(
                                color: index < _pin.length
                                    ? LuxLockPalette.gold
                                    : Colors.white.withValues(alpha: 0.18),
                              ),
                            ),
                          ),
                      ],
                    ),
                    const SizedBox(height: 14),
                    Text(
                      _error ?? pinService.lastSyncLabel,
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                        color: _error == null
                            ? LuxLockPalette.textMuted
                            : LuxLockPalette.coral,
                      ),
                    ),
                  ],
                );
              },
            ),
          ),
          const SizedBox(height: 18),
          _PinKeypad(
            onDigit: _appendDigit,
            onBackspace: _removeDigit,
            onClear: _clearPin,
          ),
        ],
      ),
    );
  }
}

class _PinKeypad extends StatelessWidget {
  final ValueChanged<String> onDigit;
  final VoidCallback onBackspace;
  final VoidCallback onClear;

  const _PinKeypad({
    required this.onDigit,
    required this.onBackspace,
    required this.onClear,
  });

  @override
  Widget build(BuildContext context) {
    const keys = ['1', '2', '3', '4', '5', '6', '7', '8', '9'];

    return Column(
      children: [
        GridView.count(
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          crossAxisCount: 3,
          crossAxisSpacing: 12,
          mainAxisSpacing: 12,
          childAspectRatio: 1.35,
          children: [
            for (final key in keys)
              _PinKeyButton(label: key, onTap: () => onDigit(key)),
            _PinIconButton(
              icon: Icons.close_rounded,
              label: 'Clear',
              onTap: onClear,
            ),
            _PinKeyButton(label: '0', onTap: () => onDigit('0')),
            _PinIconButton(
              icon: Icons.backspace_outlined,
              label: 'Delete',
              onTap: onBackspace,
            ),
          ],
        ),
      ],
    );
  }
}

class _PinKeyButton extends StatelessWidget {
  final String label;
  final VoidCallback onTap;

  const _PinKeyButton({required this.label, required this.onTap});

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(22),
        child: Ink(
          decoration: BoxDecoration(
            color: const Color(0x1AFFFFFF),
            borderRadius: BorderRadius.circular(22),
            border: Border.all(color: const Color(0x30F7D778)),
          ),
          child: Center(
            child: Text(
              label,
              style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                fontSize: 28,
                color: LuxLockPalette.textPrimary,
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _PinIconButton extends StatelessWidget {
  final IconData icon;
  final String label;
  final VoidCallback onTap;

  const _PinIconButton({
    required this.icon,
    required this.label,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: label,
      child: Material(
        color: Colors.transparent,
        child: InkWell(
          onTap: onTap,
          borderRadius: BorderRadius.circular(22),
          child: Ink(
            decoration: BoxDecoration(
              color: const Color(0x12000000),
              borderRadius: BorderRadius.circular(22),
              border: Border.all(color: const Color(0x22FFFFFF)),
            ),
            child: Icon(icon, color: LuxLockPalette.textPrimary),
          ),
        ),
      ),
    );
  }
}
