import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';

import '../services/luxlock_connection_service.dart';
import '../theme/luxlock_theme.dart';
import '../widgets/luxlock_ui.dart';

const int historyLogsQueryLimit = 500;

class HistoryLogsScreen extends StatelessWidget {
  const HistoryLogsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return LuxLockScaffold(
      eyebrow: 'History',
      title: 'Access Logs',
      subtitle: 'Newest records from the device.',
      child: AnimatedBuilder(
        animation: LuxLockConnectionService.instance,
        builder: (context, _) {
          final connection = LuxLockConnectionService.instance;
          if (!connection.canReachCommandPath) {
            return const _HistoryMessageList(
              child: _HistoryMessagePanel(
                icon: Icons.cloud_off_rounded,
                title: 'History Offline',
                message:
                    'Firebase cannot be reached right now. Commands and cloud history will update after reconnection.',
              ),
            );
          }

          final historyQuery = luxLockDb
              .child('devices/esp32_1/history')
              .orderByChild('epoch')
              .limitToLast(historyLogsQueryLimit);

          return StreamBuilder<DatabaseEvent>(
            stream: historyQuery.onValue,
            builder: (context, snapshot) {
              if (snapshot.hasError) {
                return _HistoryMessageList(
                  child: _HistoryMessagePanel(
                    icon: Icons.cloud_off_rounded,
                    title: 'Unable to load history',
                    message: 'Error loading history: ${snapshot.error}',
                  ),
                );
              }

              if (!snapshot.hasData || snapshot.data?.snapshot.value == null) {
                return const _HistoryMessageList(
                  child: _HistoryMessagePanel(
                    icon: Icons.inbox_outlined,
                    title: 'No history logs yet',
                    message:
                        'Once the ESP32 writes access results, the newest entries will appear here automatically.',
                  ),
                );
              }

              final rawData = snapshot.data!.snapshot.value;

              if (rawData is! Map) {
                return const _HistoryMessageList(
                  child: _HistoryMessagePanel(
                    icon: Icons.data_object_rounded,
                    title: 'Invalid history log format',
                    message:
                        'The history node is present, but its structure does not match the expected Firebase payload.',
                  ),
                );
              }

              final data = Map<dynamic, dynamic>.from(rawData);
              final entries = data.entries.toList();
              final logs = entries.where((entry) => entry.value is Map).map((
                entry,
              ) {
                final value = Map<dynamic, dynamic>.from(entry.value as Map);

                return LogRow(
                  firebaseKey: entry.key?.toString() ?? '',
                  time: value['time']?.toString() ?? '',
                  date: value['date']?.toString() ?? '',
                  caseValue: value['case']?.toString() ?? 'N/A',
                  authorized: value['authorized']?.toString() ?? '',
                );
              }).toList();
              logs.sort(LogRow.compareNewestFirst);

              final summary = HistoryLogSummary.fromLogs(logs);

              return ListView.separated(
                physics: const AlwaysScrollableScrollPhysics(),
                itemCount: logs.length + 1,
                separatorBuilder: (context, index) =>
                    const SizedBox(height: 14),
                itemBuilder: (context, index) {
                  if (index == 0) {
                    return LuxLockPanel(
                      padding: const EdgeInsets.all(16),
                      child: _HistorySummaryGrid(summary: summary),
                    );
                  }

                  return _HistoryLogCard(logs[index - 1]);
                },
              );
            },
          );
        },
      ),
    );
  }
}

class _HistoryMessageList extends StatelessWidget {
  final Widget child;

  const _HistoryMessageList({required this.child});

  @override
  Widget build(BuildContext context) {
    return ListView(
      physics: const AlwaysScrollableScrollPhysics(),
      children: [
        SizedBox(
          height: MediaQuery.of(context).size.height * 0.45,
          child: child,
        ),
      ],
    );
  }
}

class LogRow {
  final String firebaseKey;
  final String time;
  final String date;
  final String caseValue;
  final String authorized;
  final DateTime? sortTimestamp;

  LogRow({
    required this.firebaseKey,
    required this.time,
    required this.date,
    required this.caseValue,
    required this.authorized,
  }) : sortTimestamp = _parseTimestamp(date, time);

  HistoryStatusType get statusType {
    final normalized = authorized.trim().toLowerCase();

    if (normalized == 'yes') {
      return HistoryStatusType.authorized;
    }

    if (normalized == 'no' || normalized.startsWith('fingerprint failed')) {
      return HistoryStatusType.denied;
    }

    if (normalized.startsWith('override') || normalized.startsWith('auto')) {
      return HistoryStatusType.override;
    }

    if (normalized.startsWith('tamper')) {
      return HistoryStatusType.tamper;
    }

    return HistoryStatusType.unknown;
  }

  static int compareNewestFirst(LogRow left, LogRow right) {
    final leftTimestamp = left.sortTimestamp;
    final rightTimestamp = right.sortTimestamp;

    if (leftTimestamp != null && rightTimestamp != null) {
      final timestampOrder = rightTimestamp.compareTo(leftTimestamp);
      if (timestampOrder != 0) {
        return timestampOrder;
      }
    } else if (leftTimestamp != null) {
      return -1;
    } else if (rightTimestamp != null) {
      return 1;
    }

    return right.firebaseKey.compareTo(left.firebaseKey);
  }

  static DateTime? _parseTimestamp(String date, String time) {
    final dateParts = date.split('-');
    if (dateParts.length != 3) {
      return null;
    }

    final year = int.tryParse(dateParts[0]);
    final month = int.tryParse(dateParts[1]);
    final day = int.tryParse(dateParts[2]);
    if (year == null || month == null || day == null) {
      return null;
    }

    final timeMatch = RegExp(
      r'^(\d{1,2}):(\d{2})\s*(AM|PM)$',
      caseSensitive: false,
    ).firstMatch(time.trim());
    if (timeMatch == null) {
      return null;
    }

    final rawHour = int.tryParse(timeMatch.group(1) ?? '');
    final minute = int.tryParse(timeMatch.group(2) ?? '');
    final meridiem = (timeMatch.group(3) ?? '').toUpperCase();
    if (rawHour == null || minute == null || rawHour < 1 || rawHour > 12) {
      return null;
    }

    var hour = rawHour % 12;
    if (meridiem == 'PM') {
      hour += 12;
    }

    return DateTime(year, month, day, hour, minute);
  }
}

class HistoryLogSummary {
  final int totalLoaded;
  final int authorized;
  final int denied;
  final int override;
  final int tamper;

  const HistoryLogSummary({
    required this.totalLoaded,
    required this.authorized,
    required this.denied,
    required this.override,
    required this.tamper,
  });

  factory HistoryLogSummary.fromLogs(List<LogRow> logs) {
    return HistoryLogSummary(
      totalLoaded: logs.length,
      authorized: logs.where((log) => log.statusType.isAuthorized).length,
      denied: logs.where((log) => log.statusType.isDenied).length,
      override: logs.where((log) => log.statusType.isOverride).length,
      tamper: logs.where((log) => log.statusType.isTamper).length,
    );
  }
}

enum HistoryStatusType {
  authorized,
  denied,
  override,
  tamper,
  unknown;

  bool get isAuthorized => this == HistoryStatusType.authorized;
  bool get isDenied => this == HistoryStatusType.denied;
  bool get isOverride => this == HistoryStatusType.override;
  bool get isTamper => this == HistoryStatusType.tamper;
}

class _HistorySummaryGrid extends StatelessWidget {
  final HistoryLogSummary summary;

  const _HistorySummaryGrid({required this.summary});

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final columns = constraints.maxWidth >= 520 ? 5 : 2;
        final spacing = 10.0;
        final itemWidth =
            (constraints.maxWidth - (spacing * (columns - 1))) / columns;

        return Wrap(
          spacing: spacing,
          runSpacing: spacing,
          children: [
            _SummaryStatBox(
              width: itemWidth,
              label: 'Total',
              value: '${summary.totalLoaded}',
              accent: LuxLockPalette.cyanGlow,
            ),
            _SummaryStatBox(
              width: itemWidth,
              label: 'Allowed',
              value: '${summary.authorized}',
              accent: LuxLockPalette.mint,
            ),
            _SummaryStatBox(
              width: itemWidth,
              label: 'Denied',
              value: '${summary.denied}',
              accent: LuxLockPalette.coral,
            ),
            _SummaryStatBox(
              width: itemWidth,
              label: 'Override',
              value: '${summary.override}',
              accent: LuxLockPalette.gold,
            ),
            _SummaryStatBox(
              width: itemWidth,
              label: 'Tamper',
              value: '${summary.tamper}',
              accent: LuxLockPalette.coral,
            ),
          ],
        );
      },
    );
  }
}

class _SummaryStatBox extends StatelessWidget {
  final double width;
  final String label;
  final String value;
  final Color accent;

  const _SummaryStatBox({
    required this.width,
    required this.label,
    required this.value,
    required this.accent,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: width,
      child: _SummaryStat(label: label, value: value, accent: accent),
    );
  }
}

class _HistoryMessagePanel extends StatelessWidget {
  final IconData icon;
  final String title;
  final String message;

  const _HistoryMessagePanel({
    required this.icon,
    required this.title,
    required this.message,
  });

  @override
  Widget build(BuildContext context) {
    return Center(
      child: LuxLockPanel(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 40, color: LuxLockPalette.gold),
            const SizedBox(height: 16),
            Text(title, style: Theme.of(context).textTheme.titleLarge),
            const SizedBox(height: 10),
            Text(
              message,
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.bodyMedium,
            ),
          ],
        ),
      ),
    );
  }
}

class _SummaryStat extends StatelessWidget {
  final String label;
  final String value;
  final Color accent;

  const _SummaryStat({
    required this.label,
    required this.value,
    required this.accent,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 12),
      decoration: BoxDecoration(
        color: const Color(0x16000000),
        borderRadius: BorderRadius.circular(18),
        border: Border.all(color: accent.withValues(alpha: 0.24)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Text(
            label,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: Theme.of(
              context,
            ).textTheme.bodyMedium?.copyWith(fontSize: 12),
          ),
          const SizedBox(height: 8),
          Text(
            value,
            style: Theme.of(
              context,
            ).textTheme.headlineMedium?.copyWith(fontSize: 24, color: accent),
          ),
        ],
      ),
    );
  }
}

class _HistoryLogCard extends StatelessWidget {
  final LogRow row;

  const _HistoryLogCard(this.row);

  @override
  Widget build(BuildContext context) {
    final iconColor = switch (row.statusType) {
      HistoryStatusType.authorized => LuxLockPalette.mint,
      HistoryStatusType.denied => LuxLockPalette.coral,
      HistoryStatusType.override => LuxLockPalette.gold,
      HistoryStatusType.tamper => LuxLockPalette.coral,
      HistoryStatusType.unknown => LuxLockPalette.textMuted,
    };

    final iconData = switch (row.statusType) {
      HistoryStatusType.authorized => Icons.verified_user_rounded,
      HistoryStatusType.denied => Icons.gpp_bad_rounded,
      HistoryStatusType.override => Icons.admin_panel_settings_rounded,
      HistoryStatusType.tamper => Icons.warning_amber_rounded,
      HistoryStatusType.unknown => Icons.help_outline_rounded,
    };

    return LuxLockPanel(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 48,
                height: 48,
                decoration: BoxDecoration(
                  color: iconColor.withValues(alpha: 0.18),
                  borderRadius: BorderRadius.circular(16),
                ),
                child: Icon(iconData, color: iconColor),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      row.time,
                      style: Theme.of(context).textTheme.titleLarge,
                    ),
                    const SizedBox(height: 4),
                    Text(
                      row.date,
                      style: Theme.of(context).textTheme.bodyMedium,
                    ),
                  ],
                ),
              ),
              LuxLockBadge(
                label: row.authorized.isEmpty ? 'Unknown' : row.authorized,
                backgroundColor: iconColor.withValues(alpha: 0.18),
                foregroundColor: iconColor,
              ),
            ],
          ),
          const SizedBox(height: 16),
          Container(
            padding: const EdgeInsets.all(14),
            decoration: BoxDecoration(
              color: const Color(0x14FFFFFF),
              borderRadius: BorderRadius.circular(20),
            ),
            child: Row(
              children: [
                Text(
                  'Case',
                  style: Theme.of(context).textTheme.labelLarge?.copyWith(
                    color: LuxLockPalette.textMuted,
                  ),
                ),
                const Spacer(),
                LuxLockBadge(
                  label: row.caseValue,
                  backgroundColor: const Color(0x20F4D58D),
                  foregroundColor: LuxLockPalette.gold,
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
