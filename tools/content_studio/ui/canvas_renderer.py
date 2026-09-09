from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QPoint, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPen

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import ENTITY_CATEGORIES, MapDocument
from ..interaction.selection_controller import SelectionController
from ..services.assets import AssetCatalog
from .canvas_camera import CanvasCamera
from .preview import load_definition_image


class CanvasRenderer:
    """Presentation-only renderer for the authored map canvas."""

    def __init__(self, camera: CanvasCamera, selection: SelectionController) -> None:
        self.camera = camera
        self.selection = selection
        self.assets = AssetCatalog()
        self.document: MapDocument | None = None
        self.workspace: ContentWorkspace | None = None
        self.asset_root: Path | None = None
        self.grid_visible = True
        self.preview_world: tuple[int, int] | None = None
        self.preview_image: QImage | None = None
        self.preview_kind = ""
        self.pointer_tile: tuple[int, int] | None = None
        self.rectangle_start: tuple[int, int] | None = None

    def set_context(self, document: MapDocument | None, workspace: ContentWorkspace | None,
                    asset_root: Path | None) -> None:
        self.document = document
        self.workspace = workspace
        self.asset_root = asset_root
        self.assets.refresh(asset_root, workspace.root if workspace else None)

    def set_grid_visible(self, visible: bool) -> None:
        self.grid_visible = visible

    def render(self, painter: QPainter, viewport_width: int, viewport_height: int) -> None:
        painter.fillRect(0, 0, viewport_width, viewport_height, QColor("#20252b"))
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        document = self.document
        if document is None:
            painter.setPen(QColor("#b8c2cc"))
            painter.drawText(0, 0, viewport_width, viewport_height, Qt.AlignmentFlag.AlignCenter, "No map selected")
            return
        map_width = document.width * document.tile_size
        map_height = document.height * document.tile_size
        origin = self._point(0, 0, viewport_width, viewport_height)
        destination = self._point(map_width, map_height, viewport_width, viewport_height)
        painter.fillRect(origin.x(), origin.y(), destination.x() - origin.x(), destination.y() - origin.y(), QColor("#323b42"))
        self._draw_tiles(painter, viewport_width, viewport_height)
        self._draw_collision(painter, viewport_width, viewport_height)
        self._draw_entities(painter, viewport_width, viewport_height)
        self._draw_spawns(painter, viewport_width, viewport_height)
        self._draw_links(painter, viewport_width, viewport_height)
        self._draw_regions(painter, viewport_width, viewport_height)
        self._draw_preview(painter, viewport_width, viewport_height)
        if self.grid_visible:
            self._draw_grid(painter, viewport_width, viewport_height)

    def _point(self, x: int, y: int, viewport_width: int, viewport_height: int) -> QPoint:
        document = self.document
        assert document is not None
        result = self.camera.world_to_screen(x, y, document.width * document.tile_size, document.height * document.tile_size,
                                             viewport_width, viewport_height)
        return QPoint(*result)

    def _world(self, point: QPoint, viewport_width: int, viewport_height: int) -> tuple[int, int]:
        document = self.document
        assert document is not None
        return self.camera.screen_to_world(point.x(), point.y(), document.width * document.tile_size,
                                           document.height * document.tile_size, viewport_width, viewport_height)

    def _tile(self, point: QPoint, viewport_width: int, viewport_height: int) -> tuple[int, int]:
        document = self.document
        assert document is not None
        x, y = self._world(point, viewport_width, viewport_height)
        return x // document.tile_size, y // document.tile_size

    def _draw_tiles(self, painter: QPainter, viewport_width: int, viewport_height: int) -> None:
        document = self.document
        assert document is not None
        left, top, right, bottom = self._visible_tile_bounds(viewport_width, viewport_height)
        for layer_index, layer in enumerate(document.layers):
            if not layer.get("visible", True):
                continue
            cells = layer.get("cells", [])
            if not isinstance(cells, list):
                continue
            alpha = max(70, 210 - layer_index * 25)
            for y in range(top, bottom + 1):
                for x in range(left, right + 1):
                    cell_index = y * document.width + x
                    index = cells[cell_index] if cell_index < len(cells) else None
                    if not isinstance(index, int):
                        continue
                    references = document.data.get("tileReferences", [])
                    reference = references[index] if isinstance(references, list) and index < len(references) else None
                    if not isinstance(reference, dict):
                        continue
                    color = QColor.fromHsv((index * 47 + layer_index * 83) % 360, 110, 185, alpha)
                    image = self._tile_image(reference, index)
                    target = self._point(x * document.tile_size, y * document.tile_size, viewport_width, viewport_height)
                    size = max(1, round(document.tile_size * self.camera.zoom))
                    if image is None:
                        painter.fillRect(target.x(), target.y(), size, size, color)
                    else:
                        painter.drawImage(target.x(), target.y(), image.scaled(size, size, Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.FastTransformation))

    def _tile_image(self, reference: dict[str, object], index: int) -> QImage | None:
        document = self.document
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
        tile_size = int(tileset.data.get("tileSize", document.tile_size if document else 16))
        source = image.copy((source_index % columns) * tile_size, (source_index // columns) * tile_size, tile_size, tile_size)
        return source.mirrored(True, False) if int(reference.get("flags", 0)) & 1 else source

    def _draw_collision(self, painter: QPainter, viewport_width: int, viewport_height: int) -> None:
        document = self.document
        assert document is not None
        collision = document.data.get("collision", [])
        if not isinstance(collision, list):
            return
        size = max(1, round(document.tile_size * self.camera.zoom))
        left, top, right, bottom = self._visible_tile_bounds(viewport_width, viewport_height)
        for y in range(top, bottom + 1):
            for x in range(left, right + 1):
                index = y * document.width + x
                if index < len(collision) and collision[index]:
                    point = self._point(x * document.tile_size, y * document.tile_size, viewport_width, viewport_height)
                    painter.fillRect(point.x(), point.y(), size, size, QColor(220, 70, 70, 80))

    def _draw_entities(self, painter: QPainter, viewport_width: int, viewport_height: int) -> None:
        document = self.document
        assert document is not None
        colors = {"enemies": QColor("#ed6a5a"), "npcs": QColor("#6ac5ed"), "objects": QColor("#edc35a"), "pickups": QColor("#9be564")}
        for category in ENTITY_CATEGORIES:
            values = document.data.get(category, [])
            if not isinstance(values, list):
                continue
            for value in values:
                if not isinstance(value, dict) or not isinstance(value.get("position"), dict):
                    continue
                position = value["position"]
                point = self._point(int(position.get("x", 0)), int(position.get("y", 0)), viewport_width, viewport_height)
                radius = max(3, round(5 * self.camera.zoom))
                color = colors[category]
                identifier = value.get("id")
                if self.selection.matches(category, identifier):
                    painter.setPen(QPen(QColor("white"), 2)); painter.drawEllipse(point, radius + 3, radius + 3)
                painter.setBrush(color); painter.setPen(QPen(color.darker(140), 1)); painter.drawEllipse(point, radius, radius)
                painter.setPen(QColor("#f5f5f5")); painter.drawText(point + QPoint(radius + 3, 4), str(value.get("definitionId", "")))

    def _draw_spawns(self, painter: QPainter, viewport_width: int, viewport_height: int) -> None:
        document = self.document
        assert document is not None
        spawns = document.data.get("playerSpawns", [])
        if not isinstance(spawns, list):
            return
        for value in spawns:
            if not isinstance(value, dict) or not isinstance(value.get("position"), dict):
                continue
            position = value["position"]
            point = self._point(int(position.get("x", 0)), int(position.get("y", 0)), viewport_width, viewport_height)
            radius = max(5, round(7 * self.camera.zoom))
            painter.setPen(QPen(QColor("#ffec99") if self.selection.matches("playerSpawns", value.get("id")) else QColor("#ffffff"), 3 if self.selection.matches("playerSpawns", value.get("id")) else 2))
            painter.drawLine(point.x() - radius, point.y(), point.x() + radius, point.y()); painter.drawLine(point.x(), point.y() - radius, point.x(), point.y() + radius)
            painter.drawText(point + QPoint(radius + 3, 4), str(value.get("id", "spawn")))

    def _draw_links(self, painter: QPainter, viewport_width: int, viewport_height: int) -> None:
        document = self.document
        assert document is not None
        links = document.data.get("links", [])
        if not isinstance(links, list):
            return
        for link in links:
            if not isinstance(link, dict) or not isinstance(link.get("trigger"), dict):
                continue
            bounds = link["trigger"]
            start = self._point(int(bounds.get("x", 0)), int(bounds.get("y", 0)), viewport_width, viewport_height)
            end = self._point(int(bounds.get("x", 0)) + int(bounds.get("width", 0)), int(bounds.get("y", 0)) + int(bounds.get("height", 0)), viewport_width, viewport_height)
            painter.setBrush(QColor(245, 184, 75, 45)); painter.setPen(QPen(QColor("#ffffff") if self.selection.matches("links", link.get("id")) else QColor("#f0b35b"), 3 if self.selection.matches("links", link.get("id")) else 2, Qt.PenStyle.DotLine))
            painter.drawRect(start.x(), start.y(), end.x() - start.x(), end.y() - start.y())
            painter.setPen(QColor("#f5d59b")); painter.drawText(start + QPoint(3, 14), str(link.get("id", "link")))

    def _draw_regions(self, painter: QPainter, viewport_width: int, viewport_height: int) -> None:
        document = self.document
        assert document is not None
        regions = document.data.get("regions", [])
        if not isinstance(regions, list):
            return
        for region in regions:
            if not isinstance(region, dict) or not isinstance(region.get("bounds"), dict):
                continue
            bounds = region["bounds"]
            start = self._point(int(bounds.get("x", 0)), int(bounds.get("y", 0)), viewport_width, viewport_height)
            end = self._point(int(bounds.get("x", 0)) + int(bounds.get("width", 0)), int(bounds.get("y", 0)) + int(bounds.get("height", 0)), viewport_width, viewport_height)
            selected = self.selection.matches("regions", region.get("id"))
            painter.setBrush(Qt.BrushStyle.NoBrush); painter.setPen(QPen(QColor("#ffffff") if selected else QColor("#c084fc"), 3 if selected else 2, Qt.PenStyle.DashLine))
            painter.drawRect(start.x(), start.y(), end.x() - start.x(), end.y() - start.y())

    def _draw_preview(self, painter: QPainter, viewport_width: int, viewport_height: int) -> None:
        if self.preview_world is not None:
            point = self._point(*self.preview_world, viewport_width, viewport_height)
            size = max(2, round((self.document.tile_size if self.document else 16) * self.camera.zoom))
            if self.preview_image is not None:
                image = self.preview_image.scaled(max(size, self.preview_image.width()), max(size, self.preview_image.height()), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.FastTransformation)
                painter.setOpacity(0.55); painter.drawImage(point.x() - image.width() // 2, point.y() - image.height() // 2, image); painter.setOpacity(1.0)
            else:
                painter.fillRect(point.x() - size // 2, point.y() - size // 2, size, size, QColor(95, 220, 120, 105))
            painter.setPen(QPen(QColor("#8ff0a4"), 2)); painter.drawRect(point.x() - size // 2, point.y() - size // 2, size, size)
        if self.rectangle_start is not None and self.pointer_tile is not None and self.document is not None:
            start_x = min(self.rectangle_start[0], self.pointer_tile[0]) * self.document.tile_size
            start_y = min(self.rectangle_start[1], self.pointer_tile[1]) * self.document.tile_size
            end_x = (max(self.rectangle_start[0], self.pointer_tile[0]) + 1) * self.document.tile_size
            end_y = (max(self.rectangle_start[1], self.pointer_tile[1]) + 1) * self.document.tile_size
            start = self._point(start_x, start_y, viewport_width, viewport_height); end = self._point(end_x, end_y, viewport_width, viewport_height)
            painter.setBrush(QColor(139, 184, 232, 45)); painter.setPen(QPen(QColor("#8bb8e8"), 2, Qt.PenStyle.DashLine)); painter.drawRect(start.x(), start.y(), end.x() - start.x(), end.y() - start.y())

    def _draw_grid(self, painter: QPainter, viewport_width: int, viewport_height: int) -> None:
        document = self.document
        assert document is not None
        size = max(1, round(document.tile_size * self.camera.zoom))
        if size < 4:
            return
        origin = self._point(0, 0, viewport_width, viewport_height)
        end = self._point(document.width * document.tile_size, document.height * document.tile_size, viewport_width, viewport_height)
        painter.setPen(QPen(QColor(255, 255, 255, 28), 1))
        for x in range(document.width + 1):
            point = self._point(x * document.tile_size, 0, viewport_width, viewport_height); painter.drawLine(point.x(), origin.y(), point.x(), end.y())
        for y in range(document.height + 1):
            point = self._point(0, y * document.tile_size, viewport_width, viewport_height); painter.drawLine(origin.x(), point.y(), end.x(), point.y())

    def _visible_tile_bounds(self, viewport_width: int, viewport_height: int) -> tuple[int, int, int, int]:
        document = self.document
        assert document is not None
        first = self._tile(QPoint(0, 0), viewport_width, viewport_height); last = self._tile(QPoint(viewport_width, viewport_height), viewport_width, viewport_height)
        left = max(0, min(first[0], last[0]) - 1); top = max(0, min(first[1], last[1]) - 1)
        right = min(document.width - 1, max(first[0], last[0]) + 1); bottom = min(document.height - 1, max(first[1], last[1]) + 1)
        return left, top, max(left, right), max(top, bottom)
