"""Authoring service for per-tile binary collision masks.

Tile collision belongs to the tileset definition, not to map cells and not to
sprite/animation masks. One collision cell represents one logical pixel of the
tile.
"""

from __future__ import annotations

from dataclasses import dataclass

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition


TILE_COLLISIONS_FIELD = "tileCollisions"


@dataclass(frozen=True, slots=True)
class TileCollisionMask:
    source_index: int
    width: int
    height: int
    cells: tuple[int, ...]

    @property
    def has_solid_pixels(self) -> bool:
        return any(self.cells)


class TileCollisionService:
    """CRUD facade for pixel collision authored directly on tileset tiles."""

    def __init__(self, workspace: ContentWorkspace | None = None) -> None:
        self.workspace = workspace

    def set_context(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    def masks(self, tileset_id: str) -> tuple[TileCollisionMask, ...]:
        definition = self._require_tileset(tileset_id)
        raw = definition.data.get(TILE_COLLISIONS_FIELD, [])

        if raw is None:
            return ()

        if not isinstance(raw, list):
            raise ValueError(
                f"{tileset_id} tileCollisions must be an array"
            )

        result = [
            self._decode_mask(definition, value)
            for value in raw
        ]

        source_indices = [value.source_index for value in result]

        if len(source_indices) != len(set(source_indices)):
            raise ValueError(
                f"{tileset_id} contains duplicate tile collision sourceIndex values"
            )

        return tuple(
            sorted(
                result,
                key=lambda value: value.source_index,
            )
        )

    def mask(
        self,
        tileset_id: str,
        source_index: int,
    ) -> TileCollisionMask | None:
        for value in self.masks(tileset_id):
            if value.source_index == source_index:
                return value

        return None

    def has_mask(
        self,
        tileset_id: str,
        source_index: int,
    ) -> bool:
        return self.mask(tileset_id, source_index) is not None

    def set_mask(
        self,
        tileset_id: str,
        source_index: int,
        cells: list[int] | tuple[int, ...],
    ) -> TileCollisionMask:
        definition = self._require_tileset(tileset_id)

        tile_size = self._tile_size(definition)

        self._validate_source_index(
            definition,
            source_index,
        )

        normalized = self._normalize_cells(
            cells,
            tile_size,
            tile_size,
        )

        entry: dict[str, object] = {
            "sourceIndex": source_index,
            "width": tile_size,
            "height": tile_size,
            "cells": list(normalized),
        }

        existing = definition.data.get(
            TILE_COLLISIONS_FIELD,
            [],
        )

        if existing is None:
            existing = []

        if not isinstance(existing, list):
            raise ValueError(
                f"{tileset_id} tileCollisions must be an array"
            )

        updated: list[object] = []
        replaced = False

        for value in existing:
            if (
                isinstance(value, dict)
                and value.get("sourceIndex") == source_index
            ):
                if not replaced:
                    updated.append(entry)
                    replaced = True
                continue

            updated.append(value)

        if not replaced:
            updated.append(entry)

        updated.sort(
            key=lambda value: (
                int(value.get("sourceIndex", -1))
                if isinstance(value, dict)
                else -1
            )
        )

        self.workspace.update(
            definition,
            TILE_COLLISIONS_FIELD,
            updated,
        )

        result = self.mask(
            tileset_id,
            source_index,
        )

        if result is None:
            raise RuntimeError(
                "tile collision mask could not be saved"
            )

        return result

    def remove_mask(
        self,
        tileset_id: str,
        source_index: int,
    ) -> bool:
        definition = self._require_tileset(tileset_id)

        self._validate_source_index(
            definition,
            source_index,
        )

        existing = definition.data.get(
            TILE_COLLISIONS_FIELD,
            [],
        )

        if existing is None:
            return False

        if not isinstance(existing, list):
            raise ValueError(
                f"{tileset_id} tileCollisions must be an array"
            )

        updated = [
            value
            for value in existing
            if not (
                isinstance(value, dict)
                and value.get("sourceIndex") == source_index
            )
        ]

        if len(updated) == len(existing):
            return False

        self.workspace.update(
            definition,
            TILE_COLLISIONS_FIELD,
            updated,
        )

        return True

    def empty_mask(
        self,
        tileset_id: str,
        source_index: int,
    ) -> TileCollisionMask:
        definition = self._require_tileset(tileset_id)

        self._validate_source_index(
            definition,
            source_index,
        )

        tile_size = self._tile_size(definition)

        return TileCollisionMask(
            source_index,
            tile_size,
            tile_size,
            (0,) * (tile_size * tile_size),
        )

    def _require_tileset(
        self,
        tileset_id: str,
    ) -> ContentDefinition:
        if self.workspace is None:
            raise ValueError(
                "content workspace is unavailable"
            )

        definition = self.workspace.find(
            "tilesets",
            tileset_id,
        )

        if definition is None:
            raise ValueError(
                f"tileset not found: {tileset_id}"
            )

        return definition

    @staticmethod
    def _tile_size(
        definition: ContentDefinition,
    ) -> int:
        value = definition.data.get("tileSize")

        if (
            not isinstance(value, int)
            or isinstance(value, bool)
            or value <= 0
        ):
            raise ValueError(
                f"{definition.definition_id} has invalid tileSize"
            )

        return value

    @staticmethod
    def _tile_count(
        definition: ContentDefinition,
    ) -> int:
        columns = definition.data.get("columns")
        rows = definition.data.get("rows")

        if (
            not isinstance(columns, int)
            or isinstance(columns, bool)
            or columns <= 0
            or not isinstance(rows, int)
            or isinstance(rows, bool)
            or rows <= 0
        ):
            raise ValueError(
                f"{definition.definition_id} has invalid atlas dimensions"
            )

        return columns * rows

    def _validate_source_index(
        self,
        definition: ContentDefinition,
        source_index: int,
    ) -> None:
        if (
            not isinstance(source_index, int)
            or isinstance(source_index, bool)
            or source_index < 0
            or source_index >= self._tile_count(definition)
        ):
            raise ValueError(
                f"sourceIndex {source_index} is outside tileset "
                f"{definition.definition_id}"
            )

    @staticmethod
    def _normalize_cells(
        cells: list[int] | tuple[int, ...],
        width: int,
        height: int,
    ) -> tuple[int, ...]:
        expected = width * height

        if len(cells) != expected:
            raise ValueError(
                f"tile collision requires exactly {expected} cells"
            )

        result: list[int] = []

        for value in cells:
            if isinstance(value, bool):
                result.append(1 if value else 0)
                continue

            if (
                not isinstance(value, int)
                or value not in (0, 1)
            ):
                raise ValueError(
                    "tile collision cells must be binary"
                )

            result.append(value)

        return tuple(result)

    def _decode_mask(
        self,
        definition: ContentDefinition,
        value: object,
    ) -> TileCollisionMask:
        if not isinstance(value, dict):
            raise ValueError(
                f"{definition.definition_id} contains an invalid tile collision entry"
            )

        source_index = value.get("sourceIndex")
        width = value.get("width")
        height = value.get("height")
        cells = value.get("cells")

        if (
            not isinstance(source_index, int)
            or isinstance(source_index, bool)
        ):
            raise ValueError(
                "tile collision sourceIndex must be an integer"
            )

        self._validate_source_index(
            definition,
            source_index,
        )

        tile_size = self._tile_size(definition)

        if width != tile_size or height != tile_size:
            raise ValueError(
                f"tile collision {source_index} must be "
                f"{tile_size}x{tile_size}"
            )

        if not isinstance(cells, list):
            raise ValueError(
                "tile collision cells must be an array"
            )

        normalized = self._normalize_cells(
            cells,
            tile_size,
            tile_size,
        )

        return TileCollisionMask(
            source_index,
            tile_size,
            tile_size,
            normalized,
        )
