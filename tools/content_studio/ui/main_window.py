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
from ..services.localization import Translator
from ..services.autosave import autosave
from ..services.preferences import load_preferences, save_preferences
from ..services.toolchain import CppToolchain, PlaytestService
from .map_canvas import MapCanvas
from .preview import PreviewWidget
from .scene_editor import SceneEditorWidget
from .widgets import AssetBrowser, CollectionPanel, ContentBrowser, LayersPanel, MapBrowser, StructuredInspector, TilePalette, set_path


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
        self.selected_definition: ContentDefinition | None = None
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
        file_menu = self.menuBar().addMenu("File")
        edit_menu = self.menuBar().addMenu("Edit")
        view_menu = self.menuBar().addMenu("View")
        self.actions: dict[str, QAction] = {}
        for key, title, callback in (("new", "New Project", self.new_project), ("open", "Open Project...", self.open_project), ("new_content", "New Content Workspace...", self.new_content), ("open_content", "Open Content...", self.open_content), ("save", "Save", self.save), ("save_as", "Save As...", self.save_as), ("save_all", "Save All", self.save_all), ("validate", "Validate Workspace", self.validate), ("export", "Export DMAP", self.export_maps), ("playtest", "Playtest", self.toggle_playtest), ("quit", "Exit", self.close)):
            action = QAction(title, self); action.triggered.connect(callback); self.actions[key] = action; file_menu.addAction(action)
        self.actions["new"].setShortcut("Ctrl+Shift+N")
        self.actions["open"].setShortcut("Ctrl+O")
        self.actions["save"].setShortcut("Ctrl+S")
        self.actions["save_as"].setShortcut("Ctrl+Shift+S")
        self.actions["validate"].setShortcut("F7")
        self.actions["playtest"].setShortcut("F5")
        self.actions["undo"] = QAction("Undo", self); self.actions["undo"].setShortcut("Ctrl+Z"); self.actions["undo"].triggered.connect(self.undo); edit_menu.addAction(self.actions["undo"])
        self.actions["redo"] = QAction("Redo", self); self.actions["redo"].setShortcut("Ctrl+Y"); self.actions["redo"].triggered.connect(self.redo); edit_menu.addAction(self.actions["redo"])
        grid = QAction("Grid", self, checkable=True, checked=True); grid.triggered.connect(self.map_canvas_grid); view_menu.addAction(grid)
        language = view_menu.addMenu("Language")
        for code, name in (("pt-BR", "Português (Brasil)"), ("en-US", "English")):
            action = QAction(name, self); action.triggered.connect(lambda checked=False, value=code: self.set_language(value)); language.addAction(action)

    def _build_ui(self) -> None:
        toolbar = QToolBar("Tools", self)
        self.addToolBar(toolbar)
        tool_group = QActionGroup(self)
        tool_group.setExclusive(True)
        for key, title in (("select", "Select"), ("pencil", "Pencil"), ("erase", "Erase"), ("rectangle", "Rectangle"), ("fill", "Fill"), ("collision", "Collision"), ("entity", "Entity"), ("spawn", "Player Spawn"), ("region", "Region"), ("pan", "Pan")):
            action = QAction(title, self); action.setCheckable(True); action.triggered.connect(lambda checked=False, value=key: self.set_tool(value)); toolbar.addAction(action); tool_group.addAction(action)
            if key == "select": action.setChecked(True)
        self.tool_actions = toolbar.actions()
        self.mode_tabs = QTabWidget()
        self.map_canvas = MapCanvas()
        self.map_canvas.selection_changed.connect(self._map_selection_changed)
        self.map_canvas.document_changed.connect(self._refresh_map)
        self.map_canvas.status_changed.connect(self.set_status)
        self.map_browser = MapBrowser()
        self.map_browser.selected.connect(self._select_map)
        self.map_browser.new_requested.connect(self.new_map)
        self.map_browser.import_requested.connect(self.import_map)
        self.map_browser.remove_requested.connect(self.remove_map)
        self.map_browser.entry_requested.connect(self.set_entry_map)
        self.layers = LayersPanel()
        self.layers.changed.connect(self._refresh_map)
        self.layers.selected.connect(self._layer_selected)
        self.tile_palette = TilePalette()
        self.tile_palette.selected.connect(self._tile_selected)
        self.map_collections = {
            name: CollectionPanel(name, label)
            for name, label in (("links", "Map Links"), ("playerSpawns", "Player Spawns"), ("regions", "Regions"), ("worldRules", "World Rules"), ("encounters", "Encounters"))
        }
        for panel in self.map_collections.values():
            panel.changed.connect(self._refresh_map)
        self.entity_browser = ContentBrowser(self.workspace, ("enemies", "npcs", "objects", "pickups"))
        self.entity_browser.selected.connect(self._entity_selected)
        self.entity_browser.place_requested.connect(self._place_definition)
        self.entity_browser.definition_changed.connect(self._refresh_all)
        self.map_inspector = StructuredInspector()
        self.map_inspector.changed.connect(self._edit_map_field)
        map_left_tabs = QTabWidget()
        map_left_tabs.addTab(self.map_browser, "Maps")
        map_left_tabs.addTab(self.layers, "Layers")
        map_left_tabs.addTab(self.tile_palette, "Tiles")
        map_left_tabs.addTab(self.entity_browser, "Entities")
        self.scene_editor = SceneEditorWidget()
        self.scene_editor.changed.connect(self._refresh_map)
        map_left_tabs.addTab(self.scene_editor, "Scenes")
        collections_tabs = QTabWidget()
        for name, panel in self.map_collections.items():
            collections_tabs.addTab(panel, panel.windowTitle() or name)
        map_left_tabs.addTab(collections_tabs, "Rules / Links")
        map_split = QSplitter(Qt.Orientation.Horizontal)
        map_split.addWidget(map_left_tabs); map_split.addWidget(self.map_canvas); map_split.addWidget(self.map_inspector)
        map_split.setStretchFactor(1, 1)
        map_page = QWidget(); map_layout = QVBoxLayout(map_page); map_layout.addWidget(map_split)
        self.mode_tabs.addTab(map_page, "MAP")

        self.content_browser = ContentBrowser(self.workspace)
        self.content_browser.selected.connect(self._content_selected)
        self.content_browser.place_requested.connect(self._place_definition)
        self.content_browser.definition_changed.connect(self._refresh_all)
        self.content_inspector = StructuredInspector()
        self.content_inspector.changed.connect(self._edit_content_field)
        self.asset_browser = AssetBrowser()
        self.asset_browser.selected.connect(lambda entry: self.set_status(str(entry.relative_path) if entry else "No asset selected"))
        self.preview = PreviewWidget()
        content_browsers = QTabWidget()
        content_browsers.addTab(self.content_browser, "Definitions")
        content_browsers.addTab(self.asset_browser, "Assets")
        content_split = QSplitter(Qt.Orientation.Horizontal)
        content_split.addWidget(content_browsers); content_split.addWidget(self.preview); content_split.addWidget(self.content_inspector)
        content_split.setStretchFactor(1, 1)
        content_page = QWidget(); content_layout = QVBoxLayout(content_page); content_layout.addWidget(content_split)
        self.mode_tabs.addTab(content_page, "CONTENT")
        self.diagnostics_view = QPlainTextEdit(); self.diagnostics_view.setReadOnly(True); self.diagnostics_view.setMaximumHeight(150)
        root = QSplitter(Qt.Orientation.Vertical); root.addWidget(self.mode_tabs); root.addWidget(self.diagnostics_view); root.setStretchFactor(0, 1)
        self.setCentralWidget(root)
        self.statusBar().showMessage("Ready")

    def _refresh_all(self) -> None:
        self.map_browser.refresh([document.map_id for document in self.project.maps], self.project.active_map.map_id)
        self._refresh_map()
        self.content_browser.set_workspace(self.workspace)
        self.entity_browser.set_workspace(self.workspace)
        self.tile_palette.set_workspace(self.workspace)
        self.content_inspector.set_workspace(self.workspace)
        self.map_inspector.set_workspace(self.workspace)
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

    def _entity_selected(self, definition: ContentDefinition | None) -> None:
        self.selected_definition = definition
        if definition:
            self.content_inspector.set_object(f"{definition.display_name} [{definition.origin}]", definition.data)
            self.preview.show_definition(definition, self.workspace, self.asset_root)
            self.map_canvas.set_entity_selection(definition.category, definition.definition_id)

    def _content_selected(self, definition: ContentDefinition | None) -> None:
        self.selected_definition = definition
        if definition:
            self.content_inspector.set_object(f"{definition.display_name} [{definition.origin}]", definition.data)
            self.preview.show_definition(definition, self.workspace, self.asset_root)

    def _edit_content_field(self, path: str, value: object) -> None:
        if self.workspace and self.selected_definition:
            try:
                self.workspace.update(self.selected_definition, path, value)
                self.set_status("Definition edited")
                self._refresh_diagnostics(self.workspace.validate_local(self.selected_definition))
                self._refresh_all()
            except (KeyError, TypeError, ValueError) as error:
                self.set_status(str(error))

    def _map_selection_changed(self, selection: object) -> None:
        if not selection:
            self.map_inspector.clear("No selection")
            return
        category, identifier = selection
        value = self.project.active_map.entity(category, identifier)
        if value is not None:
            self.map_inspector.set_object(f"{category}: {identifier}", value)

    def _edit_map_field(self, path: str, value: object) -> None:
        selection = self.map_canvas.selected_entity
        if not selection:
            return
        entity = self.project.active_map.entity(*selection)
        if entity is None:
            return
        self.project.active_map.mutate("Edit Placement", lambda: set_path(entity, path, value))
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

    def set_tool(self, tool: str) -> None:
        self.map_canvas.set_tool(tool)
        self.set_status(f"Tool: {tool}")

    def map_canvas_grid(self, checked: bool) -> None:
        self.map_canvas.set_grid_visible(checked)

    def _tile_selected(self, tileset_id: str, source_index: int, flags: int) -> None:
        self.map_canvas.selected_tile = (tileset_id, source_index, flags)
        self.set_status(f"Tile selected: {tileset_id} [{source_index}]")

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
            source = temp / "export.uworld"; source.write_text(__import__("json").dumps(self.project.authored_data(), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
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
        changed = self.project.active_map.undo() or (self.workspace.undo() if self.workspace else False)
        if changed: self._refresh_all()

    def redo(self) -> None:
        changed = self.project.active_map.redo() or (self.workspace.redo() if self.workspace else False)
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
        self.setWindowTitle(self.translator("app") + (" *" if self.has_unsaved_changes() else ""))

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
