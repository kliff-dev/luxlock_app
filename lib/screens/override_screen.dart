import 'dart:async';

import 'package:flutter/material.dart';

import '../models/case_id.dart';
import '../services/luxlock_connection_service.dart';
import '../theme/luxlock_theme.dart';
import '../widgets/luxlock_ui.dart';

Future<LuxLockOverrideCommandResult> sendOverride({
  required String target,
  // "case1" | "case2" | "case3" | "case1_case2" | "case1_case3" | "case2_case3" | "all"
  required String action, // "open" | "close" | "stop"
}) async {
  return LuxLockConnectionService.instance.sendOverrideCommand(
    target: target,
    action: action,
  );
}

Future<bool> waitForOverrideConfirmation(
  int nonce, {
  Duration timeout = const Duration(seconds: 15),
}) async {
  final expectedNonce = nonce.toString();
  final deadline = DateTime.now().add(timeout);

  while (DateTime.now().isBefore(deadline)) {
    final status = LuxLockConnectionService.instance.deviceStatus;
    if (status.lastProcessedOverrideNonce == expectedNonce) {
      if (status.lastOverrideResult == 'accepted') {
        return true;
      }

      final error = status.lastOverrideError.isNotEmpty
          ? status.lastOverrideError
          : 'The ESP32 rejected the override command.';
      throw StateError(error);
    }

    await Future<void>.delayed(const Duration(milliseconds: 250));
  }

  return false;
}

class OverrideScreen extends StatefulWidget {
  const OverrideScreen({super.key});

  @override
  State<OverrideScreen> createState() => _OverrideScreenState();
}

class _OverrideScreenState extends State<OverrideScreen> {
  final Set<int> _selectedCases = <int>{};

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

  IconData _iconFromMask(int mask) {
    return switch (mask) {
      1 => Icons.looks_one_rounded,
      2 => Icons.looks_two_rounded,
      4 => Icons.looks_3_rounded,
      7 => Icons.emergency_share_rounded,
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

  void _confirmSelectedOverride(
    BuildContext context,
    _OverrideAvailability availability, {
    required String action,
  }) {
    final mask = _selectedMask;
    final target = LuxLockCaseSelection.overrideTargetFromMask(mask);

    if (target.isEmpty) {
      _showSelectionRequired(context);
      return;
    }

    final allowed = switch (action) {
      'open' => availability.canOpenMask(mask),
      'close' => availability.canCloseMask(mask),
      'stop' => availability.canStopMask(mask),
      _ => false,
    };

    if (!allowed) {
      return;
    }

    final caseLabel = LuxLockCaseSelection.labelFromMask(mask);
    final label = '$caseLabel ${action.toUpperCase()}';
    _confirmOverride(context, target: target, action: action, label: label);
  }

  void _showSelectionRequired(BuildContext context) {
    showDialog<void>(
      context: context,
      builder: (_) => AlertDialog(
        title: const Text('Select a Case'),
        content: const Text(
          'Choose at least one case before sending a command.',
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

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: LuxLockConnectionService.instance,
      builder: (context, _) {
        final connection = LuxLockConnectionService.instance;
        final availability = _OverrideAvailability.fromDeviceStatus(
          connection.deviceStatus,
          deviceResponsive: connection.canSendOverrideCommands,
          deviceStatusLabel: connection.statusLabel,
        );
        final selectedMask = _selectedMask;
        final selectedLabel = LuxLockCaseSelection.labelFromMask(selectedMask);

        return LuxLockScaffold(
          eyebrow: 'Override',
          title: 'Master Key',
          subtitle: 'Select cases, then send a manual command.',
          child: ListView(
            physics: const AlwaysScrollableScrollPhysics(),
            children: [
              LuxLockPanel(
                padding: const EdgeInsets.all(14),
                child: Row(
                  children: [
                    Icon(
                      availability.buttonsLocked
                          ? Icons.lock_clock_rounded
                          : Icons.verified_rounded,
                      color: availability.buttonsLocked
                          ? LuxLockPalette.coral
                          : LuxLockPalette.mint,
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: Text(
                        availability.bannerMessage,
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
                              selectedMask,
                            ).withValues(alpha: 0.18),
                            borderRadius: BorderRadius.circular(18),
                          ),
                          child: Icon(
                            _iconFromMask(selectedMask),
                            color: _accentFromMask(selectedMask),
                          ),
                        ),
                        const SizedBox(width: 14),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(
                                'Override selection',
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
                    for (final caseOverride in _cases) ...[
                      _OverrideCaseSelectionTile(
                        data: caseOverride,
                        selected: _selectedCases.contains(
                          caseOverride.caseNumber,
                        ),
                        status: availability.statusFor(caseOverride.target),
                        onTap: () => _toggleCase(caseOverride.caseNumber),
                      ),
                      if (caseOverride != _cases.last)
                        const SizedBox(height: 10),
                    ],
                  ],
                ),
              ),
              const SizedBox(height: 18),
              LuxLockPanel(
                color: const Color(0xD9192E45),
                padding: const EdgeInsets.all(16),
                child: LayoutBuilder(
                  builder: (context, constraints) {
                    final buttons = [
                      ElevatedButton.icon(
                        onPressed:
                            _hasSelectedCases &&
                                availability.canOpenMask(selectedMask)
                            ? () => _confirmSelectedOverride(
                                context,
                                availability,
                                action: 'open',
                              )
                            : null,
                        icon: const Icon(Icons.lock_open_rounded),
                        label: Text(selectedMask == 7 ? 'OPEN ALL' : 'OPEN'),
                      ),
                      OutlinedButton.icon(
                        onPressed:
                            _hasSelectedCases &&
                                availability.canCloseMask(selectedMask)
                            ? () => _confirmSelectedOverride(
                                context,
                                availability,
                                action: 'close',
                              )
                            : null,
                        icon: const Icon(Icons.lock_rounded),
                        label: Text(selectedMask == 7 ? 'CLOSE ALL' : 'CLOSE'),
                      ),
                      ElevatedButton.icon(
                        style: ElevatedButton.styleFrom(
                          backgroundColor: LuxLockPalette.coral,
                          foregroundColor: Colors.white,
                          disabledBackgroundColor: LuxLockPalette.coral
                              .withValues(alpha: 0.35),
                          disabledForegroundColor: Colors.white.withValues(
                            alpha: 0.72,
                          ),
                        ),
                        onPressed:
                            _hasSelectedCases &&
                                availability.canStopMask(selectedMask)
                            ? () => _confirmSelectedOverride(
                                context,
                                availability,
                                action: 'stop',
                              )
                            : null,
                        icon: const Icon(Icons.pause_circle_filled_rounded),
                        label: Text(selectedMask == 7 ? 'STOP ALL' : 'STOP'),
                      ),
                    ];

                    if (constraints.maxWidth < 620) {
                      return Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          buttons[0],
                          const SizedBox(height: 12),
                          buttons[1],
                          const SizedBox(height: 12),
                          buttons[2],
                        ],
                      );
                    }

                    return Row(
                      children: [
                        Expanded(child: buttons[0]),
                        const SizedBox(width: 12),
                        Expanded(child: buttons[1]),
                        const SizedBox(width: 12),
                        Expanded(child: buttons[2]),
                      ],
                    );
                  },
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

const List<_CaseOverride> _cases = [
  _CaseOverride(
    caseNumber: 1,
    title: 'Case 1',
    target: 'case1',
    accent: LuxLockPalette.gold,
    icon: Icons.looks_one_rounded,
  ),
  _CaseOverride(
    caseNumber: 2,
    title: 'Case 2',
    target: 'case2',
    accent: LuxLockPalette.mint,
    icon: Icons.looks_two_rounded,
  ),
  _CaseOverride(
    caseNumber: 3,
    title: 'Case 3',
    target: 'case3',
    accent: LuxLockPalette.coral,
    icon: Icons.looks_3_rounded,
  ),
];

class _OverrideAvailability {
  final Map<String, _ActuatorStatus> statuses;
  final bool hasLiveStatus;
  final bool deviceResponsive;
  final String deviceStatusLabel;

  const _OverrideAvailability({
    required this.statuses,
    required this.hasLiveStatus,
    required this.deviceResponsive,
    required this.deviceStatusLabel,
  });

  factory _OverrideAvailability.fromDeviceStatus(
    LuxLockDeviceStatus deviceStatus, {
    required bool deviceResponsive,
    required String deviceStatusLabel,
  }) {
    final statuses = <String, _ActuatorStatus>{};
    for (final caseOverride in _cases) {
      final status = deviceStatus.actuators[caseOverride.target];
      statuses[caseOverride.target] = status == null
          ? const _ActuatorStatus.unknown()
          : _ActuatorStatus.fromDeviceStatus(status);
    }

    return _OverrideAvailability(
      statuses: statuses,
      hasLiveStatus: deviceStatus.actuators.isNotEmpty,
      deviceResponsive: deviceResponsive,
      deviceStatusLabel: deviceStatusLabel,
    );
  }

  _ActuatorStatus statusFor(String target) =>
      statuses[target] ?? const _ActuatorStatus.unknown();

  List<_ActuatorStatus> statusesForMask(int mask) {
    final selected = <_ActuatorStatus>[];
    for (final caseOverride in _cases) {
      if ((mask & caseOverride.mask) != 0) {
        selected.add(statusFor(caseOverride.target));
      }
    }
    return selected;
  }

  bool get buttonsLocked =>
      !hasLiveStatus ||
      !deviceResponsive ||
      statuses.values.any((status) => status.active || status.cooldownActive);

  bool canOpenMask(int mask) {
    final selected = statusesForMask(mask);
    return hasLiveStatus &&
        deviceResponsive &&
        !buttonsLocked &&
        selected.isNotEmpty &&
        selected.every((status) => !status.isUnknown) &&
        selected.any((status) => !status.positionOpen);
  }

  bool canCloseMask(int mask) {
    final selected = statusesForMask(mask);
    return hasLiveStatus &&
        deviceResponsive &&
        !buttonsLocked &&
        selected.isNotEmpty &&
        selected.every((status) => !status.isUnknown) &&
        selected.any((status) => status.positionOpen);
  }

  bool canStopMask(int mask) {
    final selected = statusesForMask(mask);
    return hasLiveStatus &&
        deviceResponsive &&
        selected.isNotEmpty &&
        selected.any((status) => status.active);
  }

  String get bannerMessage {
    if (!deviceResponsive) {
      return '$deviceStatusLabel. Controls locked.';
    }

    if (!hasLiveStatus) {
      return 'Waiting for case status.';
    }

    if (statuses.values.any((status) => status.active)) {
      if (statuses.values.any((status) => status.almostDone)) {
        return 'Almost done. Controls paused.';
      }

      return 'Case moving. Controls paused.';
    }

    if (statuses.values.any((status) => status.cooldownActive)) {
      return 'Cooling down. Please wait.';
    }

    return 'Ready for manual control.';
  }
}

class _ActuatorStatus {
  final bool active;
  final bool positionOpen;
  final bool autoCloseArmed;
  final bool cooldownActive;
  final String state;

  const _ActuatorStatus({
    required this.active,
    required this.positionOpen,
    required this.autoCloseArmed,
    required this.cooldownActive,
    required this.state,
  });

  const _ActuatorStatus.unknown()
    : active = false,
      positionOpen = false,
      autoCloseArmed = false,
      cooldownActive = false,
      state = 'unknown';

  factory _ActuatorStatus.fromDeviceStatus(LuxLockActuatorStatus status) {
    return _ActuatorStatus(
      active: status.active,
      positionOpen: status.positionOpen,
      autoCloseArmed: status.autoCloseArmed,
      cooldownActive: status.cooldownActive,
      state: status.state,
    );
  }

  bool get isUnknown => state == 'unknown';

  String get label {
    if (state == 'almost_open') {
      return 'ALMOST OPEN';
    }

    if (state == 'almost_close') {
      return 'ALMOST CLOSED';
    }

    if (active) {
      return state == 'close' ? 'CLOSING' : 'OPENING';
    }

    if (cooldownActive) {
      return 'COOLDOWN';
    }

    if (positionOpen) {
      return autoCloseArmed ? 'OPEN / AUTO' : 'OPEN';
    }

    if (state == 'unknown') {
      return 'UNKNOWN';
    }

    return 'CLOSED';
  }

  Color get badgeBackgroundColor {
    if (active) {
      return LuxLockPalette.gold.withValues(alpha: 0.2);
    }

    if (cooldownActive) {
      return LuxLockPalette.coral.withValues(alpha: 0.18);
    }

    if (positionOpen) {
      return LuxLockPalette.mint.withValues(alpha: 0.18);
    }

    return const Color(0x14FFFFFF);
  }

  Color get badgeForegroundColor {
    if (active) {
      return LuxLockPalette.gold;
    }

    if (cooldownActive) {
      return LuxLockPalette.coral;
    }

    if (positionOpen) {
      return LuxLockPalette.mint;
    }

    return LuxLockPalette.textMuted;
  }

  bool get almostDone =>
      active && (state == 'almost_open' || state == 'almost_close');
}

void _confirmOverride(
  BuildContext context, {
  required String target,
  required String action,
  required String label,
}) {
  showDialog(
    context: context,
    builder: (_) => AlertDialog(
      title: const Text('Confirm Override'),
      content: Text('Send command: $label?'),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('Cancel'),
        ),
        ElevatedButton(
          onPressed: () async {
            Navigator.pop(context);

            try {
              final result = await sendOverride(target: target, action: action);
              final confirmed =
                  result.confirmedImmediately ||
                  await waitForOverrideConfirmation(result.nonce);

              if (!context.mounted) return;

              ScaffoldMessenger.of(context).showSnackBar(
                SnackBar(
                  content: Text(
                    confirmed
                        ? '$label confirmed by ESP32 via ${result.deliveryLabel}'
                        : '$label sent, but the ESP32 did not confirm in time',
                  ),
                ),
              );
            } catch (e) {
              if (!context.mounted) return;

              ScaffoldMessenger.of(
                context,
              ).showSnackBar(SnackBar(content: Text('Failed: $e')));
            }
          },
          child: const Text('Confirm'),
        ),
      ],
    ),
  );
}

class _OverrideCaseSelectionTile extends StatelessWidget {
  final _CaseOverride data;
  final bool selected;
  final _ActuatorStatus status;
  final VoidCallback onTap;

  const _OverrideCaseSelectionTile({
    required this.data,
    required this.selected,
    required this.status,
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
                ? data.accent.withValues(alpha: 0.12)
                : const Color(0x0FFFFFFF),
            border: Border.all(
              color: selected
                  ? data.accent.withValues(alpha: 0.72)
                  : Colors.white.withValues(alpha: 0.08),
            ),
            borderRadius: BorderRadius.circular(16),
          ),
          child: Row(
            children: [
              Checkbox(
                value: selected,
                onChanged: (_) => onTap(),
                activeColor: data.accent,
                checkColor: LuxLockPalette.midnight,
                side: BorderSide(color: data.accent.withValues(alpha: 0.72)),
              ),
              const SizedBox(width: 8),
              Icon(data.icon, color: data.accent),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  data.title,
                  style: Theme.of(context).textTheme.titleMedium,
                ),
              ),
              LuxLockBadge(
                label: status.label,
                backgroundColor: status.badgeBackgroundColor,
                foregroundColor: status.badgeForegroundColor,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _CaseOverride {
  final int caseNumber;
  final String title;
  final String target;
  final Color accent;
  final IconData icon;

  const _CaseOverride({
    required this.caseNumber,
    required this.title,
    required this.target,
    required this.accent,
    required this.icon,
  });

  int get mask => 1 << (caseNumber - 1);
}
