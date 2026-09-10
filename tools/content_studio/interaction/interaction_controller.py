from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from ..model.content_workspace import ContentWorkspace
from ..model.tile_semantics import TerrainProfile, TerrainSelection
from ..model.map_document import ENTITY_CATEGORIES
from .drag_payload import StudioDragPayload
from .map_editing_service import MapEditingService
from .selection_controller import Selection, SelectionController
from ..services.terrain_painting_service import TerrainPaintingService


@dataclass(frozen=True, slots=True)
class InteractionResult:
    changed: bool = False
    selection: Selection | None = None
    status: str = ""


class InteractionController:
    """Interprets semantic canvas input and delegates mutations to the service."""

    def __init__(self, editing: MapEditingService, selection: SelectionController,
                 workspace: ContentWorkspace | None = None,
                 status: Callable[[str], None] | None = None,
                 message: Callable[[str], str] | None = None) -> None:
        self.editing = editing
        self.selection = selection
        self.workspace = workspace
        self.status = status
        self.message = message or (lambda value: value)
        self.active_payload: StudioDragPayload | None = None
        self.collision_overlay = False
        self._collision_fill_solid = True
        self.snap_enabled = True
        self._drag_origin: tuple[int, int] | None = None
        self._last_cell: tuple[int, int] | None = None
        self.terrain_painter: TerrainPaintingService | None = None
        self.active_terrain: TerrainSelection | None = None
        self.active_room: TerrainProfile | None = None
        self._terrain_cells: set[tuple[int, int]] = set()

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    def set_active_payload(self, payload: StudioDragPayload | None) -> None:
        self.active_payload = payload

    def set_terrain_painter(self, painter: TerrainPaintingService | None) -> None:
        self.terrain_painter = painter

    def set_terrain_selection(self, selection: TerrainSelection | None) -> None:
        self.active_terrain = selection
        self.active_room = None
        self.active_payload = None

    def set_room_profile(self, profile: TerrainProfile | None) -> None:
        self.active_room = profile
        self.active_terrain = None
        self.active_payload = None

    def set_collision_overlay(self, enabled: bool) -> None:
        self.collision_overlay = enabled

    def set_collision_fill_solid(self, solid: bool) -> None:
        self._collision_fill_solid = solid

    def press(self, button: str, tile: tuple[int, int], world: tuple[int, int],
              modifiers: frozenset[str] = frozenset()) -> InteractionResult:
        self._drag_origin = tile
        self._last_cell = tile
        if self.active_room and self.terrain_painter and button == "left":
            return InteractionResult(status=self.message("room_preview"))
        if self.active_terrain and self.terrain_painter and button in {"left", "right"}:
            if "ctrl" in modifiers and button == "left":
                result = self.terrain_painter.fill_terrain(tile, self.active_terrain)
                self._drag_origin = None
                self._last_cell = None
                return self._terrain_result(result)
            self._terrain_cells = {tile}
            if "shift" in modifiers:
                return InteractionResult(status=self.message("terrain_rectangle_preview"))
            return InteractionResult()
        payload = self.active_payload
        if self.collision_overlay:
            if button == "left" and "ctrl" in modifiers:
                return self._fill(tile)
            if button in {"left", "right"}:
                if "shift" in modifiers:
                    return InteractionResult(status="Collision rectangle preview")
                self.editing.set_collision([tile], button == "left")
                return InteractionResult(True, status_for_collision(button == "left"))
        if payload and payload.kind in {"ContentDefinition", "Reference", "MapElement"} and button == "left":
            selection = self.editing.place_payload(payload, world)
            if selection:
                self.selection.select_value(selection)
                return InteractionResult(True, selection, "Placed and selected")
        if button == "left" and "ctrl" in modifiers:
            return self._fill(tile)
        if payload and payload.kind in {"Tile", "TileBrush"}:
            if button == "left":
                if "shift" in modifiers:
                    return InteractionResult(status="Rectangle preview")
                self._paint(tile)
                return InteractionResult(True)
            if button == "right":
                self.editing.erase_tiles([tile])
                return InteractionResult(True)
        if button == "left":
            self.selection.clear()
        return InteractionResult()

    def move(self, buttons: frozenset[str], tile: tuple[int, int],
             modifiers: frozenset[str] = frozenset()) -> InteractionResult:
        if not buttons or self._last_cell == tile:
            return InteractionResult()
        previous = self._last_cell
        self._last_cell = tile
        if self.active_room and self.terrain_painter and "left" in buttons:
            return InteractionResult(status=self.message("room_preview"))
        if self.active_terrain and self.terrain_painter and ("left" in buttons or "right" in buttons):
            if "shift" not in modifiers:
                self._terrain_cells.add(tile)
            return InteractionResult()
        if self.collision_overlay and ("left" in buttons or "right" in buttons):
            if "shift" not in modifiers:
                self.editing.set_collision([tile], "left" in buttons)
                return InteractionResult(True)
        payload = self.active_payload
        if payload and payload.kind in {"Tile", "TileBrush"}:
            if "shift" not in modifiers:
                if "right" in buttons:
                    self.editing.erase_tiles([tile])
                elif "left" in buttons:
                    self._paint(tile)
                return InteractionResult(True)
        del previous
        return InteractionResult()

    def release(self, button: str, tile: tuple[int, int],
                modifiers: frozenset[str] = frozenset()) -> InteractionResult:
        origin = self._drag_origin
        self._drag_origin = None
        self._last_cell = None
        if origin is None:
            return InteractionResult()
        if self.active_room and self.terrain_painter and button == "left":
            result = self.terrain_painter.paint_room(origin, tile, self.active_room)
            return self._terrain_result(result)
        if self.active_terrain and self.terrain_painter and button in {"left", "right"}:
            cells = set(self._terrain_cells)
            if "shift" in modifiers:
                cells = set(self.editing.rectangle_cells(origin, tile))
            self._terrain_cells.clear()
            result = self.terrain_painter.paint_terrain(cells, self.active_terrain, erase=button == "right")
            return self._terrain_result(result)
        if self.collision_overlay and button in {"left", "right"} and "shift" in modifiers:
            self.editing.set_collision(self.editing.rectangle_cells(origin, tile), button == "left")
            return InteractionResult(True)
        payload = self.active_payload
        if payload and payload.kind in {"Tile", "TileBrush"} and button == "left" and "shift" in modifiers:
            if payload.kind == "TileBrush":
                self.editing.paint_rectangle(origin, tile, payload.tileset_id, payload.source_indices[0], payload.flags,
                                              self._brush(payload))
            else:
                self.editing.paint_rectangle(origin, tile, payload.tileset_id, payload.source_indices[0], payload.flags)
            return InteractionResult(True)
        return InteractionResult()

    def drop(self, payload: StudioDragPayload, world: tuple[int, int]) -> InteractionResult:
        if payload.kind in {"Tile", "TileBrush", "Stamp"}:
            self.active_payload = payload
            return InteractionResult(False, status="Brush selected; drag on the map to paint")
        selection = self.editing.place_payload(payload, world)
        if selection:
            self.selection.select_value(selection)
            return InteractionResult(True, selection, "Placed and selected")
        return InteractionResult(status="Unsupported drop")

    def _paint(self, tile: tuple[int, int]) -> None:
        payload = self.active_payload
        if payload is None or payload.kind not in {"Tile", "TileBrush"}:
            return
        brush = self._brush(payload) if payload.kind == "TileBrush" else None
        if brush:
            self.editing.paint_tiles([tile], payload.tileset_id, payload.source_indices[0], payload.flags, brush)
        else:
            self.editing.paint_tiles([tile], payload.tileset_id, payload.source_indices[0], payload.flags)

    def _fill(self, tile: tuple[int, int]) -> InteractionResult:
        payload = self.active_payload
        if self.collision_overlay:
            self.editing.fill_collision(tile, self._collision_fill_solid)
            return InteractionResult(True)
        if payload is None or payload.kind not in {"Tile", "TileBrush"}:
            return InteractionResult(status="Select a tile or content definition first")
        self.editing.fill_tiles(tile, payload.tileset_id, payload.source_indices[0], payload.flags)
        return InteractionResult(True)

    def _terrain_result(self, result: object) -> InteractionResult:
        changed = bool(getattr(result, "changed", False))
        warnings = tuple(getattr(result, "warnings", ()))
        if any(value.startswith("no compatible") for value in warnings):
            return InteractionResult(changed, status=self.message("terrain_missing_candidate"))
        return InteractionResult(changed, status="; ".join(warnings))

    def _brush(self, payload: StudioDragPayload) -> list[tuple[int, int, str, int, int]]:
        # TileBrush carries source indices rather than duplicating tileset
        # metadata.  The palette's rectangular selection is reconstructed
        # from atlas columns when the payload is consumed by the canvas.
        columns = max(1, len(payload.source_indices))
        if self.workspace:
            tileset = self.workspace.find("tilesets", payload.tileset_id)
            if tileset:
                columns = max(1, int(tileset.data.get("columns", columns)))
        coordinates = [(source % columns, source // columns, source) for source in payload.source_indices]
        min_x = min((value[0] for value in coordinates), default=0)
        min_y = min((value[1] for value in coordinates), default=0)
        return [(x - min_x, y - min_y, payload.tileset_id, source, payload.flags)
                for x, y, source in coordinates]


def status_for_collision(enabled: bool) -> str:
    return "Collision on" if enabled else "Collision off"
