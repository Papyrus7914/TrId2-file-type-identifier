#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

import yaml


@dataclass(frozen=True)
class DetectionPattern:
    pos: int | None
    bytes_value: bytes
    case_sensitive: bool


@dataclass(frozen=True)
class FileTypeEntry:
    identifier: str
    name: str
    description: str
    url: str
    file_name_extensions: list[str]
    mime_type: str
    positional_patterns: list[DetectionPattern]
    any_position_patterns: list[DetectionPattern]
    source_path: Path


def iter_yaml_files(database_dir: Path) -> list[Path]:
    return sorted(
        [path for path in database_dir.iterdir() if path.is_file() and path.suffix.lower() == ".yaml"],
        key=lambda path: (path.name.casefold(), path.name),
    )


def identifier_for_file(path: Path) -> str:
    return "m" + hashlib.md5(path.read_bytes()).hexdigest()


def load_yaml(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    data = yaml.load(text, Loader=yaml.CLoader)

    if not isinstance(data, dict):
        raise ValueError(f"{path}: expected a YAML mapping at the document root")

    return data


def require_string(data: dict[str, Any], key: str, path: Path) -> str:
    value = data.get(key)
    if not isinstance(value, str):
        raise ValueError(f"{path}: expected {key} to be a string")
    return value


def optional_string(data: dict[str, Any], key: str, path: Path) -> str:
    value = data.get(key, "")
    if value == "":
        return ""
    if not isinstance(value, str):
        raise ValueError(f"{path}: expected {key} to be a string")
    return value


def parse_extensions(data: dict[str, Any], path: Path) -> list[str]:
    value = data.get("FileNameExtensions")
    if value is None:
        return []
    if not isinstance(value, list):
        raise ValueError(f"{path}: expected FileNameExtensions to be a list")
    if not all(isinstance(item, str) for item in value):
        raise ValueError(f"{path}: expected FileNameExtensions to be a list of strings")
    return value


def parse_pos(value: Any, path: Path) -> int | None:
    if value == "any":
        return None

    if isinstance(value, int) and not isinstance(value, bool):
        pos = value
    elif isinstance(value, str):
        try:
            pos = int(value, 10)
        except ValueError as exc:
            raise ValueError(f"{path}: invalid DetectionPatterns Pos value: {value!r}") from exc
    else:
        raise ValueError(f"{path}: expected DetectionPatterns Pos to be an integer or 'any'")

    if pos < 0:
        raise ValueError(f"{path}: DetectionPatterns Pos must be non-negative")

    return pos


def parse_bool(value: Any, path: Path) -> bool:
    if isinstance(value, bool):
        return value
    if not isinstance(value, str):
        raise ValueError(f"{path}: expected CaseSensitive to be true or false")

    lowered = value.strip().lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False

    raise ValueError(f"{path}: expected CaseSensitive to be true or false")


def parse_byte_literal(value: Any, path: Path) -> int:
    if isinstance(value, int) and not isinstance(value, bool):
        byte_value = value
    elif isinstance(value, str):
        try:
            byte_value = int(value, 16) if value.lower().startswith("0x") else int(value, 10)
        except ValueError as exc:
            raise ValueError(f"{path}: invalid byte literal: {value!r}") from exc
    else:
        raise ValueError(f"{path}: expected Bytes entries to be integers")

    if not 0 <= byte_value <= 0xFF:
        raise ValueError(f"{path}: byte value is outside 0x00..0xFF: {value!r}")

    return byte_value


def bytes_from_ascii_string(value: Any, path: Path) -> bytes:
    if not isinstance(value, str):
        raise ValueError(f"{path}: expected AsciiString to be a string")

    byte_values: list[int] = []
    for char in value:
        code_point = ord(char)
        if code_point > 0xFF:
            raise ValueError(f"{path}: AsciiString contains a non-byte character U+{code_point:04X}")
        byte_values.append(code_point)

    return bytes(byte_values)


def ascii_lower_byte(value: int) -> int:
    if ord("A") <= value <= ord("Z"):
        return value + (ord("a") - ord("A"))
    return value


def ascii_lower_bytes(value: bytes) -> bytes:
    return bytes(ascii_lower_byte(byte) for byte in value)


def parse_patterns(data: dict[str, Any], path: Path) -> list[DetectionPattern]:
    value = data.get("DetectionPatterns")
    if not isinstance(value, list):
        raise ValueError(f"{path}: expected DetectionPatterns to be a list")

    patterns: list[DetectionPattern] = []
    for index, item in enumerate(value):
        if not isinstance(item, dict):
            raise ValueError(f"{path}: expected DetectionPatterns[{index}] to be a mapping")

        pos = parse_pos(item.get("Pos"), path)
        has_ascii_string = "AsciiString" in item
        has_bytes = "Bytes" in item

        if has_ascii_string == has_bytes:
            raise ValueError(f"{path}: DetectionPatterns[{index}] must contain exactly one of AsciiString or Bytes")

        if has_ascii_string:
            bytes_value = bytes_from_ascii_string(item["AsciiString"], path)
        else:
            bytes_node = item["Bytes"]
            if not isinstance(bytes_node, list):
                raise ValueError(f"{path}: expected Bytes to be a list")
            bytes_value = bytes(parse_byte_literal(byte, path) for byte in bytes_node)

        case_sensitive = parse_bool(item.get("CaseSensitive", "true"), path)
        if not case_sensitive:
            bytes_value = ascii_lower_bytes(bytes_value)
        patterns.append(
            DetectionPattern(
                pos=pos,
                bytes_value=bytes_value,
                case_sensitive=case_sensitive,
            )
        )

    return patterns


def load_entries(paths: Sequence[Path]) -> list[FileTypeEntry]:
    identifiers = {path: identifier_for_file(path) for path in paths}
    if len(set(identifiers.values())) != len(identifiers):
        raise ValueError("generated C++ identifiers are not unique")

    entries: list[FileTypeEntry] = []

    for path in paths:
        data = load_yaml(path)
        detection_patterns = parse_patterns(data, path)
        positional_patterns = [pattern for pattern in detection_patterns if pattern.pos is not None]
        if not positional_patterns:
            raise ValueError(f"{path}: expected at least one positional DetectionPatterns entry")

        entries.append(
            FileTypeEntry(
                identifier=identifiers[path],
                name=require_string(data, "Name", path),
                description=optional_string(data, "Description", path),
                url=optional_string(data, "URL", path),
                file_name_extensions=parse_extensions(data, path),
                mime_type=optional_string(data, "MimeType", path),
                positional_patterns=positional_patterns,
                any_position_patterns=[pattern for pattern in detection_patterns if pattern.pos is None],
                source_path=path,
            )
        )

    return entries


def cpp_text_literal(value: str) -> str:
    return f'R"TRID2({value})TRID2"sv'


def cpp_detection_literal(value: bytes) -> str:
    return '"' + "".join(f"\\x{byte_value:02X}" for byte_value in value) + '"sv'


def write_positional_pattern(stream: Any, pattern: DetectionPattern) -> None:
    if pattern.pos is None:
        raise ValueError(f"{pattern}: expected a positional pattern")

    case_sensitive = "true" if pattern.case_sensitive else "false"

    stream.write(f"""  {{
    .pos = {pattern.pos},
    .bytes = {cpp_detection_literal(pattern.bytes_value)},
    .caseSensitive = {case_sensitive},
  }},
""")


def write_any_position_pattern(stream: Any, pattern: DetectionPattern) -> None:
    if pattern.pos is not None:
        raise ValueError(f"{pattern}: expected an any-position pattern")

    case_sensitive = "true" if pattern.case_sensitive else "false"

    stream.write(f"""  {{
    .bytes = {cpp_detection_literal(pattern.bytes_value)},
    .caseSensitive = {case_sensitive},
  }},
""")


def write_entry(stream: Any, entry: FileTypeEntry) -> None:
    if entry.file_name_extensions:
        extensions = " ".join(f"{cpp_text_literal(extension)}," for extension in entry.file_name_extensions)
        stream.write(f"inline constexpr auto {entry.identifier}_FileNameExtensions = std::to_array<std::string_view>({{ {extensions} }});\n")

    if entry.positional_patterns:
        stream.write(f"inline constexpr auto {entry.identifier}_PositionalPatterns = std::to_array<FileTypeDescription::PositionalPattern>({{\n")
        for pattern in entry.positional_patterns:
            write_positional_pattern(stream, pattern)
        stream.write("});\n")

    if entry.any_position_patterns:
        stream.write(f"inline constexpr auto {entry.identifier}_AnyPositionPatterns = std::to_array<FileTypeDescription::PatternAtAnyPosition>({{\n")
        for pattern in entry.any_position_patterns:
            write_any_position_pattern(stream, pattern)
        stream.write("});\n")

    stream.write(f"""inline constexpr FileTypeDescription {entry.identifier} = {{
  .Name = {cpp_text_literal(entry.name)},
""")
    stream.write(f"  .Description = {cpp_text_literal(entry.description)},\n")
    stream.write(f"  .Url = {cpp_text_literal(entry.url)},\n")
    if entry.file_name_extensions:
        stream.write(f"  .FileNameExtensions = {entry.identifier}_FileNameExtensions,\n")
    else:
        stream.write("  .FileNameExtensions = {},\n")
    stream.write(f"  .MimeType = {cpp_text_literal(entry.mime_type)},\n")
    if entry.positional_patterns:
        stream.write(f"  .PositionalPatterns = {entry.identifier}_PositionalPatterns,\n")
    else:
        stream.write("  .PositionalPatterns = {},\n")
    if entry.any_position_patterns:
        stream.write(f"  .AnyPositionPatterns = {entry.identifier}_AnyPositionPatterns,\n")
    else:
        stream.write("  .AnyPositionPatterns = {},\n")
    stream.write("};\n\n")


def write_header(entries: Sequence[FileTypeEntry], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = output_path.with_name(f"{output_path.name}.tmp")

    with temp_path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("""#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace TrId2Lib::Impl {
using namespace std::literals::string_view_literals;

struct FileTypeDescription final {
  struct PositionalPattern {
    std::size_t pos;
    std::string_view bytes;
    bool caseSensitive;
  };

  struct PatternAtAnyPosition {
    std::string_view bytes;
    bool caseSensitive;
  };

  std::string_view Name;
  std::string_view Description;
  std::string_view Url;
  std::span<const std::string_view> FileNameExtensions;
  std::string_view MimeType;
  std::span<const PositionalPattern> PositionalPatterns;
  std::span<const PatternAtAnyPosition> AnyPositionPatterns;
};

""")

        for entry in entries:
            write_entry(stream, entry)

        stream.write("inline constexpr auto fileTypeDescriptions = std::to_array({\n")
        for entry in entries:
            stream.write(f"  &{entry.identifier},\n")
        stream.write("});\n\n}\n")

    temp_path.replace(output_path)


def main() -> int:
    database_dir = Path(__file__).resolve().parent / "../database"
    output_path = Path(sys.argv[1])
    output_path.parent.mkdir(parents=True, exist_ok=True)

    if not database_dir.is_dir():
        print(f"Database folder does not exist: {database_dir}", file=sys.stderr)
        return 2

    try:
        paths = iter_yaml_files(database_dir)
        if not paths:
            print(f"No YAML files found in {database_dir}", file=sys.stderr)
            return 2

        entries = load_entries(paths)
        write_header(entries, output_path)
    except Exception as exc:
        print(f"Generation failed: {exc}", file=sys.stderr)
        return 1

    print(f"Generated {output_path} from {len(entries)} YAML file(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
