from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QMimeData, QSize, Qt, Signal
from PySide6.QtGui import QDrag, QIcon, QImage, QMouseEvent, QPixmap
from PySide6.QtWidgets import QLabel, QListWidget, QListWidgetItem, QVBoxLayout, QWidget

from ...model.content_workspace import ContentWorkspace
from ...interaction.drag_payload import StudioDragPayload


class TileAtlasListWidget(QListWidget):
    """Atlas list with rectangular mouse selection and typed brush drags."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.columns = 1
        self._selection_start: int | None = None
        self.setDragEnabled(True)

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            item = self.itemAt(event.position().toPoint())
            self._selection_start = self.row(item) if item else None
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self._selection_start is not None and event.buttons() & Qt.MouseButton.LeftButton:
            item = self.itemAt(event.position().toPoint())
            if item:
                self._select_rectangle(self._selection_start, self.row(item))
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            item = self.itemAt(event.position().toPoint())
            if item and self._selection_start is not None:
                self._select_rectangle(self._selection_start, self.row(item))
            self._selection_start = None
        super().mouseReleaseEvent(event)

    def startDrag(self, supported_actions: Qt.DropActions) -> None:  # type: ignore[override]
        del supported_actions
        values = [int(item.data(Qt.ItemDataRole.UserRole)) for item in self.selectedItems()]
        tileset_id = self.property("tilesetId")
        if not values or not isinstance(tileset_id, str) or not tileset_id:
            return
        payload = StudioDragPayload.tile_brush(tileset_id, values)
        mime = QMimeData(); payload.put_mime_data(mime)
        drag = QDrag(self); drag.setMimeData(mime); drag.exec(Qt.DropAction.CopyAction)

    def _select_rectangle(self, first: int, last: int) -> None:
        left, right = sorted((first % self.columns, last % self.columns))
        top, bottom = sorted((first // self.columns, last // self.columns))
        self.blockSignals(True)
        self.clearSelection()
        for row in range(top, bottom + 1):
            for column in range(left, right + 1):
                index = row * self.columns + column
                if index < self.count():
                    self.item(index).setSelected(True)
        self.blockSignals(False)
        self.itemSelectionChanged.emit()


class TileAtlasWidget(QWidget):
    """Visual atlas picker; source indices remain item data, not the main UX."""

    selected = Signal(str, int, int)
    brush_selected = Signal(str, object, int)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace: ContentWorkspace | None = None
        self.asset_root: Path | None = None
        self.tileset_id = ""
        self.tiles = TileAtlasListWidget()
        self.tiles.setViewMode(QListWidget.ViewMode.IconMode)
        self.tiles.setResizeMode(QListWidget.ResizeMode.Adjust)
        self.tiles.setMovement(QListWidget.Movement.Static)
        self.tiles.setSelectionMode(QListWidget.SelectionMode.ExtendedSelection)
        self.tiles.setIconSize(QPixmap(32, 32).size())
        self.tiles.setGridSize(QSize(42, 42))
        self.tiles.itemSelectionChanged.connect(self._selection_changed)
        self.title = QLabel()
        self.title.setWordWrap(True)
        layout = QVBoxLayout(self)
        layout.addWidget(self.title)
        layout.addWidget(self.tiles, 1)

    def set_context(self, workspace: ContentWorkspace | None, asset_root: Path | None) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.refresh()

    def set_tileset(self, tileset_id: str) -> None:
        self.tileset_id = tileset_id
        self.refresh()

    def refresh(self) -> None:
        self.tiles.clear()
        if not self.workspace or not self.tileset_id:
            self.title.setText("")
            return
        definition = self.workspace.find("tilesets", self.tileset_id)
        if definition is None:
            self.title.setText(self.tileset_id)
            return
        columns = max(1, int(definition.data.get("columns", 1)))
        rows = max(1, int(definition.data.get("rows", 1)))
        tile_size = max(1, int(definition.data.get("tileSize", 16)))
        self.tiles.columns = columns
        # QListView normally chooses the number of IconMode columns from the
        # current widget width.  That makes a 16-column source atlas appear as
        # a different layout when the dock is resized.  Keep the viewport
        # exactly wide enough for the authored grid so visual position and
        # sourceIndex (row * columns + column) stay identical.
        grid_width = self.tiles.gridSize().width()
        scrollbar_width = self.tiles.verticalScrollBar().sizeHint().width()
        self.tiles.setFixedWidth(columns * grid_width + 2 * self.tiles.frameWidth() + scrollbar_width + 6)
        self.tiles.setProperty("tilesetId", self.tileset_id)
        self.title.setText(f"{definition.display_name}\n{columns} × {rows} tiles")
        relative = definition.data.get("relativeAssetPath")
        image = QImage(str(self.asset_root / relative)) if self.asset_root and isinstance(relative, str) else QImage()
        for source_index in range(columns * rows):
            item = QListWidgetItem()
            item.setToolTip(f"{self.tileset_id} / {source_index}")
            item.setData(Qt.ItemDataRole.UserRole, source_index)
            if not image.isNull():
                tile = image.copy(source_index % columns * tile_size, source_index // columns * tile_size, tile_size, tile_size)
                item.setIcon(QIcon(QPixmap.fromImage(tile).scaled(32, 32, Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.FastTransformation)))
            self.tiles.addItem(item)
        if self.tiles.count():
            self.tiles.setCurrentRow(0)

    def _selection_changed(self) -> None:
        if not self.tileset_id:
            return
        values = [int(item.data(Qt.ItemDataRole.UserRole)) for item in self.tiles.selectedItems()]
        if not values:
            current = self.tiles.currentItem()
            values = [int(current.data(Qt.ItemDataRole.UserRole))] if current else []
        if not values:
            return
        self.selected.emit(self.tileset_id, values[0], 0)
        self.brush_selected.emit(self.tileset_id, values, 0)
