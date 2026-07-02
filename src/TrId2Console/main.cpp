#include "TrId2Lib/TrId2FileTypeDetector.h"
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <iostream>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals::string_view_literals;

static void printHumanReadableResults(const std::vector<TrId2Lib::FileType>& candidates) {
  if (candidates.empty()) {
    std::cout << "No potential file types found.\n";
    return;
  }

  for (const auto& [Name, Description, Url, FileNameExtensions, MimeType, Probability] : candidates) {
    std::cout << std::format("{:5.1f}% ", Probability);

    if (!FileNameExtensions.empty()) {
      std::cout << std::format("({}) ", FileNameExtensions | std::views::join_with(", "sv) | std::ranges::to<std::string>());
    }

    std::cout << Name << '\n';
    if (!Description.empty()) {
      std::cout << "    Description: " << Description << '\n';
    }
    if (!Url.empty()) {
      std::cout << "    Url: " << Url << '\n';
    }
    if (!MimeType.empty()) {
      std::cout << "    MimeType: " << MimeType << '\n';
    }
  }
}

int main(const int argc, char** argv) {
  CLI::App app{std::format("Detect file type based on its content. Version {}", CONSOLE_APP_VERSION)};

  std::string filePathString;
  bool jsonOutput = false;
  int numberOfCandidates = 5;

  app.add_flag("--json", jsonOutput, "Print results as JSON.");
  app.add_option("--number-of-candidates", numberOfCandidates, "Maximum number of candidates to output.")->capture_default_str();
  app.add_option("file", filePathString, "File to inspect.")->required();

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }

  try {
    if (numberOfCandidates < 1) {
      throw std::runtime_error{"'number-of-candidates' must be greater than 0."};
    }

    const auto candidates = TrId2Lib::detectFileType(std::filesystem::path{filePathString}, static_cast<std::size_t>(numberOfCandidates));

    if (jsonOutput) {
      const auto jsonResults =
          candidates | std::views::transform([](const TrId2Lib::FileType& fileType) {
            return nlohmann::json{
                {"Name", fileType.Name},         {"Description", fileType.Description},
                {"Url", fileType.Url},           {"FileNameExtensions", fileType.FileNameExtensions | std::ranges::to<std::vector<std::string>>()},
                {"MimeType", fileType.MimeType}, {"Probability", fileType.Probability},
            };
          }) |
          std::ranges::to<nlohmann::json>();
      std::cout << jsonResults.dump(2) << '\n';
    } else {
      printHumanReadableResults(candidates);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}