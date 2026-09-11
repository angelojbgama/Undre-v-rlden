from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QDragEnterEvent, QDropEvent
from PySide6.QtWidgets import (
    QGridLayout, QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QMenu, QMessageBox, QPushButton,
    QSplitter, QVBoxLayout, QWidget, QLineEdit,
)

from ...interaction.drag_payload import StudioDragPayload
from ...model.content_workspace import ContentWorkspace
from ...model.world_project import WorldProject
from ...services.import_service import SUPPORTED_IMAGE_SUFFIXES
from ...services.localization import Translator
from ...services.tileset_library import TilesetLibrary
from .batch_tileset_import_dialog import BatchTilesetImportDialog
from .tile_atlas_widget import TileAtlasWidget
from ..terrain.terrain_rule_dialog import TerrainRuleDialog


class TilesetLibraryWidget(QWidget):
    """A visual, searchable library of all project tilesets."""

    selected = Signal(str, int, int)
    brush_selected = Signal(str, object, int)
    status_changed = Signal(str)
    changed = Signal()

    def __init__(self, workspace: ContentWorkspace | None = None, project: WorldProject | None = None,
                 asset_root: Path | None = None, translator: Translator | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator or Translator()
        self.library = TilesetLibrary(workspace, project)
        self.asset_root = asset_root
        self.map_tile_size: int | None = None
        self.search = QLineEdit(); self.search.setPlaceholderText(self.translate("search_tilesets")); self.search.textChanged.connect(self.refresh)
        self.tilesets = QListWidget(); self.tilesets.currentItemChanged.connect(self._tileset_changed)
        self.tilesets.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.tilesets.customContextMenuRequested.connect(self._context_menu)
        self.atlas = TileAtlasWidget(); self.atlas.selected.connect(self.selected); self.atlas.brush_selected.connect(self.brush_selected)
        self.atlas.tiles.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.atlas.tiles.customContextMenuRequested.connect(self._atlas_context_menu)
        self.add_files_button = QPushButton(self.translate("add_files")); self.add_files_button.clicked.connect(self.add_files)
        self.add_folder_button = QPushButton(self.translate("add_folder")); self.add_folder_button.clicked.connect(self.add_folder)
        self.reimport_button = QPushButton(self.translate("reimport")); self.reimport_button.clicked.connect(self.reimport_selected)
        self.delete_button = QPushButton(self.translate("delete")); self.delete_button.clicked.connect(self.delete_selected)
        buttons = QGridLayout()
        for index, button in enumerate((self.add_files_button, self.add_folder_button, self.reimport_button, self.delete_button)):
            buttons.addWidget(button, index // 2, index % 2)
        left = QWidget(); left_layout = QVBoxLayout(left); left_layout.addWidget(QLabel(self.translate("tilesets"))); left_layout.addWidget(self.search); left_layout.addWidget(self.tilesets, 1); left_layout.addLayout(buttons)
        splitter = QSplitter(Qt.Orientation.Horizontal); splitter.addWidget(left); splitter.addWidget(self.atlas); splitter.setStretchFactor(1, 1)
        splitter.setChildrenCollapsible(True); splitter.setHandleWidth(8); splitter.setSizes([240, 520])
        layout = QVBoxLayout(self); layout.addWidget(splitter)
        self.setAcceptDrops(True)
        self.set_workspace(workspace, project)

    def set_workspace(self, workspace: ContentWorkspace | None, project: WorldProject | None = None) -> None:
        self.library.set_context(workspace, project)
        self.refresh()

    def set_project(self, project: WorldProject | None) -> None:
        self.library.set_context(self.library.workspace, project)

    def set_asset_root(self, asset_root: Path | None) -> None:
        self.asset_root = asset_root
        self.atlas.set_context(self.library.workspace, asset_root)

    def set_map_tile_size(self, tile_size: int | None) -> None:
        if self.map_tile_size == tile_size:
            return
        self.map_tile_size = tile_size
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.search.setPlaceholderText(self.translate("search_tilesets"))
        self.add_files_button.setText(self.translate("add_files")); self.add_folder_button.setText(self.translate("add_folder")); self.reimport_button.setText(self.translate("reimport")); self.delete_button.setText(self.translate("delete"))
        self.refresh()

    def refresh(self) -> None:
        current_item = self.tilesets.currentItem()
        current = current_item.data(Qt.ItemDataRole.UserRole) if current_item else None
        self.tilesets.blockSignals(True); self.tilesets.clear()
        for definition in self.library.definitions(self.search.text()):
            item = QListWidgetItem(f"{definition.display_name}")
            item.setToolTip(definition.definition_id)
            item.setData(Qt.ItemDataRole.UserRole, definition.definition_id)
            size = definition.data.get("tileSize")
            compatible = self.map_tile_size is None or size == self.map_tile_size
            if not compatible:
                item.setText(f"{definition.display_name} ({size}px — {self.translate('incompatible')})")
                item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsEnabled)
            self.tilesets.addItem(item)
        self.tilesets.blockSignals(False)
        row = -1
        if current:
            row = next(
                (
                    index
                    for index in range(self.tilesets.count())
                    if self.tilesets.item(index).data(Qt.ItemDataRole.UserRole) == current
                ),
                -1,
            )
        if row < 0:
            row = next((index for index in range(self.tilesets.count()) if self.tilesets.item(index).flags() & Qt.ItemFlag.ItemIsEnabled), -1)
        if row >= 0:
            self.tilesets.setCurrentRow(row)
        else:
            self.atlas.set_tileset("")
        has_selection = self._selected_definition() is not None
        self.reimport_button.setEnabled(has_selection)
        self.delete_button.setEnabled(has_selection)

    def add_files(self) -> None:
        dialog = BatchTilesetImportDialog(self.library, self.asset_root, self.translate, self)
        from PySide6.QtWidgets import QFileDialog
        paths, _ = QFileDialog.getOpenFileNames(self, self.translate("add_files"), "", "Images (*.png *.jpg *.jpeg *.bmp *.gif)")
        dialog.add_paths([Path(value) for value in paths])
        if dialog.exec():
            self.library.usage_index.rebuild(); self.refresh(); self.changed.emit()

    def add_folder(self) -> None:
        dialog = BatchTilesetImportDialog(self.library, self.asset_root, self.translate, self)
        from PySide6.QtWidgets import QFileDialog
        folder = QFileDialog.getExistingDirectory(self, self.translate("add_folder"))
        if not folder:
            return
        try:
            dialog.add_paths(self.library.discover_files(Path(folder), recursive=False))
        except ValueError as error:
            self.status_changed.emit(str(error)); return
        if dialog.exec():
            self.library.usage_index.rebuild(); self.refresh(); self.changed.emit()

    def reimport_selected(self) -> None:
        definition = self._selected_definition()
        if definition is None or self.asset_root is None:
            self.status_changed.emit(self.translate("tileset_asset_root_required")); return
        relative = definition.data.get("relativeAssetPath")
        if not isinstance(relative, str):
            self.status_changed.emit(self.translate("invalid")); return
        result = self.library.reimport(definition.definition_id, self.asset_root / relative, self.asset_root)
        if not result.ok:
            self.status_changed.emit("\n".join(issue.message for issue in result.diagnostics or [])); return
        self.refresh(); self.changed.emit(); self.status_changed.emit(self.translate("reimported"))

    def delete_selected(self) -> None:
        definition = self._selected_definition()
        if definition is None:
            return
        ok, diagnostics = self.library.delete(definition.definition_id)
        if not ok:
            QMessageBox.warning(self, self.translate("delete"), "\n".join(issue.message for issue in diagnostics))
            return
        self.refresh(); self.changed.emit()

    def _selected_definition(self):
        item = self.tilesets.currentItem()
        return self.library.workspace.find("tilesets", str(item.data(Qt.ItemDataRole.UserRole))) if item and self.library.workspace else None

    def _context_menu(self, position: object) -> None:
        item = self.tilesets.itemAt(position)  # type: ignore[arg-type]
        definition = self.library.workspace.find("tilesets", str(item.data(Qt.ItemDataRole.UserRole))) if item and self.library.workspace else None
        if definition is None or self.library.workspace is None:
            return
        self.tilesets.setCurrentItem(item)
        menu = self._tileset_context_menu(definition.definition_id)
        menu.exec(self.tilesets.viewport().mapToGlobal(position))  # type: ignore[arg-type]

    def _atlas_context_menu(self, position: object) -> None:
        definition = self._selected_definition()
        if definition is None:
            return
        menu = self._tileset_context_menu(definition.definition_id)
        menu.exec(self.atlas.tiles.viewport().mapToGlobal(position))  # type: ignore[arg-type]

    def _tileset_context_menu(self, tileset_id: str) -> QMenu:
        menu = QMenu(self)
        manage = menu.addAction(self.translate("configure_terrain_rule"))
        manage.triggered.connect(lambda: self._open_terrain_rule(tileset_id))
        return menu

    def _open_terrain_rule(self, tileset_id: str) -> None:
        if self.library.workspace is None:
            return
        dialog = TerrainRuleDialog(self.library.workspace, self.asset_root, tileset_id, self.translate, self,
                                   self.library.project)
        if dialog.exec():
            self.library.usage_index.rebuild()
            self.refresh()
            self.changed.emit()
            self.status_changed.emit(self.translate("terrain_rule_saved"))

    def _tileset_changed(self, item: QListWidgetItem | None, unused: QListWidgetItem | None = None) -> None:
        del unused
        if item is None:
            self.atlas.set_tileset("")
            self.reimport_button.setEnabled(False); self.delete_button.setEnabled(False)
            return
        definition = self._selected_definition()
        if definition is None:
            return
        self.atlas.set_context(self.library.workspace, self.asset_root)
        self.atlas.set_tileset(definition.definition_id)
        self.reimport_button.setEnabled(True); self.delete_button.setEnabled(True)

    def dragEnterEvent(self, event: QDragEnterEvent) -> None:
        if any(url.isLocalFile() and Path(url.toLocalFile()).suffix.casefold() in SUPPORTED_IMAGE_SUFFIXES for url in event.mimeData().urls()):
            event.acceptProposedAction()
        else:
            event.ignore()

    def dropEvent(self, event: QDropEvent) -> None:
        paths = [Path(url.toLocalFile()) for url in event.mimeData().urls() if url.isLocalFile() and Path(url.toLocalFile()).suffix.casefold() in SUPPORTED_IMAGE_SUFFIXES]
        if not paths:
            event.ignore(); return
        dialog = BatchTilesetImportDialog(self.library, self.asset_root, self.translate, self)
        dialog.add_paths(paths)
        if dialog.exec():
            self.library.usage_index.rebuild(); self.refresh(); self.changed.emit()
        event.acceptProposedAction()
