#!/usr/bin/env python3
from __future__ import annotations
import json
import sys
import yaml
from pathlib import Path
from typing import Any


def load_yaml(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        data = yaml.load(stream, Loader=getattr(yaml, "CSafeLoader", yaml.SafeLoader))

    if not isinstance(data, dict):
        raise ValueError(f"{path}: expected a YAML mapping at the document root")

    for pattern in data.get("DetectionPatterns", []):
        if "AsciiString" in pattern:
            pattern["Bytes"] = list(pattern.pop("AsciiString").encode("latin-1"))

    return data


database_dir = Path(__file__).resolve().parent.parent / "database"
output_path = Path(sys.argv[1])

paths = sorted(
    [
        path
        for path in database_dir.iterdir()
        if path.is_file() and path.suffix.lower() == ".yaml"
    ],
    key=lambda path: (path.name.casefold(), path.name),
)

entries = [load_yaml(path) for path in paths]

output_path.parent.mkdir(parents=True, exist_ok=True)
with output_path.open("w", encoding="utf-8", newline="\n") as stream:
    json.dump(entries, stream, ensure_ascii=False, separators=(",", ":"))

print(f"Generated {output_path} from {len(entries)} YAML file(s).")
