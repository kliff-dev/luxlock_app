import 'dart:async';

import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../models/case_id.dart';
import '../services/luxlock_connection_service.dart';
import '../theme/luxlock_theme.dart';
import '../widgets/luxlock_ui.dart';

Future<int> sendEnrollCommand({
  required String access, // "1" | "2" | "3" | "1,2" | "1,3" | "2,3" | "ALL"
}) async {
  return LuxLockConnectionService.instance.sendEnrollmentCommand(
    access: access,
  );
}

Future<void> sendEnrollCancel({required int nonce}) async {
  return LuxLockConnectionService.instance.sendEnrollmentCancel(nonce: nonce);
}

Future<int> sendDeleteAllFingerprintsCommand() async {
  return LuxLockConnectionService.instance.sendDeleteAllFingerprintsCommand();
}

Future<int> sendDeleteFingerprintAccessCommand({
  required String deleteAccess,
}) async {
  return LuxLockConnectionService.instance.sendDeleteFingerprintAccessCommand(
    deleteAccess: deleteAccess,
  );
}

class RegisterScreen extends StatefulWidget {
  const RegisterScreen({super.key});

  @override
  State<RegisterScreen> createState() => _RegisterScreenState();
}

class _RegisterScreenState extends State<RegisterScreen> {
  static const List<_AccessOption> _caseOptions = [
    _AccessOption(
      caseNumber: 1,
      label: 'Case 1',
      accessValue: '1',
      description: 'Single-case access for compartment 1.',
      icon: Icons.looks_one_rounded,
      accent: LuxLockPalette.gold,
    ),
    _AccessOption(
      caseNumber: 2,
      label: 'Case 2',
      accessValue: '2',
      description: 'Single-case access for compartment 2.',
      icon: Icons.looks_two_rounded,
      accent: LuxLockPalette.mint,
    ),
    _AccessOption(
      caseNumber: 3,
      label: 'Case 3',
      accessValue: '3',
      description: 'Single-case access for compartment 3.',
      icon: Icons.looks_3_rounded,
      accent: LuxLockPalette.coral,
    ),
  ];

  final Set<int> _selectedCases = <int>{};

  DatabaseReference get _enrollmentStatusRef =>
      luxLockDb.child('devices/esp32_1/status/enrollment');

  int get _selectedMask {
    return LuxLockCaseSelection.maskFromCases(_selectedCases);
  }

  bool get _hasSelectedCases => _selectedCases.isNotEmpty;

  void _toggleCase(int caseNumber) {
    setState(() {
      if (_selectedCases.contains(caseNumber)) {
        _selectedCases.remove(caseNumber);
      } else {
        _selectedCases.add(caseNumber);
      }
    });
  }

  _AccessOption? get _selectedAccessOption {
    final mask = _selectedMask;
    if (mask == 0) {
      return null;
    }

    return _AccessOption(
      caseNumber: 0,
      label: LuxLockCaseSelection.labelFromMask(mask),
      accessValue: LuxLockCaseSelection.accessValueFromMask(mask),
      description: _descriptionFromMask(mask),
      icon: _iconFromMask(mask),
      accent: _accentFromMask(mask),
    );
  }

  String _descriptionFromMask(int mask) {
    if (mask == 7) {
      return 'Full access across every secured case.';
    }

    if (mask == 1 || mask == 2 || mask == 4) {
      return 'Single-case access for the selected compartment.';
    }

    return 'Shared access across the selected compartments.';
  }

  IconData _iconFromMask(int mask) {
    return switch (mask) {
      1 => Icons.looks_one_rounded,
      2 => Icons.looks_two_rounded,
      4 => Icons.looks_3_rounded,
      7 => Icons.lock_open_rounded,
      _ => Icons.grid_view_rounded,
    };
  }

  Color _accentFromMask(int mask) {
    return switch (mask) {
      1 => LuxLockPalette.gold,
      2 => LuxLockPalette.mint,
      4 => LuxLockPalette.coral,
      7 => Colors.white,
      _ => const Color(0xFF8CE0D4),
    };
  }

  Future<void> _handleRegister(BuildContext context) async {
    final option = _selectedAccessOption;
    if (option == null) {
      await _showSelectionRequiredDialog(context);
      return;
    }

    final readiness = await _checkRegisterReadiness();

    if (!context.mounted) return;

    if (!readiness.canRegister) {
      await showDialog<void>(
        context: context,
        builder: (_) => AlertDialog(
          title: const Text('Registration Blocked'),
          content: Text(readiness.message),
          actions: [
            ElevatedButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('OK'),
            ),
          ],
        ),
      );
      return;
    }

    showDialog(
      context: context,
      builder: (_) => AlertDialog(
        title: const Text('Confirm Registration'),
        content: Text(
          'Start fingerprint enrollment for ${option.label}?\n\n'
          'After pressing Confirm, place your finger on the sensor when the ESP32 is ready.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () async {
              Navigator.pop(context);
              await _startEnrollmentFlow(context, option);
            },
            child: const Text('Confirm'),
          ),
        ],
      ),
    );
  }

  Future<void> _startEnrollmentFlow(
    BuildContext context,
    _AccessOption option,
  ) async {
    BuildContext? progressDialogContext;
    ValueNotifier<_EnrollmentProgress>? progressNotifier;
    var cancelRequested = false;
    final localCancel = Completer<void>();

    try {
      final nonce = await sendEnrollCommand(access: option.accessValue);
      progressNotifier = ValueNotifier<_EnrollmentProgress>(
        _EnrollmentProgress.waiting(optionLabel: option.label, nonce: nonce),
      );

      if (!context.mounted) return;

      showDialog<void>(
        context: context,
        barrierDismissible: false,
        builder: (dialogContext) {
          progressDialogContext = dialogContext;
          return _EnrollmentProgressDialog(
            nonce: nonce,
            optionLabel: option.label,
            progressListenable: progressNotifier!,
            onCancel: () async {
              if (cancelRequested) {
                return;
              }

              cancelRequested = true;
              if (!localCancel.isCompleted) {
                localCancel.complete();
              }

              unawaited(_sendCancelRequest(nonce));
            },
          );
        },
      );

      final result = await _waitForEnrollmentResult(
        nonce: nonce,
        localCancelSignal: localCancel.future,
        progressNotifier: progressNotifier,
      );

      if (progressDialogContext != null && progressDialogContext!.mounted) {
        Navigator.of(progressDialogContext!).pop();
      }

      if (!context.mounted) return;

      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            result.success
                ? 'Fingerprint registered for ${option.label}'
                : result.message,
          ),
        ),
      );
    } catch (e) {
      if (progressDialogContext != null && progressDialogContext!.mounted) {
        Navigator.of(progressDialogContext!).pop();
      }

      if (!context.mounted) return;

      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Failed: $e')));
    } finally {
      progressNotifier?.dispose();
    }
  }

  Future<_EnrollmentResult> _waitForEnrollmentResult({
    required int nonce,
    Future<void>? localCancelSignal,
    ValueNotifier<_EnrollmentProgress>? progressNotifier,
  }) async {
    final completer = Completer<_EnrollmentResult>();

    late final StreamSubscription<DatabaseEvent> subscription;
    late final Timer timeout;
    late final Timer localProgressTimer;

    void completeOnce(_EnrollmentResult result) {
      if (!completer.isCompleted) {
        completer.complete(result);
      }
    }

    void handleProgress(_EnrollmentProgress progress) {
      if (progress.nonce != nonce) {
        return;
      }

      progressNotifier?.value = progress;

      if (progress.state == 'success' || progress.state == 'wipe_success') {
        completeOnce(
          _EnrollmentResult(
            success: true,
            message: progress.message.isNotEmpty
                ? progress.message
                : 'Fingerprint command completed successfully.',
          ),
        );
      } else if (progress.state == 'canceled') {
        completeOnce(
          const _EnrollmentResult(
            success: false,
            message: 'Fingerprint registration canceled.',
          ),
        );
      } else if (progress.state == 'failed') {
        completeOnce(
          _EnrollmentResult(
            success: false,
            message: progress.message.isNotEmpty
                ? progress.message
                : 'Fingerprint registration failed.',
          ),
        );
      }
    }

    localCancelSignal?.then((_) {
      completeOnce(
        const _EnrollmentResult(
          success: false,
          message: 'Fingerprint registration canceled.',
        ),
      );
    });

    timeout = Timer(
      const Duration(minutes: 1),
      () => completeOnce(
        const _EnrollmentResult(
          success: false,
          message: 'Timed out waiting for fingerprint registration.',
        ),
      ),
    );

    localProgressTimer = Timer.periodic(const Duration(milliseconds: 250), (_) {
      handleProgress(
        _EnrollmentProgress.fromDeviceStatus(
          LuxLockConnectionService.instance.deviceStatus,
        ),
      );
    });

    subscription = _enrollmentStatusRef.onValue.listen((event) {
      handleProgress(_EnrollmentProgress.fromSnapshot(event.snapshot));
    });

    final result = await completer.future;
    await subscription.cancel();
    localProgressTimer.cancel();
    timeout.cancel();
    return result;
  }

  Future<void> _sendCancelRequest(int nonce) async {
    try {
      await sendEnrollCancel(nonce: nonce);
    } catch (_) {
      // Best-effort cleanup when leaving the registration flow.
    }
  }

  Future<void> _handleDeleteFingerprints(BuildContext context) async {
    if (!_hasSelectedCases) {
      await _showSelectionRequiredDialog(context);
      return;
    }

    final readiness = _checkFingerprintAdminReadiness();

    if (!context.mounted) return;

    if (!readiness.canRegister) {
      await showDialog<void>(
        context: context,
        builder: (_) => AlertDialog(
          title: const Text('Delete Blocked'),
          content: Text(readiness.message),
          actions: [
            ElevatedButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('OK'),
            ),
          ],
        ),
      );
      return;
    }

    final status = LuxLockConnectionService.instance.deviceStatus;

    if (status.registeredAccessMask == 0) {
      await showDialog<void>(
        context: context,
        builder: (_) => AlertDialog(
          title: const Text('No Fingerprints Found'),
          content: const Text(
            'The ESP32 has not reported any stored fingerprint access yet.',
          ),
          actions: [
            ElevatedButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('OK'),
            ),
          ],
        ),
      );
      return;
    }

    final selectedMask = _selectedMask;
    final matchingAccess = status.registeredAccessMask & selectedMask;
    if (matchingAccess == 0) {
      await showDialog<void>(
        context: context,
        builder: (_) => AlertDialog(
          title: const Text('No Matching Fingerprints'),
          content: Text(
            'No stored fingerprint access currently matches ${LuxLockCaseSelection.labelFromMask(selectedMask)}.',
          ),
          actions: [
            ElevatedButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('OK'),
            ),
          ],
        ),
      );
      return;
    }

    final selected = _DeleteFingerprintOption(
      label: LuxLockCaseSelection.labelFromMask(selectedMask),
      accessValue: LuxLockCaseSelection.accessValueFromMask(selectedMask),
      icon: _iconFromMask(selectedMask),
      accent: _accentFromMask(selectedMask),
      deleteAll: selectedMask == 7,
    );

    final confirmed = await _confirmDeleteFingerprintOption(context, selected);
    if (confirmed != true || !context.mounted) {
      return;
    }

    await _runDeleteFingerprintCommand(context, selected);
  }

  Future<bool?> _confirmDeleteFingerprintOption(
    BuildContext context,
    _DeleteFingerprintOption option,
  ) {
    final description = option.deleteAll
        ? 'This will remove every stored fingerprint from the sensor and clear all fingerprint access mappings. This cannot be undone.'
        : 'This will remove fingerprint access for ${option.label}. Shared fingerprints will keep access to their other assigned cases.';

    return showDialog<bool>(
      context: context,
      builder: (_) => AlertDialog(
        title: Text(
          option.deleteAll
              ? 'Delete All Fingerprints?'
              : 'Delete ${option.label} Fingerprints?',
        ),
        content: Text(description),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancel'),
          ),
          ElevatedButton(
            onPressed: () => Navigator.pop(context, true),
            child: Text(option.deleteAll ? 'Delete All' : 'Delete'),
          ),
        ],
      ),
    );
  }

  Future<void> _showSelectionRequiredDialog(BuildContext context) {
    return showDialog<void>(
      context: context,
      builder: (_) => AlertDialog(
        title: const Text('Select a Case'),
        content: const Text(
          'Choose at least one case before registering or deleting access.',
        ),
        actions: [
          ElevatedButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  Future<void> _runDeleteFingerprintCommand(
    BuildContext context,
    _DeleteFingerprintOption option,
  ) async {
    BuildContext? progressDialogContext;
    ValueNotifier<_EnrollmentProgress>? progressNotifier;

    try {
      final nonce = option.deleteAll
          ? await sendDeleteAllFingerprintsCommand()
          : await sendDeleteFingerprintAccessCommand(
              deleteAccess: option.accessValue,
            );
      progressNotifier = ValueNotifier<_EnrollmentProgress>(
        _EnrollmentProgress.waiting(
          optionLabel: option.label,
          nonce: nonce,
          message: 'Waiting for the ESP32 to delete fingerprints...',
        ),
      );

      if (!context.mounted) return;

      showDialog<void>(
        context: context,
        barrierDismissible: false,
        builder: (dialogContext) {
          progressDialogContext = dialogContext;
          return _EnrollmentProgressDialog(
            nonce: nonce,
            optionLabel: option.label,
            progressListenable: progressNotifier!,
            title: 'Delete Fingerprints',
            waitingMessage: 'Waiting for the ESP32 to delete fingerprints...',
            showCancel: false,
            onCancel: () async {},
          );
        },
      );

      final result = await _waitForEnrollmentResult(
        nonce: nonce,
        progressNotifier: progressNotifier,
      );

      if (progressDialogContext != null && progressDialogContext!.mounted) {
        Navigator.of(progressDialogContext!).pop();
      }

      if (!context.mounted) return;

      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text(result.message)));
    } catch (e) {
      if (progressDialogContext != null && progressDialogContext!.mounted) {
        Navigator.of(progressDialogContext!).pop();
      }

      if (!context.mounted) return;

      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Failed: $e')));
    } finally {
      progressNotifier?.dispose();
    }
  }

  _RegisterReadiness _checkFingerprintAdminReadiness() {
    final deviceStatus = LuxLockConnectionService.instance.deviceStatus;
    if (!LuxLockConnectionService.instance.canSendEnrollmentCommands) {
      return _RegisterReadiness.blocked(
        LuxLockConnectionService.instance.blockedEnrollmentCommandMessage,
      );
    }

    if (!deviceStatus.fingerprintReady) {
      return const _RegisterReadiness.blocked(
        'Fingerprint sensor is not ready yet. Please wait for the ESP32 to finish initializing it.',
      );
    }

    if (deviceStatus.enrollInProgress) {
      return const _RegisterReadiness.blocked(
        'A fingerprint enrollment is already in progress. Please wait for it to finish first.',
      );
    }

    if (deviceStatus.anyActuatorActive) {
      return const _RegisterReadiness.blocked(
        'A case is still moving. Please wait for the movement to finish before deleting fingerprints.',
      );
    }

    if (deviceStatus.anyActuatorCoolingDown) {
      return const _RegisterReadiness.blocked(
        'A case just finished moving. Please wait a moment before deleting fingerprints.',
      );
    }

    return const _RegisterReadiness.allowed();
  }

  Future<_RegisterReadiness> _checkRegisterReadiness() async {
    final deviceStatus = LuxLockConnectionService.instance.deviceStatus;
    if (!LuxLockConnectionService.instance.canSendEnrollmentCommands) {
      return _RegisterReadiness.blocked(
        LuxLockConnectionService.instance.blockedEnrollmentCommandMessage,
      );
    }

    if (!deviceStatus.fingerprintReady) {
      return const _RegisterReadiness.blocked(
        'Fingerprint sensor is not ready yet. Please wait for the ESP32 to finish initializing it.',
      );
    }

    if (deviceStatus.enrollInProgress) {
      return const _RegisterReadiness.blocked(
        'Another fingerprint enrollment is already in progress. Please wait for it to finish first.',
      );
    }

    if (deviceStatus.anyActuatorActive) {
      return const _RegisterReadiness.blocked(
        'Please wait for the case movement to finish before registering a fingerprint.',
      );
    }

    if (deviceStatus.anyActuatorCoolingDown) {
      return const _RegisterReadiness.blocked(
        'A case just finished moving. Please wait a moment before registering a fingerprint.',
      );
    }

    if (deviceStatus.anyCaseOpen) {
      return const _RegisterReadiness.blocked(
        'Please close the case before registering a fingerprint.',
      );
    }

    return const _RegisterReadiness.allowed();
  }

  @override
  Widget build(BuildContext context) {
    final selectedLabel = LuxLockCaseSelection.labelFromMask(_selectedMask);

    return LuxLockScaffold(
      eyebrow: 'Register',
      title: 'Fingerprint Access',
      subtitle: 'Select cases, then register or delete access.',
      child: ListView(
        physics: const AlwaysScrollableScrollPhysics(),
        children: [
          LuxLockPanel(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Container(
                      width: 48,
                      height: 48,
                      decoration: BoxDecoration(
                        color: _accentFromMask(
                          _selectedMask,
                        ).withValues(alpha: 0.18),
                        borderRadius: BorderRadius.circular(18),
                      ),
                      child: Icon(
                        _iconFromMask(_selectedMask),
                        color: _accentFromMask(_selectedMask),
                      ),
                    ),
                    const SizedBox(width: 14),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'Access selection',
                            style: Theme.of(context).textTheme.titleLarge,
                          ),
                          const SizedBox(height: 4),
                          Text(
                            _hasSelectedCases
                                ? selectedLabel
                                : 'Choose one or more cases.',
                            style: Theme.of(context).textTheme.bodyMedium
                                ?.copyWith(color: LuxLockPalette.textMuted),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                for (final option in _caseOptions) ...[
                  _CaseSelectionTile(
                    option: option,
                    selected: _selectedCases.contains(option.caseNumber),
                    onTap: () => _toggleCase(option.caseNumber),
                  ),
                  if (option != _caseOptions.last) const SizedBox(height: 10),
                ],
              ],
            ),
          ),
          const SizedBox(height: 18),
          LayoutBuilder(
            builder: (context, constraints) {
              final buttons = [
                ElevatedButton.icon(
                  onPressed: _hasSelectedCases
                      ? () => unawaited(_handleRegister(context))
                      : null,
                  icon: const Icon(Icons.person_add_alt_1_rounded),
                  label: const Text('REGISTER'),
                ),
                OutlinedButton.icon(
                  onPressed: _hasSelectedCases
                      ? () => unawaited(_handleDeleteFingerprints(context))
                      : null,
                  icon: const Icon(Icons.delete_forever_rounded),
                  label: const Text('DELETE'),
                ),
              ];

              if (constraints.maxWidth < 520) {
                return Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    buttons[0],
                    const SizedBox(height: 12),
                    buttons[1],
                  ],
                );
              }

              return Row(
                children: [
                  Expanded(child: buttons[0]),
                  const SizedBox(width: 12),
                  Expanded(child: buttons[1]),
                ],
              );
            },
          ),
        ],
      ),
    );
  }
}

class _CaseSelectionTile extends StatelessWidget {
  final _AccessOption option;
  final bool selected;
  final VoidCallback onTap;

  const _CaseSelectionTile({
    required this.option,
    required this.selected,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(16),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
          decoration: BoxDecoration(
            color: selected
                ? option.accent.withValues(alpha: 0.12)
                : const Color(0x0FFFFFFF),
            border: Border.all(
              color: selected
                  ? option.accent.withValues(alpha: 0.72)
                  : Colors.white.withValues(alpha: 0.08),
            ),
            borderRadius: BorderRadius.circular(16),
          ),
          child: Row(
            children: [
              Checkbox(
                value: selected,
                onChanged: (_) => onTap(),
                activeColor: option.accent,
                checkColor: LuxLockPalette.midnight,
                side: BorderSide(color: option.accent.withValues(alpha: 0.72)),
              ),
              const SizedBox(width: 8),
              Icon(option.icon, color: option.accent),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      option.label,
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                    const SizedBox(height: 2),
                    Text(
                      option.description,
                      style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: LuxLockPalette.textMuted,
                      ),
                    ),
                  ],
                ),
              ),
              if (selected)
                Icon(Icons.check_circle_rounded, color: option.accent),
            ],
          ),
        ),
      ),
    );
  }
}

class _EnrollmentProgressDialog extends StatelessWidget {
  final int nonce;
  final String optionLabel;
  final ValueListenable<_EnrollmentProgress> progressListenable;
  final Future<void> Function() onCancel;
  final String title;
  final String? waitingMessage;
  final bool showCancel;

  const _EnrollmentProgressDialog({
    required this.nonce,
    required this.optionLabel,
    required this.progressListenable,
    required this.onCancel,
    this.title = 'Register Fingerprint',
    this.waitingMessage,
    this.showCancel = true,
  });

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<_EnrollmentProgress>(
      valueListenable: progressListenable,
      builder: (context, progress, _) {
        final dialogProgress = progress.nonce == nonce
            ? progress
            : _EnrollmentProgress.waiting(
                optionLabel: optionLabel,
                nonce: nonce,
                message: waitingMessage,
              );

        return AlertDialog(
          title: Text(title),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const SizedBox(
                width: 36,
                height: 36,
                child: CircularProgressIndicator(strokeWidth: 3),
              ),
              const SizedBox(height: 18),
              Text(dialogProgress.message, textAlign: TextAlign.center),
            ],
          ),
          actions: [
            if (showCancel)
              TextButton(
                onPressed: () {
                  onCancel();
                },
                child: const Text('Cancel'),
              ),
          ],
        );
      },
    );
  }
}

class _EnrollmentProgress {
  final int nonce;
  final String state;
  final String message;

  const _EnrollmentProgress({
    required this.nonce,
    required this.state,
    required this.message,
  });

  const _EnrollmentProgress.empty()
    : nonce = 0,
      state = 'idle',
      message = 'Waiting for the ESP32 to start enrollment...';

  factory _EnrollmentProgress.waiting({
    required String optionLabel,
    required int nonce,
    String? message,
  }) {
    return _EnrollmentProgress(
      nonce: nonce,
      state: 'waiting_for_finger',
      message:
          message ??
          'Please place your finger on the sensor to register for $optionLabel.',
    );
  }

  factory _EnrollmentProgress.fromSnapshot(DataSnapshot snapshot) {
    final raw = snapshot.value;
    if (raw is! Map) {
      return const _EnrollmentProgress.empty();
    }

    final data = Map<dynamic, dynamic>.from(raw);
    final state = data['state']?.toString() ?? 'idle';
    final message = data['message']?.toString() ?? '';
    final nonce = int.tryParse(data['nonce']?.toString() ?? '') ?? 0;
    final access = data['access']?.toString() ?? '';

    return _EnrollmentProgress(
      nonce: nonce,
      state: state,
      message: _messageForState(state, message, access),
    );
  }

  factory _EnrollmentProgress.fromDeviceStatus(LuxLockDeviceStatus status) {
    return _EnrollmentProgress(
      nonce: status.enrollmentNonce,
      state: status.enrollmentState,
      message: _messageForState(
        status.enrollmentState,
        status.enrollmentMessage,
        status.enrollmentAccess,
      ),
    );
  }

  static String _messageForState(
    String state,
    String fallbackMessage,
    String access,
  ) {
    switch (state) {
      case 'waiting_for_finger':
        return 'Please place your finger on the sensor for access $access.';
      case 'remove_finger':
        return 'Remove your finger from the sensor.';
      case 'scan_second_time':
        return 'Place the same finger again to finish registration.';
      case 'deleting_all':
        return fallbackMessage.isNotEmpty
            ? fallbackMessage
            : 'Deleting all stored fingerprints.';
      case 'deleting_access':
        return fallbackMessage.isNotEmpty
            ? fallbackMessage
            : 'Deleting selected fingerprint access.';
      case 'wipe_success':
        return fallbackMessage.isNotEmpty
            ? fallbackMessage
            : 'All fingerprints have been deleted.';
      case 'success':
        return fallbackMessage.isNotEmpty
            ? fallbackMessage
            : 'Fingerprint registered. Finalizing...';
      case 'canceled':
        return fallbackMessage.isNotEmpty
            ? fallbackMessage
            : 'Fingerprint registration canceled.';
      case 'failed':
        return fallbackMessage.isNotEmpty
            ? fallbackMessage
            : 'Fingerprint registration failed.';
      default:
        return fallbackMessage.isNotEmpty
            ? fallbackMessage
            : 'Waiting for the ESP32 to start enrollment...';
    }
  }
}

class _EnrollmentResult {
  final bool success;
  final String message;

  const _EnrollmentResult({required this.success, required this.message});
}

class _RegisterReadiness {
  final bool canRegister;
  final String message;

  const _RegisterReadiness._({
    required this.canRegister,
    required this.message,
  });

  const _RegisterReadiness.allowed() : this._(canRegister: true, message: '');

  const _RegisterReadiness.blocked(String message)
    : this._(canRegister: false, message: message);
}

class _AccessOption {
  final int caseNumber;
  final String label;
  final String accessValue;
  final String description;
  final IconData icon;
  final Color accent;

  const _AccessOption({
    required this.caseNumber,
    required this.label,
    required this.accessValue,
    required this.description,
    required this.icon,
    required this.accent,
  });
}

class _DeleteFingerprintOption {
  final String label;
  final String accessValue;
  final IconData icon;
  final Color accent;
  final bool deleteAll;

  const _DeleteFingerprintOption({
    required this.label,
    required this.accessValue,
    required this.icon,
    required this.accent,
    this.deleteAll = false,
  });
}
