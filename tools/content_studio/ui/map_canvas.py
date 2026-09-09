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
        self.selected_entity: tuple[str, object] | None = None
        self.selected_tile: tuple[str, int, int] | None = None
        self.selected_brush: list[tuple[int, int, str, int, int]] = []
        self.selected_stamp_id = ""
        self.layer_index = 0
        self.selected_stamp: str = ""
        self.grid_visible = True
        self.zoom = 1.0
        self.pan = QPoint(0, 0)
        self._drag_start: tuple[int, int] | None = None
        self._drag_last: tuple[int, int] | None = None
        self._region_start: tuple[int, int] | None = None
        self._rectangle_start: tuple[int, int] | None = None
        self._moving: tuple[str, object] | None = None
        self._preview: tuple[int, int] | None = None
        self._pointer_tile: tuple[int, int] | None = None

    def set_context(self, document: MapDocument | None, workspace: ContentWorkspace | None, asset_root: Path | None) -> None:
        document_changed = document is not self.document
        self.document = document
        self.workspace = workspace
        self.asset_root = asset_root
        self.assets.refresh(asset_root, workspace.root if workspace else None)
        if document_changed:
            self.selected_entity = None
        self._preview = None
        self._pointer_tile = None
        self.update()

    def set_tool(self, tool: str) -> None:
        self.tool = tool
        if tool not in {"entity", "spawn"}:
            self._preview = None
        if tool != "entity":
            self.selected_entity_category = ""
            self.selected_definition_id = ""
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

    def set_brush(self, tileset_id: str, source_indices: list[int], flags: int = 0) -> None:
        self.selected_brush = []
        if not source_indices:
            return
        columns = 1
        if self.workspace:
            tileset = self.workspace.find("tilesets", tileset_id)
            if tileset:
                columns = max(1, int(tileset.data.get("columns", 1)))
        coordinates = [(index % columns, index // columns, index) for index in source_indices]
        min_x = min(value[0] for value in coordinates)
        min_y = min(value[1] for value in coordinates)
        for x, y, index in coordinates:
            self.selected_brush.append((x - min_x, y - min_y, tileset_id, index, flags))
        self.selected_tile = (tileset_id, source_indices[0], flags)
        self.update()

    def set_stamp_selection(self, stamp_id: str) -> None:
        self.selected_stamp_id = stamp_id
        self.set_tool("stamp")

    def cancel_placement(self) -> None:
        if self.tool in {"entity", "spawn"}:
            self.tool = "select"
            self.selected_entity_category = ""
            self.selected_definition_id = ""
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
        self._draw_links(painter)
        self._draw_regions(painter)
        self._draw_operation_preview(painter)
        if self._preview is not None and self.tool == "entity":
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
        elif self._preview is not None and self.tool == "stamp":
            preview = self.world_to_screen(self._preview[0] * self.document.tile_size,
                                           self._preview[1] * self.document.tile_size)
            width, height = self._stamp_size()
            rect_end = self.world_to_screen((self._preview[0] + width) * self.document.tile_size,
                                             (self._preview[1] + height) * self.document.tile_size)
            painter.setBrush(QColor(190, 135, 245, 70))
            painter.setPen(QPen(QColor("#c084fc"), 2, Qt.PenStyle.DashLine))
            painter.drawRect(preview.x(), preview.y(), rect_end.x() - preview.x(), rect_end.y() - preview.y())
        if self.grid_visible:
            self._draw_grid(painter, origin)
        painter.end()

    def _draw_tiles(self, painter: QPainter, origin: QPoint) -> None:
        assert self.document is not None
        tile_size = self.document.tile_size
        left, top, right, bottom = self._visible_tile_bounds()
        for layer_index, layer in enumerate(self.document.layers):
            if not layer.get("visible", True):
                continue
            cells = layer.get("cells", [])
            if not isinstance(cells, list):
                continue
            alpha = max(70, 210 - layer_index * 25)
            for y in range(top, bottom + 1):
                for x in range(left, right + 1):
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
        if root is None:
            return None
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
        left, top, right, bottom = self._visible_tile_bounds()
        for y in range(top, bottom + 1):
            for x in range(left, right + 1):
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
                if self.selected_entity == (category, value.get("id")):
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
            if self.selected_entity == ("playerSpawns", value.get("id")):
                painter.setPen(QPen(QColor("#ffec99"), 3))
            painter.drawLine(point.x() - radius, point.y(), point.x() + radius, point.y())
            painter.drawLine(point.x(), point.y() - radius, point.x(), point.y() + radius)
            painter.drawText(point + QPoint(radius + 3, 4), str(value.get("id", "spawn")))

    def _draw_links(self, painter: QPainter) -> None:
        assert self.document is not None
        links = self.document.data.get("links", [])
        if not isinstance(links, list):
            return
        painter.setBrush(QColor(245, 184, 75, 45)); painter.setPen(QPen(QColor("#f0b35b"), 2, Qt.PenStyle.DotLine))
        for link in links:
            if not isinstance(link, dict) or not isinstance(link.get("trigger"), dict):
                continue
            bounds = link["trigger"]
            start = self.world_to_screen(int(bounds.get("x", 0)), int(bounds.get("y", 0)))
            end = self.world_to_screen(int(bounds.get("x", 0)) + int(bounds.get("width", 0)), int(bounds.get("y", 0)) + int(bounds.get("height", 0)))
            if self.selected_entity == ("links", link.get("id")):
                painter.setPen(QPen(QColor("#ffffff"), 3))
            painter.drawRect(start.x(), start.y(), end.x() - start.x(), end.y() - start.y())
            painter.setPen(QColor("#f5d59b")); painter.drawText(start + QPoint(3, 14), str(link.get("id", "link")))

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
            if self.selected_entity == ("regions", region.get("id")):
                painter.setPen(QPen(QColor("#ffffff"), 3))
            painter.drawRect(start.x(), start.y(), end.x() - start.x(), end.y() - start.y())

    def _draw_grid(self, painter: QPainter, origin: QPoint) -> None:
        assert self.document is not None
        size = max(1, round(self.document.tile_size * self.zoom))
        if size < 4:
            return
        left, top, right, bottom = self._visible_tile_bounds()
        painter.setPen(QPen(QColor(255, 255, 255, 28), 1))
        for x in range(left, right + 2):
            point = self.world_to_screen(x * self.document.tile_size, 0)
            painter.drawLine(point.x(), origin.y(), point.x(), self.world_to_screen(0, self.document.height * self.document.tile_size).y())
        for y in range(top, bottom + 2):
            point = self.world_to_screen(0, y * self.document.tile_size)
            painter.drawLine(origin.x(), point.y(), self.world_to_screen(self.document.width * self.document.tile_size, 0).x(), point.y())

    def _visible_tile_bounds(self) -> tuple[int, int, int, int]:
        assert self.document is not None
        first = self.screen_to_world(QPoint(0, 0))
        last = self.screen_to_world(QPoint(self.width(), self.height()))
        left = max(0, min(first[0], last[0]) // self.document.tile_size - 1)
        top = max(0, min(first[1], last[1]) // self.document.tile_size - 1)
        right = min(self.document.width - 1, max(first[0], last[0]) // self.document.tile_size + 1)
        bottom = min(self.document.height - 1, max(first[1], last[1]) // self.document.tile_size + 1)
        return left, top, max(left, right), max(top, bottom)

    def _draw_operation_preview(self, painter: QPainter) -> None:
        if not self.document or self._rectangle_start is None or self._pointer_tile is None:
            return
        if self.tool not in {"rectangle", "tile_selection", "collision_rectangle", "collision_rectangle_erase"}:
            return
        start = self._rectangle_start; end = self._pointer_tile
        left = min(start[0], end[0]); top = min(start[1], end[1])
        right = max(start[0], end[0]) + 1; bottom = max(start[1], end[1]) + 1
        origin = self.world_to_screen(left * self.document.tile_size, top * self.document.tile_size)
        finish = self.world_to_screen(right * self.document.tile_size, bottom * self.document.tile_size)
        color = QColor("#8bb8e8") if self.tool == "tile_selection" else QColor("#e8c47b")
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 45))
        painter.setPen(QPen(color, 2, Qt.PenStyle.DashLine))
        painter.drawRect(origin.x(), origin.y(), finish.x() - origin.x(), finish.y() - origin.y())

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
        self._pointer_tile = tile
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
        elif self.tool in {"collision", "collision_erase"}:
            self.document.set_collision([tile], self.tool == "collision")
            self.document_changed.emit(); self.update()
        elif self.tool == "eyedropper":
            self._pick_tile(tile)
        elif self.tool == "tile_selection":
            self._rectangle_start = tile
        elif self.tool == "stamp":
            self._place_stamp(tile)
        elif self.tool == "link":
            spawns = self.document.data.get("playerSpawns", [])
            target_spawn = str(spawns[0].get("id", "")) if isinstance(spawns, list) and spawns and isinstance(spawns[0], dict) else ""
            self.document.add_link(f"link.editor.{len(self.document.data.get('links', [])) + 1}", tile[0] * self.document.tile_size, tile[1] * self.document.tile_size, self.document.tile_size, self.document.tile_size, self.document.map_id, target_spawn)
            self.document_changed.emit(); self.update()
        elif self.tool in {"collision_fill", "collision_fill_erase"}:
            self._fill_collision(tile, self.tool == "collision_fill")
        elif self.tool == "region":
            self._region_start = world
        elif self.tool == "pan":
            self._drag_last = mouse.position().toPoint()

    def mouseMoveEvent(self, event: object) -> None:
        if not self.document or not hasattr(event, "position"):
            return
        mouse = event  # type: ignore[assignment]
        point = mouse.position().toPoint()
        self._pointer_tile = self.tile_at(point)
        world = self.screen_to_world(point)
        if self.tool == "entity" and self.selected_definition_id:
            self._preview = self._snap_world(world); self.update()
        elif self.tool == "stamp" and self.selected_stamp_id:
            self._preview = self.tile_at(point); self.update()
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
        elif self.tool in {"collision", "collision_erase"} and tile != self._drag_start:
            self.document.set_collision([tile], self.tool == "collision"); self._drag_start = tile; self.document_changed.emit(); self.update()
        elif self.tool in {"rectangle", "tile_selection", "collision_rectangle", "collision_rectangle_erase"}:
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
        if self._rectangle_start is not None and self.tool in {"rectangle", "tile_selection", "collision_rectangle", "collision_rectangle_erase"}:
            end = self.tile_at(mouse.position().toPoint())
            start = self._rectangle_start
            cells = [(x, y) for y in range(min(start[1], end[1]), max(start[1], end[1]) + 1)
                     for x in range(min(start[0], end[0]), max(start[0], end[0]) + 1)]
            if self.tool == "tile_selection":
                self._select_tile_rectangle(start, end)
            elif self.tool in {"collision_rectangle", "collision_rectangle_erase"}:
                self.document.set_collision(cells, self.tool == "collision_rectangle")
                self.document_changed.emit(); self.update()
            elif self.selected_tile:
                self.document.set_tiles(self.layer_index, cells, self.selected_tile[0], self.selected_tile[1], self.selected_tile[2])
                self.document_changed.emit(); self.update()
            else:
                self.status_changed.emit("Select a tileset tile first")
            self._rectangle_start = None
        if self._moving is not None:
            category, identifier = self._moving
            world = self._snap_world(self.screen_to_world(mouse.position().toPoint()))
            try:
                if category in ENTITY_CATEGORIES:
                    self.document.move_entity(category, int(identifier), *world)
                elif category == "playerSpawns":
                    self.document.mutate("Move Player Spawn", lambda: self._set_position("playerSpawns", identifier, world))
                elif category == "regions":
                    self.document.move_region(str(identifier), *world)
                elif category == "links":
                    self.document.mutate("Move Map Link", lambda: self._set_link_position(identifier, world))
                self.document_changed.emit()
            except ValueError:
                pass
            self._moving = None
        self._drag_start = None
        self._pointer_tile = None

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
            if category in ENTITY_CATEGORIES:
                self.document.delete_entity(category, int(identifier))
            else:
                self._delete_collection_item(category, identifier)
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
        elif self.selected_brush:
            self.document.paint_brush(self.layer_index, tile[0], tile[1], self.selected_brush)
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
        replacement = self.document.find_tile_reference(self.selected_tile[0], self.selected_tile[1], self.selected_tile[2])
        if replacement is None:
            # An unused tile reference cannot be the target of a fill.  The
            # actual reference is added atomically by set_tiles below.
            replacement = -1
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

    def _pick_tile(self, tile: tuple[int, int]) -> None:
        if not self.document or not (0 <= tile[0] < self.document.width and 0 <= tile[1] < self.document.height):
            return
        cells = self.document.layers[self.layer_index].get("cells", [])
        index = cells[tile[1] * self.document.width + tile[0]] if isinstance(cells, list) else None
        references = self.document.data.get("tileReferences", [])
        if not isinstance(index, int) or not isinstance(references, list) or index >= len(references):
            self.status_changed.emit("No tile at this position")
            return
        reference = references[index]
        if not isinstance(reference, dict) or not isinstance(reference.get("tilesetId"), str):
            return
        self.set_brush(str(reference["tilesetId"]), [int(reference.get("sourceIndex", 0))], int(reference.get("flags", 0)))
        self.set_tool("pencil")
        self.status_changed.emit("Tile selected from map")

    def _select_tile_rectangle(self, start: tuple[int, int], end: tuple[int, int]) -> None:
        if not self.document:
            return
        cells = self.document.layers[self.layer_index].get("cells", [])
        references = self.document.data.get("tileReferences", [])
        if not isinstance(cells, list) or not isinstance(references, list):
            return
        brush: list[tuple[int, int, str, int, int]] = []
        for y in range(min(start[1], end[1]), max(start[1], end[1]) + 1):
            for x in range(min(start[0], end[0]), max(start[0], end[0]) + 1):
                if not (0 <= x < self.document.width and 0 <= y < self.document.height):
                    continue
                value = cells[y * self.document.width + x]
                if not isinstance(value, int) or value >= len(references) or not isinstance(references[value], dict):
                    continue
                reference = references[value]
                tileset_id = reference.get("tilesetId")
                if isinstance(tileset_id, str):
                    brush.append((x - min(start[0], end[0]), y - min(start[1], end[1]), tileset_id,
                                  int(reference.get("sourceIndex", 0)), int(reference.get("flags", 0))))
        if brush:
            self.selected_brush = brush
            self.selected_tile = (brush[0][2], brush[0][3], brush[0][4])
            self.status_changed.emit(f"Selected {len(brush)} tile(s)")

    def _place_stamp(self, tile: tuple[int, int]) -> None:
        if not self.document or not self.workspace or not self.selected_stamp_id:
            return
        stamp = self.workspace.find("stamps", self.selected_stamp_id)
        if not stamp or not isinstance(stamp.data.get("cells"), list):
            self.status_changed.emit("Stamp is unavailable")
            return
        pattern: list[tuple[int, int, str, int, int]] = []
        for cell in stamp.data["cells"]:
            if not isinstance(cell, dict):
                continue
            semantic = self.workspace.find("tileSemantics", str(cell.get("tileId", "")))
            if semantic:
                tileset_id = semantic.data.get("tilesetId")
                if isinstance(tileset_id, str):
                    pattern.append((int(cell.get("x", 0)), int(cell.get("y", 0)), tileset_id,
                                   int(semantic.data.get("sourceIndex", 0)), 0))
        if pattern:
            self.document.place_pattern(self.layer_index, tile[0], tile[1], pattern)
            self.document_changed.emit(); self.update()

    def _stamp_size(self) -> tuple[int, int]:
        if self.workspace and self.selected_stamp_id:
            stamp = self.workspace.find("stamps", self.selected_stamp_id)
            if stamp:
                return max(1, int(stamp.data.get("width", 1))), max(1, int(stamp.data.get("height", 1)))
        return 1, 1

    def _fill_collision(self, tile: tuple[int, int], solid: bool) -> None:
        if not self.document or not (0 <= tile[0] < self.document.width and 0 <= tile[1] < self.document.height):
            return
        collision = self.document.data.get("collision", [])
        if not isinstance(collision, list):
            return
        target = bool(collision[tile[1] * self.document.width + tile[0]])
        if target == solid:
            return
        pending = [tile]
        visited: set[tuple[int, int]] = set()
        while pending:
            current = pending.pop()
            if current in visited or not (0 <= current[0] < self.document.width and 0 <= current[1] < self.document.height):
                continue
            visited.add(current)
            if bool(collision[current[1] * self.document.width + current[0]]) != target:
                continue
            pending.extend(((current[0] - 1, current[1]), (current[0] + 1, current[1]),
                            (current[0], current[1] - 1), (current[0], current[1] + 1)))
        self.document.set_collision(visited, solid)
        self.document_changed.emit(); self.update()

    def _snap_world(self, world: tuple[int, int]) -> tuple[int, int]:
        if not self.document:
            return world
        size = self.document.tile_size
        return (round(world[0] / size) * size, round(world[1] / size) * size)

    def _hit_entity(self, world: tuple[int, int]) -> tuple[str, object] | None:
        if not self.document:
            return None
        best: tuple[str, object] | None = None
        best_distance = max(12, int(self.document.tile_size * 0.75)) ** 2
        for category in ENTITY_CATEGORIES:
            for value in self.document.data.get(category, []):
                if not isinstance(value, dict) or not isinstance(value.get("position"), dict):
                    continue
                position = value["position"]
                distance = (int(position.get("x", 0)) - world[0]) ** 2 + (int(position.get("y", 0)) - world[1]) ** 2
                if distance <= best_distance:
                    best_distance = distance; best = (category, value.get("id"))
        for value in self.document.data.get("playerSpawns", []) if isinstance(self.document.data.get("playerSpawns"), list) else []:
            if isinstance(value, dict) and isinstance(value.get("position"), dict):
                position = value["position"]; distance = (int(position.get("x", 0)) - world[0]) ** 2 + (int(position.get("y", 0)) - world[1]) ** 2
                if distance <= best_distance: best_distance = distance; best = ("playerSpawns", value.get("id"))
        for category in ("regions", "links"):
            for value in self.document.data.get(category, []) if isinstance(self.document.data.get(category), list) else []:
                bounds = value.get("bounds") if category == "regions" else value.get("trigger") if isinstance(value, dict) else None
                if isinstance(value, dict) and isinstance(bounds, dict) and int(bounds.get("x", 0)) <= world[0] < int(bounds.get("x", 0)) + int(bounds.get("width", 0)) and int(bounds.get("y", 0)) <= world[1] < int(bounds.get("y", 0)) + int(bounds.get("height", 0)):
                    best = (category, value.get("id"))
        return best

    def _set_position(self, category: str, identifier: object, position: tuple[int, int]) -> None:
        value = next((entry for entry in self.document.data.get(category, []) if isinstance(entry, dict) and entry.get("id") == identifier), None) if self.document else None
        if isinstance(value, dict): value["position"] = {"x": position[0], "y": position[1]}

    def _set_link_position(self, identifier: object, position: tuple[int, int]) -> None:
        if not self.document: return
        value = next((entry for entry in self.document.data.get("links", []) if isinstance(entry, dict) and entry.get("id") == identifier), None)
        if isinstance(value, dict) and isinstance(value.get("trigger"), dict): value["trigger"]["x"] = position[0]; value["trigger"]["y"] = position[1]

    def _delete_collection_item(self, category: str, identifier: object) -> None:
        if not self.document: return
        values = self.document.data.get(category, [])
        if isinstance(values, list): self.document.mutate(f"Delete {category}", lambda: values.__setitem__(slice(None), [value for value in values if not (isinstance(value, dict) and value.get("id") == identifier)]))
