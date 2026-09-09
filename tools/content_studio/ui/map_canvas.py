from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QPoint, Qt, Signal
from PySide6.QtGui import QColor, QImage, QPainter, QPen
from PySide6.QtWidgets import QWidget

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import ENTITY_CATEGORIES, MapDocument
from ..services.assets import AssetCatalog
from .preview import load_definition_image


class MapCanvas(QWidget):
    selection_changed = Signal(object)
    document_changed = Signal()
    status_changed = Signal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setMinimumSize(400, 300)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.document: MapDocument | None = None
        self.workspace: ContentWorkspace | None = None
        self.asset_root: Path | None = None
        self.assets = AssetCatalog()
        self.tool = "select"
        self.selected_entity_category = ""
        self.selected_definition_id = ""
        self.selected_entity: tuple[str, int] | None = None
        self.selected_tile: tuple[str, int, int] | None = None
        self.layer_index = 0
        self.selected_stamp: str = ""
        self.grid_visible = True
        self.zoom = 1.0
        self.pan = QPoint(0, 0)
        self._drag_start: tuple[int, int] | None = None
        self._drag_last: tuple[int, int] | None = None
        self._region_start: tuple[int, int] | None = None
        self._rectangle_start: tuple[int, int] | None = None
        self._moving: tuple[str, int] | None = None
        self._preview: tuple[int, int] | None = None

    def set_context(self, document: MapDocument | None, workspace: ContentWorkspace | None, asset_root: Path | None) -> None:
        self.document = document
        self.workspace = workspace
        self.asset_root = asset_root
        self.assets.refresh(asset_root, workspace.root if workspace else None)
        self.selected_entity = None
        self._preview = None
        self.update()

    def set_tool(self, tool: str) -> None:
        self.tool = tool
        if tool != "entity":
            self._preview = None
        self.update()

    def set_grid_visible(self, visible: bool) -> None:
        self.grid_visible = visible
        self.update()

    def set_layer(self, index: int) -> None:
        if self.document and 0 <= index < len(self.document.layers):
            self.layer_index = index
            self.update()

    def set_entity_selection(self, category: str, definition_id: str) -> None:
        self.selected_entity_category = category
        self.selected_definition_id = definition_id
        self.tool = "entity"
        self._preview = None
        self.setFocus()
        self.update()

    def cancel_placement(self) -> None:
        if self.tool in {"entity", "spawn"}:
            self.tool = "select"
        self._preview = None
        self._drag_start = None
        self._rectangle_start = None
        self.update()

    def fit_map(self) -> None:
        if not self.document or self.document.width <= 0 or self.document.height <= 0:
            return
        map_width = self.document.width * self.document.tile_size
        map_height = self.document.height * self.document.tile_size
        self.zoom = max(0.25, min(4.0, min(self.width() / map_width, self.height() / map_height) * 0.92))
        self.pan = QPoint(0, 0)
        self.update()

    def world_to_screen(self, x: int, y: int) -> QPoint:
        if not self.document:
            return QPoint()
        width = self.document.width * self.document.tile_size
        height = self.document.height * self.document.tile_size
        origin_x = (self.width() - width * self.zoom) / 2 + self.pan.x()
        origin_y = (self.height() - height * self.zoom) / 2 + self.pan.y()
        return QPoint(round(origin_x + x * self.zoom), round(origin_y + y * self.zoom))

    def screen_to_world(self, point: QPoint) -> tuple[int, int]:
        if not self.document:
            return 0, 0
        width = self.document.width * self.document.tile_size
        height = self.document.height * self.document.tile_size
        origin_x = (self.width() - width * self.zoom) / 2 + self.pan.x()
        origin_y = (self.height() - height * self.zoom) / 2 + self.pan.y()
        return round((point.x() - origin_x) / self.zoom), round((point.y() - origin_y) / self.zoom)

    def tile_at(self, point: QPoint) -> tuple[int, int]:
        x, y = self.screen_to_world(point)
        if not self.document:
            return 0, 0
        return x // self.document.tile_size, y // self.document.tile_size

    def paintEvent(self, unused_event: object) -> None:
        del unused_event
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#20252b"))
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        if not self.document:
            painter.setPen(QColor("#b8c2cc"))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "No map selected")
            painter.end()
            return
        map_pixel_width = self.document.width * self.document.tile_size
        map_pixel_height = self.document.height * self.document.tile_size
        origin = self.world_to_screen(0, 0)
        destination = self.world_to_screen(map_pixel_width, map_pixel_height)
        painter.fillRect(origin.x(), origin.y(), destination.x() - origin.x(), destination.y() - origin.y(), QColor("#323b42"))
        self._draw_tiles(painter, origin)
        self._draw_collision(painter, origin)
        self._draw_entities(painter)
        self._draw_spawns(painter)
        self._draw_regions(painter)
        if self._preview is not None:
            preview = self.world_to_screen(self._preview[0], self._preview[1])
            size = max(2, round(self.document.tile_size * self.zoom))
            ghost = self._selected_entity_image()
            if ghost is not None:
                ghost = ghost.scaled(max(size, ghost.width()), max(size, ghost.height()),
                                     Qt.AspectRatioMode.KeepAspectRatio,
                                     Qt.TransformationMode.FastTransformation)
                painter.setOpacity(0.55)
                painter.drawImage(preview.x() - ghost.width() // 2, preview.y() - ghost.height() // 2, ghost)
                painter.setOpacity(1.0)
            else:
                painter.fillRect(preview.x() - size // 2, preview.y() - size // 2, size, size, QColor(95, 220, 120, 105))
            painter.setPen(QPen(QColor("#8ff0a4"), 2))
            painter.drawRect(preview.x() - size // 2, preview.y() - size // 2, size, size)
        if self.grid_visible:
            self._draw_grid(painter, origin)
        painter.end()

    def _draw_tiles(self, painter: QPainter, origin: QPoint) -> None:
        assert self.document is not None
        tile_size = self.document.tile_size
        for layer_index, layer in enumerate(self.document.layers):
            if not layer.get("visible", True):
                continue
            cells = layer.get("cells", [])
            if not isinstance(cells, list):
                continue
            alpha = max(70, 210 - layer_index * 25)
            for y in range(self.document.height):
                for x in range(self.document.width):
                    index = cells[y * self.document.width + x] if y * self.document.width + x < len(cells) else None
                    if index is None or not isinstance(index, int):
                        continue
                    reference = self.document.data.get("tileReferences", [])[index] if index < len(self.document.data.get("tileReferences", [])) else None
                    if not isinstance(reference, dict):
                        continue
                    color = QColor.fromHsv((index * 47 + layer_index * 83) % 360, 110, 185, alpha)
                    image = self._tile_image(reference, index)
                    target = self.world_to_screen(x * tile_size, y * tile_size)
                    size = max(1, round(tile_size * self.zoom))
                    if image is None:
                        painter.fillRect(target.x(), target.y(), size, size, color)
                    else:
                        painter.drawImage(target.x(), target.y(), image.scaled(size, size, Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.FastTransformation))

    def _tile_image(self, reference: dict[str, object], index: int) -> QImage | None:
        if not self.workspace:
            return None
        tileset_id = reference.get("tilesetId")
        if not isinstance(tileset_id, str):
            return None
        tileset = self.workspace.find("tilesets", tileset_id)
        if not tileset:
            return None
        relative = tileset.data.get("relativeAssetPath")
        if not isinstance(relative, str):
            return None
        root = self.asset_root if tileset.data.get("root", "gameAssets") == "gameAssets" else self.workspace.root
        image = QImage(str(root / relative))
        if image.isNull():
            return None
        columns = max(1, int(tileset.data.get("columns", 1)))
        source_index = int(reference.get("sourceIndex", index))
        tile_size = int(tileset.data.get("tileSize", self.document.tile_size if self.document else 16))
        source = image.copy((source_index % columns) * tile_size, (source_index // columns) * tile_size, tile_size, tile_size)
        if int(reference.get("flags", 0)) & 1:
            return source.mirrored(True, False)
        return source

    def _draw_collision(self, painter: QPainter, origin: QPoint) -> None:
        assert self.document is not None
        collision = self.document.data.get("collision", [])
        if not isinstance(collision, list):
            return
        size = max(1, round(self.document.tile_size * self.zoom))
        for y in range(self.document.height):
            for x in range(self.document.width):
                if y * self.document.width + x < len(collision) and collision[y * self.document.width + x]:
                    point = self.world_to_screen(x * self.document.tile_size, y * self.document.tile_size)
                    painter.fillRect(point.x(), point.y(), size, size, QColor(220, 70, 70, 80))

    def _draw_entities(self, painter: QPainter) -> None:
        assert self.document is not None
        colors = {"enemies": QColor("#ed6a5a"), "npcs": QColor("#6ac5ed"), "objects": QColor("#edc35a"), "pickups": QColor("#9be564")}
        for category in ENTITY_CATEGORIES:
            values = self.document.data.get(category, [])
            if not isinstance(values, list):
                continue
            for value in values:
                if not isinstance(value, dict) or not isinstance(value.get("position"), dict):
                    continue
                position = value["position"]
                x = int(position.get("x", 0)); y = int(position.get("y", 0))
                point = self.world_to_screen(x, y)
                radius = max(3, round(5 * self.zoom))
                color = colors[category]
                if self.selected_entity == (category, int(value.get("id", -1))):
                    painter.setPen(QPen(QColor("white"), 2))
                    painter.drawEllipse(point, radius + 3, radius + 3)
                painter.setBrush(color)
                painter.setPen(QPen(color.darker(140), 1))
                painter.drawEllipse(point, radius, radius)
                painter.setPen(QColor("#f5f5f5"))
                painter.drawText(point + QPoint(radius + 3, 4), str(value.get("definitionId", "")))

    def _draw_spawns(self, painter: QPainter) -> None:
        assert self.document is not None
        spawns = self.document.data.get("playerSpawns", [])
        if not isinstance(spawns, list):
            return
        painter.setPen(QPen(QColor("#ffffff"), 2))
        for value in spawns:
            if not isinstance(value, dict) or not isinstance(value.get("position"), dict):
                continue
            position = value["position"]
            point = self.world_to_screen(int(position.get("x", 0)), int(position.get("y", 0)))
            radius = max(5, round(7 * self.zoom))
            painter.drawLine(point.x() - radius, point.y(), point.x() + radius, point.y())
            painter.drawLine(point.x(), point.y() - radius, point.x(), point.y() + radius)
            painter.drawText(point + QPoint(radius + 3, 4), str(value.get("id", "spawn")))

    def _selected_entity_image(self) -> QImage | None:
        if not self.workspace or not self.selected_definition_id:
            return None
        definition = self.workspace.find(self.selected_entity_category, self.selected_definition_id)
        return load_definition_image(definition, self.workspace, self.asset_root) if definition else None

    def _draw_regions(self, painter: QPainter) -> None:
        assert self.document is not None
        regions = self.document.data.get("regions", [])
        if not isinstance(regions, list):
            return
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.setPen(QPen(QColor("#c084fc"), 2, Qt.PenStyle.DashLine))
        for region in regions:
            if not isinstance(region, dict) or not isinstance(region.get("bounds"), dict):
                continue
            bounds = region["bounds"]
            start = self.world_to_screen(int(bounds.get("x", 0)), int(bounds.get("y", 0)))
            end = self.world_to_screen(int(bounds.get("x", 0)) + int(bounds.get("width", 0)), int(bounds.get("y", 0)) + int(bounds.get("height", 0)))
            painter.drawRect(start.x(), start.y(), end.x() - start.x(), end.y() - start.y())

    def _draw_grid(self, painter: QPainter, origin: QPoint) -> None:
        assert self.document is not None
        size = max(1, round(self.document.tile_size * self.zoom))
        if size < 4:
            return
        painter.setPen(QPen(QColor(255, 255, 255, 28), 1))
        for x in range(self.document.width + 1):
            point = self.world_to_screen(x * self.document.tile_size, 0)
            painter.drawLine(point.x(), origin.y(), point.x(), self.world_to_screen(0, self.document.height * self.document.tile_size).y())
        for y in range(self.document.height + 1):
            point = self.world_to_screen(0, y * self.document.tile_size)
            painter.drawLine(origin.x(), point.y(), self.world_to_screen(self.document.width * self.document.tile_size, 0).x(), point.y())

    def mousePressEvent(self, event: object) -> None:
        if not self.document or not hasattr(event, "button"):
            return
        mouse = event  # type: ignore[assignment]
        if mouse.button() == Qt.MouseButton.MiddleButton:
            self._drag_last = mouse.position().toPoint()
            return
        if mouse.button() != Qt.MouseButton.LeftButton:
            return
        tile = self.tile_at(mouse.position().toPoint())
        world = self.screen_to_world(mouse.position().toPoint())
        self._drag_start = tile
        self._drag_last = mouse.position().toPoint()
        if self.tool == "entity" and self.selected_definition_id:
            position = self._snap_world(world)
            self.document.add_entity(self.selected_entity_category, self.selected_definition_id, *position)
            self._preview = position
            self.document_changed.emit()
            self.status_changed.emit(f"Placed {self.selected_definition_id}; click again or Escape to finish")
        elif self.tool == "spawn":
            self.document.add_player_spawn(self.document.next_spawn_id(), *self._snap_world(world))
            self.document_changed.emit()
            self.status_changed.emit("Player spawn placed")
        elif self.tool == "select":
            self.selected_entity = self._hit_entity(world)
            self._moving = self.selected_entity
            self.selection_changed.emit(self.selected_entity)
            self.update()
        elif self.tool in {"pencil", "erase"}:
            self._paint_tile(tile)
        elif self.tool in {"rectangle", "fill"}:
            self._rectangle_start = tile
            if self.tool == "fill":
                self._fill(tile)
        elif self.tool == "collision":
            self.document.set_collision([tile], True)
            self.document_changed.emit(); self.update()
        elif self.tool == "region":
            self._region_start = world
        elif self.tool == "pan":
            self._drag_last = mouse.position().toPoint()

    def mouseMoveEvent(self, event: object) -> None:
        if not self.document or not hasattr(event, "position"):
            return
        mouse = event  # type: ignore[assignment]
        point = mouse.position().toPoint()
        world = self.screen_to_world(point)
        if self.tool == "entity" and self.selected_definition_id:
            self._preview = self._snap_world(world); self.update()
        if self._drag_last is not None and (mouse.buttons() & Qt.MouseButton.MiddleButton or self.tool == "pan"):
            delta = point - self._drag_last
            self.pan += delta
            self._drag_last = point
            self.update(); return
        if self._drag_start is None or not (mouse.buttons() & Qt.MouseButton.LeftButton):
            return
        tile = self.tile_at(point)
        if self.tool in {"pencil", "erase"} and tile != self._drag_start:
            self._paint_tile(tile); self._drag_start = tile
        elif self.tool == "collision" and tile != self._drag_start:
            self.document.set_collision([tile], True); self._drag_start = tile; self.document_changed.emit(); self.update()
        elif self.tool == "rectangle":
            self.update()

    def mouseReleaseEvent(self, event: object) -> None:
        if not self.document or not hasattr(event, "button"):
            return
        mouse = event  # type: ignore[assignment]
        if mouse.button() != Qt.MouseButton.LeftButton:
            self._drag_last = None
            return
        if self._region_start is not None:
            end = self.screen_to_world(mouse.position().toPoint())
            start = self._region_start
            x = min(start[0], end[0]); y = min(start[1], end[1])
            self.document.add_region(f"region.{len(self.document.data.get('regions', [])) + 1}", x, y, max(1, abs(end[0] - start[0])), max(1, abs(end[1] - start[1])))
            self._region_start = None; self.document_changed.emit(); self.update()
        if self._rectangle_start is not None and self.tool == "rectangle":
            end = self.tile_at(mouse.position().toPoint())
            start = self._rectangle_start
            cells = [(x, y) for y in range(min(start[1], end[1]), max(start[1], end[1]) + 1)
                     for x in range(min(start[0], end[0]), max(start[0], end[0]) + 1)]
            if self.selected_tile:
                self.document.set_tiles(self.layer_index, cells, self.selected_tile[0], self.selected_tile[1], self.selected_tile[2])
                self.document_changed.emit(); self.update()
            else:
                self.status_changed.emit("Select a tileset tile first")
            self._rectangle_start = None
        if self._moving is not None:
            category, identifier = self._moving
            world = self._snap_world(self.screen_to_world(mouse.position().toPoint()))
            try:
                self.document.move_entity(category, identifier, *world)
                self.document_changed.emit()
            except ValueError:
                pass
            self._moving = None
        self._drag_start = None

    def wheelEvent(self, event: object) -> None:
        if not hasattr(event, "angleDelta"):
            return
        wheel = event  # type: ignore[assignment]
        old = self.zoom
        self.zoom = max(0.25, min(8.0, old * (1.15 if wheel.angleDelta().y() > 0 else 1 / 1.15)))
        self.update()

    def keyPressEvent(self, event: object) -> None:
        if hasattr(event, "key") and event.key() == Qt.Key.Key_Escape:
            self.cancel_placement()
        elif hasattr(event, "key") and event.key() == Qt.Key.Key_Delete and self.selected_entity:
            category, identifier = self.selected_entity
            self.document.delete_entity(category, identifier)
            self.selected_entity = None
            self.selection_changed.emit(None)
            self.document_changed.emit()
            self.update()
        else:
            super().keyPressEvent(event)  # type: ignore[arg-type]

    def _paint_tile(self, tile: tuple[int, int]) -> None:
        if not self.document or not (0 <= tile[0] < self.document.width and 0 <= tile[1] < self.document.height):
            return
        if self.tool == "erase":
            self.document.set_tile(self.layer_index, tile[0], tile[1], None)
        elif self.selected_tile:
            self.document.set_tile(self.layer_index, tile[0], tile[1], self.selected_tile[0], self.selected_tile[1], self.selected_tile[2])
        else:
            self.status_changed.emit("Select a tileset tile first")
            return
        self.document_changed.emit(); self.update()

    def _fill(self, tile: tuple[int, int]) -> None:
        if not self.document or not (0 <= tile[0] < self.document.width and 0 <= tile[1] < self.document.height):
            return
        if not self.selected_tile:
            self.status_changed.emit("Select a tileset tile first")
            return
        cells = self.document.layers[self.layer_index].get("cells", [])
        if not isinstance(cells, list):
            return
        target = cells[tile[1] * self.document.width + tile[0]]
        replacement = self.document.tile_reference(self.selected_tile[0], self.selected_tile[1], self.selected_tile[2])
        if target == replacement:
            return
        pending = [tile]
        visited: set[tuple[int, int]] = set()
        while pending:
            current = pending.pop()
            if current in visited or not (0 <= current[0] < self.document.width and 0 <= current[1] < self.document.height):
                continue
            visited.add(current)
            if cells[current[1] * self.document.width + current[0]] != target:
                continue
            pending.extend(((current[0] - 1, current[1]), (current[0] + 1, current[1]),
                            (current[0], current[1] - 1), (current[0], current[1] + 1)))
        self.document.set_tiles(self.layer_index, visited, self.selected_tile[0], self.selected_tile[1], self.selected_tile[2])
        self.document_changed.emit(); self.update()

    def _snap_world(self, world: tuple[int, int]) -> tuple[int, int]:
        if not self.document:
            return world
        size = self.document.tile_size
        return (round(world[0] / size) * size, round(world[1] / size) * size)

    def _hit_entity(self, world: tuple[int, int]) -> tuple[str, int] | None:
        if not self.document:
            return None
        best: tuple[str, int] | None = None
        best_distance = max(12, int(self.document.tile_size * 0.75)) ** 2
        for category in ENTITY_CATEGORIES:
            for value in self.document.data.get(category, []):
                if not isinstance(value, dict) or not isinstance(value.get("position"), dict):
                    continue
                position = value["position"]
                distance = (int(position.get("x", 0)) - world[0]) ** 2 + (int(position.get("y", 0)) - world[1]) ** 2
                if distance <= best_distance:
                    best_distance = distance; best = (category, int(value.get("id", -1)))
        return best
