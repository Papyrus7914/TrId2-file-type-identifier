// This file checks that the `FileTypeDetector.h` is self-contained and can be included without errors.
#include "TrId2Lib/TrId2FileTypeDetector.h"

void foo_some_unique_function_name_test123() {
  const TrId2Lib::FileTypeDetector detector{"TrId2Database.json"};
  const auto res = detector.detectFileType("", 5U);
  static_cast<void>(res[0].Name);
}
