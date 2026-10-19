#include "TrId2Lib/TrId2FileTypeDetector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <doctest.h>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

[[nodiscard]] static const TrId2Lib::FileTypeDetector& databaseDetector() {
  static const TrId2Lib::FileTypeDetector detector{TRID2_DATABASE_PATH};
  return detector;
}

[[nodiscard]] static bool containsCandidate(const std::vector<TrId2Lib::FileType>& candidates, const std::string_view name) {
  return std::ranges::any_of(candidates, [name](const auto& candidate) { return candidate.Name == name; });
}

TEST_CASE("detectFileType returns empty vector for empty input") {
  const auto candidates = databaseDetector().detectFileType(std::span<const std::byte>{}, 5U);
  CHECK(candidates.empty());
}

TEST_CASE("detectFileType throws when file cannot be opened") {
  const auto path = std::filesystem::temp_directory_path() / "TrId2_missing_file_for_open_test.bin";
  std::error_code ec;
  std::filesystem::remove(path, ec);

  CHECK_THROWS_AS(static_cast<void>(databaseDetector().detectFileType(path, 5U)), std::runtime_error);
}

TEST_CASE("detectFileType recognizes stable in-memory signatures") {
  SUBCASE("PNG") {
    constexpr auto bytes = std::to_array<unsigned char>(
        {0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n', 0x00U, 0x00U, 0x00U, '\r', 'I', 'H', 'D', 'R', 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U});

    const auto result = databaseDetector().detectFileType(std::as_bytes(std::span{bytes}), 5U);

    const auto& candidates = result;
    REQUIRE_FALSE(candidates.empty());
    CHECK(candidates.size() <= 5U);
    CHECK(candidates.front().Name == "Portable Network Graphics");
  }

  SUBCASE("ZIP") {
    constexpr auto bytes = std::to_array<unsigned char>({'P', 'K', 0x05U, 0x06U, 0x00U, 0x00U, 0x00U, 0x00U});

    const auto result = databaseDetector().detectFileType(std::as_bytes(std::span{bytes}), 5U);

    const auto& candidates = result;
    REQUIRE_FALSE(candidates.empty());
    CHECK(candidates.front().Name == "ZIP compressed archive (empty)");
  }

  SUBCASE("RTF") {
    constexpr std::string_view bytes = "{\\rtf";
    const auto result = databaseDetector().detectFileType(std::as_bytes(std::span{bytes}), 5U);

    const auto& candidates = result;
    REQUIRE_FALSE(candidates.empty());
    CHECK(candidates.front().Name == "Rich Text Format");
  }

  SUBCASE("CHM") {
    constexpr auto bytes = std::to_array<unsigned char>({'I', 'T', 'S', 'F', 0x03U, 0x00U, 0x00U, 0x00U});

    const auto result = databaseDetector().detectFileType(std::as_bytes(std::span{bytes}), 5U);

    const auto& candidates = result;
    REQUIRE_FALSE(candidates.empty());
    CHECK(candidates.front().Name == "Windows HELP File");
  }
}

TEST_CASE("case-insensitive patterns match lowercase input") {
  std::array<char, 128U> bytes{};
  bytes[1] = '\x01';
  constexpr std::string_view tableNames = "cmap glyf head loca maxp name post $hmtx 6hhea";
  std::ranges::copy(tableNames, bytes.begin() + 64);

  const auto result = databaseDetector().detectFileType(std::as_bytes(std::span{bytes}), 20U);

  CHECK(containsCandidate(result, "TrueType Font"));
}

TEST_CASE("any-position patterns require every positional pattern to match") {
  std::array<char, 80U> bytes{};
  bytes.fill(' ');
  bytes[8] = '\0';
  constexpr std::string_view tableNames = "CMAP GLYF HEAD HHEA HMTX LOCA MAXP NAME POST";
  std::copy(tableNames.begin(), tableNames.end(), bytes.begin() + 16);

  const auto candidates = databaseDetector().detectFileType(std::as_bytes(std::span{bytes}), 20U);

  const auto matchedTrueType = containsCandidate(candidates, "TrueType Font");
  CHECK_FALSE(matchedTrueType);
}

TEST_CASE("candidate count limits returned results") {
  constexpr auto bytes = std::to_array<unsigned char>(
      {0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n', 0x00U, 0x00U, 0x00U, '\r', 'I', 'H', 'D', 'R', 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U});

  const auto result = databaseDetector().detectFileType(std::as_bytes(std::span{bytes}), 2U);

  const auto& candidates = result;
  REQUIRE(candidates.size() <= 2U);
  CHECK(candidates.front().Probability >= candidates.back().Probability);
}

TEST_CASE("detectFileType samples both ends of oversized files") {
  const auto path = std::filesystem::temp_directory_path() / "TrId2_oversized_file_for_tail_detection.bin";
  std::error_code ec;
  std::filesystem::remove(path, ec);

  {
    std::ofstream file{path, std::ios::binary};
    REQUIRE(file);
    constexpr char firstByte = '\xFF';
    file.write(&firstByte, 1);
    file.seekp(TrId2Lib::Impl::MaxDetectionBytes, std::ios::beg);
    file.write("TAG", 3);
    REQUIRE(file);
  }

  const auto result = databaseDetector().detectFileType(path, 5U);
  std::filesystem::remove(path, ec);

  CHECK(containsCandidate(result, "MP3 audio (ID3 v1.x tag)"));
}

TEST_CASE("test data sample files produce strict candidates") {
  struct ExpectedCandidate final {
    std::string_view name;
    double probability{};
  };

  struct ExpectedSample final {
    std::string_view relativePath;
    std::vector<ExpectedCandidate> candidates;
  };

  const std::vector<ExpectedSample> expectedSamples{
      {"images/background.jpg", {{"JFIF JPEG bitmap", 50.018743}, {"JPEG bitmap", 37.485943}, {"MP3 audio", 12.495314}}},
      {"app-image/icon.png", {{"Portable Network Graphics", 100.0}}},
      {"images/atop.gif", {{"GIF89a bitmap", 66.666667}, {"GIF bitmap (generic)", 33.333333}}},
      {"images/ImageMagick.ico", {{"Windows Icon", 50.006245}, {"Windows Icon (even big)", 49.993755}}},
      {"PerlMagick/t/tiff/input_16_matte.tiff", {{"Tagged Image File Format Bitmap (little endian)", 100.0}}},
      {"PerlMagick/t/input.pcx", {{"PCX bitmap (v3.0)", 100.0}}},
      {"PerlMagick/t/input.tga",
       {{"Truevision TGA/TARGA bitmap (uncompressed, RGB image)", 66.661112}, {"Lotus 123/Symphony worksheet/format/configuration (V1-V2)", 33.338888}}},
      {"PerlMagick/t/input.fits", {{"Flexible Image Transport System bitmap (gen)", 100.0}}},
      {"Magick++/demo/model.miff", {{"ImageMagick Machine independent File Format bitmap", 100.0}}},
      {"PerlMagick/t/input_p3.ppm", {{"Portable PixMap bitmap (ASCII)", 100.0}}},
      {"PerlMagick/t/input_p2.pgm", {{"Portable GrayMap bitmap (ASCII)", 100.0}}},
      {"PerlMagick/t/input_p1.pbm", {{"Portable BitMap (ASCII)", 100.0}}},
      {"PerlMagick/t/ps/input.ps", {{"PostScript document", 66.666667}, {"PostScript document (minimal)", 33.333333}}},
      {"images/bitcoin.svg", {{"Scalable Vector Graphics (var.3)", 74.358974}, {"Generic XML (ASCII)", 25.641026}}},
      {"PerlMagick/t/jng/input_gray_idat.jng", {{"JPEG Network Graphics bitmap", 100.0}}},
      {"PerlMagick/t/fpx/input_256.fpx",
       {{"Kodak FlashPix bitmap", 67.134269}, {"Microsoft PhotoDraw drawing", 29.659319}, {"Generic OLE2 / Multistream Compound", 3.206413}}},
      {"PerlMagick/t/hdf/input_256.hdf", {{"NCSA Hierarchical Data Format", 100.0}}},
      {"PerlMagick/t/input_gray_lsb_08bit.mat",
       {{"Matlab Level 5 MAT-File", 65.494657}, {"Matlab MAT-File (generic)", 24.164081}, {"SMS Material", 10.341262}}},
      {"PerlMagick/t/Generic.ttf", {{"TrueType Font", 82.762187}, {"GEM bitmap (v1)", 10.342688}, {"DEGAS med-res bitmap", 6.895125}}},
      {"config/coder.xml", {{"Generic XML (ASCII)", 100.0}}},
      {"images/logo.eps",
       {{"Adobe Illustrator graphics", 78.207048},
        {"Encapsulated PostScript", 15.089491},
        {"PostScript document", 4.468974},
        {"PostScript document (minimal)", 2.234487}}},
      {"PerlMagick/t/input.xpm", {{"X PixMap bitmap (rem)", 50.877193}, {"X PixMap bitmap", 49.122807}}},
      {"PerlMagick/t/input.xbm", {{"X Bitmap", 100.0}}},
      {"PerlMagick/t/openjp2/input.jp2", {{"JPEG-2000 Code Stream bitmap", 80.0}, {"MP3 audio", 20.0}}},
      {"PerlMagick/t/bzlib/input.miff.bz2", {{"bzip2 compressed archive", 100.0}}},
      {"PerlMagick/t/zlib/input.miff.gz", {{"GZipped data", 100.0}}},
      {"config/cmyk.icm", {{"ICC Color profile (generic)", 100.0}}},
      {"PerlMagick/t/wmf/clock.wmf", {{"Windows Metafile", 50.02914}, {"Aldus Placeable Metafile", 49.97086}}},
      {"PerlMagick/t/x11/congrats.fig", {{"FIG vector drawing", 100.0}}},
      {"PerlMagick/t/input_p7.p7", {{"Xv's Visual Schnauzer bitmap", 100.0}}},
      {"images/rose.pnm", {{"Portable PixMap bitmap (binary)", 100.0}}},
      {"PerlMagick/t/input.sgi", {{"Silicon Graphics RGB bitmap", 74.93453}, {"Silicon Graphics bitmap", 25.06547}}},
      {"PerlMagick/t/input.rle", {{"Utah Raster Toolkit bitmap", 100.0}}},
      {"PerlMagick/t/input.viff", {{"Khoros Visualization Image File Format bitmap (v1.0)", 99.05894}, {"Sybase iAnywhere database files", 0.94106}}},
      {"PerlMagick/t/input.wpg", {{"WordPerfect Graphics bitmap", 44.456787}, {"WordPerfect (generic)", 44.43457}, {"MP3 audio", 11.108643}}},
      {"PerlMagick/t/input.tim", {{"PSX TIM 4bpp bitmap", 88.879013}, {"LTAC compressed audio (v1.61)", 11.120987}}},
      {"PerlMagick/t/input.dcx", {{"Graphics Multipage PCX bitmap", 98.814229}, {"VXD Driver", 0.716403}, {"Sybase iAnywhere database files", 0.469368}}},
      {"PerlMagick/t/jbig/input.jbig", {{"JBIG raster bitmap", 100.0}}},
      {"PerlMagick/t/png/input.mng", {{"Multiple-image Network Graphics bitmap/anim", 100.0}}},
      {"PerlMagick/t/hpgl/input.hpgl", {{"Hewlett-Packard Graphics Language", 100.0}}},
  };

  for (const auto& [relativePath, expectedCandidates] : expectedSamples) {
    const auto path = std::filesystem::path{TEST_DATA_FOLDER} / std::string{relativePath};
    INFO("test data sample: " << path.string());

    const auto result = databaseDetector().detectFileType(path, 5U);
    const auto& candidates = result;
    REQUIRE(expectedCandidates.size() == candidates.size());
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
      CAPTURE(index);
      const auto& actualCandidate = candidates[index];
      const auto& expectedCandidate = expectedCandidates[index];
      CHECK(actualCandidate.Name == expectedCandidate.name);
      CAPTURE(actualCandidate.Probability);
      CAPTURE(expectedCandidate.probability);
      CHECK(std::abs(actualCandidate.Probability - expectedCandidate.probability) <= 0.005);
    }
  }
}
