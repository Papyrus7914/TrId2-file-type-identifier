#pragma once

#include "TrId2Database.h"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace TrId2Lib {

struct FileType final {
  std::string_view Name;
  std::string_view Description;
  std::string_view Url;
  std::span<const std::string_view> FileNameExtensions;
  std::string_view MimeType;
  double Probability{};
};

namespace Impl {

inline constexpr std::size_t MaxDetectionBytes = 10U * 1024U * 1024U;

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

  const auto readBytes = [&file, &filePath](std::vector<char>& bytes, const std::uintmax_t offset, const std::size_t bytesToRead) {
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
  std::string result;
  result.resize(value.size());
  std::ranges::transform(value, result.begin(), [](const char byte) { return static_cast<char>(asciiToLower(byte)); });
  return result;
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

[[nodiscard]] inline std::vector<FileType> detectFileTypeImpl(const std::string_view fileBytes, const std::size_t candidateCount) {
  if (candidateCount == 0U) {
    return std::vector<FileType>{};
  }

  const auto detectionSize = std::min(fileBytes.size(), MaxDetectionBytes);
  const auto data = fileBytes.substr(0U, detectionSize);
  const MatchContext matchContext{data};

  std::vector<ScoredFileType> candidates;
  std::uint64_t totalPoints = 0U;

  for (const auto* description : fileTypeDescriptions) {
    std::uint64_t points = 0U;
    bool allPatternsMatched = true;

    for (const auto& [pos, bytes, caseSensitive] : description->PositionalPatterns) {
      if (!matchesAt(matchContext.data(), bytes, pos, caseSensitive)) {
        allPatternsMatched = false;
        break;
      }
      points += pos == 0 ? bytes.size() * 1000 : bytes.size();
    }

    if (allPatternsMatched) {
      for (const auto& [bytes, caseSensitive] : description->AnyPositionPatterns) {
        if (!matchContext.contains(bytes, caseSensitive)) {
          allPatternsMatched = false;
          break;
        }
        points += bytes.size() * 500U;
      }
    }

    if (allPatternsMatched && points > 0U) {
      candidates.push_back({.description = description, .points = points});
      totalPoints += points;
    }
  }

  if (candidates.empty()) {
    return {};
  }

  std::ranges::stable_sort(candidates, [](const auto& lhs, const auto& rhs) { return lhs.points > rhs.points; });

  std::vector<FileType> results;
  results.reserve(std::min(candidateCount, candidates.size()));
  for (const auto resultCount = std::min(candidateCount, candidates.size()); const auto& [description, points] : candidates | std::views::take(resultCount)) {
    const auto probability = totalPoints == 0U ? 0.0 : static_cast<double>(points) * 100.0 / static_cast<double>(totalPoints);
    results.push_back(FileType{
        .Name = description->Name,
        .Description = description->Description,
        .Url = description->Url,
        .FileNameExtensions = description->FileNameExtensions,
        .MimeType = description->MimeType,
        .Probability = probability,
    });
  }
  return results;
}

}

[[nodiscard]] inline std::vector<FileType> detectFileType(const std::filesystem::path& filePath, const std::size_t maxNumberOfCandidates) {
  const auto fileBytes = Impl::readFromFile(filePath, Impl::MaxDetectionBytes);
  return Impl::detectFileTypeImpl(std::string_view{fileBytes.data(), fileBytes.size()}, maxNumberOfCandidates);
}

}
