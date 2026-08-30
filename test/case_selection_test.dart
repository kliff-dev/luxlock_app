import 'package:flutter_test/flutter_test.dart';
import 'package:luxlock_app/models/case_id.dart';

void main() {
  group('LuxLockCaseSelection', () {
    test('builds access and override values for every checklist selection', () {
      final cases =
          <List<int>, ({int mask, String access, String target, String label})>{
            [1]: (mask: 1, access: '1', target: 'case1', label: 'Case 1'),
            [2]: (mask: 2, access: '2', target: 'case2', label: 'Case 2'),
            [1, 2]: (
              mask: 3,
              access: '1,2',
              target: 'case1_case2',
              label: 'Case 1 & 2',
            ),
            [3]: (mask: 4, access: '3', target: 'case3', label: 'Case 3'),
            [1, 3]: (
              mask: 5,
              access: '1,3',
              target: 'case1_case3',
              label: 'Case 1 & 3',
            ),
            [2, 3]: (
              mask: 6,
              access: '2,3',
              target: 'case2_case3',
              label: 'Case 2 & 3',
            ),
            [1, 2, 3]: (
              mask: 7,
              access: 'ALL',
              target: 'all',
              label: 'All Cases',
            ),
          };

      for (final entry in cases.entries) {
        final actualMask = LuxLockCaseSelection.maskFromCases(entry.key);

        expect(actualMask, entry.value.mask);
        expect(
          LuxLockCaseSelection.accessValueFromMask(actualMask),
          entry.value.access,
        );
        expect(
          LuxLockCaseSelection.overrideTargetFromMask(actualMask),
          entry.value.target,
        );
        expect(
          LuxLockCaseSelection.labelFromMask(actualMask),
          entry.value.label,
        );
        expect(LuxLockCaseSelection.isValidMask(actualMask), isTrue);
      }
    });

    test('ignores invalid case numbers and safely rejects empty masks', () {
      final mask = LuxLockCaseSelection.maskFromCases([0, 4, 2]);

      expect(mask, 2);
      expect(LuxLockCaseSelection.isValidMask(0), isFalse);
      expect(LuxLockCaseSelection.accessValueFromMask(0), isEmpty);
      expect(LuxLockCaseSelection.overrideTargetFromMask(0), isEmpty);
      expect(LuxLockCaseSelection.labelFromMask(0), 'No Case Selected');
    });
  });
}
