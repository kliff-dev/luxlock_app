enum CaseID { case1, case2, case3, all }

extension CaseIDLabel on CaseID {
  String get label {
    switch (this) {
      case CaseID.case1:
        return 'Case 1';
      case CaseID.case2:
        return 'Case 2';
      case CaseID.case3:
        return 'Case 3';
      case CaseID.all:
        return 'All Cases';
    }
  }
}

class LuxLockCaseSelection {
  const LuxLockCaseSelection._();

  static int maskFromCases(Iterable<int> cases) {
    var mask = 0;
    for (final caseNumber in cases) {
      if (caseNumber < 1 || caseNumber > 3) {
        continue;
      }

      mask |= 1 << (caseNumber - 1);
    }
    return mask;
  }

  static bool isValidMask(int mask) => mask >= 1 && mask <= 7;

  static String accessValueFromMask(int mask) {
    return switch (mask) {
      1 => '1',
      2 => '2',
      3 => '1,2',
      4 => '3',
      5 => '1,3',
      6 => '2,3',
      7 => 'ALL',
      _ => '',
    };
  }

  static String overrideTargetFromMask(int mask) {
    return switch (mask) {
      1 => 'case1',
      2 => 'case2',
      3 => 'case1_case2',
      4 => 'case3',
      5 => 'case1_case3',
      6 => 'case2_case3',
      7 => 'all',
      _ => '',
    };
  }

  static String labelFromMask(int mask) {
    return switch (mask) {
      1 => 'Case 1',
      2 => 'Case 2',
      3 => 'Case 1 & 2',
      4 => 'Case 3',
      5 => 'Case 1 & 3',
      6 => 'Case 2 & 3',
      7 => 'All Cases',
      _ => 'No Case Selected',
    };
  }
}
