"""Typed, tooling-only views over the authored tile semantic definitions.

The JSON contract remains the authored Content contract.  These small
objects make the rest of the Studio independent from dictionary key spelling
and provide a single place for topology/edge names.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

from .types import ContentDefinition


TOPOLOGIES = (
    "unknown", "interior", "straightHorizontal", "straightVertical",
    "outerCorner", "innerCorner", "cap", "junction", "architecturalDetail",
)
ROLES = ("floor", "wall", "corner", "ledge", "opening", "detail", "unknown")
EDGES = ("unknown", "floor", "masonry", "voidEdge", "terminal")


@dataclass(frozen=True, slots=True)
class TileSemantic:
    definition_id: str
    tileset_id: str
    source_index: int
    family: str
    role: str
    topology: str
    north: str = "unknown"
    east: str = "unknown"
    south: str = "unknown"
    west: str = "unknown"
    preferred_layer: str = ""
    flip_x_allowed: bool = False
    variant_weight: int = 1
    source_definition: ContentDefinition | None = None

    @classmethod
    def from_definition(cls, definition: ContentDefinition) -> "TileSemantic":
        data = definition.data
        return cls(
            definition.definition_id,
            _text(data.get("tilesetId")),
            _integer(data.get("sourceIndex")),
            _text(data.get("family")),
            _text(data.get("role"), "unknown"),
            _text(data.get("topology"), "unknown"),
            _text(data.get("north"), "unknown"),
            _text(data.get("east"), "unknown"),
            _text(data.get("south"), "unknown"),
            _text(data.get("west"), "unknown"),
            _text(data.get("preferredLayer")),
            bool(data.get("flipXAllowed", False)),
            _integer(data.get("variantWeight"), 1),
            definition,
        )

    @property
    def reference(self) -> tuple[str, int]:
        return self.tileset_id, self.source_index

    @property
    def edges(self) -> dict[str, str]:
        return {"north": self.north, "east": self.east, "south": self.south, "west": self.west}


@dataclass(frozen=True, slots=True)
class TerrainFamily:
    """Derived grouping; it is intentionally not a second authored database."""

    family: str
    semantics: tuple[TileSemantic, ...]

    @property
    def roles(self) -> tuple[str, ...]:
        return tuple(sorted({value.role for value in self.semantics if value.role}))

    @property
    def tileset_ids(self) -> tuple[str, ...]:
        return tuple(sorted({value.tileset_id for value in self.semantics if value.tileset_id}))


@dataclass(frozen=True, slots=True)
class TerrainSelection:
    family: str
    role: str = "floor"
    seed: int = 0
    # Tooling-only override.  ``None`` preserves the map collision state;
    # explicit values are authored into the map cells by the same gesture.
    collision: bool | None = None


@dataclass(frozen=True, slots=True)
class TerrainProfile:
    """A tooling profile that composes existing floor/wall semantics."""

    name: str
    floor: TerrainSelection
    boundary: TerrainSelection


def as_semantics(definitions: Iterable[ContentDefinition]) -> tuple[TileSemantic, ...]:
    return tuple(TileSemantic.from_definition(value) for value in definitions)


def _text(value: object, default: str = "") -> str:
    return value.strip() if isinstance(value, str) else default


def _integer(value: object, default: int = 0) -> int:
    return int(value) if isinstance(value, int) and not isinstance(value, bool) else default
