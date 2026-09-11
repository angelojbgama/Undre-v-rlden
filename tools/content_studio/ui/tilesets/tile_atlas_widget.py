from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QMimeData, Qt, Signal
from PySide6.QtGui import QDrag, QIcon, QImage, QMouseEvent, QPixmap
from PySide6.QtWidgets import (
    QAbstractItemView, QHeaderView, QLabel, QTableWidget, QTableWidgetItem,
    QVBoxLayout, QWidget,
)

from ...model.content_workspace import ContentWorkspace
from ...interaction.drag_payload import StudioDragPayload


class TileAtlasListWidget(QTableWidget):
    """Atlas grid whose visual cells keep the source image coordinates."""

    CELL_SIZE = 42

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.columns = 1
        self._selection_start: int | None = None
        self.horizontalHeader().hide()
        self.verticalHeader().hide()
        self.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
        self.verticalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
        self.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectItems)
        self.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
        self.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.setSortingEnabled(False)
        self.setShowGrid(False)
        self.setDragEnabled(True)

    def configure_grid(self, columns: int, rows: int) -> None:
        self.clear()
        self.columns = max(1, columns)
        self.setColumnCount(self.columns)
        self.setRowCount(max(0, rows))
        for column in range(self.columnCount()):
            self.setColumnWidth(column, self.CELL_SIZE)
        for row in range(self.rowCount()):
            self.setRowHeight(row, self.CELL_SIZE)

    def count(self) -> int:
        return self.rowCount() * self.columnCount()

    def item(self, source_index: int, column: int | None = None) -> QTableWidgetItem | None:  # type: ignore[override]
        if column is not None:
            return super().item(source_index, column)
        if source_index < 0 or source_index >= self.count():
            return None
        return super().item(source_index // self.columns, source_index % self.columns)

    def set_source_item(self, source_index: int, item: QTableWidgetItem) -> None:
        self.setItem(source_index // self.columns, source_index % self.columns, item)

    def setCurrentRow(self, source_index: int) -> None:  # noqa: N802 - QListWidget compatibility
        if 0 <= source_index < self.count():
            self.setCurrentCell(source_index // self.columns, source_index % self.columns)

    @staticmethod
    def _source_index(item: QTableWidgetItem | None) -> int | None:
        if item is None:
            return None
        value = item.data(Qt.ItemDataRole.UserRole)
        return int(value) if isinstance(value, int) else None

    def mousePressEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            item = self.itemAt(event.position().toPoint())
            self._selection_start = self._source_index(item)
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:
        if self._selection_start is not None and event.buttons() & Qt.MouseButton.LeftButton:
            item = self.itemAt(event.position().toPoint())
            if item:
                source_index = self._source_index(item)
                if source_index is not None:
                    self._select_rectangle(self._selection_start, source_index)
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            item = self.itemAt(event.position().toPoint())
            if item and self._selection_start is not None:
                source_index = self._source_index(item)
                if source_index is not None:
                    self._select_rectangle(self._selection_start, source_index)
            self._selection_start = None
        super().mouseReleaseEvent(event)

    def startDrag(self, supported_actions: Qt.DropActions) -> None:  # type: ignore[override]
        del supported_actions
        values = sorted(int(item.data(Qt.ItemDataRole.UserRole)) for item in self.selectedItems())
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
                    item = self.item(index)
                    if item is not None:
                        item.setSelected(True)
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
        self.family_label = "Family"
        self._rendered_tileset_id = ""
        self.tiles = TileAtlasListWidget()
        self.tiles.setIconSize(QPixmap(32, 32).size())
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

    def set_family_label(self, label: str) -> None:
        self.family_label = label

    def refresh(self) -> None:
        selected_indices = (
            sorted(int(item.data(Qt.ItemDataRole.UserRole)) for item in self.tiles.selectedItems())
            if self._rendered_tileset_id == self.tileset_id
            else []
        )
        self.tiles.blockSignals(True)
        self.tiles.configure_grid(1, 0)
        if not self.workspace or not self.tileset_id:
            self.title.setText("")
            self._rendered_tileset_id = ""
            self.tiles.blockSignals(False)
            return
        definition = self.workspace.find("tilesets", self.tileset_id)
        if definition is None:
            self.title.setText(self.tileset_id)
            self._rendered_tileset_id = self.tileset_id
            self.tiles.blockSignals(False)
            return
        columns = max(1, int(definition.data.get("columns", 1)))
        rows = max(1, int(definition.data.get("rows", 1)))
        tile_size = max(1, int(definition.data.get("tileSize", 16)))
        self.tiles.configure_grid(columns, rows)
        self.tiles.setProperty("tilesetId", self.tileset_id)
        self.title.setText(f"{definition.display_name}\n{columns} × {rows} tiles")
        relative = definition.data.get("relativeAssetPath")
        image = QImage(str(self.asset_root / relative)) if self.asset_root and isinstance(relative, str) else QImage()
        semantics = {
            int(value.data.get("sourceIndex", -1)): value
            for value in self.workspace.definitions("tileSemantics")
            if value.data.get("tilesetId") == self.tileset_id
        }
        for source_index in range(columns * rows):
            item = QTableWidgetItem()
            semantic = semantics.get(source_index)
            if semantic is None:
                item.setToolTip(f"{self.tileset_id} / {source_index}")
            else:
                family = str(semantic.data.get("family", "")).strip()
                details = [semantic.display_name, semantic.definition_id]
                if family:
                    details.append(f"{self.family_label}: {family}")
                details.append(f"{self.tileset_id} / {source_index}")
                item.setToolTip("\n".join(details))
            item.setData(Qt.ItemDataRole.UserRole, source_index)
            if not image.isNull():
                tile = image.copy(source_index % columns * tile_size, source_index // columns * tile_size, tile_size, tile_size)
                item.setIcon(QIcon(QPixmap.fromImage(tile).scaled(32, 32, Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.FastTransformation)))
            self.tiles.set_source_item(source_index, item)
        valid_selection = [index for index in selected_indices if 0 <= index < self.tiles.count()]
        if not valid_selection and self.tiles.count():
            valid_selection = [0]
        if valid_selection:
            self.tiles.setCurrentRow(valid_selection[0])
        for source_index in valid_selection:
            item = self.tiles.item(source_index)
            if item is not None:
                item.setSelected(True)
        changed_tileset = self._rendered_tileset_id != self.tileset_id
        self._rendered_tileset_id = self.tileset_id
        self.tiles.blockSignals(False)
        if changed_tileset:
            self._selection_changed()

    def _selection_changed(self) -> None:
        if not self.tileset_id:
            return
        values = sorted(int(item.data(Qt.ItemDataRole.UserRole)) for item in self.tiles.selectedItems())
        if not values:
            current = self.tiles.currentItem()
            values = [int(current.data(Qt.ItemDataRole.UserRole))] if current else []
        if not values:
            return
        self.selected.emit(self.tileset_id, values[0], 0)
        self.brush_selected.emit(self.tileset_id, values, 0)
