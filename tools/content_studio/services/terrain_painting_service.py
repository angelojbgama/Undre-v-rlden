"""Semantic terrain painting built on the existing MapDocument command model."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

from ..interaction.map_editing_service import MapEditingService
from ..model.content_workspace import ContentWorkspace
from ..model.map_document import MapDocument
from ..model.tile_semantics import TerrainProfile, TerrainSelection
from .autotile_resolver import AutoTileResolver, EAST, NORTH, SOUTH, WEST
from .tile_semantic_catalog import TileSemanticCatalog


@dataclass(frozen=True, slots=True)
class TerrainPaintResult:
    changed: bool = False
    affected_cells: tuple[tuple[int, int], ...] = ()
    warnings: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class TerrainCollisionPolicy:
    """Opt-in role policy; the default terrain workflow never changes collision."""

    solid_roles: frozenset[str] = frozenset()

    def solid_for(self, role: str) -> bool | None:
        return True if role in self.solid_roles else None


class TerrainPaintingService:
    """UI-neutral authoring operations for smart floor, wall and rooms."""

    def __init__(self, document: MapDocument | None = None, workspace: ContentWorkspace | None = None,
                 editing: MapEditingService | None = None,
                 catalog: TileSemanticCatalog | None = None,
                 resolver: AutoTileResolver | None = None,
                 collision_policy: TerrainCollisionPolicy | None = None) -> None:
        self.document = document
        self.workspace = workspace
        self.editing = editing or MapEditingService(document, workspace=workspace)
        self.catalog = catalog or TileSemanticCatalog(workspace)
        self.resolver = resolver or AutoTileResolver(self.catalog)
        self.collision_policy = collision_policy

    def set_context(self, document: MapDocument | None, workspace: ContentWorkspace | None) -> None:
        self.document = document
        self.workspace = workspace
        self.editing.set_document(document)
        self.editing.set_workspace(workspace)
        self.catalog.set_workspace(workspace)

    def paint_terrain(self, cells: Iterable[tuple[int, int]], selection: TerrainSelection,
                      layer_index: int | None = None, erase: bool = False,
                      label: str = "Paint Smart Terrain") -> TerrainPaintResult:
        document = self._require_document()
        if layer_index is not None:
            self.editing.set_layer(layer_index)
        target = self._in_bounds(cells, document)
        if not target:
            return TerrainPaintResult()
        layer = self.editing.layer_index
        old_active = self._active_cells(layer, selection)
        active = set(old_active)
        if erase:
            active.difference_update(target)
        else:
            active.update(target)
        affected = set(target)
        if selection.role == "wall":
            for x, y in target:
                affected.update(self._neighbors((x, y), document))
            affected = {cell for cell in affected if cell in active or cell in old_active or cell in target}
        assignments: dict[tuple[int, int], tuple[str, int, int] | None] = {}
        collision: dict[tuple[int, int], bool] = {}
        warnings: list[str] = []
        for position in sorted(affected, key=lambda value: (value[1], value[0])):
            if position not in active:
                if position in target:
                    assignments[position] = None
                    self._set_collision(collision, position, selection.role, False)
                continue
            resolved = self._resolve(selection, position, active, document, warnings)
            if resolved is not None:
                assignments[position] = (resolved.tileset_id, resolved.source_index, resolved.flags)
                self._set_collision(collision, position, selection.role, True)
        if not assignments:
            return TerrainPaintResult(False, tuple(sorted(affected)), tuple(dict.fromkeys(warnings)))
        try:
            changed = self.editing.apply_tile_assignments(assignments, label, collision)
        except ValueError as error:
            warnings.append(str(error))
            return TerrainPaintResult(False, tuple(sorted(affected)), tuple(dict.fromkeys(warnings)))
        return TerrainPaintResult(changed, tuple(sorted(affected)), tuple(dict.fromkeys(warnings)))

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
        collision: dict[tuple[int, int], bool] = {}
        warnings: list[str] = []
        for position in sorted(boundary, key=lambda value: (value[1], value[0])):
            resolved = self._resolve(profile.boundary, position, boundary, document, warnings)
            if resolved is not None:
                assignments[position] = (resolved.tileset_id, resolved.source_index, resolved.flags)
                self._set_collision(collision, position, profile.boundary.role, True)
        for position in sorted(rect - boundary, key=lambda value: (value[1], value[0])):
            resolved = self._resolve(profile.floor, position, set(), document, warnings)
            if resolved is not None:
                assignments[position] = (resolved.tileset_id, resolved.source_index, resolved.flags)
                self._set_collision(collision, position, profile.floor.role, True)
        if not assignments:
            return TerrainPaintResult(False, tuple(sorted(rect)), tuple(dict.fromkeys(warnings)))
        try:
            changed = self.editing.apply_tile_assignments(assignments, "Create Smart Room", collision)
        except ValueError as error:
            warnings.append(str(error))
            return TerrainPaintResult(False, tuple(sorted(rect)), tuple(dict.fromkeys(warnings)))
        return TerrainPaintResult(changed, tuple(sorted(rect)), tuple(dict.fromkeys(warnings)))

    def _resolve(self, selection: TerrainSelection, position: tuple[int, int], active: set[tuple[int, int]],
                 document: MapDocument, warnings: list[str]):
        resolved = self.resolver.resolve(selection.family, selection.role, position, active,
                                         document.map_id, selection.seed, document.tile_size)
        if resolved is None:
            warnings.append(f"no compatible {selection.role} candidate for {selection.family}")
        return resolved

    def _set_collision(self, target: dict[tuple[int, int], bool], position: tuple[int, int], role: str, painting: bool) -> None:
        if self.collision_policy is None:
            return
        policy_value = self.collision_policy.solid_for(role)
        if policy_value is not None:
            target[position] = policy_value if painting else False

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

    @staticmethod
    def _neighbors(position: tuple[int, int], document: MapDocument) -> set[tuple[int, int]]:
        x, y = position
        return {(nx, ny) for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y))
                if 0 <= nx < document.width and 0 <= ny < document.height}

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
