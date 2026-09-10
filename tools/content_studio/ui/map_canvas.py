from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QPoint, Qt, Signal
from PySide6.QtGui import QDragEnterEvent, QDragMoveEvent, QDropEvent, QKeyEvent, QMouseEvent, QPainter, QWheelEvent
from PySide6.QtWidgets import QWidget

from ..interaction.drag_payload import StudioDragPayload
from ..interaction.interaction_controller import InteractionController, InteractionResult
from ..interaction.map_editing_service import MapEditingService
from ..interaction.selection_controller import Selection, SelectionController
from ..model.content_workspace import ContentWorkspace
from ..model.map_document import ENTITY_CATEGORIES, MapDocument
from ..model.tile_semantics import TerrainProfile, TerrainSelection
from .canvas_camera import CanvasCamera
from .canvas_renderer import CanvasRenderer
from .preview import load_definition_image
from ..services.terrain_painting_service import TerrainPaintingService
from ..services.localization import Translator


class MapCanvas(QWidget):
    """Visual host for the authored map editor.

    Qt events are translated to semantic input and delegated to the
    interaction/editing layers. Rendering and viewport math live in their
    respective collaborators; this widget owns presentation state and
    compatibility shims for the existing MainWindow API.
    """

    selection_changed = Signal(object)
    document_changed = Signal()
    status_changed = Signal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setMinimumSize(160, 120)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setAcceptDrops(True)
        self.document: MapDocument | None = None
        self.workspace: ContentWorkspace | None = None
        self.asset_root: Path | None = None
        self.translate = Translator()
        self.camera = CanvasCamera()
        self.selection_controller = SelectionController(self._selection_changed)
        self.editing = MapEditingService()
        self.interaction = InteractionController(self.editing, self.selection_controller, status=self._set_status, message=self.translate)
        self.terrain_painter = TerrainPaintingService(editing=self.editing)
        self.interaction.set_terrain_painter(self.terrain_painter)
        self.renderer = CanvasRenderer(self.camera, self.selection_controller)
        # Kept as a compatibility alias for integrations that used the old
        # widget-owned image cache.  Rendering ownership now lives in
        # CanvasRenderer.
        self.assets = self.renderer.assets
        self.tool = "select"
        self.selected_entity_category = ""
        self.selected_definition_id = ""
        self.selected_entity: tuple[str, object] | None = None
        self.selected_tile: tuple[str, int, int] | None = None
        self.selected_brush: list[tuple[int, int, str, int, int]] = []
        self.selected_stamp_id = ""
        self.selected_stamp = ""
        self.layer_index = 0
        self.grid_visible = True
        self._moving: Selection | None = None
        self._space_down = False
        self._middle_pan = False
        self._last_pan_point = QPoint()
        self._tile_selection_start: tuple[int, int] | None = None

    @property
    def zoom(self) -> float:
        return self.camera.zoom

    @zoom.setter
    def zoom(self, value: float) -> None:
        self.camera.zoom = value

    @property
    def pan(self) -> QPoint:
        return QPoint(self.camera.pan_x, self.camera.pan_y)

    @pan.setter
    def pan(self, value: QPoint) -> None:
        self.camera.set_pan(value.x(), value.y())

    def set_context(self, document: MapDocument | None, workspace: ContentWorkspace | None, asset_root: Path | None) -> None:
        changed = document is not self.document
        self.document = document
        self.workspace = workspace
        self.asset_root = asset_root
        self.editing.set_document(document)
        self.editing.set_workspace(workspace)
        self.interaction.set_workspace(workspace)
        self.terrain_painter.set_context(document, workspace)
        self.renderer.set_context(document, workspace, asset_root)
        if changed:
            self.layer_index = 0
            self.selection_controller.clear()
            self._moving = None
        self.editing.set_layer(self.layer_index)
        self.renderer.preview_world = None
        self.renderer.pointer_tile = None
        self.update()

    def set_translator(self, translator: Translator) -> None:
        self.translate = translator
        self.interaction.message = translator

    def set_tool(self, tool: str) -> None:
        self.tool = tool
        collision_tools = {"collision", "collision_erase", "collision_rectangle", "collision_rectangle_erase", "collision_fill", "collision_fill_erase"}
        self.interaction.set_collision_overlay(tool in collision_tools)
        self.interaction.set_collision_fill_solid(tool != "collision_fill_erase")
        if tool not in {"terrain", "room"}:
            self.interaction.set_terrain_selection(None)
            self.interaction.set_room_profile(None)
        if tool == "spawn":
            self.interaction.set_active_payload(StudioDragPayload.map_element_payload("player_spawn"))
        elif tool == "region":
            self.interaction.set_active_payload(StudioDragPayload.map_element_payload("region"))
        elif tool in {"link", "transition"}:
            self.interaction.set_active_payload(StudioDragPayload.map_element_payload("map_transition"))
        elif tool == "select":
            self.interaction.set_active_payload(None)
        if tool not in {"entity", "spawn", "region", "link", "transition"}:
            self.renderer.preview_world = None
        if tool != "entity":
            self.selected_entity_category = ""
            self.selected_definition_id = ""
        self.update()

    def set_grid_visible(self, visible: bool) -> None:
        self.grid_visible = visible
        self.renderer.set_grid_visible(visible)
        self.update()

    def set_snap_enabled(self, enabled: bool) -> None:
        self.camera.snap_enabled = enabled
        self.interaction.snap_enabled = enabled

    def set_collision_overlay(self, enabled: bool) -> None:
        self.interaction.set_collision_overlay(enabled)
        self.update()

    def set_layer(self, index: int) -> None:
        if self.document and 0 <= index < len(self.document.layers):
            self.layer_index = index
            self.editing.set_layer(index)
            self.update()

    def set_entity_selection(self, category: str, definition_id: str) -> None:
        self.selected_entity_category = category
        self.selected_definition_id = definition_id
        self.tool = "entity"
        self.interaction.set_active_payload(StudioDragPayload.content(category, definition_id))
        self.renderer.preview_world = None
        self.setFocus()
        self.update()

    def set_active_payload(self, payload: StudioDragPayload | None) -> None:
        self.interaction.set_active_payload(payload)
        self.interaction.set_terrain_selection(None)
        self.interaction.set_room_profile(None)
        if payload and payload.content_reference:
            self.selected_entity_category = payload.category
            self.selected_definition_id = payload.definition_id
            self.tool = "entity"
        elif payload and payload.kind == "MapElement":
            self.tool = "map_element"
        elif payload and payload.kind in {"Tile", "TileBrush"}:
            self.tool = "paint"
        elif payload and payload.kind == "Stamp":
            self.selected_stamp_id = payload.definition_id
            self.selected_stamp = payload.definition_id
            self.tool = "stamp"
        elif payload is None:
            self.tool = "select"
        self.update()

    def set_brush(self, tileset_id: str, source_indices: list[int], flags: int = 0) -> None:
        self.interaction.set_terrain_selection(None)
        self.interaction.set_room_profile(None)
        self.interaction.set_collision_overlay(False)
        self.selected_brush = []
        if not source_indices:
            self.interaction.set_active_payload(None)
            return
        columns = 1
        if self.workspace:
            tileset = self.workspace.find("tilesets", tileset_id)
            if tileset:
                columns = max(1, int(tileset.data.get("columns", 1)))
        coordinates = [(index % columns, index // columns, index) for index in source_indices]
        min_x = min(value[0] for value in coordinates); min_y = min(value[1] for value in coordinates)
        self.selected_brush = [(x - min_x, y - min_y, tileset_id, index, flags) for x, y, index in coordinates]
        self.selected_tile = (tileset_id, source_indices[0], flags)
        self.tool = "paint"
        self.interaction.set_active_payload(StudioDragPayload.tile_brush(tileset_id, source_indices, flags))
        self.update()

    def set_terrain_selection(self, selection: TerrainSelection | None) -> None:
        if selection is None:
            self.interaction.set_terrain_selection(None)
            return
        self.tool = "terrain"
        self.interaction.set_collision_overlay(False)
        self.selected_entity_category = ""
        self.selected_definition_id = ""
        self.interaction.set_terrain_selection(selection)
        self.renderer.preview_world = None
        self.setFocus(); self.update()

    def set_room_profile(self, profile: TerrainProfile | None) -> None:
        if profile is None:
            self.interaction.set_room_profile(None)
            return
        self.tool = "room"
        self.interaction.set_collision_overlay(False)
        self.selected_entity_category = ""
        self.selected_definition_id = ""
        self.interaction.set_room_profile(profile)
        self.renderer.preview_world = None
        self.setFocus(); self.update()

    def set_stamp_selection(self, stamp_id: str) -> None:
        self.selected_stamp_id = stamp_id
        self.selected_stamp = stamp_id
        self.tool = "stamp"
        self.interaction.set_active_payload(StudioDragPayload("Stamp", definition_id=stamp_id))
        self.update()

    def cancel_placement(self) -> None:
        if self.tool in {"entity", "spawn", "region", "link", "transition"}:
            self.tool = "select"
            self.selected_entity_category = ""
            self.selected_definition_id = ""
            self.interaction.set_active_payload(None)
        self.renderer.preview_world = None
        self.renderer.rectangle_start = None
        self.update()

    def fit_map(self) -> None:
        if not self.document or self.document.width <= 0 or self.document.height <= 0:
            return
        self.camera.fit(self.document.width * self.document.tile_size, self.document.height * self.document.tile_size, self.width(), self.height())
        self.update()

    def world_to_screen(self, x: int, y: int) -> QPoint:
        if not self.document:
            return QPoint()
        return QPoint(*self.camera.world_to_screen(x, y, self.document.width * self.document.tile_size,
                                                    self.document.height * self.document.tile_size, self.width(), self.height()))

    def screen_to_world(self, point: QPoint) -> tuple[int, int]:
        if not self.document:
            return 0, 0
        return self.camera.screen_to_world(point.x(), point.y(), self.document.width * self.document.tile_size,
                                           self.document.height * self.document.tile_size, self.width(), self.height())

    def tile_at(self, point: QPoint) -> tuple[int, int]:
        x, y = self.screen_to_world(point)
        return (x // self.document.tile_size, y // self.document.tile_size) if self.document else (0, 0)

    def paintEvent(self, event: object) -> None:
        del event
        painter = QPainter(self)
        self.renderer.render(painter, self.width(), self.height())
        painter.end()

    def mousePressEvent(self, event: QMouseEvent) -> None:
        point = event.position().toPoint()
        if event.button() == Qt.MouseButton.MiddleButton or (event.button() == Qt.MouseButton.LeftButton and self._space_down):
            self._middle_pan = True
            self._last_pan_point = point
            return
        if not self.document:
            return
        button = self._button_name(event.button())
        modifiers = self._modifiers(event.modifiers())
        tile = self.tile_at(point)
        world = self._snap_world(self.screen_to_world(point))
        if self.tool == "select" and button == "left":
            self._moving = self._hit_selection(self.screen_to_world(point))
            self.selection_controller.select_value(self._moving)
            self.update()
            return
        if self.tool == "eyedropper" and button == "left":
            self._pick_tile(tile)
            return
        if self.tool == "tile_selection" and button == "left":
            self._tile_selection_start = tile
            self.renderer.rectangle_start = tile
            return
        if self.tool == "stamp" and button == "left":
            self._place_stamp(tile)
            return
        if self.tool in {"erase", "collision_erase"} and button == "left":
            button = "right"
        if self.tool in {"fill", "collision_fill", "collision_fill_erase"} and button == "left":
            modifiers = frozenset((*modifiers, "ctrl"))
        if self.tool in {"rectangle", "collision_rectangle", "collision_rectangle_erase"} and button == "left":
            modifiers = frozenset((*modifiers, "shift"))
        if self.tool == "room" and button == "left":
            modifiers = frozenset((*modifiers, "shift"))
        result = self.interaction.press(button, tile, world, modifiers)
        self._apply_result(result)
        if "shift" in modifiers:
            self.renderer.rectangle_start = tile
        self.renderer.pointer_tile = tile
        self.update()

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        point = event.position().toPoint()
        if self._middle_pan:
            delta = point - self._last_pan_point
            self.camera.pan_by(delta.x(), delta.y())
            self._last_pan_point = point
            self.update()
            return
        if not self.document:
            return
        tile = self.tile_at(point)
        self.renderer.pointer_tile = tile
        world = self._snap_world(self.screen_to_world(point))
        payload = self.interaction.active_payload
        if payload and payload.kind in {"ContentDefinition", "Reference", "MapElement"}:
            self.renderer.preview_world = world
            self.renderer.preview_image = self._payload_image(payload)
        modifiers = self._modifiers(event.modifiers())
        buttons = frozenset(self._button_name(button) for button in (Qt.MouseButton.LeftButton, Qt.MouseButton.RightButton) if event.buttons() & button)
        if self.tool in {"erase", "collision_erase"} and "left" in buttons:
            buttons = frozenset({"right", *[value for value in buttons if value != "left"]})
        if self.tool in {"rectangle", "collision_rectangle", "collision_rectangle_erase"}:
            modifiers = frozenset((*modifiers, "shift"))
        if self.tool == "room":
            modifiers = frozenset((*modifiers, "shift"))
        result = self.interaction.move(buttons, tile, modifiers)
        self._apply_result(result)
        self.update()

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        point = event.position().toPoint()
        if event.button() == Qt.MouseButton.MiddleButton or self._middle_pan:
            self._middle_pan = False
            return
        if not self.document:
            return
        tile = self.tile_at(point)
        modifiers = self._modifiers(event.modifiers())
        button = self._button_name(event.button())
        if self.tool in {"collision_rectangle_erase"} and button == "left":
            button = "right"
        if self.tool in {"rectangle", "collision_rectangle", "collision_rectangle_erase"}:
            modifiers = frozenset((*modifiers, "shift"))
        result = self.interaction.release(button, tile, modifiers)
        self._apply_result(result)
        if self._tile_selection_start is not None:
            self._select_tile_rectangle(self._tile_selection_start, tile)
            self._tile_selection_start = None
        if self._moving and event.button() == Qt.MouseButton.LeftButton:
            self._move_selection(self._snap_world(self.screen_to_world(point)))
            self._moving = None
        self.renderer.rectangle_start = None
        self.renderer.pointer_tile = None
        if button == "left" and self.tool not in {"entity", "spawn", "region", "link", "transition"}:
            self.renderer.preview_world = None
        self.update()

    def wheelEvent(self, event: QWheelEvent) -> None:
        self.camera.zoom_by(1.15 if event.angleDelta().y() > 0 else 1 / 1.15)
        self.update()

    def keyPressEvent(self, event: QKeyEvent) -> None:
        if event.key() == Qt.Key.Key_Space:
            self._space_down = True
            return
        if event.key() == Qt.Key.Key_Escape:
            self.cancel_placement()
        elif event.key() == Qt.Key.Key_Delete:
            self.delete_selection()
        else:
            super().keyPressEvent(event)

    def delete_selection(self) -> bool:
        selection = self.selection_controller.current
        if selection is None or self.document is None:
            return False
        try:
            if selection.category in ENTITY_CATEGORIES:
                self.editing.delete_entity(selection.category, int(selection.identifier))
            else:
                self.editing.delete_map_element(selection.category, selection.identifier)
        except (TypeError, ValueError):
            return False
        self.selection_controller.clear()
        self.document_changed.emit()
        self.update()
        return True

    def keyReleaseEvent(self, event: QKeyEvent) -> None:
        if event.key() == Qt.Key.Key_Space:
            self._space_down = False
        else:
            super().keyReleaseEvent(event)

    def dragEnterEvent(self, event: QDragEnterEvent) -> None:
        payload = StudioDragPayload.from_mime_data(event.mimeData())
        if payload and payload.kind in {"ContentDefinition", "Reference", "MapElement", "Tile", "TileBrush", "Stamp"}:
            event.acceptProposedAction()
        else:
            event.ignore()

    def dragMoveEvent(self, event: QDragMoveEvent) -> None:
        payload = StudioDragPayload.from_mime_data(event.mimeData())
        if payload and self.document:
            world = self._snap_world(self.screen_to_world(event.position().toPoint()))
            self.renderer.preview_world = world
            self.renderer.preview_image = self._payload_image(payload)
            self.update()
            event.acceptProposedAction()
        else:
            event.ignore()

    def dropEvent(self, event: QDropEvent) -> None:
        payload = StudioDragPayload.from_mime_data(event.mimeData())
        if payload and self.document:
            world = self._snap_world(self.screen_to_world(event.position().toPoint()))
            result = self.interaction.drop(payload, world)
            self._apply_result(result)
            if payload.content_reference:
                self.selected_entity_category = payload.category
                self.selected_definition_id = payload.definition_id
            event.acceptProposedAction()
            self.renderer.preview_world = None
            self.update()
        else:
            event.ignore()

    def leaveEvent(self, event: object) -> None:
        del event
        self.renderer.preview_world = None
        self.update()

    def _selection_changed(self, selection: Selection | None) -> None:
        self.selected_entity = selection.as_tuple() if selection else None
        self.selection_changed.emit(self.selected_entity)

    def _apply_result(self, result: InteractionResult) -> None:
        if result.status:
            self._set_status(result.status)
        if result.changed:
            self.document_changed.emit()

    def _set_status(self, message: str) -> None:
        self.status_changed.emit(message)

    @staticmethod
    def _button_name(button: Qt.MouseButton) -> str:
        return {Qt.MouseButton.LeftButton: "left", Qt.MouseButton.RightButton: "right", Qt.MouseButton.MiddleButton: "middle"}.get(button, "")

    @staticmethod
    def _modifiers(modifiers: Qt.KeyboardModifiers) -> frozenset[str]:
        values: set[str] = set()
        if modifiers & Qt.KeyboardModifier.ShiftModifier:
            values.add("shift")
        if modifiers & Qt.KeyboardModifier.ControlModifier:
            values.add("ctrl")
        if modifiers & Qt.KeyboardModifier.AltModifier:
            values.add("alt")
        return frozenset(values)

    def _snap_world(self, world: tuple[int, int]) -> tuple[int, int]:
        if not self.document or not self.camera.snap_enabled:
            return world
        size = self.document.tile_size
        return round(world[0] / size) * size, round(world[1] / size) * size

    def _payload_image(self, payload: StudioDragPayload):
        if not self.workspace or not payload.content_reference:
            return None
        definition = self.workspace.find(payload.category, payload.definition_id)
        return load_definition_image(definition, self.workspace, self.asset_root) if definition else None

    def _hit_selection(self, world: tuple[int, int]) -> Selection | None:
        if not self.document:
            return None
        best: Selection | None = None
        best_distance = max(12, int(self.document.tile_size * 0.75)) ** 2
        for category in ENTITY_CATEGORIES:
            for value in self.document.data.get(category, []):
                if not isinstance(value, dict) or not isinstance(value.get("position"), dict):
                    continue
                position = value["position"]
                distance = (int(position.get("x", 0)) - world[0]) ** 2 + (int(position.get("y", 0)) - world[1]) ** 2
                if distance <= best_distance:
                    best_distance = distance; best = Selection(category, value.get("id", 0))
        for category in ("playerSpawns", "regions", "links"):
            for value in self.document.data.get(category, []) if isinstance(self.document.data.get(category), list) else []:
                if not isinstance(value, dict):
                    continue
                bounds = value.get("position") if category == "playerSpawns" else value.get("bounds") if category == "regions" else value.get("trigger")
                if category == "playerSpawns" and isinstance(bounds, dict):
                    distance = (int(bounds.get("x", 0)) - world[0]) ** 2 + (int(bounds.get("y", 0)) - world[1]) ** 2
                    if distance <= best_distance:
                        best_distance = distance; best = Selection(category, value.get("id", ""))
                elif isinstance(bounds, dict) and int(bounds.get("x", 0)) <= world[0] < int(bounds.get("x", 0)) + int(bounds.get("width", 0)) and int(bounds.get("y", 0)) <= world[1] < int(bounds.get("y", 0)) + int(bounds.get("height", 0)):
                    best = Selection(category, value.get("id", ""))
        return best

    def _move_selection(self, world: tuple[int, int]) -> None:
        if not self._moving:
            return
        try:
            if self._moving.category in ENTITY_CATEGORIES:
                self.editing.move_entity(self._moving.category, int(self._moving.identifier), *world)
            else:
                self.editing.move_map_element(self._moving.category, self._moving.identifier, *world)
            self.document_changed.emit()
        except (TypeError, ValueError):
            pass

    def _pick_tile(self, tile: tuple[int, int]) -> None:
        if not self.document or not (0 <= tile[0] < self.document.width and 0 <= tile[1] < self.document.height):
            return
        cells = self.document.layers[self.layer_index].get("cells", []); references = self.document.data.get("tileReferences", [])
        index = cells[tile[1] * self.document.width + tile[0]] if isinstance(cells, list) else None
        if not isinstance(index, int) or not isinstance(references, list) or index >= len(references) or not isinstance(references[index], dict):
            self._set_status("No tile at this position")
            return
        reference = references[index]
        if isinstance(reference.get("tilesetId"), str):
            self.set_brush(str(reference["tilesetId"]), [int(reference.get("sourceIndex", 0))], int(reference.get("flags", 0)))
            self._set_status("Tile selected from map")

    def _select_tile_rectangle(self, start: tuple[int, int], end: tuple[int, int]) -> None:
        if not self.document:
            return
        cells = self.document.layers[self.layer_index].get("cells", []); references = self.document.data.get("tileReferences", [])
        if not isinstance(cells, list) or not isinstance(references, list):
            return
        left, top = min(start[0], end[0]), min(start[1], end[1])
        brush: list[tuple[int, int, str, int, int]] = []
        for y in range(top, max(start[1], end[1]) + 1):
            for x in range(left, max(start[0], end[0]) + 1):
                if not (0 <= x < self.document.width and 0 <= y < self.document.height):
                    continue
                value = cells[y * self.document.width + x]
                if isinstance(value, int) and value < len(references) and isinstance(references[value], dict) and isinstance(references[value].get("tilesetId"), str):
                    reference = references[value]
                    brush.append((x - left, y - top, str(reference["tilesetId"]), int(reference.get("sourceIndex", 0)), int(reference.get("flags", 0))))
        if brush:
            self.selected_brush = brush
            self.selected_tile = (brush[0][2], brush[0][3], brush[0][4])
            self.interaction.set_active_payload(StudioDragPayload.tile_brush(brush[0][2], [item[3] for item in brush], brush[0][4]))
            self._set_status(f"Selected {len(brush)} tile(s)")

    def _place_stamp(self, tile: tuple[int, int]) -> None:
        if not self.document or not self.workspace or not self.selected_stamp_id:
            return
        stamp = self.workspace.find("stamps", self.selected_stamp_id)
        if not stamp or not isinstance(stamp.data.get("cells"), list):
            self._set_status("Stamp is unavailable")
            return
        pattern: list[tuple[int, int, str, int, int]] = []
        for cell in stamp.data["cells"]:
            if not isinstance(cell, dict):
                continue
            semantic = self.workspace.find("tileSemantics", str(cell.get("tileId", "")))
            if semantic and isinstance(semantic.data.get("tilesetId"), str):
                pattern.append((int(cell.get("x", 0)), int(cell.get("y", 0)), str(semantic.data["tilesetId"]), int(semantic.data.get("sourceIndex", 0)), 0))
        if pattern:
            self.editing.place_stamp(tile, pattern)
            self.document_changed.emit()
