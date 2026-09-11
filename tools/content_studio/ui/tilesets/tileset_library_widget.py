from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QMimeData, Qt, Signal
from PySide6.QtGui import QDrag, QDragEnterEvent, QDragMoveEvent, QDropEvent
from PySide6.QtWidgets import (
    QAbstractItemView, QGridLayout, QInputDialog, QLabel, QMenu, QMessageBox, QPushButton,
    QSplitter, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget, QLineEdit,
)

from ...model.content_workspace import ContentWorkspace
from ...model.world_project import WorldProject
from ...services.import_service import SUPPORTED_IMAGE_SUFFIXES
from ...services.localization import Translator
from ...services.tileset_library import TilesetLibrary
from ..tileset_import_dialog import TilesetImportDialog
from .tile_atlas_widget import TileAtlasWidget
from .tileset_properties_dialog import TilesetPropertiesDialog
from ..terrain.terrain_rule_dialog import TerrainRuleDialog
from ..terrain.tile_semantic_editor import TileSemanticDialog

_TILESET_MIME = "application/x-dungeon-underworld-tileset-folder"
_ITEM_KIND_ROLE = Qt.ItemDataRole.UserRole + 1
_FOLDER_NAME_ROLE = Qt.ItemDataRole.UserRole + 2


class TilesetFolderTree(QTreeWidget):
    """Folder tree whose internal drops add a tileset to a tooling folder."""

    tileset_dropped = Signal(str, str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setHeaderHidden(True)
        self.setDragEnabled(True)
        self.setAcceptDrops(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DragDrop)
        self.setDefaultDropAction(Qt.DropAction.CopyAction)
        self.setDropIndicatorShown(True)

    def startDrag(self, supported_actions: Qt.DropActions) -> None:  # type: ignore[override]
        del supported_actions
        item = self.currentItem()
        tileset_id = item.data(0, Qt.ItemDataRole.UserRole) if item else None
        if not tileset_id or item.data(0, _ITEM_KIND_ROLE) != "tileset":
            return
        mime = QMimeData()
        mime.setData(_TILESET_MIME, str(tileset_id).encode("utf-8"))
        drag = QDrag(self)
        drag.setMimeData(mime)
        drag.exec(Qt.DropAction.CopyAction)

    def dragEnterEvent(self, event: QDragEnterEvent) -> None:
        if event.mimeData().hasFormat(_TILESET_MIME):
            event.acceptProposedAction()
            return
        event.ignore()

    def dragMoveEvent(self, event: QDragMoveEvent) -> None:
        folder = self._drop_folder(event.position().toPoint())
        if folder is not None:
            event.acceptProposedAction()
            return
        event.ignore()

    def dropEvent(self, event: QDropEvent) -> None:
        folder = self._drop_folder(event.position().toPoint())
        if folder is None:
            event.ignore()
            return
        tileset_id = bytes(event.mimeData().data(_TILESET_MIME)).decode("utf-8").strip()
        if not tileset_id:
            event.ignore()
            return
        self.tileset_dropped.emit(tileset_id, folder)
        event.acceptProposedAction()

    def _drop_folder(self, position: object) -> str | None:
        item = self.itemAt(position)  # type: ignore[arg-type]
        if item is not None and item.data(0, _ITEM_KIND_ROLE) == "tileset":
            item = item.parent()
        if item is None or item.data(0, _ITEM_KIND_ROLE) != "folder":
            return None
        value = item.data(0, _FOLDER_NAME_ROLE)
        return str(value) if value is not None else None


class TilesetLibraryWidget(QWidget):
    """A visual, searchable library of all project tilesets."""

    selected = Signal(str, int, int)
    brush_selected = Signal(str, object, int)
    status_changed = Signal(str)
    changed = Signal()
    folder_groups_changed = Signal(object)

    def __init__(self, workspace: ContentWorkspace | None = None, project: WorldProject | None = None,
                 asset_root: Path | None = None, translator: Translator | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator or Translator()
        self.library = TilesetLibrary(workspace, project)
        self._folder_groups: dict[str, list[str]] = {}
        self.asset_root = asset_root
        self.map_tile_size: int | None = None
        self.search = QLineEdit(); self.search.setPlaceholderText(self.translate("search_tilesets")); self.search.textChanged.connect(self.refresh)
        self.tilesets = TilesetFolderTree(); self.tilesets.currentItemChanged.connect(self._tileset_changed)
        self.tilesets.tileset_dropped.connect(self._tileset_dropped)
        self.tilesets.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.tilesets.customContextMenuRequested.connect(self._context_menu)
        self.atlas = TileAtlasWidget(); self.atlas.set_family_label(self.translate("family")); self.atlas.selected.connect(self.selected); self.atlas.brush_selected.connect(self.brush_selected)
        self.atlas.tiles.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.atlas.tiles.customContextMenuRequested.connect(self._atlas_context_menu)
        self.add_files_button = QPushButton(self.translate("import_tileset")); self.add_files_button.clicked.connect(self.add_files)
        self.new_folder_button = QPushButton(self.translate("new_tileset_folder")); self.new_folder_button.clicked.connect(self.create_tileset_folder)
        self.reimport_button = QPushButton(self.translate("reimport")); self.reimport_button.clicked.connect(self.reimport_selected)
        self.delete_button = QPushButton(self.translate("delete")); self.delete_button.clicked.connect(self.delete_selected)
        buttons = QGridLayout()
        for index, button in enumerate((self.add_files_button, self.new_folder_button, self.reimport_button, self.delete_button)):
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

    def set_folder_groups(self, groups: dict[str, list[str]] | None) -> None:
        self._folder_groups = {
            str(folder).strip(): list(dict.fromkeys(str(value) for value in tileset_ids))
            for folder, tileset_ids in (groups or {}).items()
            if str(folder).strip()
        }
        self.refresh()

    def folder_groups(self) -> dict[str, list[str]]:
        return {folder: list(tileset_ids) for folder, tileset_ids in self._folder_groups.items()}

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
        self.atlas.set_family_label(self.translate("family"))
        self.search.setPlaceholderText(self.translate("search_tilesets"))
        self.add_files_button.setText(self.translate("import_tileset")); self.new_folder_button.setText(self.translate("new_tileset_folder")); self.reimport_button.setText(self.translate("reimport")); self.delete_button.setText(self.translate("delete"))
        self.refresh()

    def refresh(self) -> None:
        current_item = self.tilesets.currentItem()
        current = current_item.data(0, Qt.ItemDataRole.UserRole) if current_item else None
        self.tilesets.blockSignals(True); self.tilesets.clear()
        definitions = self.library.definitions(self.search.text())
        definitions_by_id = {definition.definition_id: definition for definition in definitions}
        assigned_ids: set[str] = set()
        selected_item: QTreeWidgetItem | None = None
        for folder, member_ids in sorted(self._folder_groups.items(), key=lambda value: value[0].casefold()):
            members = [definitions_by_id[value] for value in member_ids if value in definitions_by_id]
            if self.search.text().strip() and not members:
                continue
            folder_item = self._folder_item(folder, folder)
            self.tilesets.addTopLevelItem(folder_item)
            for definition in members:
                child = self._definition_item(definition)
                folder_item.addChild(child)
                assigned_ids.add(definition.definition_id)
                if definition.definition_id == current and selected_item is None:
                    selected_item = child
            folder_item.setExpanded(True)
        unassigned = [definition for definition in definitions if definition.definition_id not in assigned_ids]
        if unassigned:
            folder_item = self._folder_item(self.translate("tileset_no_folder"), "")
            self.tilesets.addTopLevelItem(folder_item)
            for definition in unassigned:
                child = self._definition_item(definition)
                folder_item.addChild(child)
                if definition.definition_id == current and selected_item is None:
                    selected_item = child
            folder_item.setExpanded(True)
        self.tilesets.blockSignals(False)
        if selected_item is None:
            selected_item = self._first_enabled_tileset_item()
        if selected_item is not None:
            self.tilesets.setCurrentItem(selected_item)
        else:
            self.atlas.set_tileset("")
        has_selection = self._selected_definition() is not None
        self.reimport_button.setEnabled(has_selection)
        self.delete_button.setEnabled(has_selection)

    def _folder_item(self, label: str, folder: str) -> QTreeWidgetItem:
        item = QTreeWidgetItem([label])
        item.setData(0, _ITEM_KIND_ROLE, "folder")
        item.setData(0, _FOLDER_NAME_ROLE, folder)
        item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsDragEnabled)
        return item

    def _definition_item(self, definition: object) -> QTreeWidgetItem:
        display_name = str(definition.display_name)  # type: ignore[attr-defined]
        item = QTreeWidgetItem([display_name])
        item.setToolTip(0, str(definition.definition_id))  # type: ignore[attr-defined]
        item.setData(0, Qt.ItemDataRole.UserRole, definition.definition_id)  # type: ignore[attr-defined]
        item.setData(0, _ITEM_KIND_ROLE, "tileset")
        size = definition.data.get("tileSize")  # type: ignore[attr-defined]
        compatible = self.map_tile_size is None or size == self.map_tile_size
        if not compatible:
            item.setText(0, f"{display_name} ({size}px — {self.translate('incompatible')})")
            item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsEnabled)
        return item

    def _first_enabled_tileset_item(self) -> QTreeWidgetItem | None:
        for index in range(self.tilesets.topLevelItemCount()):
            folder = self.tilesets.topLevelItem(index)
            for child_index in range(folder.childCount()):
                child = folder.child(child_index)
                if child.flags() & Qt.ItemFlag.ItemIsEnabled:
                    return child
        return None

    def add_files(self) -> None:
        from PySide6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getOpenFileName(
            self, self.translate("import_tileset"), "",
            "Images (*.png *.jpg *.jpeg *.bmp *.gif)",
        )
        if not path:
            return
        self._import_one_tileset(Path(path))

    def create_tileset_folder(self) -> None:
        name, accepted = QInputDialog.getText(
            self, self.translate("new_tileset_folder"), self.translate("tileset_folder_name"),
        )
        if not accepted:
            return
        normalized = name.strip()
        if not normalized:
            return
        if self._folder_named(normalized) is not None:
            QMessageBox.warning(self, self.translate("tileset_folder"), self.translate("tileset_folder_exists"))
            return
        self._folder_groups[normalized] = []
        self._folders_changed()

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
        for member_ids in self._folder_groups.values():
            while definition.definition_id in member_ids:
                member_ids.remove(definition.definition_id)
        self.folder_groups_changed.emit(self.folder_groups())
        self.refresh(); self.changed.emit()

    def _selected_definition(self):
        item = self.tilesets.currentItem()
        return self.library.workspace.find("tilesets", str(item.data(0, Qt.ItemDataRole.UserRole))) if item and item.data(0, _ITEM_KIND_ROLE) == "tileset" and self.library.workspace else None

    def _context_menu(self, position: object) -> None:
        item = self.tilesets.itemAt(position)  # type: ignore[arg-type]
        if item is None:
            return
        self.tilesets.setCurrentItem(item)
        if item.data(0, _ITEM_KIND_ROLE) == "folder":
            menu = self._folder_context_menu(str(item.data(0, _FOLDER_NAME_ROLE)))
        else:
            definition = self._selected_definition()
            if definition is None:
                return
            parent = item.parent()
            current_folder = str(parent.data(0, _FOLDER_NAME_ROLE)) if parent else ""
            menu = self._tileset_context_menu(definition.definition_id, current_folder)
        menu.exec(self.tilesets.viewport().mapToGlobal(position))  # type: ignore[arg-type]

    def _atlas_context_menu(self, position: object) -> None:
        definition = self._selected_definition()
        item = self.atlas.tiles.itemAt(position)  # type: ignore[arg-type]
        if definition is None or item is None:
            return
        source_index = item.data(Qt.ItemDataRole.UserRole)
        if not isinstance(source_index, int):
            return
        self.atlas.tiles.setCurrentItem(item)
        menu = self._atlas_tile_context_menu(definition.definition_id, source_index)
        menu.exec(self.atlas.tiles.viewport().mapToGlobal(position))  # type: ignore[arg-type]

    def _atlas_tile_context_menu(self, tileset_id: str, source_index: int) -> QMenu:
        menu = QMenu(self)
        edit = menu.addAction(self.translate("edit_tile_name_family"))
        edit.triggered.connect(lambda: self._open_tile_semantic(tileset_id, source_index))
        menu.addSeparator()
        manage = menu.addAction(self.translate("configure_terrain_rule"))
        manage.triggered.connect(lambda: self._open_terrain_rule(tileset_id))
        return menu

    def _tileset_context_menu(self, tileset_id: str, current_folder: str = "") -> QMenu:
        menu = QMenu(self)
        manage_tileset = menu.addAction(self.translate("manage_tileset"))
        manage_tileset.triggered.connect(lambda: self._open_tileset_properties(tileset_id))
        folders = menu.addMenu(self.translate("add_to_tileset_folder"))
        create_folder = folders.addAction(self.translate("new_tileset_folder"))
        create_folder.triggered.connect(self.create_tileset_folder)
        if self._folder_groups:
            folders.addSeparator()
        for folder in sorted(self._folder_groups, key=str.casefold):
            action = folders.addAction(folder)
            action.setCheckable(True)
            action.setChecked(tileset_id in self._folder_groups[folder])
            action.toggled.connect(
                lambda checked, value=folder: self._assign_tileset_folder(tileset_id, value, checked))
        if current_folder:
            remove_from_folder = menu.addAction(self.translate("remove_from_tileset_folder"))
            remove_from_folder.triggered.connect(
                lambda: self._assign_tileset_folder(tileset_id, current_folder, False))
        menu.addSeparator()
        manage = menu.addAction(self.translate("configure_terrain_rule"))
        manage.triggered.connect(lambda: self._open_terrain_rule(tileset_id))
        return menu

    def _folder_context_menu(self, folder: str) -> QMenu:
        menu = QMenu(self)
        if not folder:
            create = menu.addAction(self.translate("new_tileset_folder"))
            create.triggered.connect(self.create_tileset_folder)
            return menu
        import_tileset = menu.addAction(self.translate("import_tileset"))
        import_tileset.triggered.connect(lambda: self._add_files_to_folder(folder))
        menu.addSeparator()
        rename = menu.addAction(self.translate("rename_tileset_folder"))
        rename.triggered.connect(lambda: self._rename_tileset_folder(folder))
        delete = menu.addAction(self.translate("delete_tileset_folder"))
        delete.triggered.connect(lambda: self._delete_tileset_folder(folder))
        return menu

    def _add_files_to_folder(self, folder: str) -> None:
        from PySide6.QtWidgets import QFileDialog
        path, _ = QFileDialog.getOpenFileName(
            self, self.translate("import_tileset"), "",
            "Images (*.png *.jpg *.jpeg *.bmp *.gif)",
        )
        if path:
            self._import_one_tileset(Path(path), folder)

    def _import_one_tileset(self, path: Path, folder: str = "") -> None:
        workspace = self.library.workspace
        if workspace is None or (folder and folder not in self._folder_groups):
            return
        dialog = TilesetImportDialog(
            workspace, self.asset_root, translator=self.translate, parent=self,
        )
        dialog.tileset_id.setText(self.library.suggest_id(path))
        dialog.display_name.setText(path.stem)
        dialog.source.setText(str(path))
        if not dialog.exec():
            return
        tileset_id = dialog.tileset_id.text().strip()
        if folder and tileset_id not in self._folder_groups[folder]:
            self._folder_groups[folder].append(tileset_id)
            self.folder_groups_changed.emit(self.folder_groups())
        self.library.usage_index.rebuild()
        self.refresh()
        self.changed.emit()

    def _folder_named(self, name: str) -> str | None:
        folded = name.casefold()
        return next((folder for folder in self._folder_groups if folder.casefold() == folded), None)

    def _rename_tileset_folder(self, folder: str) -> None:
        name, accepted = QInputDialog.getText(
            self, self.translate("rename_tileset_folder"), self.translate("tileset_folder_name"), text=folder,
        )
        normalized = name.strip()
        if not accepted or not normalized or normalized == folder:
            return
        existing = self._folder_named(normalized)
        if existing is not None and existing != folder:
            QMessageBox.warning(self, self.translate("tileset_folder"), self.translate("tileset_folder_exists"))
            return
        members = self._folder_groups.pop(folder, [])
        self._folder_groups[normalized] = members
        self._folders_changed()

    def _delete_tileset_folder(self, folder: str) -> None:
        answer = QMessageBox.question(
            self, self.translate("delete_tileset_folder"),
            self.translate("tileset_folder_delete_confirm", folder=folder),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        self._folder_groups.pop(folder, None)
        self._folders_changed()

    def _tileset_dropped(self, tileset_id: str, folder: str) -> None:
        if folder:
            self._assign_tileset_folder(tileset_id, folder, True)
            return
        changed = False
        for member_ids in self._folder_groups.values():
            while tileset_id in member_ids:
                member_ids.remove(tileset_id)
                changed = True
        if changed:
            self._folders_changed(tileset_id)

    def _assign_tileset_folder(self, tileset_id: str, folder: str, assigned: bool) -> None:
        member_ids = self._folder_groups.get(folder)
        if member_ids is None:
            return
        changed = False
        if assigned and tileset_id not in member_ids:
            member_ids.append(tileset_id)
            changed = True
        elif not assigned and tileset_id in member_ids:
            member_ids.remove(tileset_id)
            changed = True
        if changed:
            self._folders_changed(tileset_id)

    def _folders_changed(self, selected_tileset_id: str = "") -> None:
        if selected_tileset_id:
            for index in range(self.tilesets.topLevelItemCount()):
                folder = self.tilesets.topLevelItem(index)
                for child_index in range(folder.childCount()):
                    child = folder.child(child_index)
                    if child.data(0, Qt.ItemDataRole.UserRole) == selected_tileset_id:
                        self.tilesets.setCurrentItem(child)
                        break
        self.folder_groups_changed.emit(self.folder_groups())
        self.refresh()

    def _open_tileset_properties(self, tileset_id: str) -> None:
        try:
            dialog = TilesetPropertiesDialog(
                self.library, tileset_id, self.asset_root, self.translate, self,
                folder_groups=self._folder_groups,
            )
        except ValueError as error:
            self.status_changed.emit(str(error))
            return
        if dialog.exec():
            selected_folders = dialog.selected_folders()
            folders_changed = False
            for folder, member_ids in self._folder_groups.items():
                should_be_member = folder in selected_folders
                is_member = tileset_id in member_ids
                if should_be_member and not is_member:
                    member_ids.append(tileset_id)
                    folders_changed = True
                elif not should_be_member and is_member:
                    member_ids.remove(tileset_id)
                    folders_changed = True
            if folders_changed:
                self.folder_groups_changed.emit(self.folder_groups())
            self.library.usage_index.rebuild()
            self.refresh()
            self.changed.emit()
            self.status_changed.emit(self.translate("tileset_properties_saved"))

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

    def _open_tile_semantic(self, tileset_id: str, source_index: int) -> None:
        if self.library.workspace is None:
            return
        dialog = TileSemanticDialog(
            self.library.workspace, tileset_id, source_index, self.translate, self,
        )
        if dialog.exec():
            self.library.usage_index.rebuild()
            self.atlas.refresh()
            self.changed.emit()
            self.status_changed.emit(self.translate("semantic_saved"))

    def _tileset_changed(self, item: QTreeWidgetItem | None, unused: QTreeWidgetItem | None = None) -> None:
        del unused
        if item is None or item.data(0, _ITEM_KIND_ROLE) != "tileset":
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
        if len(paths) != 1:
            event.ignore(); return
        self._import_one_tileset(paths[0])
        event.acceptProposedAction()
