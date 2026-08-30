import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../services/luxlock_pin_service.dart';
import '../theme/luxlock_theme.dart';
import '../widgets/luxlock_ui.dart';

class ChangePinScreen extends StatefulWidget {
  const ChangePinScreen({super.key});

  @override
  State<ChangePinScreen> createState() => _ChangePinScreenState();
}

class _ChangePinScreenState extends State<ChangePinScreen> {
  final TextEditingController _currentPinController = TextEditingController();
  final TextEditingController _newPinController = TextEditingController();
  final TextEditingController _confirmPinController = TextEditingController();
  bool _saving = false;

  @override
  void dispose() {
    _currentPinController.dispose();
    _newPinController.dispose();
    _confirmPinController.dispose();
    super.dispose();
  }

  Future<void> _savePin() async {
    final currentPin = _currentPinController.text.trim();
    final newPin = _newPinController.text.trim();
    final confirmPin = _confirmPinController.text.trim();

    if (newPin != confirmPin) {
      _showMessage('New PIN and confirmation do not match.');
      return;
    }

    setState(() => _saving = true);
    try {
      await LuxLockPinService.instance.changePin(
        currentPin: currentPin,
        newPin: newPin,
      );

      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('PIN changed successfully.')),
      );
      Navigator.pop(context);
    } catch (e) {
      if (!mounted) return;
      _showMessage(e.toString().replaceFirst('Bad state: ', ''));
    } finally {
      if (mounted) {
        setState(() => _saving = false);
      }
    }
  }

  void _showMessage(String message) {
    ScaffoldMessenger.of(
      context,
    ).showSnackBar(SnackBar(content: Text(message)));
  }

  @override
  Widget build(BuildContext context) {
    return LuxLockScaffold(
      eyebrow: 'Security',
      title: 'Change PIN',
      subtitle: 'PIN changes require Firebase access.',
      onRefresh: LuxLockPinService.instance.syncFromFirebase,
      child: AnimatedBuilder(
        animation: LuxLockPinService.instance,
        builder: (context, _) {
          final canChange = LuxLockPinService.instance.canChangePin;

          return ListView(
            physics: const AlwaysScrollableScrollPhysics(),
            children: [
              LuxLockPanel(
                padding: const EdgeInsets.all(16),
                color: canChange
                    ? null
                    : LuxLockPalette.coral.withValues(alpha: 0.10),
                child: Row(
                  children: [
                    Icon(
                      canChange
                          ? Icons.cloud_done_rounded
                          : Icons.cloud_off_rounded,
                      color: canChange
                          ? LuxLockPalette.mint
                          : LuxLockPalette.coral,
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: Text(
                        canChange
                            ? 'Firebase is available. You can update the PIN now.'
                            : 'Offline mode. You can still unlock the app, but PIN changes are disabled.',
                        style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: LuxLockPalette.textPrimary,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: 18),
              LuxLockPanel(
                child: Column(
                  children: [
                    _PinField(
                      controller: _currentPinController,
                      label: 'Current PIN',
                    ),
                    const SizedBox(height: 14),
                    _PinField(controller: _newPinController, label: 'New PIN'),
                    const SizedBox(height: 14),
                    _PinField(
                      controller: _confirmPinController,
                      label: 'Confirm New PIN',
                    ),
                    const SizedBox(height: 18),
                    ElevatedButton.icon(
                      onPressed: canChange && !_saving ? _savePin : null,
                      icon: _saving
                          ? const SizedBox(
                              width: 18,
                              height: 18,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : const Icon(Icons.save_rounded),
                      label: Text(_saving ? 'SAVING' : 'SAVE PIN'),
                    ),
                  ],
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

class _PinField extends StatelessWidget {
  final TextEditingController controller;
  final String label;

  const _PinField({required this.controller, required this.label});

  @override
  Widget build(BuildContext context) {
    return TextField(
      controller: controller,
      obscureText: true,
      keyboardType: TextInputType.number,
      maxLength: 4,
      textAlign: TextAlign.center,
      style: Theme.of(context).textTheme.titleLarge,
      inputFormatters: [
        FilteringTextInputFormatter.digitsOnly,
        LengthLimitingTextInputFormatter(4),
      ],
      decoration: InputDecoration(
        labelText: label,
        counterText: '',
        filled: true,
        fillColor: const Color(0x14000000),
        border: OutlineInputBorder(borderRadius: BorderRadius.circular(18)),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(18),
          borderSide: const BorderSide(color: Color(0x30F7D778)),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(18),
          borderSide: const BorderSide(color: LuxLockPalette.gold),
        ),
      ),
    );
  }
}
