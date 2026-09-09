from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any

from ..model.types import ContentReference


DRAG_MIME_TYPE = "application/x-dungeon-underworld-studio-payload"
PAYLOAD_VERSION = 1
CONTENT_KINDS = frozenset({"ContentDefinition", "Tile", "TileBrush", "Stamp", "MapElement", "Asset", "Reference"})


@dataclass(frozen=True, slots=True)
class StudioDragPayload:
    """Transport-neutral drag payload shared by all authoring palettes.

    Qt only transports the compact representation.  The editor never infers a
    definition from a label painted in a widget; category and definition ID are
    validated before a payload can reach an editing service.
    """

    kind: str
    category: str = ""
    definition_id: str = ""
    map_element: str = ""
    tileset_id: str = ""
    source_indices: tuple[int, ...] = ()
    flags: int = 0

    def __post_init__(self) -> None:
        if self.kind not in CONTENT_KINDS:
            raise ValueError(f"unknown studio drag payload kind: {self.kind}")
        if self.kind in {"ContentDefinition", "Reference"} and not ContentReference(self.category, self.definition_id).is_valid():
            raise ValueError("content payload needs category and definitionId")
        if self.kind == "MapElement" and not self.map_element.strip():
            raise ValueError("map element payload needs a type")
        if self.kind in {"Tile", "TileBrush"} and (not self.tileset_id.strip() or not self.source_indices):
            raise ValueError("tile payload needs a tileset and source index")

    @classmethod
    def content(cls, category: str, definition_id: str) -> "StudioDragPayload":
        return cls("ContentDefinition", category, definition_id)

    @classmethod
    def reference(cls, reference: ContentReference) -> "StudioDragPayload":
        return cls("Reference", reference.category, reference.definition_id)

    @classmethod
    def map_element_payload(cls, element: str) -> "StudioDragPayload":
        return cls("MapElement", map_element=element)

    @classmethod
    def tile(cls, tileset_id: str, source_index: int, flags: int = 0) -> "StudioDragPayload":
        return cls("Tile", tileset_id=tileset_id, source_indices=(int(source_index),), flags=int(flags))

    @classmethod
    def tile_brush(cls, tileset_id: str, source_indices: list[int] | tuple[int, ...], flags: int = 0) -> "StudioDragPayload":
        return cls("TileBrush", tileset_id=tileset_id, source_indices=tuple(int(value) for value in source_indices), flags=int(flags))

    @property
    def content_reference(self) -> ContentReference | None:
        if self.kind not in {"ContentDefinition", "Reference"}:
            return None
        return ContentReference(self.category, self.definition_id)

    def to_dict(self) -> dict[str, Any]:
        return {
            "version": PAYLOAD_VERSION,
            "kind": self.kind,
            "category": self.category,
            "definitionId": self.definition_id,
            "mapElement": self.map_element,
            "tilesetId": self.tileset_id,
            "sourceIndices": list(self.source_indices),
            "flags": self.flags,
        }

    def to_bytes(self) -> bytes:
        return json.dumps(self.to_dict(), ensure_ascii=False, separators=(",", ":")).encode("utf-8")

    @classmethod
    def from_bytes(cls, value: bytes | bytearray | memoryview) -> "StudioDragPayload | None":
        try:
            raw = json.loads(bytes(value).decode("utf-8"))
            if not isinstance(raw, dict) or raw.get("version") != PAYLOAD_VERSION:
                return None
            indices = raw.get("sourceIndices", [])
            if not isinstance(indices, list) or not all(isinstance(item, int) and not isinstance(item, bool) for item in indices):
                return None
            return cls(
                str(raw.get("kind", "")),
                str(raw.get("category", "")),
                str(raw.get("definitionId", "")),
                str(raw.get("mapElement", "")),
                str(raw.get("tilesetId", "")),
                tuple(indices),
                int(raw.get("flags", 0)),
            )
        except (ValueError, TypeError, KeyError, json.JSONDecodeError, UnicodeDecodeError):
            return None

    def put_mime_data(self, mime_data: object) -> None:
        if not hasattr(mime_data, "setData"):
            raise TypeError("mime data object does not support setData")
        mime_data.setData(DRAG_MIME_TYPE, self.to_bytes())  # type: ignore[attr-defined]

    @classmethod
    def from_mime_data(cls, mime_data: object) -> "StudioDragPayload | None":
        if not hasattr(mime_data, "hasFormat") or not mime_data.hasFormat(DRAG_MIME_TYPE):  # type: ignore[attr-defined]
            return None
        return cls.from_bytes(mime_data.data(DRAG_MIME_TYPE))  # type: ignore[attr-defined]
