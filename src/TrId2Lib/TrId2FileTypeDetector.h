#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <ranges>
#include <simdjson.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace TrId2Lib {

struct FileType final {
  std::string Name;
  std::string Description;
  std::string Url;
  std::vector<std::string> FileNameExtensions;
  std::string MimeType;
  double Probability{};
};

namespace Impl {

inline constexpr std::size_t MaxDetectionBytes = 10U * 1024U * 1024U;

struct PositionalPattern final {
  std::size_t pos{};
  std::string bytes;
  bool caseSensitive{};
};

struct AnyPositionPattern final {
  std::string bytes;
  bool caseSensitive{};
};

struct FileTypeDescription final {
  FileType type;
  std::vector<PositionalPattern> PositionalPatterns;
  std::vector<AnyPositionPattern> AnyPositionPatterns;
};

[[nodiscard]] inline std::vector<char> readFromFile(const std::filesystem::path& filePath, const std::size_t maxBytesToRead) {
  errno = 0;
  std::ifstream file{filePath, std::ios::binary};
  if (!file) {
    throw std::runtime_error{std::format("Unable to open file: '{}'. Error message: {}", filePath.string(),
                                         std::error_code{errno != 0 ? errno : ENOENT, std::generic_category()}.message())};
  }

  std::error_code ec;
  const auto fileSize = std::filesystem::file_size(filePath, ec);
  if (ec) {
    throw std::runtime_error{std::format("Unable to read file size: '{}'. Error message: {}", filePath.string(), ec.message())};
  }

  const auto readBytes = [&](std::vector<char>& bytes, const std::uintmax_t offset, const std::size_t bytesToRead) {
    if (bytesToRead == 0U) {
      return;
    }

    file.clear();
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
      throw std::runtime_error{std::format("Error seeking file: '{}'.", filePath.string())};
    }

    const auto bytesOffset = bytes.size();
    bytes.resize(bytesOffset + bytesToRead);
    file.read(bytes.data() + bytesOffset, static_cast<std::streamsize>(bytesToRead));
    if (file.bad()) {
      throw std::runtime_error{std::format("Error reading file: '{}'.", filePath.string())};
    }
    bytes.resize(bytesOffset + static_cast<std::size_t>(file.gcount()));
  };

  std::vector<char> bytes;
  if (fileSize <= maxBytesToRead) {
    readBytes(bytes, 0U, static_cast<std::size_t>(fileSize));
  } else {
    const auto chunkBytesToRead = maxBytesToRead / 2U;
    bytes.reserve(chunkBytesToRead * 2U);
    readBytes(bytes, 0U, chunkBytesToRead);
    readBytes(bytes, fileSize - chunkBytesToRead, chunkBytesToRead);
  }

  return bytes;
}

struct ScoredFileType final {
  const FileTypeDescription* description{};
  std::uint64_t points{};
};

[[nodiscard]] inline unsigned char asciiToLower(const char value) noexcept {
  const auto byte = static_cast<unsigned char>(value);
  if (byte >= static_cast<unsigned char>('A') && byte <= static_cast<unsigned char>('Z')) {
    return static_cast<unsigned char>(byte + (static_cast<unsigned char>('a') - static_cast<unsigned char>('A')));
  }
  return byte;
}

[[nodiscard]] inline bool matchesAt(const std::string_view data, const std::string_view patternBytes, const std::size_t pos, const bool caseSensitive) {
  if (patternBytes.empty() || pos > data.size()) {
    return false;
  }

  const auto prefix = data.substr(pos, patternBytes.size());

  if (caseSensitive) {
    return prefix == patternBytes;
  }

  return std::ranges::equal(prefix, patternBytes, [](const char dataByte, const char lowerPatternByte) noexcept {
    return asciiToLower(dataByte) == static_cast<unsigned char>(lowerPatternByte);
  });
}

[[nodiscard]] inline std::string asciiToLower(const std::string_view value) {
  return value | std::views::transform([](const char byte) { return static_cast<char>(asciiToLower(byte)); }) | std::ranges::to<std::string>();
}

// Decode JSON byte array directly into a binary string.
[[nodiscard]] inline std::string patternBytes(simdjson::ondemand::array values) {
  std::string bytes;

  for (auto value : values) {
    bytes.push_back(static_cast<char>(value.get_uint8().value()));
  }

  return bytes;
}

// Parse each pattern in one forward-only pass.
inline void parsePattern(simdjson::ondemand::object pattern, FileTypeDescription& description) {
  std::size_t pos = 0U;
  std::string bytes;
  bool caseSensitive = true;
  bool anyPosition = false;
  bool hasPos = false;
  bool hasBytes = false;

  for (auto field : pattern) {
    const auto key = field.key().value();

    if (key == "Pos") {
      hasPos = true;

      auto value = field.value();

      if (value.type().value() == simdjson::ondemand::json_type::string) {
        if (value.get_string().value() != "any") {
          throw std::runtime_error{"Invalid detection pattern position."};
        }

        anyPosition = true;
      } else {
        const auto position = value.get_uint64().value();

        if (position > std::numeric_limits<std::size_t>::max()) {
          throw std::runtime_error{"Detection pattern position is out of range."};
        }

        pos = static_cast<std::size_t>(position);
        anyPosition = false;
      }
    } else if (key == "Bytes") {
      hasBytes = true;
      bytes = patternBytes(field.value().get_array());
    } else if (key == "CaseSensitive") {
      caseSensitive = field.value().get_bool().value();
    }
  }

  if (!hasPos || !hasBytes) {
    throw std::runtime_error{"Detection pattern is missing Pos or Bytes."};
  }

  if (!caseSensitive) {
    bytes = asciiToLower(bytes);
  }

  if (anyPosition) {
    description.AnyPositionPatterns.push_back({.bytes = std::move(bytes), .caseSensitive = caseSensitive});
  } else {
    description.PositionalPatterns.push_back({.pos = pos, .bytes = std::move(bytes), .caseSensitive = caseSensitive});
  }
}

// Parse each file type using one pass over its JSON object.
[[nodiscard]] inline FileTypeDescription parseDescription(simdjson::ondemand::object entry) {
  FileTypeDescription description;

  bool hasName = false;
  bool hasDetectionPatterns = false;

  for (auto field : entry) {
    const auto key = field.key().value();

    if (key == "Name") {
      description.type.Name.assign(field.value().get_string().value());
      hasName = true;
    } else if (key == "Description") {
      description.type.Description.assign(field.value().get_string().value());
    } else if (key == "URL") {
      description.type.Url.assign(field.value().get_string().value());
    } else if (key == "FileNameExtensions") {
      simdjson::ondemand::array extensions = field.value().get_array();

      for (auto extension : extensions) {
        description.type.FileNameExtensions.emplace_back(extension.get_string().value());
      }
    } else if (key == "MimeType") {
      description.type.MimeType.assign(field.value().get_string().value());
    } else if (key == "DetectionPatterns") {
      hasDetectionPatterns = true;

      simdjson::ondemand::array patterns = field.value().get_array();

      for (auto pattern : patterns) {
        parsePattern(pattern.get_object(), description);
      }
    }
  }

  if (!hasName || !hasDetectionPatterns) {
    throw std::runtime_error{"File type description is missing required fields."};
  }

  return description;
}

// Load directly into simdjson's padded buffer.
// No intermediate DOM or separate JSON deserialization.
[[nodiscard]] inline std::vector<FileTypeDescription> loadDatabase(const std::filesystem::path& databasePath) {
  simdjson::padded_string json;

  const auto error = simdjson::padded_string::load(databasePath.native()).get(json);

  if (error) {
    throw std::runtime_error{
        std::format("Unable to open the database JSON file: '{}'. Error message: {}", databasePath.string(), simdjson::error_message(error))};
  }

  simdjson::ondemand::parser parser;
  simdjson::ondemand::document document = parser.iterate(json);

  std::vector<FileTypeDescription> descriptions;

  simdjson::ondemand::array entries = document.get_array();

  for (auto entry : entries) {
    descriptions.push_back(parseDescription(entry.get_object()));
  }

  return descriptions;
}

class MatchContext final {
public:
  explicit MatchContext(const std::string_view data) : data_{data} {}

  [[nodiscard]] std::string_view data() const noexcept { return data_; }

  [[nodiscard]] bool contains(const std::string_view patternBytes, const bool caseSensitive) const {
    if (patternBytes.empty() || patternBytes.size() > data_.size()) {
      return false;
    }

    if (caseSensitive) {
      return data_.contains(patternBytes);
    }

    return loweredData().contains(patternBytes);
  }

private:
  [[nodiscard]] std::string_view loweredData() const {
    if (!loweredData_) {
      loweredData_ = asciiToLower(data_);
    }

    return *loweredData_;
  }

  std::string_view data_;
  mutable std::optional<std::string> loweredData_;
};

[[nodiscard]] inline std::vector<FileType> detectFileTypeImpl(const std::string_view fileBytes, const std::size_t candidateCount,
                                                              const std::span<const FileTypeDescription> fileTypeDescriptions) {
  if (candidateCount == 0U) {
    return {};
  }

  const auto detectionSize = std::min(fileBytes.size(), MaxDetectionBytes);

  const auto data = fileBytes.substr(0U, detectionSize);
  const MatchContext matchContext{data};

  std::vector<ScoredFileType> candidates;
  std::uint64_t totalPoints = 0U;

  for (const auto& description : fileTypeDescriptions) {
    std::uint64_t points = 0U;
    const auto allPatternsMatched = std::ranges::all_of(description.PositionalPatterns,
                                                        [&](const auto& pattern) {
                                                          const auto& [pos, bytes, caseSensitive] = pattern;
                                                          if (!matchesAt(matchContext.data(), bytes, pos, caseSensitive)) {
                                                            return false;
                                                          }

                                                          points += pos == 0 ? bytes.size() * 1000 : bytes.size();
                                                          return true;
                                                        }) &&
                                    std::ranges::all_of(description.AnyPositionPatterns, [&](const auto& pattern) {
                                      const auto& [bytes, caseSensitive] = pattern;
                                      if (!matchContext.contains(bytes, caseSensitive)) {
                                        return false;
                                      }

                                      points += bytes.size() * 500U;
                                      return true;
                                    });

    if (allPatternsMatched && points > 0U) {
      candidates.push_back({.description = &description, .points = points});
      totalPoints += points;
    }
  }

  if (candidates.empty()) {
    return {};
  }

  std::ranges::stable_sort(candidates, [](const auto& lhs, const auto& rhs) { return lhs.points > rhs.points; });

  return candidates | std::views::take(std::min(candidateCount, candidates.size())) | std::views::transform([&](const auto& candidate) {
           auto result = candidate.description->type;
           result.Probability = totalPoints == 0U ? 0.0 : static_cast<double>(candidate.points) * 100.0 / static_cast<double>(totalPoints);
           return result;
         }) |
         std::ranges::to<std::vector<FileType>>();
}

} // namespace Impl

class FileTypeDetector final {
public:
  explicit FileTypeDetector(const std::filesystem::path& databasePath) : descriptions_{Impl::loadDatabase(databasePath)} {}

  [[nodiscard]] std::vector<FileType> detectFileType(const std::filesystem::path& filePath, const std::size_t maxNumberOfCandidates) const {
    const auto fileBytes = Impl::readFromFile(filePath, Impl::MaxDetectionBytes);

    return detectFileType(std::as_bytes(std::span{fileBytes}), maxNumberOfCandidates);
  }

  [[nodiscard]] std::vector<FileType> detectFileType(const std::span<const std::byte> fileBytes, const std::size_t maxNumberOfCandidates) const {
    return Impl::detectFileTypeImpl(std::string_view{reinterpret_cast<const char*>(fileBytes.data()), fileBytes.size()}, maxNumberOfCandidates, descriptions_);
  }

private:
  std::vector<Impl::FileTypeDescription> descriptions_;
};

} // namespace TrId2Lib
