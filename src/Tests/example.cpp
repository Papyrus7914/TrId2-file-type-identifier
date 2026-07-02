// Include TrId2 library header
#include "TrId2Lib/TrId2FileTypeDetector.h"

#include <iostream>
#include <string_view>
#include <vector>

void exampleMethod() {
  // Detect possible file types.
  // `TrId2Lib::detectFileType` uses the file type database from the `TrId2Database.h` header.
  // The database is compiled into your binary. Thus there is no need to configure anything. You can use `TrId2Lib::detectFileType` right away.
  const std::vector<TrId2Lib::FileType> candidates = TrId2Lib::detectFileType(/* filePath */ "C:/file/some.png", /* maxNumberOfCandidates */ 5U);

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