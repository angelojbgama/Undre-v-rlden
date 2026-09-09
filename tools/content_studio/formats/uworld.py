from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from ..model.types import Diagnostic, JsonValue
from .json_io import DuplicateKeyError, load_json, write_atomic
from .umap import decode_map

WORLD_FORMAT = "dungeon-underworld-world-project"
WORLD_VERSION = 1


@dataclass(slots=True)
class WorldDecode:
    data: dict[str, JsonValue] | None
    diagnostics: list[Diagnostic]


def decode_world(value: object, source_path: Path | None = None) -> WorldDecode:
    if not isinstance(value, dict):
        return WorldDecode(None, [Diagnostic("error", "world project root must be an object", source_path=source_path, code="root_not_object")])
    diagnostics: list[Diagnostic] = []
    if value.get("format") != WORLD_FORMAT:
        diagnostics.append(Diagnostic("error", "invalid world project format", "format", "invalid_format", source_path=source_path))
    if value.get("version") != WORLD_VERSION:
        diagnostics.append(Diagnostic("error", f"unsupported world project version: {value.get('version')!r}", "version", "invalid_version", source_path=source_path))
    if not isinstance(value.get("entryMapId"), str) or not value.get("entryMapId"):
        diagnostics.append(Diagnostic("error", "entryMapId must be a non-empty string", "entryMapId", "invalid_entry_map", source_path=source_path))
    maps = value.get("maps")
    if not isinstance(maps, list):
        diagnostics.append(Diagnostic("error", "maps must be an array", "maps", "invalid_maps", source_path=source_path))
    else:
        for index, map_value in enumerate(maps):
            decoded = decode_map(map_value, source_path)
            diagnostics.extend(Diagnostic(issue.severity, issue.message, f"maps[{index}].{issue.path}" if issue.path else f"maps[{index}]", issue.code, issue.definition_id, issue.source_path) for issue in decoded.diagnostics)
    return WorldDecode(value, diagnostics)


def load_world(path: Path) -> WorldDecode:
    try:
        return decode_world(load_json(path), path)
    except (OSError, UnicodeError, ValueError, DuplicateKeyError) as error:
        return WorldDecode(None, [Diagnostic("error", str(error), source_path=path, code="json_decode")])


def write_world(path: Path, data: dict[str, JsonValue]) -> None:
    write_atomic(path, data)


def new_world(map_data: dict[str, JsonValue]) -> dict[str, JsonValue]:
    map_id = map_data.get("id")
    if not isinstance(map_id, str) or not map_id:
        raise ValueError("a world project needs a valid initial map")
    return {"format": WORLD_FORMAT, "version": WORLD_VERSION, "entryMapId": map_id, "maps": [map_data]}

