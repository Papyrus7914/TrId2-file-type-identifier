// Include TrId2 library header
#include "TrId2Lib/TrId2FileTypeDetector.h"

#include <iostream>
#include <string_view>
#include <vector>

void exampleMethod() {
  // Load the database once and reuse this detector to inspect multiple files.
  const TrId2Lib::FileTypeDetector detector{"C:/database/TrId2Database.json"};
  const std::vector<TrId2Lib::FileType> candidates = detector.detectFileType(/* filePath */ "C:/file/some.png", /* maxNumberOfCandidates */ 5U);

  // If library didn't manage to guess a file type, it returns an empty vector.
  if (candidates.empty())
    return;

  // Print all candidates
  for (const TrId2Lib::FileType& candidate : candidates) {
    std::cout << "Name: " << candidate.Name << std::endl;
    std::cout << "Description: " << candidate.Description << std::endl;
    std::cout << "Url: " << candidate.Url << std::endl;
    std::cout << "MimeType: " << candidate.MimeType << std::endl;
    std::cout << "Probability: " << candidate.Probability << std::endl;
    std::cout << "FileNameExtensions: ";
    for (const std::string_view extension : candidate.FileNameExtensions) {
      std::cout << extension << " ";
    }
    std::cout << std::endl;
  }
}
