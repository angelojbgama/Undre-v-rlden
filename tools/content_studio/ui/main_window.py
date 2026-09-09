from __future__ import annotations

import shutil
import sys
import tempfile
from pathlib import Path

from PySide6.QtCore import QTimer, Qt
from PySide6.QtGui import QAction, QActionGroup
from PySide6.QtWidgets import (
    QApplication, QFileDialog, QInputDialog, QMainWindow, QMessageBox,
    QPlainTextEdit, QSplitter, QTabWidget, QToolBar, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import MapDocument
from ..model.types import ContentDefinition, Diagnostic
from ..model.world_project import WorldProject
from ..formats.uworld import write_world
from ..interaction.command_coordinator import CommandCoordinator
from ..interaction.drag_payload import StudioDragPayload
from ..services.import_service import ImportService
from ..services.localization import Translator
from ..services.autosave import autosave
from ..services.preferences import load_preferences, save_preferences
from ..services.toolchain import CppToolchain, PlaytestService
from .map_canvas import MapCanvas
from .preview import PreviewWidget
from .scene_editor import SceneEditorWidget
from .tileset_import_dialog import TilesetImportDialog
from .widgets import AssetBrowser, CollectionPanel, ContentBrowser, LayersPanel, MapBrowser, MapElementsPalette, SemanticPalette, StructuredInspector, TilePalette, set_path


class MainWindow(QMainWindow):
    def __init__(self, project: WorldProject | None = None, workspace: ContentWorkspace | None = None,
                 asset_root: Path | None = None, toolchain: CppToolchain | None = None) -> None:
        super().__init__()
        self.preferences = load_preferences()
        self.translator = Translator(self.preferences.language)
        self.project = project or WorldProject.new()
        self.workspace = workspace
        self.asset_root = asset_root or (Path(self.preferences.asset_root) if self.preferences.asset_root else None)
        self.toolchain = toolchain or CppToolchain(asset_root=self.asset_root)
        self.playtest = PlaytestService(self.toolchain)
        self.import_service = ImportService()
        self.command_coordinator = CommandCoordinator()
        self.selected_definition: ContentDefinition | None = None
        self._definition_history: list[tuple[str, str]] = []
        self._last_content_definition: tuple[str, str] | None = None
        self.diagnostics: list[Diagnostic] = []
        self.setWindowTitle(self.translator("app"))
        self.resize(1400, 900)
        self._build_actions()
        self._build_ui()
        self.autosave_timer = QTimer(self)
        self.autosave_timer.setInterval(60_000)
        self.autosave_timer.timeout.connect(self._autosave)
        self.autosave_timer.start()
        self._refresh_all()

    def _build_actions(self) -> None:
        file_menu = self.menuBar().addMenu(self.translator("file"))
        edit_menu = self.menuBar().addMenu(self.translator("edit"))
        view_menu = self.menuBar().addMenu(self.translator("view"))
        self._menus = {"file": file_menu, "edit": edit_menu, "view": view_menu}
        self.actions: dict[str, QAction] = {}
        for key, title, callback in (("new", "New Project", self.new_project), ("open", "Open Project...", self.open_project), ("new_content", "New Content Workspace...", self.new_content), ("open_content", "Open Content...", self.open_content), ("save", "Save", self.save), ("save_as", "Save As...", self.save_as), ("save_all", "Save All", self.save_all), ("validate", "Validate Workspace", self.validate), ("export", "Export DMAP", self.export_maps), ("playtest", "Playtest", self.toggle_playtest), ("import_tileset", "Import Tileset...", self.import_tileset), ("quit", "Exit", self.close)):
            action = QAction(title, self); action.triggered.connect(callback); self.actions[key] = action; file_menu.addAction(action)
        self.actions["new"].setShortcut("Ctrl+Shift+N")
        self.actions["open"].setShortcut("Ctrl+O")
        self.actions["save"].setShortcut("Ctrl+S")
        self.actions["save_as"].setShortcut("Ctrl+Shift+S")
        self.actions["validate"].setShortcut("F7")
        self.actions["playtest"].setShortcut("F5")
        self.actions["undo"] = QAction("Undo", self); self.actions["undo"].setShortcut("Ctrl+Z"); self.actions["undo"].triggered.connect(self.undo); edit_menu.addAction(self.actions["undo"])
        self.actions["redo"] = QAction("Redo", self); self.actions["redo"].setShortcut("Ctrl+Y"); self.actions["redo"].triggered.connect(self.redo); edit_menu.addAction(self.actions["redo"])
        grid = QAction(self.translator("grid"), self, checkable=True, checked=True); grid.triggered.connect(self.map_canvas_grid); view_menu.addAction(grid); self.actions["grid"] = grid
        snap = QAction(self.translator("snap"), self, checkable=True, checked=True); snap.triggered.connect(self.map_canvas_snap); view_menu.addAction(snap); self.actions["snap"] = snap
        overlays = QAction(self.translator("overlays"), self, checkable=True, checked=False); overlays.triggered.connect(self.map_canvas_overlays); view_menu.addAction(overlays); self.actions["overlays"] = overlays
        frame = QAction("Frame Map", self); frame.setShortcut("Home"); frame.triggered.connect(lambda: self.map_canvas.fit_map()); view_menu.addAction(frame); self.actions["frame"] = frame
        language = view_menu.addMenu(self.translator("language")); self._language_menu = language
        for code, name in (("pt-BR", "Português (Brasil)"), ("en-US", "English")):
            action = QAction(name, self); action.triggered.connect(lambda checked=False, value=code: self.set_language(value)); language.addAction(action)

    def _build_ui(self) -> None:
        toolbar = QToolBar(self.translator("tools"), self)
        self._toolbar = toolbar
        self.addToolBar(toolbar)
        tool_group = QActionGroup(self)
        tool_group.setExclusive(True)
        self._tool_keys: list[str] = []
        select = QAction(self.translator("select"), self); select.setCheckable(True); select.setChecked(True); select.triggered.connect(lambda: self.set_tool("select")); tool_group.addAction(select); toolbar.addAction(select)
        toolbar.addAction(self.actions["grid"])
        toolbar.addAction(self.actions["snap"])
        toolbar.addAction(self.actions["overlays"])
        toolbar.addSeparator()
        toolbar.addAction(self.actions["playtest"])
        self.tool_actions = [select, self.actions["grid"], self.actions["snap"], self.actions["overlays"], self.actions["playtest"]]
        self._tool_keys = ["select", "grid", "snap", "overlays", "playtest_toolbar"]
        self.mode_tabs = QTabWidget()
        self.map_canvas = MapCanvas()
        self.map_canvas.selection_changed.connect(self._map_selection_changed)
        self.map_canvas.document_changed.connect(self._map_changed)
        self.map_canvas.status_changed.connect(self.set_status)
        self.map_browser = MapBrowser()
        self.map_browser.selected.connect(self._select_map)
        self.map_browser.new_requested.connect(self.new_map)
        self.map_browser.import_requested.connect(self.import_map)
        self.map_browser.remove_requested.connect(self.remove_map)
        self.map_browser.entry_requested.connect(self.set_entry_map)
        self.layers = LayersPanel()
        self.layers.changed.connect(self._map_changed)
        self.layers.selected.connect(self._layer_selected)
        self.tile_palette = TilePalette()
        self.tile_palette.selected.connect(self._tile_selected)
        self.tile_palette.brush_selected.connect(self._brush_selected)
        self.semantic_palette = SemanticPalette()
        self.semantic_palette.tile_selected.connect(self._tile_selected)
        self.semantic_palette.stamp_selected.connect(self.map_canvas.set_stamp_selection)
        self.map_elements = MapElementsPalette({
            "player_spawn": self.translator("player_spawn"), "map_transition": self.translator("map_transition"),
            "region": self.translator("region_element"), "hint": self.translator("map_elements_hint"),
        })
        self.map_elements.selected.connect(self.map_canvas.set_active_payload)
        self.map_collections = {
            name: CollectionPanel(name, label)
            for name, label in (("links", "Map Links"), ("playerSpawns", "Player Spawns"), ("regions", "Regions"), ("worldRules", "World Rules"), ("encounters", "Encounters"))
        }
        for panel in self.map_collections.values():
            panel.changed.connect(self._map_changed)
        self.entity_browser = ContentBrowser(self.workspace, ("enemies", "npcs", "objects", "pickups"), translator=self.translator)
        self.entity_browser.selected.connect(self._entity_selected)
        self.entity_browser.place_requested.connect(self._place_definition)
        self.entity_browser.definition_changed.connect(self._content_changed)
        self.map_inspector = StructuredInspector()
        self.map_inspector.changed.connect(self._edit_map_field)
        map_left_tabs = QTabWidget()
        map_left_tabs.addTab(self.map_browser, "Maps")
        map_left_tabs.addTab(self.layers, "Layers")
        map_left_tabs.addTab(self.tile_palette, "Tiles")
        map_left_tabs.addTab(self.semantic_palette, "Semantics / Stamps")
        map_left_tabs.addTab(self.map_elements, self.translator("map_elements"))
        map_left_tabs.addTab(self.entity_browser, "Entities")
        self.scene_editor = SceneEditorWidget()
        self.scene_editor.changed.connect(self._map_changed)
        self.scene_editor.diagnostics_changed.connect(self._refresh_diagnostics)
        map_left_tabs.addTab(self.scene_editor, "Scenes")
        collections_tabs = QTabWidget()
        for name, panel in self.map_collections.items():
            collections_tabs.addTab(panel, panel.windowTitle() or name)
        map_left_tabs.addTab(collections_tabs, "Rules / Links")
        self._map_left_tabs = map_left_tabs
        self._collections_tabs = collections_tabs
        map_split = QSplitter(Qt.Orientation.Horizontal)
        map_split.addWidget(map_left_tabs); map_split.addWidget(self.map_canvas); map_split.addWidget(self.map_inspector)
        map_split.setStretchFactor(1, 1)
        self._map_split = map_split
        map_split.setSizes([self.preferences.left_panel_width, 700, self.preferences.right_panel_width])
        map_page = QWidget(); map_layout = QVBoxLayout(map_page); map_layout.addWidget(map_split)
        self.mode_tabs.addTab(map_page, "MAP")

        self.content_browser = ContentBrowser(self.workspace, translator=self.translator)
        self.content_browser.selected.connect(self._content_selected)
        self.content_browser.place_requested.connect(self._place_definition)
        self.content_browser.find_usages_requested.connect(self._show_usages)
        self.content_browser.back_requested.connect(self._back_definition)
        self.content_browser.definition_changed.connect(self._content_changed)
        self.content_inspector = StructuredInspector()
        self.content_inspector.changed.connect(self._edit_content_field)
        self.content_inspector.collection_changed.connect(self._edit_content_collection)
        self.asset_browser = AssetBrowser()
        self.asset_browser.selected.connect(self._asset_selected)
        self.asset_browser.assign_requested.connect(self._assign_asset)
        self.preview = PreviewWidget()
        content_browsers = QTabWidget()
        content_browsers.addTab(self.content_browser, "Definitions")
        content_browsers.addTab(self.asset_browser, "Assets")
        content_split = QSplitter(Qt.Orientation.Horizontal)
        content_split.addWidget(content_browsers); content_split.addWidget(self.preview); content_split.addWidget(self.content_inspector)
        content_split.setStretchFactor(1, 1)
        self._content_split = content_split
        content_split.setSizes([self.preferences.left_panel_width, 700, self.preferences.right_panel_width])
        content_page = QWidget(); content_layout = QVBoxLayout(content_page); content_layout.addWidget(content_split)
        self.mode_tabs.addTab(content_page, "CONTENT")
        self.mode_tabs.currentChanged.connect(lambda index: self.command_coordinator.mark("content" if index == 1 else "map"))
        self._content_browsers = content_browsers
        self.diagnostics_view = QPlainTextEdit(); self.diagnostics_view.setReadOnly(True); self.diagnostics_view.setMaximumHeight(150)
        root = QSplitter(Qt.Orientation.Vertical); root.addWidget(self.mode_tabs); root.addWidget(self.diagnostics_view); root.setStretchFactor(0, 1)
        self.setCentralWidget(root)
        self.statusBar().showMessage(self.translator("ready"))
        self._retranslate_ui()

    def _retranslate_ui(self) -> None:
        """Retranslate the application shell without replacing native editors."""
        labels = {
            "new": "new_project", "open": "open_project", "new_content": "new_content",
            "open_content": "open_content", "save": "save", "save_as": "save_as",
            "save_all": "save_all", "validate": "validate", "export": "export",
            "playtest": "playtest", "import_tileset": "import_tileset", "quit": "quit", "undo": "undo", "redo": "redo",
            "grid": "grid", "frame": "frame",
        }
        for action_key, translation_key in labels.items():
            if action_key in self.actions:
                self.actions[action_key].setText(self.translator(translation_key))
        self._menus["file"].setTitle(self.translator("file"))
        self._menus["edit"].setTitle(self.translator("edit"))
        self._menus["view"].setTitle(self.translator("view"))
        self._language_menu.setTitle(self.translator("language"))
        for action, translation_key in zip(self.tool_actions, self._tool_keys):
            action.setText(self.translator(translation_key))
        self._toolbar.setWindowTitle(self.translator("tools"))
        self.mode_tabs.setTabText(0, self.translator("map"))
        self.mode_tabs.setTabText(1, self.translator("content"))
        for index, key in enumerate(("maps", "layers", "tiles", "semantics_stamps", "map_elements", "entities", "scenes", "rules_links")):
            if index < self._map_left_tabs.count():
                self._map_left_tabs.setTabText(index, self.translator(key))
        self._content_browsers.setTabText(0, self.translator("definitions"))
        self._content_browsers.setTabText(1, self.translator("assets"))
        self.entity_browser.set_translator(self.translator)
        self.content_browser.set_translator(self.translator)
        self.map_elements.retranslate({
            "player_spawn": self.translator("player_spawn"), "map_transition": self.translator("map_transition"),
            "region": self.translator("region_element"), "hint": self.translator("map_elements_hint"),
        })

    def _refresh_all(self) -> None:
        self.map_browser.refresh([document.map_id for document in self.project.maps], self.project.active_map.map_id)
        self._refresh_map()
        self.content_browser.set_workspace(self.workspace)
        self.entity_browser.set_workspace(self.workspace)
        self.tile_palette.set_workspace(self.workspace)
        self.tile_palette.set_asset_root(self.asset_root)
        self.semantic_palette.set_workspace(self.workspace)
        self.content_inspector.set_workspace(self.workspace)
        self.scene_editor.inspector.set_workspace(self.workspace)
        self.map_inspector.set_workspace(self.workspace)
        map_ids = [value.map_id for value in self.project.maps]
        self.map_inspector.set_map_ids(map_ids)
        for panel in self.map_collections.values():
            panel.set_map_ids(map_ids)
        self.asset_browser.set_roots(self.asset_root, self.workspace.root if self.workspace else None)
        self._refresh_diagnostics(self.diagnostics)
        self._update_title()

    def _refresh_map(self) -> None:
        document = self.project.active_map
        self.map_canvas.set_context(document, self.workspace, self.asset_root)
        self.layers.set_document(document)
        self.scene_editor.set_document(document)
        for panel in self.map_collections.values():
            panel.set_document(document)
        self.map_browser.refresh([value.map_id for value in self.project.maps], document.map_id)
        if self.map_canvas.selected_entity:
            self._map_selection_changed(self.map_canvas.selected_entity)

    def _map_changed(self) -> None:
        self.command_coordinator.mark("map")
        self._refresh_map()

    def _entity_selected(self, definition: ContentDefinition | None) -> None:
        self.selected_definition = definition
        if definition:
            self.content_inspector.set_object(f"{definition.display_name} [{definition.origin}]", definition.data)
            self.preview.show_definition(definition, self.workspace, self.asset_root)
            self.map_canvas.set_entity_selection(definition.category, definition.definition_id)

    def _content_selected(self, definition: ContentDefinition | None) -> None:
        if definition:
            key = (definition.category, definition.definition_id)
            if self._last_content_definition and self._last_content_definition != key:
                self._definition_history.append(self._last_content_definition)
            self._last_content_definition = key
        self.selected_definition = definition
        if definition:
            self.content_inspector.set_object(f"{definition.display_name} [{definition.origin}]", definition.data)
            self.preview.show_definition(definition, self.workspace, self.asset_root)

    def _show_usages(self, definition: object) -> None:
        if not self.workspace or not isinstance(definition, ContentDefinition):
            return
        usages = self.workspace.find_usages(definition.definition_id)
        if not usages:
            self.set_status("No usages found")
            return
        labels = [f"{item.display_name} [{item.category}:{item.definition_id}]" for item in usages]
        chosen, accepted = QInputDialog.getItem(self, "Find Usages", "Referenced by:", labels, 0, False)
        if accepted:
            index = labels.index(chosen)
            target = usages[index]
            self.content_browser.select_definition(target.category, target.definition_id)

    def _back_definition(self) -> None:
        if not self._definition_history:
            self.set_status("No previous definition")
            return
        category, definition_id = self._definition_history.pop()
        self._last_content_definition = None
        self.content_browser.select_definition(category, definition_id)

    def _asset_selected(self, entry: object) -> None:
        if entry is None:
            self.preview.show_asset(None)
            self.set_status("No asset selected")
            return
        self.preview.show_asset(entry)  # type: ignore[arg-type]
        self.set_status(str(entry.relative_path))  # type: ignore[attr-defined]

    def _assign_asset(self, entry: object) -> None:
        if not self.workspace or not self.selected_definition or not hasattr(entry, "relative_path"):
            return
        definition = self.workspace.find(self.selected_definition.category, self.selected_definition.definition_id)
        if definition is None or definition.origin != "project":
            self.set_status("Builtin definitions are read-only")
            return
        try:
            if definition.category == "visualImages":
                self.workspace.update(definition, "root", "gameAssets" if entry.root == "gameAssets" else "contentWorkspace")  # type: ignore[attr-defined]
                self.workspace.update(definition, "relativePath", entry.relative_path.as_posix())  # type: ignore[attr-defined]
            elif definition.category == "tilesets":
                self.workspace.update(definition, "relativeAssetPath", entry.relative_path.as_posix())  # type: ignore[attr-defined]
            else:
                self.set_status("Select a Visual Image or Tileset definition to assign an asset")
                return
            self._refresh_all(); self.set_status("Asset assigned")
        except (KeyError, TypeError, ValueError) as error:
            self.set_status(str(error))

    def _edit_content_field(self, path: str, value: object) -> None:
        if self.workspace and self.selected_definition:
            try:
                self.command_coordinator.mark("content")
                self.workspace.update(self.selected_definition, path, value)
                self.set_status("Definition edited")
                self._refresh_diagnostics(self.workspace.validate_local(self.selected_definition))
                self._refresh_all()
            except (KeyError, TypeError, ValueError) as error:
                self.set_status(str(error))

    def _edit_content_collection(self, path: str, action: str) -> None:
        if not self.workspace or not self.selected_definition:
            return
        try:
            self.command_coordinator.mark("content")
            self.workspace.mutate_collection(self.selected_definition, path, action)
            self.set_status("Collection updated")
            self._refresh_all()
        except (KeyError, TypeError, ValueError) as error:
            self.set_status(str(error))

    def _map_selection_changed(self, selection: object) -> None:
        if not selection:
            self.map_inspector.clear("No selection")
            return
        category, identifier = selection
        value = self.project.active_map.entity(category, identifier) if category in {"enemies", "npcs", "objects", "pickups"} else next((entry for entry in self.project.active_map.all_collection(category) if entry.get("id") == identifier), None)
        if value is not None:
            self.map_inspector.set_object(f"{category}: {identifier}", value)

    def _edit_map_field(self, path: str, value: object) -> None:
        selection = self.map_canvas.selected_entity
        if not selection:
            return
        category, identifier = selection
        if category in {"enemies", "npcs", "objects", "pickups"}:
            value_to_edit = self.project.active_map.entity(category, int(identifier))
        else:
            value_to_edit = next((entry for entry in self.project.active_map.all_collection(category)
                                  if isinstance(entry, dict) and entry.get("id") == identifier), None)
        if value_to_edit is None:
            return
        self.command_coordinator.mark("map")
        self.project.active_map.mutate("Edit Placement", lambda: set_path(value_to_edit, path, value))
        self._refresh_map()

    def _place_definition(self, category: str, definition_id: str) -> None:
        if self.workspace:
            definition = self.workspace.find(category, definition_id)
            if definition is None:
                self._refresh_diagnostics([Diagnostic("error", f"definition not found: {definition_id}", code="missing_definition")])
                return
            local_issues = self.workspace.validate_local(definition)
            if any(issue.is_error for issue in local_issues):
                self._refresh_diagnostics(local_issues)
                self.set_status(f"Cannot place {definition_id}: fix its dependencies first")
                return
        self.mode_tabs.setCurrentIndex(0)
        self.map_canvas.set_entity_selection(category, definition_id)
        self.set_status(f"Placement active: {definition_id}. Click the map or press Escape.")

    def _content_changed(self) -> None:
        self.command_coordinator.mark("content")
        self._refresh_all()

    def set_tool(self, tool: str) -> None:
        self.map_canvas.set_tool(tool)
        self.set_status(f"Tool: {tool}")

    def map_canvas_grid(self, checked: bool) -> None:
        self.map_canvas.set_grid_visible(checked)

    def map_canvas_snap(self, checked: bool) -> None:
        self.map_canvas.set_snap_enabled(checked)

    def map_canvas_overlays(self, checked: bool) -> None:
        self.map_canvas.set_collision_overlay(checked)

    def import_tileset(self) -> None:
        if self.workspace is None:
            self.show_error("Open a content workspace before importing a tileset")
            return
        dialog = TilesetImportDialog(self.workspace, self.asset_root, self.import_service, self.translator, self)
        if dialog.exec():
            self.command_coordinator.mark("content")
            self._refresh_all()
            self.set_status("Tileset imported")

    def _tile_selected(self, tileset_id: str, source_index: int, flags: int) -> None:
        self.map_canvas.selected_tile = (tileset_id, source_index, flags)
        self.map_canvas.set_brush(tileset_id, [source_index], flags)
        self.set_status(f"Tile selected: {tileset_id} [{source_index}]")

    def _brush_selected(self, tileset_id: str, source_indices: object, flags: int) -> None:
        if isinstance(source_indices, list) and all(isinstance(value, int) for value in source_indices):
            self.map_canvas.set_brush(tileset_id, source_indices, flags)

    def _layer_selected(self, index: int) -> None:
        self.map_canvas.set_layer(index)

    def _select_map(self, map_id: str) -> None:
        try:
            self.project.select_map(map_id); self._refresh_map()
        except ValueError as error:
            self.set_status(str(error))

    def new_project(self) -> None:
        if not self._confirm_unsaved():
            return
        self.project = WorldProject.new(); self._refresh_all(); self.set_status("New blank project")

    def new_content(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "New Content Workspace")
        if not path or not self._confirm_unsaved():
            return
        try:
            self.workspace = ContentWorkspace.new(Path(path))
            self.workspace.save_all()
            self._refresh_all()
            self.set_status("New project content workspace")
        except OSError as error:
            self.show_error(str(error))

    def new_map(self) -> None:
        map_id, accepted = QInputDialog.getText(self, "New Map", "MapId:", text=f"map.{len(self.project.maps) + 1}")
        if not accepted or not map_id.strip():
            return
        width, accepted = QInputDialog.getInt(self, "New Map", "Width:", 32, 1, 4096)
        if not accepted: return
        height, accepted = QInputDialog.getInt(self, "New Map", "Height:", 24, 1, 4096)
        if not accepted: return
        try:
            self.project.add_map(MapDocument.new(map_id.strip(), width, height))
            self._refresh_all()
        except ValueError as error:
            self.show_error(str(error))

    def open_project(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Open Authored Project", "", "Authored (*.uworld *.umap)")
        if not path: return
        if not self._confirm_unsaved(): return
        project, diagnostics = WorldProject.open(Path(path))
        if project is None:
            self._refresh_diagnostics(diagnostics); return
        self.project = project; self.preferences.last_project = path; save_preferences(self.preferences)
        self._refresh_all(); self._refresh_diagnostics(diagnostics)

    def open_content(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "Open Content Workspace")
        if not path or not self._confirm_unsaved(): return
        self.workspace = ContentWorkspace.open(Path(path)); self.preferences.last_project = path; save_preferences(self.preferences)
        self._refresh_all(); self.set_status("Content workspace opened")

    def save(self) -> None:
        try:
            if self.project.path is None:
                self.save_as()
                return
            self.project.save()
            if self.workspace: self.workspace.save_all()
            self.set_status("Saved")
        except (OSError, ValueError) as error:
            self.show_error(str(error))

    def save_as(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Save Authored Project", "", "World Project (*.uworld);;Map (*.umap)")
        if path:
            try: self.project.save_as(Path(path)); self.set_status("Saved")
            except (OSError, ValueError) as error: self.show_error(str(error))

    def save_all(self) -> None:
        try:
            if self.workspace: self.workspace.save_all()
            if self.project.path is None:
                self.save_as()
                return
            self.project.save()
            self.set_status("All authored documents saved")
        except (OSError, ValueError) as error:
            self.show_error(str(error))

    def validate(self) -> None:
        issues = self.project.validate_cross_map()
        if self.workspace:
            result, tool_issues = self.toolchain.validate_workspace(self.workspace.root)
            del result
            issues.extend(tool_issues)
        else:
            issues.append(Diagnostic("error", "no content workspace selected", code="workspace_missing"))
        self._refresh_diagnostics(issues)
        self.set_status("Validation passed" if not any(issue.is_error for issue in issues) else "Validation failed")

    def export_maps(self) -> None:
        if not self.workspace:
            self.show_error("Open a project content workspace before exporting")
            return
        directory = QFileDialog.getExistingDirectory(self, "Export DMAP directory")
        if not directory: return
        temp = Path(tempfile.mkdtemp(prefix="underworld-studio-export-"))
        try:
            source = temp / "export.uworld"; write_world(source, self.project.authored_data())
            result, issues = self.toolchain.compile_world(source, Path(directory), self.workspace.root)
            self._refresh_diagnostics(issues)
            self.set_status("DMAP export completed" if result.ok else "DMAP export failed")
        finally:
            shutil.rmtree(temp, ignore_errors=True)

    def toggle_playtest(self) -> None:
        if self.playtest.process and self.playtest.process.poll() is None:
            self.playtest.stop(); self.set_status("Playtest stopped"); return
        if not self.workspace:
            self.show_error("Open a content workspace before starting playtest"); return
        success, issues = self.playtest.start(self.project, self.workspace, self.asset_root)
        self._refresh_diagnostics(issues); self.set_status("Playtest started" if success else "Playtest failed")

    def undo(self) -> None:
        changed = self.command_coordinator.undo(self.project.active_map, self.workspace)
        if changed: self._refresh_all()

    def redo(self) -> None:
        changed = self.command_coordinator.redo(self.project.active_map, self.workspace)
        if changed: self._refresh_all()

    def import_map(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "Import UMAP", "", "Authored Map (*.umap)")
        if path:
            try: self.project.import_map(Path(path)); self._refresh_all()
            except ValueError as error: self.show_error(str(error))

    def remove_map(self, map_id: str) -> None:
        try: self.project.remove_map(map_id); self._refresh_all()
        except ValueError as error: self.show_error(str(error))

    def set_entry_map(self, map_id: str) -> None:
        try: self.project.set_entry_map(map_id); self._refresh_all()
        except ValueError as error: self.show_error(str(error))

    def set_language(self, language: str) -> None:
        self.translator.set_language(language); self.preferences.language = language; save_preferences(self.preferences)
        self._retranslate_ui()
        self._update_title()

    def _update_title(self) -> None:
        self.setWindowTitle(self.translator("app") + (" *" if self.has_unsaved_changes() else ""))

    def _autosave(self) -> None:
        if not self.has_unsaved_changes():
            return
        try:
            paths = autosave(self.project, self.workspace)
            if paths:
                self.set_status(f"Autosave written ({len(paths)} file(s))")
        except OSError as error:
            self._refresh_diagnostics([Diagnostic("error", f"autosave failed: {error}", code="autosave")])

    def set_status(self, message: str) -> None:
        self.statusBar().showMessage(message)

    def _refresh_diagnostics(self, diagnostics: list[Diagnostic]) -> None:
        self.diagnostics = diagnostics
        self.diagnostics_view.setPlainText("\n".join(_format_diagnostic(issue) for issue in diagnostics) or "No diagnostics")

    def has_unsaved_changes(self) -> bool:
        return self.project.has_unsaved_changes() or bool(self.workspace and self.workspace.dirty)

    def _confirm_unsaved(self) -> bool:
        if not self.has_unsaved_changes(): return True
        answer = QMessageBox.question(self, "Unsaved Changes", "Save changes before continuing?", QMessageBox.StandardButton.Save | QMessageBox.StandardButton.Discard | QMessageBox.StandardButton.Cancel)
        if answer == QMessageBox.StandardButton.Cancel: return False
        if answer == QMessageBox.StandardButton.Save:
            self.save(); return not self.has_unsaved_changes()
        return True

    def closeEvent(self, event: object) -> None:
        if self._confirm_unsaved():
            map_sizes = self._map_split.sizes()
            if len(map_sizes) >= 3:
                self.preferences.left_panel_width = map_sizes[0]
                self.preferences.right_panel_width = map_sizes[-1]
            self.preferences.asset_root = str(self.asset_root) if self.asset_root else ""
            save_preferences(self.preferences)
            self.playtest.stop(); event.accept()  # type: ignore[attr-defined]
        else:
            event.ignore()  # type: ignore[attr-defined]

    def show_error(self, message: str) -> None:
        QMessageBox.critical(self, "Content Studio", message)


def _format_diagnostic(issue: Diagnostic) -> str:
    prefix = f"{issue.source_path}: " if issue.source_path else ""
    location = f"{issue.path}: " if issue.path else ""
    return f"{prefix}{location}[{issue.severity}] {issue.message}"


def main(argv: list[str] | None = None) -> int:
    parser = __import__("argparse").ArgumentParser(description="Dungeon Underworld Python Content Studio")
    parser.add_argument("--content", type=Path, help="authored content workspace directory")
    parser.add_argument("--project", type=Path, help="authored .uworld or .umap")
    parser.add_argument("--asset-root", type=Path, help="licensed game asset root")
    parser.add_argument("--cpp-root", type=Path, help="repository root containing C++ tools")
    args = parser.parse_args(argv)
    app = QApplication(sys.argv if argv is None else [sys.argv[0], *argv])
    workspace = ContentWorkspace.open(args.content) if args.content else None
    project = None
    if args.project:
        project, diagnostics = WorldProject.open(args.project)
        if project is None:
            print("\n".join(issue.message for issue in diagnostics), file=sys.stderr)
            return 1
    window = MainWindow(project, workspace, args.asset_root, CppToolchain(args.cpp_root, asset_root=args.asset_root) if args.cpp_root else None)
    window.show()
    return app.exec()
