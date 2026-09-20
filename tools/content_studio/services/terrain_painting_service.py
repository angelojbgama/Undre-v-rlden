"""Semantic terrain painting built on the existing MapDocument command model."""

from __future__ import annotations

from dataclasses import dataclass, replace
from typing import Iterable

from ..interaction.map_editing_service import MapEditingService
from ..model.content_workspace import ContentWorkspace
from ..model.map_document import MapDocument
from ..model.tile_semantics import TerrainProfile, TerrainSelection
from .autotile_resolver import AutoTileResolver
from .fixture_reservation_service import FixtureTerrainReservationService
from .terrain_composition import TerrainCompositionService, TerrainResolveContext
from .tile_semantic_catalog import TileSemanticCatalog


@dataclass(frozen=True, slots=True)
class TerrainPaintResult:
    changed: bool = False
    affected_cells: tuple[tuple[int, int], ...] = ()
    warnings: tuple[str, ...] = ()


class TerrainPaintingService:
    """UI-neutral authoring operations for smart floor, wall and rooms.

    Pixel Collision is owned by tilesets. Smart Terrain chooses visual and
    semantic tiles only and never authors physical collision into map cells.

    Resolution is delegated to the composition strategies: this service
    orchestrates cells, reservations and undo, and never encodes strategy
    details such as which neighbours a wall recalculates or how large a
    pattern footprint is.
    """

    def __init__(self, document: MapDocument | None = None, workspace: ContentWorkspace | None = None,
                 editing: MapEditingService | None = None,
                 catalog: TileSemanticCatalog | None = None,
                 resolver: AutoTileResolver | None = None,
                 reservations: FixtureTerrainReservationService | None = None,
                 composition: TerrainCompositionService | None = None) -> None:
        self.document = document
        self.workspace = workspace
        self.editing = editing or MapEditingService(document, workspace=workspace)
        self.catalog = catalog or TileSemanticCatalog(workspace)
        self.resolver = resolver or AutoTileResolver(self.catalog)
        self.composition = composition or TerrainCompositionService(
            self.catalog, workspace, self.resolver)
        self.reservations = reservations or FixtureTerrainReservationService(
            document,
            workspace,
        )

    def set_context(self, document: MapDocument | None, workspace: ContentWorkspace | None) -> None:
        self.document = document
        self.workspace = workspace
        self.editing.set_document(document)
        self.editing.set_workspace(workspace)
        self.catalog.set_workspace(workspace)
        # Content can mutate in place through the same workspace instance
        # (rule/variant saves, semantic edits).  ``set_workspace`` only
        # invalidates on an instance change, so refresh the index explicitly:
        # a stale pool makes every painted cell resolve to the same old tile.
        self.catalog.invalidate()
        self.composition.set_workspace(workspace)
        self.reservations.set_context(
            document,
            workspace,
        )

    def paint_terrain(self, cells: Iterable[tuple[int, int]], selection: TerrainSelection,
                      layer_index: int | None = None, erase: bool = False,
                      label: str = "Paint Smart Terrain") -> TerrainPaintResult:
        document = self._require_document()
        # A pinned pattern is a placement brush, not a freehand cell strategy.
        selection = replace(selection, pattern_id="")
        if layer_index is not None:
            self.editing.set_layer(layer_index)
        target = self._in_bounds(cells, document)
        if not target:
            return TerrainPaintResult()
        layer = self.editing.layer_index
        old_active = self._active_cells(layer, selection)
        active = set(old_active)

        reserved = (
            self.reservations.reserved_cells(
                selection.role
            )
        )

        if erase:
            active.difference_update(
                target
            )
        else:
            active.update(
                target
            )

        # Fixture reservations have two independent meanings:
        #
        # - they own the actual authored cells, so Smart Terrain must
        #   never write a tile underneath the fixture;
        # - they are virtual members of the terrain topology, so the
        #   autotiler sees a continuous wall through a Door rather than
        #   wrapping/cornering around the opening.
        active.difference_update(
            reserved
        )

        topology_active = (
            set(active)
            | reserved
        )

        # The strategy owns its influence footprint (variants influence only
        # the painted cell; connectivity adds the N/E/S/W context; patterns
        # would add their NxM reach).  Paint semantics keep one filter: only
        # cells that carry or carried this terrain are actually rewritten.
        strategy = self.composition.strategy_for(selection.family, selection.role)
        affected = {cell for cell in strategy.influence(target, document)
                    if cell in active or cell in old_active or cell in target}
        assignments: dict[tuple[int, int], tuple[str, int, int] | None] = {}
        warnings: list[str] = []
        for position in sorted(affected, key=lambda value: (value[1], value[0])):
            if position in reserved:
                if position in target:
                    assignments[position] = None
                continue

            if position not in active:
                if position in target:
                    assignments[position] = None
                continue

            resolved = self._resolve(
                selection,
                position,
                topology_active,
                document,
                warnings,
            )
            if resolved is not None:
                assignments[position] = self._assignment(position, resolved)
        if not assignments:
            return TerrainPaintResult(False, tuple(sorted(affected)), tuple(dict.fromkeys(warnings)))
        try:
            changed = self.editing.apply_tile_assignments(assignments, label)
        except ValueError as error:
            warnings.append(str(error))
            return TerrainPaintResult(False, tuple(sorted(affected)), tuple(dict.fromkeys(warnings)))
        return TerrainPaintResult(changed, tuple(sorted(affected)), tuple(dict.fromkeys(warnings)))

    def place_pattern(self, origin: tuple[int, int], selection: TerrainSelection,
                      layer_index: int | None = None,
                      label: str = "Place Smart Terrain Pattern") -> TerrainPaintResult:
        """Place one whole NxM pattern as a single atomic undoable command.

        The composition resolves through ``PatternStrategy``: equivalent
        stamps are chosen deterministically, or the selection's pinned stamp
        is used.  Fixture-owned cells are never written, map borders clip
        like the existing stamp tool, and the placement remains one command.
        """
        document = self._require_document()
        if layer_index is not None:
            self.editing.set_layer(layer_index)
        warnings: list[str] = []
        context = TerrainResolveContext(
            frozenset(), document.map_id, selection.seed, document.tile_size)
        placement = self.composition.pattern.resolve(selection, origin, context)
        if placement is None:
            warnings.append(f"no compatible pattern for {selection.family}")
            return TerrainPaintResult(False, (), tuple(warnings))
        reserved = {cell
                    for reservation in self.reservations.reservations()
                    for cell in reservation.cells}
        assignments: dict[tuple[int, int], tuple[str, int, int] | None] = {}
        for position, cell in placement.positions().items():
            if not self._inside(position, document) or position in reserved:
                continue
            assignments[position] = (cell.tileset_id, cell.source_index, cell.flags)
        affected = tuple(sorted(assignments))
        if not assignments:
            warnings.append("pattern placement is fully outside the map")
            return TerrainPaintResult(False, affected, tuple(warnings))
        try:
            changed = self.editing.apply_tile_assignments(assignments, label)
        except ValueError as error:
            warnings.append(str(error))
            return TerrainPaintResult(False, affected, tuple(warnings))
        return TerrainPaintResult(changed, affected, tuple(warnings))

    def fill_terrain(self, origin: tuple[int, int], selection: TerrainSelection,
                     layer_index: int | None = None, erase: bool = False) -> TerrainPaintResult:
        document = self._require_document()
        layer = self.editing.layer_index if layer_index is None else layer_index
        if not self._inside(origin, document):
            return TerrainPaintResult()
        target_kind = self._semantic_at(layer, *origin)
        if target_kind is None:
            target_kind = ("", "")
        cells = self._cells_with_kind(layer, target_kind, document)
        if target_kind == ("", ""):
            cells = self._empty_cells(layer, document)
        return self.paint_terrain(cells, selection, layer, erase=erase, label="Fill Smart Terrain")

    def paint_room(self, start: tuple[int, int], end: tuple[int, int], profile: TerrainProfile,
                   layer_index: int | None = None) -> TerrainPaintResult:
        document = self._require_document()
        if layer_index is not None:
            self.editing.set_layer(layer_index)
        left, right = sorted((start[0], end[0])); top, bottom = sorted((start[1], end[1]))
        rect = {(x, y) for y in range(top, bottom + 1) for x in range(left, right + 1)
                if self._inside((x, y), document)}
        if not rect:
            return TerrainPaintResult()
        boundary = {cell for cell in rect if cell[0] in {left, right} or cell[1] in {top, bottom}}
        assignments: dict[tuple[int, int], tuple[str, int, int] | None] = {}
        warnings: list[str] = []

        reserved_boundary = (
            self.reservations.reserved_cells(
                profile.boundary.role
            )
        )

        # Reserved boundary cells stay physically empty for the
        # fixture but are virtual wall members for topology. Neighboring
        # wall tiles therefore remain part of one straight boundary.
        room_occupancy = set(
            rect
        )

        for position in sorted(boundary, key=lambda value: (value[1], value[0])):
            if position in reserved_boundary:
                # A fixture-owned wall opening is explicit terrain state.
                # Smart Room may recalculate its neighbors but cannot close it.
                assignments[position] = None
                continue

            # Boundary visuals need the complete room occupancy. Passing only
            # the outline makes top and bottom centers both look like an E/W
            # stroke, so the resolver cannot distinguish their inward side.
            # Reserved fixture cells are intentionally absent from occupancy.
            resolved = self._resolve(profile.boundary, position, room_occupancy, document, warnings)
            if resolved is not None:
                assignments[position] = self._assignment(position, resolved)
        for position in sorted(rect - boundary, key=lambda value: (value[1], value[0])):
            resolved = self._resolve(profile.floor, position, set(), document, warnings)
            if resolved is not None:
                assignments[position] = self._assignment(position, resolved)
        if not assignments:
            return TerrainPaintResult(False, tuple(sorted(rect)), tuple(dict.fromkeys(warnings)))
        try:
            changed = self.editing.apply_tile_assignments(assignments, "Create Smart Room")
        except ValueError as error:
            warnings.append(str(error))
            return TerrainPaintResult(False, tuple(sorted(rect)), tuple(dict.fromkeys(warnings)))
        return TerrainPaintResult(changed, tuple(sorted(rect)), tuple(dict.fromkeys(warnings)))

    def _resolve(self, selection: TerrainSelection, position: tuple[int, int], active: set[tuple[int, int]],
                 document: MapDocument, warnings: list[str]):
        context = TerrainResolveContext(
            frozenset(active), document.map_id, selection.seed, document.tile_size)
        placement = self.composition.resolve(selection, position, context)
        if placement is None:
            warnings.append(f"no compatible {selection.role} candidate for {selection.family}")
        return placement

    @staticmethod
    def _assignment(position: tuple[int, int], placement) -> tuple[str, int, int] | None:
        del position
        if placement.empty or not placement.cells:
            return None
        cell = placement.cells[0]
        return cell.tileset_id, cell.source_index, cell.flags

    def _active_cells(self, layer: int, selection: TerrainSelection) -> set[tuple[int, int]]:
        document = self._require_document()
        result: set[tuple[int, int]] = set()
        for y in range(document.height):
            for x in range(document.width):
                semantic = self._semantic_at(layer, x, y)
                if semantic and semantic.family == selection.family and self._same_role(semantic.role, selection.role):
                    result.add((x, y))
        return result

    def _semantic_at(self, layer: int, x: int, y: int):
        document = self._require_document()
        if layer < 0 or layer >= len(document.layers) or not self._inside((x, y), document):
            return None
        cells = document.layers[layer].get("cells", [])
        references = document.data.get("tileReferences", [])
        if not isinstance(cells, list) or not isinstance(references, list):
            return None
        index = cells[y * document.width + x]
        if not isinstance(index, int) or index < 0 or index >= len(references) or not isinstance(references[index], dict):
            return None
        reference = references[index]
        return next(iter(self.catalog.by_reference(str(reference.get("tilesetId", "")),
                                                   int(reference.get("sourceIndex", 0)))), None)

    @staticmethod
    def _same_role(actual: str, requested: str) -> bool:
        return actual == requested or (requested == "wall" and actual == "corner")

    @staticmethod
    def _inside(position: tuple[int, int], document: MapDocument) -> bool:
        return 0 <= position[0] < document.width and 0 <= position[1] < document.height

    @staticmethod
    def _in_bounds(cells: Iterable[tuple[int, int]], document: MapDocument) -> set[tuple[int, int]]:
        return {(int(x), int(y)) for x, y in cells if 0 <= int(x) < document.width and 0 <= int(y) < document.height}

    def _cells_with_kind(self, layer: int, kind: tuple[str, str], document: MapDocument) -> set[tuple[int, int]]:
        return {(x, y) for y in range(document.height) for x in range(document.width)
                if (semantic := self._semantic_at(layer, x, y)) is not None
                and (semantic.family, semantic.role) == kind}

    def _empty_cells(self, layer: int, document: MapDocument) -> set[tuple[int, int]]:
        cells = document.layers[layer].get("cells", [])
        if not isinstance(cells, list):
            return set()
        return {(x, y) for y in range(document.height) for x in range(document.width)
                if cells[y * document.width + x] is None}

    def _require_document(self) -> MapDocument:
        if self.document is None:
            raise ValueError("no map document is active")
        return self.document
