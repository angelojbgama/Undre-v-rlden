from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from ..model.types import Diagnostic, JsonValue
from .json_io import DuplicateKeyError, decode_json, load_json, write_atomic

MAP_FORMAT = "dungeon-underworld-map-source"
MAP_VERSION = 4


@dataclass(slots=True)
class MapDecode:
    data: dict[str, JsonValue] | None
    diagnostics: list[Diagnostic]


def decode_map(value: object, source_path: Path | None = None) -> MapDecode:
    diagnostics: list[Diagnostic] = []
    if not isinstance(value, dict):
        return MapDecode(None, [Diagnostic("error", "authored map root must be an object", source_path=source_path, code="root_not_object")])
    if value.get("format") != MAP_FORMAT:
        diagnostics.append(Diagnostic("error", "invalid authored map format", "format", "invalid_format", source_path=source_path))
    version = value.get("version")
    if not isinstance(version, int) or isinstance(version, bool) or version < 1 or version > MAP_VERSION:
        diagnostics.append(Diagnostic("error", f"unsupported authored map version: {version!r}", "version", "invalid_version", source_path=source_path))
    # scenes and placementOverrides were introduced after the first UMAP
    # schema.  They remain optional so older authored maps can be opened and
    # saved without inventing data that was not present in the source.
    for name in ("id", "width", "height", "tileSize", "tileReferences", "layers", "collision", "playerSpawns", "enemies", "npcs", "objects", "pickups", "links", "regions", "worldRules", "encounters"):
        if name not in value:
            diagnostics.append(Diagnostic("error", f"missing required map field: {name}", name, "missing_field", source_path=source_path))
    version = value.get("version")
    if isinstance(version, int) and version < 4 and "scenes" in value:
        diagnostics.append(Diagnostic("error", "scenes require map schema version 4", "scenes", "unsupported_version", source_path=source_path))
    bindings = value.get("collisionBindings")
    if bindings is not None:
        if not isinstance(bindings, list):
            diagnostics.append(Diagnostic("error", "collisionBindings must be an array", "collisionBindings", "wrong_type", source_path=source_path))
        else:
            required = ("layer", "x", "y", "tilesetId", "sourceIndex", "flags")
            for index, binding in enumerate(bindings):
                path = f"collisionBindings[{index}]"
                if not isinstance(binding, dict):
                    diagnostics.append(Diagnostic("error", "collision binding must be an object", path, "wrong_type", source_path=source_path))
                    continue
                for name in required:
                    if name not in binding:
                        diagnostics.append(Diagnostic("error", f"missing collision binding field: {name}", f"{path}.{name}", "missing_field", source_path=source_path))
                for name in ("layer", "x", "y", "sourceIndex", "flags"):
                    number = binding.get(name)
                    if not isinstance(number, int) or isinstance(number, bool) or number < 0:
                        diagnostics.append(Diagnostic("error", "collision binding numeric field is invalid", f"{path}.{name}", "wrong_type", source_path=source_path))
                if not isinstance(binding.get("tilesetId"), str) or not binding.get("tilesetId"):
                    diagnostics.append(Diagnostic("error", "collision binding tilesetId is invalid", f"{path}.tilesetId", "wrong_type", source_path=source_path))
    return MapDecode(value, diagnostics)


def load_map(path: Path) -> MapDecode:
    try:
        return decode_map(load_json(path), path)
    except (OSError, UnicodeError, ValueError, DuplicateKeyError) as error:
        return MapDecode(None, [Diagnostic("error", str(error), source_path=path, code="json_decode")])


def decode_map_text(text: str) -> MapDecode:
    try:
        return decode_map(decode_json(text))
    except (ValueError, DuplicateKeyError) as error:
        return MapDecode(None, [Diagnostic("error", str(error), code="json_decode")])


def write_map(path: Path, data: dict[str, JsonValue]) -> None:
    write_atomic(path, data)


def new_map(map_id: str, width: int, height: int, tile_size: int = 16, include_player_spawn: bool = False) -> dict[str, JsonValue]:
    if not map_id or width <= 0 or height <= 0 or tile_size <= 0:
        raise ValueError("map id, width, height and tile size must be positive")
    cells = width * height
    return {
        "format": MAP_FORMAT,
        "version": MAP_VERSION,
        "id": map_id,
        "width": width,
        "height": height,
        "tileSize": tile_size,
        "tileReferences": [],
        "layers": [{"name": "Ground", "visible": True, "cells": [None] * cells}],
        "collision": [0] * cells,
        "collisionBindings": [],
        "playerSpawns": ([{"id": "player.start", "position": {"x": tile_size * 2, "y": tile_size * 2}, "facing": "down"}] if include_player_spawn else []),
        "enemies": [],
        "npcs": [],
        "objects": [],
        "pickups": [],
        "links": [],
        "regions": [],
        "worldRules": [],
        "encounters": [],
        "scenes": [],
        "placementOverrides": [],
    }
