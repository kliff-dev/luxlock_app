import 'package:flutter_test/flutter_test.dart';
import 'package:luxlock_app/screens/history_logs_screen.dart';

void main() {
  test('history summary counts every currently loaded log type', () {
    final logs = [
      LogRow(
        firebaseKey: '1',
        time: '10:00 AM',
        date: '2026-04-29',
        caseValue: '1',
        authorized: 'Yes',
      ),
      LogRow(
        firebaseKey: '2',
        time: '10:01 AM',
        date: '2026-04-29',
        caseValue: 'N/A',
        authorized: 'Fingerprint Failed',
      ),
      LogRow(
        firebaseKey: '3',
        time: '10:02 AM',
        date: '2026-04-29',
        caseValue: '1,3',
        authorized: 'Override Open',
      ),
      LogRow(
        firebaseKey: '4',
        time: '10:03 AM',
        date: '2026-04-29',
        caseValue: '2',
        authorized: 'Tamper L1',
      ),
      LogRow(
        firebaseKey: '5',
        time: '10:04 AM',
        date: '2026-04-29',
        caseValue: 'N/A',
        authorized: 'Unknown',
      ),
    ];

    final summary = HistoryLogSummary.fromLogs(logs);

    expect(summary.totalLoaded, 5);
    expect(summary.authorized, 1);
    expect(summary.denied, 1);
    expect(summary.override, 1);
    expect(summary.tamper, 1);
  });

  test('history screen requests a fuller recent window', () {
    expect(historyLogsQueryLimit, 500);
  });
}
