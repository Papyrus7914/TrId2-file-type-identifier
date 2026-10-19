#include <chrono>
#include <cpp-subprocess/subprocess.hpp>
#include <doctest.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct ProcessResult final {
  int exitCode{};
  std::string output;
  std::string error;
};

[[nodiscard]] ProcessResult runConsoleApp(const std::vector<std::string>& arguments) {
  std::vector<std::string> command{TRID2_CONSOLE_APP_PATH};
  command.append_range(arguments);

  auto process = subprocess::Popen(command, subprocess::output{subprocess::PIPE}, subprocess::error{subprocess::PIPE});
  const auto [output, error] = process.communicate();
  return ProcessResult{
      .exitCode = process.retcode(),
      .output = output.length == 0 ? std::string{} : std::string{output.buf.data(), output.length},
      .error = error.length == 0 ? std::string{} : std::string{error.buf.data(), error.length},
  };
}

[[nodiscard]] std::filesystem::path testDataPath(const std::filesystem::path& relativePath) { return std::filesystem::path{TEST_DATA_FOLDER} / relativePath; }

}

TEST_CASE("console app prints human-readable file type candidates") {
  const auto file = testDataPath("app-image/icon.png");

  const auto [exitCode, output, error] = runConsoleApp({file.string()});

  CHECK(exitCode == 0);
  CHECK(error.empty());
  CHECK(output.contains("(png) Portable Network Graphics"));
  CHECK(output.contains("    Url: https://en.wikipedia.org/wiki/Portable_Network_Graphics"));
  CHECK(output.contains("    MimeType: image/png"));
}

TEST_CASE("console app prints JSON file type candidates") {
  const auto file = testDataPath("app-image/icon.png");

  const auto [exitCode, output, error] = runConsoleApp({"--json", file.string()});

  CHECK(exitCode == 0);
  CHECK(error.empty());

  const auto json = nlohmann::json::parse(output);
  REQUIRE(json.is_array());
  REQUIRE_FALSE(json.empty());
  CHECK(json[0]["Name"] == "Portable Network Graphics");
  CHECK(json[0]["Url"] == "https://en.wikipedia.org/wiki/Portable_Network_Graphics");
  CHECK(json[0]["FileNameExtensions"] == nlohmann::json::array({"png"}));
  CHECK(json[0]["MimeType"] == "image/png");
  CHECK(json[0]["Probability"].is_number());
}

TEST_CASE("console app uses the database specified by --database-file-path") {
  const auto [exitCode, output, error] =
      runConsoleApp({"--json", "--database-file-path", (std::filesystem::path(TRID2_CONSOLE_APP_PATH).parent_path() / "TrId2Database.json").string(),
                     testDataPath("app-image/icon.png").string()});
  CHECK(exitCode == 0);
  CHECK(error.empty());
  const auto json = nlohmann::json::parse(output);
  REQUIRE(json.is_array());
  CHECK(json[0]["Name"] == "Portable Network Graphics");
}

TEST_CASE("--database-file-path prints error if the file does not exist") {
  const auto databasePath = std::filesystem::path(TRID2_CONSOLE_APP_PATH).parent_path() / "NonExistentDatabase.json";

  const auto [exitCode, output, error] = runConsoleApp({"--json", "--database-file-path", databasePath.string(), testDataPath("app-image/icon.png").string()});

  CHECK(exitCode != 0);
  CHECK(output.empty());
  CHECK(error.contains("Unable to open the database JSON file"));
  CHECK(error.contains(databasePath.string()));
}

TEST_CASE("console app limits the number of candidates") {
  const auto file = testDataPath("images/background.jpg");

  const auto defaultResult = runConsoleApp({"--json", file.string()});
  REQUIRE(defaultResult.exitCode == 0);
  const auto defaultJson = nlohmann::json::parse(defaultResult.output);
  REQUIRE(defaultJson.size() > 1U);

  const auto limitedResult = runConsoleApp({"--json", "--number-of-candidates", "1", file.string()});

  CHECK(limitedResult.exitCode == 0);
  CHECK(limitedResult.error.empty());
  const nlohmann::json limitedJson = nlohmann::json::parse(limitedResult.output);
  REQUIRE(limitedJson.is_array());
  CHECK(limitedJson.size() == 1U);
}

TEST_CASE("console app handles file paths longer than 260 characters") {
  auto longDirectoryName = std::format("TrId2_console_long_path_test_{}_", std::chrono::steady_clock::now().time_since_epoch().count());
  longDirectoryName.resize(248, 'x');
  const auto longDirectory = std::filesystem::temp_directory_path() / longDirectoryName / longDirectoryName / longDirectoryName;
  std::filesystem::create_directories(longDirectory);
  const auto file = longDirectory / "icon.png";
  std::filesystem::copy_file(testDataPath("app-image/icon.png"), file);
  REQUIRE(file.string().size() > 500);

  const auto [exitCode, output, error] = runConsoleApp({file.string()});

  CHECK(exitCode == 0);
  CHECK(error.empty());
  CHECK(output.contains("(png) Portable Network Graphics"));
}

TEST_CASE("console app reports errors to stderr") {
  const auto [exitCode, output, error] = runConsoleApp({"--json", "non_existent_file_123"});
  CHECK(exitCode != 0);
  CHECK(output.empty());
  CHECK(error.contains("Unable to open file"));
}
