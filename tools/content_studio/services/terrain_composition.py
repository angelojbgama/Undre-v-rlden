"""Smart Terrain composition strategies.

Smart Terrain is a composition engine rather than a fixed 3x3 concept.  The
3x3 palette is only the visual interface of the connectivity strategy; this
module is the UI-neutral place where a terrain family decides how an authored
intent becomes concrete map cells:

- ``ConnectivityStrategy`` keeps the existing 4-way autotile behavior for
  walls by delegating to the unchanged ``AutoTileResolver``.
- ``VariantStrategy`` resolves one weighted 1x1 cell for floors and other
  single-cell roles.
- ``PatternStrategy`` resolves whole NxM compositions through the existing
  authored ``stamps``; it never duplicates the stamp model.

The painting service asks a strategy for a placement and an influence
footprint; it never encodes per-strategy knowledge such as "walls re-resolve
their four neighbours".

Nothing in this module imports Qt.
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from ..model.tile_semantics import TileSemantic, TerrainSelection
from .autotile_resolver import (
    AutoTileResolver,
    stable_index,
)
from .tile_semantic_catalog import TileSemanticCatalog

if TYPE_CHECKING:
    from ..model.content_workspace import ContentWorkspace
    from ..model.map_document import MapDocument


CONNECTIVITY = "connectivity"
VARIANT = "variant"
PATTERN = "pattern"


@dataclass(frozen=True, slots=True)
class TerrainCell:
    """One concrete tile of a placement, relative to the placement anchor."""

    offset_x: int
    offset_y: int
    tileset_id: str
    source_index: int
    flags: int = 0


@dataclass(frozen=True, slots=True)
class TerrainPlacement:
    """The result of resolving one Smart Terrain anchor.

    A placement covers a single cell (connectivity and variants) or a whole
    multi-tile composition (patterns).  ``empty`` keeps the existing rule-gap
    semantics: the cell is explicitly cleared instead of resolved.
    """

    anchor: tuple[int, int]
    cells: tuple[TerrainCell, ...] = ()
    empty: bool = False
    strategy: str = ""
    pattern_id: str = ""

    def positions(self) -> dict[tuple[int, int], TerrainCell]:
        return {(self.anchor[0] + cell.offset_x, self.anchor[1] + cell.offset_y): cell
                for cell in self.cells}


@dataclass(frozen=True, slots=True)
class TerrainResolveContext:
    """Everything a strategy may need besides the selection itself."""

    occupied: frozenset[tuple[int, int]] | set[tuple[int, int]] = field(default_factory=frozenset)
    map_id: str = ""
    seed: int = 0
    map_tile_size: int | None = None


@dataclass(frozen=True, slots=True)
class StampPattern:
    """A stamp accepted as an NxM pattern of one terrain family."""

    definition_id: str
    display_name: str
    width: int
    height: int
    cells: tuple[TerrainCell, ...]


class TerrainStrategy:
    """Base strategy: resolve an anchor and report its influence footprint."""

    kind = ""

    def resolve(self, selection: TerrainSelection, position: tuple[int, int],
                context: TerrainResolveContext) -> TerrainPlacement | None:
        raise NotImplementedError

    def influence(self, positions: Iterable[tuple[int, int]],
                  document: "MapDocument") -> set[tuple[int, int]]:
        raise NotImplementedError


def _in_bounds(document: "MapDocument", x: int, y: int) -> bool:
    return 0 <= x < document.width and 0 <= y < document.height


class CellResolutionStrategy(TerrainStrategy):
    """Shared engine for 1x1 strategies.

    Both cell strategies delegate resolution to the existing
    ``AutoTileResolver`` protocol — ``resolve(family, role, position,
    neighbors, ...)`` — so wall connectivity, corner handling, rule gaps,
    weighted variant selection and any resolver following the same contract
    keep their exact behavior.
    """

    def __init__(self, resolver: AutoTileResolver, kind: str) -> None:
        self.resolver = resolver
        self.kind = kind

    def resolve(self, selection: TerrainSelection, position: tuple[int, int],
                context: TerrainResolveContext) -> TerrainPlacement | None:
        resolved = self.resolver.resolve(
            selection.family, selection.role, position, context.occupied,
            context.map_id, context.seed, context.map_tile_size)
        if resolved is None:
            return None
        cells = () if resolved.empty else (
            TerrainCell(0, 0, resolved.tileset_id, resolved.source_index, resolved.flags),)
        return TerrainPlacement(position, cells, resolved.empty, self.kind)


class ConnectivityStrategy(CellResolutionStrategy):
    """4-way connectivity: a cell plus its N/E/S/W context."""

    def __init__(self, resolver: AutoTileResolver) -> None:
        super().__init__(resolver, CONNECTIVITY)

    def influence(self, positions: Iterable[tuple[int, int]],
                  document: "MapDocument") -> set[tuple[int, int]]:
        result: set[tuple[int, int]] = set()
        for x, y in positions:
            if _in_bounds(document, x, y):
                result.add((x, y))
            for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
                if _in_bounds(document, nx, ny):
                    result.add((nx, ny))
        return result


class VariantStrategy(CellResolutionStrategy):
    """Weighted 1x1 variants: only the painted cell itself is influenced."""

    def __init__(self, resolver: AutoTileResolver) -> None:
        super().__init__(resolver, VARIANT)

    def influence(self, positions: Iterable[tuple[int, int]],
                  document: "MapDocument") -> set[tuple[int, int]]:
        del document
        return {(int(x), int(y)) for x, y in positions}


class PatternStrategy(TerrainStrategy):
    """NxM compositions resolved through the existing authored stamps.

    A stamp becomes a pattern of a family when every cell's ``tileId``
    resolves to a semantic of that same family.  No parallel pattern database
    is created and no stamp is duplicated.
    """

    def __init__(self, catalog: TileSemanticCatalog,
                 workspace: "ContentWorkspace | None" = None) -> None:
        self.catalog = catalog
        self.workspace = workspace
        self.kind = PATTERN

    def set_workspace(self, workspace: "ContentWorkspace | None") -> None:
        self.workspace = workspace

    def patterns_for(self, family: str) -> tuple[StampPattern, ...]:
        workspace = self.workspace
        if workspace is None or not family:
            return ()
        result: list[StampPattern] = []
        for stamp in workspace.definitions("stamps"):
            cells = stamp.data.get("cells")
            if not isinstance(cells, list) or not cells:
                continue
            terrain_cells: list[TerrainCell] = []
            compatible = True
            for cell in cells:
                if not isinstance(cell, dict):
                    compatible = False
                    break
                semantic = self.catalog.find(str(cell.get("tileId", "")))
                if semantic is None or semantic.family != family:
                    compatible = False
                    break
                terrain_cells.append(TerrainCell(
                    int(cell.get("x", 0)), int(cell.get("y", 0)),
                    semantic.tileset_id, semantic.source_index, 0))
            if compatible and terrain_cells:
                width = stamp.data.get("width")
                height = stamp.data.get("height")
                result.append(StampPattern(
                    str(stamp.data.get("id", "")),
                    str(stamp.data.get("displayName", "")),
                    int(width) if isinstance(width, int) else max(c.offset_x for c in terrain_cells) + 1,
                    int(height) if isinstance(height, int) else max(c.offset_y for c in terrain_cells) + 1,
                    tuple(terrain_cells)))
        return tuple(sorted(result, key=lambda value: value.definition_id))

    def pattern(self, definition_id: str, family: str) -> StampPattern | None:
        return next((value for value in self.patterns_for(family)
                     if value.definition_id == definition_id), None)

    def resolve(self, selection: TerrainSelection, position: tuple[int, int],
                context: TerrainResolveContext) -> TerrainPlacement | None:
        candidates = self.patterns_for(selection.family)
        chosen: StampPattern | None = None
        if selection.pattern_id:
            chosen = next((value for value in candidates
                           if value.definition_id == selection.pattern_id), None)
        else:
            if not candidates:
                return None
            index = stable_index(context.map_id, position, selection.family,
                                 "pattern", context.seed, len(candidates))
            chosen = candidates[index]
        if chosen is None:
            return None
        return TerrainPlacement(
            position,
            chosen.cells,
            False, PATTERN, chosen.definition_id)

    def influence(self, positions: Iterable[tuple[int, int]],
                  document: "MapDocument") -> set[tuple[int, int]]:
        # A pattern of the family can cover cells around any anchor, so the
        # recomposition region is bounded by the widest family footprint.
        reach_x = reach_y = 0
        if self.workspace is not None:
            for stamp in self.workspace.definitions("stamps"):
                reach_x = max(reach_x, max(0, int(stamp.data.get("width", 1)) - 1))
                reach_y = max(reach_y, max(0, int(stamp.data.get("height", 1)) - 1))
        result: set[tuple[int, int]] = set()
        for x, y in positions:
            for ny in range(y - reach_y, y + reach_y + 1):
                for nx in range(x - reach_x, x + reach_x + 1):
                    if _in_bounds(document, nx, ny):
                        result.add((nx, ny))
        return result


class TerrainCompositionService:
    """Chooses the composition strategy for a terrain selection.

    The service is the single place that maps family/role to a strategy.
    Walls keep 4-way connectivity; every other role resolves as weighted 1x1
    variants; patterns are placed explicitly through :class:`PatternStrategy`.
    """

    def __init__(self, catalog: TileSemanticCatalog,
                 workspace: "ContentWorkspace | None" = None,
                 resolver: AutoTileResolver | None = None) -> None:
        self.catalog = catalog
        resolved = resolver or AutoTileResolver(catalog)
        self.workspace = workspace
        self.connectivity = ConnectivityStrategy(resolved)
        self.variant = VariantStrategy(resolved)
        self.pattern = PatternStrategy(catalog, workspace)

    def set_workspace(self, workspace: "ContentWorkspace | None") -> None:
        self.workspace = workspace
        self.pattern.set_workspace(workspace)

    def strategy_for(self, family: str, role: str) -> TerrainStrategy:
        del family  # Strategies are currently chosen by role semantics.
        if role in {"wall", "corner"}:
            return self.connectivity
        return self.variant

    def resolve(self, selection: TerrainSelection, position: tuple[int, int],
                context: TerrainResolveContext) -> TerrainPlacement | None:
        return self.strategy_for(selection.family, selection.role).resolve(selection, position, context)

    def patterns_for(self, family: str) -> tuple[StampPattern, ...]:
        return self.pattern.patterns_for(family)
