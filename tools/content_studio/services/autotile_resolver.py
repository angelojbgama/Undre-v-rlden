"""UI-neutral terrain-to-tile resolution."""

from __future__ import annotations

import hashlib
from collections.abc import Iterable, Mapping
from dataclasses import dataclass

from ..model.tile_semantics import TileSemantic
from .tile_semantic_catalog import TileSemanticCatalog


NORTH = 1
EAST = 2
SOUTH = 4
WEST = 8
ORTHOGONAL_MASKS = {"north": NORTH, "east": EAST, "south": SOUTH, "west": WEST}


@dataclass(frozen=True, slots=True)
class ResolvedTile:
    tileset_id: str
    source_index: int
    flags: int = 0
    semantic_id: str = ""


def neighbor_mask(position: tuple[int, int], neighbors: Mapping[object, object] | Iterable[tuple[int, int]]) -> int:
    """Return the N/E/S/W mask for either a direction map or occupied cells."""
    if isinstance(neighbors, Mapping):
        result = 0
        for name, bit in ORTHOGONAL_MASKS.items():
            if bool(neighbors.get(name, neighbors.get(name[0].upper(), False))):
                result |= bit
        return result
    occupied = set(neighbors)
    x, y = position
    result = 0
    for name, bit, offset in (("north", NORTH, (0, -1)), ("east", EAST, (1, 0)),
                               ("south", SOUTH, (0, 1)), ("west", WEST, (-1, 0))):
        del name
        if (x + offset[0], y + offset[1]) in occupied:
            result |= bit
    return result


class AutoTileResolver:
    """Resolve terrain intent without importing Qt or touching a map widget."""

    def __init__(self, catalog: TileSemanticCatalog) -> None:
        self.catalog = catalog

    def resolve(self, family: str, role: str, position: tuple[int, int],
                neighbors: Mapping[object, object] | Iterable[tuple[int, int]] = (),
                map_id: str = "", seed: int = 0, map_tile_size: int | None = None) -> ResolvedTile | None:
        mask = neighbor_mask(position, neighbors)
        return self.resolve_mask(family, role, position, mask, map_id, seed, map_tile_size)

    def resolve_mask(self, family: str, role: str, position: tuple[int, int], mask: int,
                     map_id: str = "", seed: int = 0, map_tile_size: int | None = None) -> ResolvedTile | None:
        candidates = self._compatible(self._candidate_pool(family, role), map_tile_size)
        if not candidates:
            return None
        topology_order = self._topology_order(role, mask)
        chosen: tuple[TileSemantic, ...] = ()
        for topology in topology_order:
            chosen = tuple(value for value in candidates if value.topology == topology)
            if chosen:
                break
        if not chosen:
            # A semantic can be useful even before every topology has been
            # classified.  Interior/unknown is an explicit predictable
            # fallback, followed by any same-family/same-role candidate.
            chosen = tuple(value for value in candidates if value.topology in {"interior", "unknown"}) or candidates
        ordered = tuple(sorted(chosen, key=lambda value: (value.definition_id, value.tileset_id, value.source_index)))
        index = _stable_index(map_id, position, family, role, seed, len(ordered))
        value = ordered[index]
        return ResolvedTile(value.tileset_id, value.source_index, 0, value.definition_id)

    def _compatible(self, candidates: Iterable[TileSemantic], map_tile_size: int | None) -> tuple[TileSemantic, ...]:
        # The catalog can be used without a workspace.  In that case structural
        # resolution still works; MapEditingService performs the strict map
        # tile-size check before applying a result.
        values = tuple(candidates)
        if map_tile_size is None:
            return values
        content_workspace = self.catalog.workspace
        if content_workspace is None:
            return values
        return tuple(value for value in values
                     if (tileset := content_workspace.find("tilesets", value.tileset_id)) is not None
                     and tileset.data.get("tileSize") == map_tile_size)

    def _candidate_pool(self, family: str, role: str) -> tuple[TileSemantic, ...]:
        values = self.catalog.by_family_role(family, role)
        if role == "wall":
            # Existing authored packs sometimes classify actual corner cells as
            # role=corner while their wall brush asks for the wall family.  They
            # remain part of the same semantic family and are safe candidates.
            values += tuple(value for value in self.catalog.by_family_role(family, "corner") if value not in values)
        return values

    @staticmethod
    def _topology_order(role: str, mask: int) -> tuple[str, ...]:
        if role != "wall":
            return ("interior", "unknown")
        if mask in {NORTH | SOUTH}:
            return ("straightVertical", "interior", "unknown")
        if mask in {EAST | WEST}:
            return ("straightHorizontal", "interior", "unknown")
        if mask in {NORTH | EAST, EAST | SOUTH, SOUTH | WEST, WEST | NORTH}:
            return ("outerCorner", "innerCorner", "corner", "cap", "interior", "unknown")
        if mask in {NORTH, EAST, SOUTH, WEST, 0}:
            return ("cap", "outerCorner", "interior", "unknown")
        if mask in {NORTH | EAST | SOUTH, EAST | SOUTH | WEST,
                    SOUTH | WEST | NORTH, WEST | NORTH | EAST}:
            return ("junction", "innerCorner", "interior", "unknown")
        return ("interior", "junction", "unknown")


def _stable_index(map_id: str, position: tuple[int, int], family: str, role: str,
                  seed: int, count: int) -> int:
    if count <= 1:
        return 0
    value = "|".join((map_id, str(position[0]), str(position[1]), family, role, str(seed))).encode("utf-8")
    digest = hashlib.sha256(value).digest()
    return int.from_bytes(digest[:8], "big") % count
