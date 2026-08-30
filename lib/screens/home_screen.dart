import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../services/luxlock_connection_service.dart';
import '../theme/luxlock_theme.dart';
import '../widgets/luxlock_ui.dart';
import 'change_pin_screen.dart';
import 'history_logs_screen.dart';
import 'override_screen.dart';
import 'register_screen.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return LuxLockScaffold(
      eyebrow: 'Operations',
      title: 'LuxLock Control',
      subtitle: 'Register access, override cases, and review logs.',
      showBackButton: true,
      onBackTap: () => SystemNavigator.pop(),
      child: ListView(
        physics: const AlwaysScrollableScrollPhysics(),
        children: [
          LuxLockPanel(
            padding: const EdgeInsets.all(16),
            child: Row(
              children: [
                Expanded(
                  child: AnimatedBuilder(
                    animation: LuxLockConnectionService.instance,
                    builder: (context, _) {
                      final connection = LuxLockConnectionService.instance;
                      final fingerprintReady =
                          connection.deviceStatus.fingerprintReady;

                      return _StatusPill(
                        icon: fingerprintReady
                            ? Icons.fingerprint_rounded
                            : Icons.do_not_touch_rounded,
                        label: fingerprintReady
                            ? 'Fingerprint ready'
                            : 'Fingerprint offline',
                        iconColor: fingerprintReady
                            ? LuxLockPalette.gold
                            : LuxLockPalette.coral,
                      );
                    },
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: AnimatedBuilder(
                    animation: LuxLockConnectionService.instance,
                    builder: (context, _) {
                      final connection = LuxLockConnectionService.instance;

                      return _StatusPill(
                        icon: connection.canSendCommands
                            ? Icons.shield_rounded
                            : Icons.cloud_off_rounded,
                        label: connection.canSendCommands
                            ? 'Device ready'
                            : 'Device offline',
                        iconColor: connection.canSendCommands
                            ? LuxLockPalette.gold
                            : LuxLockPalette.coral,
                      );
                    },
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 18),
          _HomeActionCard(
            icon: Icons.person_add_alt_1_rounded,
            title: 'Register Access',
            description:
                'Assign single-case, shared, or all-case fingerprint access in a guided flow.',
            accent: LuxLockPalette.gold,
            onTap: () {
              Navigator.push(
                context,
                MaterialPageRoute(builder: (_) => const RegisterScreen()),
              );
            },
          ),
          const SizedBox(height: 14),
          _HomeActionCard(
            icon: Icons.admin_panel_settings_outlined,
            title: 'Master Key Override',
            description:
                'Open, close, or stop each case manually when intervention is required.',
            accent: LuxLockPalette.mint,
            onTap: () {
              Navigator.push(
                context,
                MaterialPageRoute(builder: (_) => const OverrideScreen()),
              );
            },
          ),
          const SizedBox(height: 14),
          _HomeActionCard(
            icon: Icons.timeline_rounded,
            title: 'History Logs',
            description:
                'Review the newest access results first, including denied attempts and shared-case entries.',
            accent: LuxLockPalette.coral,
            onTap: () {
              Navigator.push(
                context,
                MaterialPageRoute(builder: (_) => const HistoryLogsScreen()),
              );
            },
          ),
          const SizedBox(height: 14),
          _HomeActionCard(
            icon: Icons.pin_rounded,
            title: 'Change PIN',
            description:
                'Update the 4-digit app PIN. Firebase must be online for this change.',
            accent: LuxLockPalette.gold,
            onTap: () {
              Navigator.push(
                context,
                MaterialPageRoute(builder: (_) => const ChangePinScreen()),
              );
            },
          ),
        ],
      ),
    );
  }
}

class _HomeActionCard extends StatelessWidget {
  final IconData icon;
  final String title;
  final String description;
  final Color accent;
  final VoidCallback onTap;

  const _HomeActionCard({
    required this.icon,
    required this.title,
    required this.description,
    required this.accent,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(28),
      child: LuxLockPanel(
        child: Row(
          children: [
            Container(
              width: 62,
              height: 62,
              decoration: BoxDecoration(
                color: accent.withValues(alpha: 0.18),
                borderRadius: BorderRadius.circular(20),
              ),
              child: Icon(icon, color: accent, size: 30),
            ),
            const SizedBox(width: 16),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(title, style: Theme.of(context).textTheme.titleLarge),
                  const SizedBox(height: 8),
                  Text(
                    description,
                    style: Theme.of(context).textTheme.bodyMedium,
                  ),
                ],
              ),
            ),
            const SizedBox(width: 12),
            Container(
              width: 42,
              height: 42,
              decoration: BoxDecoration(
                gradient: LuxLockPalette.goldGradient,
                borderRadius: BorderRadius.circular(16),
                boxShadow: const [
                  BoxShadow(
                    color: Color(0x30F7D778),
                    blurRadius: 18,
                    offset: Offset(0, 8),
                  ),
                ],
              ),
              child: const Icon(
                Icons.arrow_forward_rounded,
                color: LuxLockPalette.midnight,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _StatusPill extends StatelessWidget {
  final IconData icon;
  final String label;
  final Color iconColor;

  const _StatusPill({
    required this.icon,
    required this.label,
    this.iconColor = LuxLockPalette.gold,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
      decoration: BoxDecoration(
        color: const Color(0x16000000),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: const Color(0x30F7D778)),
      ),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(icon, size: 18, color: iconColor),
          const SizedBox(width: 8),
          Flexible(
            child: Text(
              label,
              maxLines: 2,
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.labelLarge?.copyWith(
                color: LuxLockPalette.textPrimary,
                fontSize: 12,
                height: 1.1,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
