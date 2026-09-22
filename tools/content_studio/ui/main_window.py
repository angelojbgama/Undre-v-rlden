from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import QLibraryInfo, QTranslator, QTimer, Qt
from PySide6.QtGui import QAction, QActionGroup, QGuiApplication
from PySide6.QtWidgets import (
    QApplication, QFileDialog, QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QMainWindow,
    QMessageBox, QPushButton, QSizePolicy, QSplitter, QStackedWidget, QTabBar,
    QTabWidget, QToolBar, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import MapDocument
from ..model.types import ContentDefinition, Diagnostic
from ..model.world_project import WorldProject
from ..interaction.command_coordinator import CommandCoordinator
from ..interaction.drag_payload import StudioDragPayload
from ..services.import_service import ImportService
from ..services.localization import Translator
from ..services.door_instance_service import DoorInstanceService
from ..services.object_transition_service import ObjectTransitionService
from ..services.door_authoring_service import DoorAuthoringService
from ..services.animation_frame_mask_service import (
    AnimationFrameMaskService,
    OBJECT_COLLISION_MASK_CHANNEL,
)
from ..services.item_authoring_service import ItemAuthoringService
from ..services.autosave import autosave
from ..services.legacy_tile_collision_migration import LegacyTileCollisionMigrationService
from ..services.preferences import load_preferences, save_preferences
from ..services.toolchain import CppToolchain, PlaytestService
from ..services.world_export_service import WorldExportService
from .icon_registry import IconSize, icon
from . import theme
from .diagnostics_panel import DiagnosticsPanel
from .map_canvas import MapCanvas
from .map_properties_dialog import MapPropertiesDialog
from .settings_dialog import SettingsDialog
from .preview import PreviewWidget
from .scene_editor import SceneEditorWidget
from .widgets import AssetBrowser, CollectionPanel, ContentBrowser, LayersPanel, MapBrowser, MapElementsPalette, SemanticPalette, StructuredInspector, set_path
from .tilesets.tileset_library_widget import TilesetLibraryWidget
from .spritesheet_library_widget import SpritesheetLibraryWidget
from .object_library_widget import ObjectLibraryWidget
from .door_library_widget import DoorLibraryWidget
from .animated_collision_editor import AnimatedCollisionEditorDialog
from .door_instance_editor import DoorInstanceEditor
from .object_transition_editor import ObjectTransitionEditor
from .player_library_widget import PlayerLibraryWidget
from .attack_library_widget import AttackLibraryWidget
from .item_library_widget import ItemLibraryWidget
from .crafting_library_widget import CraftingLibraryWidget
from .presentation_library_widget import PresentationLibraryWidget
from .ui_composer_widget import UiComposerWidget
from .terrain.smart_terrain_palette import SmartTerrainPalette
from .terrain.tile_semantic_editor import TileSemanticEditor
from ..services.tile_semantic_catalog import TileSemanticCatalog


class MainWindow(QMainWindow):
    def __init__(self, project: WorldProject | None = None, workspace: ContentWorkspace | None = None,
                 asset_root: Path | None = None, toolchain: CppToolchain | None = None) -> None:
        super().__init__()
        self.preferences = load_preferences()
        self._theme_mode = theme.normalize_mode(self.preferences.theme)
        theme.apply_theme(self._theme_mode)
        self.translator = Translator(self.preferences.language)
        self.project = project or WorldProject.new()
        self.workspace = workspace
        self.default_project_path = (
            self.workspace.root.parent / "world.uworld"
            if self.workspace and self.workspace.root.name == "definitions"
            else self.project.path
        )
        self.asset_root = asset_root or (Path(self.preferences.asset_root) if self.preferences.asset_root else None)
        self.toolchain = toolchain or CppToolchain(asset_root=self.asset_root)
        self.playtest = PlaytestService(self.toolchain)
        self.import_service = ImportService()
        self.semantic_catalog = TileSemanticCatalog(self.workspace)
        self.command_coordinator = CommandCoordinator()
        self.selected_definition: ContentDefinition | None = None
        self._definition_history: list[tuple[str, str]] = []
        self._last_content_definition: tuple[str, str] | None = None
        self.diagnostics: list[Diagnostic] = []
        self.setWindowTitle(self.translator("app"))
        self.resize(1400, 900)
        self._build_actions()
        self._build_ui()

        migration = self._migrate_legacy_tile_collision()

        if migration is not None:
            self.diagnostics.extend(
                migration.diagnostics
            )

        self.autosave_timer = QTimer(self)
        self.autosave_timer.setInterval(60_000)
        self.autosave_timer.timeout.connect(self._autosave)
        self.autosave_timer.start()
        QGuiApplication.styleHints().colorSchemeChanged.connect(
            self._system_scheme_changed
        )
        self._refresh_all()
        self.actions["select"].setChecked(True)
        self.map_canvas.set_tool("select")

        if migration is not None:
            if migration.changed:
                self.set_status(
                    "Legacy tile collision migrated to Pixel Collision "
                    f"({len(migration.created_masks)} mask(s) created)"
                )
            elif not migration.ok:
                self.set_status(
                    "Legacy collision needs manual review before playtest"
                )

    def _build_actions(self) -> None:
        file_menu = self.menuBar().addMenu(self.translator("file"))
        edit_menu = self.menuBar().addMenu(self.translator("edit"))
        view_menu = self.menuBar().addMenu(self.translator("view"))
        # Top-level mnemonics (audit G12); texts are retranslated later.
        file_menu.setTitle("&" + self.translator("file"))
        edit_menu.setTitle("&" + self.translator("edit"))
        view_menu.setTitle("&" + self.translator("view"))
        self._menus = {"file": file_menu, "edit": edit_menu, "view": view_menu}
        self.actions: dict[str, QAction] = {}
        for key, title, icon_name, callback in (
            ("new", "New Project", "new", self.new_project),
            ("open", "Open Project...", "open", self.open_project),
            ("save", "Save", "save", self.save),
            ("save_as", "Save As...", "save_as", self.save_as),
            ("save_all", "Save All", "save_all", self.save_all),
            ("validate", "Validate Workspace", "validate", self.validate),
            ("export", "Export DMAP", "export", self.export_maps),
            ("playtest", "Playtest", "playtest", self.toggle_playtest),
            ("import_tileset", "Import Tileset...", "import", self.import_tileset),
            ("quit", "Exit", "exit", self.close),
        ):
            action = QAction(icon(icon_name), title, self)
            action.triggered.connect(callback)
            self.actions[key] = action
            file_menu.addAction(action)
        self.recent_menu = file_menu.addMenu(self.translator("open_recent"))
        self.recent_menu.aboutToShow.connect(self._populate_recent_menu)
        self.actions["new"].setShortcut("Ctrl+Shift+N")
        self.actions["open"].setShortcut("Ctrl+O")
        self.actions["save"].setShortcut("Ctrl+S")
        self.actions["save_as"].setShortcut("Ctrl+Shift+S")
        self.actions["validate"].setShortcut("F7")
        self.actions["playtest"].setShortcut("F5")
        self.actions["undo"] = QAction(icon("undo"), "Undo", self); self.actions["undo"].setShortcut("Ctrl+Z"); self.actions["undo"].triggered.connect(self.undo); edit_menu.addAction(self.actions["undo"])
        self.actions["redo"] = QAction(icon("redo"), "Redo", self); self.actions["redo"].setShortcut("Ctrl+Y"); self.actions["redo"].triggered.connect(self.redo); edit_menu.addAction(self.actions["redo"])
        grid = QAction(icon("grid"), self.translator("grid"), self, checkable=True, checked=True); grid.triggered.connect(self.map_canvas_grid); view_menu.addAction(grid); self.actions["grid"] = grid
        snap = QAction(icon("snap"), self.translator("snap"), self, checkable=True, checked=True); snap.triggered.connect(self.map_canvas_snap); view_menu.addAction(snap); self.actions["snap"] = snap
        frame = QAction(icon("frame_map"), "Frame Map", self); frame.setShortcut("Home"); frame.triggered.connect(lambda: self.map_canvas.fit_map()); view_menu.addAction(frame); self.actions["frame"] = frame
        language = view_menu.addMenu(self.translator("language")); self._language_menu = language
        for code, name in (("pt-BR", "Português (Brasil)"), ("en-US", "English")):
            action = QAction(name, self); action.triggered.connect(lambda checked=False, value=code: self.set_language(value)); language.addAction(action)
        theme_menu = view_menu.addMenu(self.translator("theme")); self._theme_menu = theme_menu
        self._theme_actions: dict[str, QAction] = {}
        theme_group = QActionGroup(self)
        theme_group.setExclusionPolicy(QActionGroup.ExclusionPolicy.Exclusive)
        for mode, icon_name, translation_key in (
            (theme.THEME_SYSTEM, "theme_system", "theme_system"),
            (theme.THEME_LIGHT, "theme_light", "theme_light"),
            (theme.THEME_DARK, "theme_dark", "theme_dark"),
        ):
            action = QAction(icon(icon_name), self.translator(translation_key), self)
            action.setCheckable(True)
            action.setChecked(mode == self._theme_mode)
            action.triggered.connect(lambda checked=False, value=mode: self.set_theme_mode(value))
            theme_group.addAction(action)
            theme_menu.addAction(action)
            self._theme_actions[mode] = action
        settings_action = QAction(icon("configure"), self.translator("settings"), self)
        settings_action.triggered.connect(self.open_settings)
        view_menu.addAction(settings_action)
        self._settings_action = settings_action

    def _build_ui(self) -> None:
        toolbar = QToolBar(self.translator("tools"), self)
        self._toolbar = toolbar
        toolbar.setIconSize(IconSize.TOOLBAR)
        self.addToolBar(toolbar)
        tool_group = QActionGroup(self)
        tool_group.setExclusionPolicy(QActionGroup.ExclusionPolicy.ExclusiveOptional)
        self._tool_group = tool_group
        self._tool_keys: list[str] = []
        select = QAction(icon("select"), self.translator("select"), self); select.setCheckable(True); select.setChecked(True); select.toggled.connect(self._select_tool_toggled); tool_group.addAction(select); toolbar.addAction(select); self.actions["select"] = select
        toolbar.addAction(self.actions["grid"])
        toolbar.addAction(self.actions["snap"])
        toolbar.addSeparator()
        toolbar.addAction(self.actions["playtest"])
        erase_tiles = QAction(icon("erase"), self.translator("tools_erase"), self)
        erase_tiles.setCheckable(True)
        erase_tiles.toggled.connect(self._erase_tool_toggled)
        tool_group.addAction(erase_tiles)
        toolbar.addAction(erase_tiles)
        self.actions["erase_tiles"] = erase_tiles
        self.tool_actions = [select, self.actions["grid"], self.actions["snap"], self.actions["playtest"], erase_tiles]
        self._tool_keys = ["select", "grid", "snap", "playtest_toolbar", "tools_erase"]
        self.mode_tabs = QTabBar()
        self.mode_tabs.setExpanding(False)
        self.mode_tabs.setDrawBase(True)
        self.map_canvas = MapCanvas()
        self.map_canvas.set_translator(self.translator)
        self.map_canvas.selection_changed.connect(self._map_selection_changed)
        self.map_canvas.document_changed.connect(self._map_content_changed)
        self.map_canvas.status_changed.connect(self.set_status)
        self.map_canvas.map_properties_requested.connect(
            lambda: self.edit_map(self.project.active_map.map_id))
        self.map_browser = MapBrowser()
        self.map_browser.set_translator(self.translator)
        self.map_browser.selected.connect(self._select_map)
        self.map_browser.new_requested.connect(self.new_map)
        self.map_browser.import_requested.connect(self.import_map)
        self.map_browser.remove_requested.connect(self.remove_map)
        self.map_browser.entry_requested.connect(self.set_entry_map)
        self.map_browser.edit_requested.connect(self.edit_map)
        self.layers = LayersPanel(translator=self.translator)
        self.layers.changed.connect(self._map_changed)
        self.layers.selected.connect(self._layer_selected)
        self.tileset_library = TilesetLibraryWidget(self.workspace, self.project, self.asset_root, self.translator)
        self.tileset_library.selected.connect(self._tile_selected)
        self.tileset_library.brush_selected.connect(self._brush_selected)
        self.tileset_library.selected.connect(self._atlas_tile_selected)
        self.tileset_library.changed.connect(self._content_changed)
        self.tileset_library.folder_groups_changed.connect(self._tileset_folders_changed)
        self.tileset_library.status_changed.connect(self.set_status)
        self.spritesheet_library = SpritesheetLibraryWidget(
            self.workspace, self.asset_root, self.translator)
        self.spritesheet_library.changed.connect(self._content_changed)
        self.spritesheet_library.status_changed.connect(self.set_status)
        self.object_library = ObjectLibraryWidget(
            self.workspace, self.asset_root, self.translator)
        self.object_library.selected.connect(self._entity_selected)
        self.object_library.place_requested.connect(self._place_definition)
        self.object_library.changed.connect(self._content_changed)
        self.object_library.status_changed.connect(self.set_status)
        self.door_library = DoorLibraryWidget(
            self.workspace, self.asset_root,
            self.project.active_map.tile_size, self.translator)
        self.door_library.selected.connect(self._entity_selected)
        self.door_library.place_requested.connect(
            self._place_door_definition)
        self.door_library.animated_collision_requested.connect(
            self._edit_door_animated_collision)
        self.door_library.status_changed.connect(self.set_status)
        self.player_library = PlayerLibraryWidget(
            self.workspace, self.asset_root, self.translator)
        self.player_library.changed.connect(self._content_changed)
        self.player_library.status_changed.connect(self.set_status)
        self.attack_library = AttackLibraryWidget(
            self.workspace, self.translator)
        self.attack_library.changed.connect(self._content_changed)
        self.attack_library.status_changed.connect(self.set_status)
        self.item_library = ItemLibraryWidget(
            self.workspace, self.asset_root, self.translator, self.project)
        self.item_library.place_requested.connect(self._place_definition)
        self.item_library.changed.connect(self._content_changed)
        self.item_library.status_changed.connect(self.set_status)
        self.crafting_library = CraftingLibraryWidget(
            self.workspace, self.asset_root, self.translator)
        self.crafting_library.changed.connect(self._content_changed)
        self.crafting_library.status_changed.connect(self.set_status)
        self.presentation_library = PresentationLibraryWidget(
            self.workspace, self.asset_root, self.translator)
        self.presentation_library.changed.connect(self._content_changed)
        self.presentation_library.status_changed.connect(self.set_status)
        # Compatibility alias for integrations that used the old palette name.
        self.tile_palette = self.tileset_library
        self.semantic_palette = SemanticPalette(translator=self.translator)
        self.semantic_palette.tile_selected.connect(self._tile_selected)
        self.semantic_palette.stamp_selected.connect(self._stamp_selected)
        self.semantic_editor = TileSemanticEditor(self.workspace, self.semantic_catalog, self.translator)
        self.semantic_editor.saved.connect(self._semantic_saved)
        self.smart_terrain = SmartTerrainPalette(self.semantic_catalog, self.translator)
        self.smart_terrain.terrain_selected.connect(self._terrain_selected)
        self.smart_terrain.room_requested.connect(self._room_selected)
        self.smart_terrain.pattern_selected.connect(self._pattern_selected)
        self.map_elements = MapElementsPalette({
            "player_spawn": self.translator("player_spawn"), "map_transition": self.translator("map_transition"),
            "region": self.translator("region_element"), "hint": self.translator("map_elements_hint"),
        })
        self.map_elements.selected.connect(self._map_element_selected)
        self.map_collections = {
            name: CollectionPanel(name, label, translator=self.translator)
            for name, label in (("links", "Map Links"), ("playerSpawns", "Player Spawns"), ("regions", "Regions"), ("worldRules", "World Rules"), ("encounters", "Encounters"))
        }
        for panel in self.map_collections.values():
            panel.changed.connect(self._map_changed)
        self.entity_browser = ContentBrowser(self.workspace, ("enemies", "npcs", "objects", "pickups"), translator=self.translator)
        self.entity_browser.selected.connect(self._entity_selected)
        self.entity_browser.place_requested.connect(self._place_definition)
        self.entity_browser.definition_changed.connect(self._content_changed)
        self.map_inspector = StructuredInspector(translator=self.translator)
        self.map_inspector.changed.connect(self._edit_map_field)
        self.map_inspector.collection_changed.connect(self._edit_map_collection)
        self.map_inspector.collection_value_requested.connect(
            self._add_map_collection_value)

        self.door_instance_editor = DoorInstanceEditor(
            self.translator)

        self.door_instance_editor.configuration_requested.connect(
            self._configure_selected_door)

        self.door_instance_editor.status_changed.connect(
            self.set_status)

        self.object_transition_editor = ObjectTransitionEditor(
            self.translator)

        self.object_transition_editor.configuration_requested.connect(
            self._configure_selected_object_transition)

        self.object_transition_editor.status_changed.connect(
            self.set_status)

        self.delete_map_selection_button = QPushButton(self.translator("delete"))
        self.delete_map_selection_button.setIcon(icon("delete"))
        self.delete_map_selection_button.setEnabled(False)
        self.delete_map_selection_button.clicked.connect(self._delete_map_selection)
        map_inspector_panel = QWidget()
        map_inspector_layout = QVBoxLayout(map_inspector_panel)
        map_inspector_layout.setContentsMargins(0, 0, 0, 0)
        map_inspector_layout.addWidget(self.door_instance_editor)
        map_inspector_layout.addWidget(self.object_transition_editor)
        # Empty state (audit S1): guide the author instead of a bare title.
        self.map_inspector_empty = QLabel(self.translator("inspector_empty_hint"))
        self.map_inspector_empty.setWordWrap(True)
        self.map_inspector_empty.setProperty("muted", True)
        self.map_inspector_empty.setVisible(True)
        self.map_inspector.setVisible(False)
        map_inspector_layout.addWidget(self.map_inspector_empty)
        map_inspector_layout.addWidget(self.map_inspector, 1)
        map_inspector_layout.addWidget(self.delete_map_selection_button)
        self._map_panels = QStackedWidget()
        self._map_panels.addWidget(self.map_browser)
        self._map_panels.addWidget(self.layers)
        self._map_panels.addWidget(self.tile_palette)
        self._map_panels.addWidget(self.spritesheet_library)
        self._map_panels.addWidget(self.object_library)
        self._map_panels.addWidget(self.door_library)
        self._map_panels.addWidget(self.player_library)
        self._map_panels.addWidget(self.item_library)
        self._map_panels.addWidget(self.crafting_library)
        self._map_panels.addWidget(self.presentation_library)
        self._map_panels.addWidget(self.smart_terrain)
        self._map_panels.addWidget(self.semantic_editor)
        self._map_panels.addWidget(self.semantic_palette)
        self._map_panels.addWidget(self.map_elements)
        self._map_panels.addWidget(self.entity_browser)
        self.scene_editor = SceneEditorWidget(translator=self.translator)
        self.scene_editor.changed.connect(self._map_changed)
        self.scene_editor.diagnostics_changed.connect(self._refresh_diagnostics)
        self._map_panels.addWidget(self.scene_editor)
        collections_tabs = QTabWidget()
        for name, panel in self.map_collections.items():
            collections_tabs.addTab(panel, panel.windowTitle() or name)
        self._map_panels.addWidget(collections_tabs)
        self._collections_tabs = collections_tabs
        map_split = QSplitter(Qt.Orientation.Horizontal)
        map_split.addWidget(self._map_panels); map_split.addWidget(self.map_canvas); map_split.addWidget(map_inspector_panel)
        self._configure_workspace_splitter(map_split)
        map_split.setStretchFactor(1, 1)
        self._map_split = map_split
        map_split.setSizes([self.preferences.left_panel_width, 700, self.preferences.right_panel_width])

        self.content_browser = ContentBrowser(self.workspace, translator=self.translator, project=self.project)
        self.content_browser.selected.connect(self._content_selected)
        self.content_browser.place_requested.connect(self._place_definition)
        self.content_browser.find_usages_requested.connect(self._show_usages)
        self.content_browser.back_requested.connect(self._back_definition)
        self.content_browser.definition_changed.connect(self._content_changed)
        self.content_inspector = StructuredInspector(translator=self.translator)
        self.content_inspector.changed.connect(self._edit_content_field)
        self.content_inspector.collection_changed.connect(self._edit_content_collection)
        self.asset_browser = AssetBrowser(translator=self.translator)
        self.asset_browser.selected.connect(self._asset_selected)
        self.asset_browser.assign_requested.connect(self._assign_asset)
        self.preview = PreviewWidget()
        self._content_panels = QStackedWidget()
        self._content_panels.addWidget(self.content_browser)
        self._content_panels.addWidget(self.asset_browser)
        content_split = QSplitter(Qt.Orientation.Horizontal)
        content_split.addWidget(self._content_panels); content_split.addWidget(self.preview); content_split.addWidget(self.content_inspector)
        self._configure_workspace_splitter(content_split)
        content_split.setStretchFactor(1, 1)
        self._content_split = content_split
        content_split.setSizes([self.preferences.left_panel_width, 700, self.preferences.right_panel_width])
        self.ui_composer = UiComposerWidget(self.workspace, self.translator,
                                            asset_root=self.asset_root)
        self._ui_split = QSplitter(Qt.Orientation.Horizontal)
        self._ui_split.addWidget(self.ui_composer)
        self.mode_tabs.addTab(self.translator("maps_mode"))
        self.mode_tabs.addTab(self.translator("content_mode"))
        self.mode_tabs.addTab(self.translator("ui_mode"))
        # Vertical section rail (audit G2): 15 sections never fit as
        # horizontal tabs; a sidebar keeps every section reachable.
        self._section_sidebar = QListWidget()
        self._section_sidebar.setObjectName("sectionSidebar")
        self._section_sidebar.setIconSize(IconSize.NORMAL)
        self._section_sidebar.setFixedWidth(216)
        self._section_sidebar.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._section_sidebar.currentRowChanged.connect(self._section_row_changed)
        self._workspace_pages = QStackedWidget()
        self._workspace_pages.addWidget(map_split)
        self._workspace_pages.addWidget(content_split)
        self._workspace_pages.addWidget(self._ui_split)
        self._mode_section_indexes = [0, 0, 0]
        self.mode_tabs.currentChanged.connect(self._select_mode)
        workspace = QWidget()
        workspace_layout = QVBoxLayout(workspace)
        workspace_layout.setContentsMargins(0, 0, 0, 0)
        workspace_layout.setSpacing(0)
        workspace_layout.addWidget(self.mode_tabs)
        sections_body = QWidget()
        sections_layout = QHBoxLayout(sections_body)
        sections_layout.setContentsMargins(0, 0, 0, 0)
        sections_layout.setSpacing(0)
        sections_layout.addWidget(self._section_sidebar)
        sections_layout.addWidget(self._workspace_pages, 1)
        workspace_layout.addWidget(sections_body, 1)
        self.diagnostics_panel = DiagnosticsPanel(translator=self.translator)
        self.diagnostics_panel.setMaximumHeight(190)
        self.diagnostics_panel.definition_requested.connect(self._navigate_to_definition)
        root = QSplitter(Qt.Orientation.Vertical); root.addWidget(workspace); root.addWidget(self.diagnostics_panel); root.setStretchFactor(0, 1)
        self.setCentralWidget(root)
        self.statusBar().showMessage(self.translator("ready"))
        self._retranslate_ui()
        self._select_mode(0)

    @staticmethod
    def _configure_workspace_splitter(splitter: QSplitter) -> None:
        """Allow VS Code-like narrow, wide, and hidden side panels."""
        splitter.setChildrenCollapsible(True)
        splitter.setHandleWidth(8)
        splitter.setOpaqueResize(True)
        splitter.setCollapsible(0, True)
        splitter.setCollapsible(1, False)
        splitter.setCollapsible(2, True)
        for index in (0, 2):
            panel = splitter.widget(index)
            panel.setMinimumWidth(0)
            policy = panel.sizePolicy()
            policy.setHorizontalPolicy(QSizePolicy.Policy.Ignored)
            panel.setSizePolicy(policy)

    _SECTION_KEYS = {
        0: ("maps", "layers", "tiles", "spritesheets_animations", "objects_tab",
            "doors_tab", "players_tab", "items_tab", "crafting_tab",
            "presentation_effects", "smart_terrain",
            "semantic_editor", "semantics_stamps", "map_elements", "entities", "scenes",
            "rules_links"),
        1: ("definitions", "assets"),
    }

    _SECTION_ICONS = {
        "maps": "map", "layers": "layers", "tiles": "tiles",
        "spritesheets_animations": "spritesheet", "objects_tab": "object",
        "doors_tab": "door", "players_tab": "player", "items_tab": "items",
        "crafting_tab": "box",
        "presentation_effects": "box",
        "smart_terrain": "terrain", "semantic_editor": "tag",
        "semantics_stamps": "stamp", "map_elements": "place",
        "entities": "entities", "scenes": "scenes", "rules_links": "links",
        "definitions": "definitions", "assets": "assets",
    }

    def _section_keys(self, mode_index: int) -> tuple[str, ...]:
        return self._SECTION_KEYS.get(mode_index, ())

    def _section_labels(self, mode_index: int) -> tuple[str, ...]:
        return tuple(self.translator(key) for key in self._section_keys(mode_index))

    def _rebuild_section_sidebar(self, mode_index: int) -> None:
        self._section_sidebar.blockSignals(True)
        self._section_sidebar.clear()
        for key in self._section_keys(mode_index):
            item = QListWidgetItem(icon(self._SECTION_ICONS.get(key, "map")), self.translator(key))
            item.setData(Qt.ItemDataRole.UserRole, key)
            self._section_sidebar.addItem(item)
        self._section_sidebar.blockSignals(False)

    def _select_mode(self, mode_index: int) -> None:
        if mode_index < 0:
            return
        self._workspace_pages.setCurrentIndex(mode_index)
        self.command_coordinator.mark("map" if mode_index == 0 else "content")
        map_mode = mode_index == 0
        self._toolbar.setVisible(map_mode)
        for action_key in ("grid", "snap", "frame"):
            self.actions[action_key].setEnabled(map_mode)
        self._rebuild_section_sidebar(mode_index)
        section_index = min(self._mode_section_indexes[mode_index], self._section_sidebar.count() - 1)
        self._section_sidebar.blockSignals(True)
        self._section_sidebar.setCurrentRow(section_index)
        self._section_sidebar.blockSignals(False)
        self._select_section(section_index)

    def _section_row_changed(self, row: int) -> None:
        if row >= 0:
            self._select_section(row)

    def _select_section(self, section_index: int) -> None:
        mode_index = self.mode_tabs.currentIndex()
        if mode_index < 0 or section_index < 0:
            return
        if mode_index >= 2:
            # The UI mode has a single composer page and no sections.
            return
        self._mode_section_indexes[mode_index] = section_index
        panels = self._content_panels if mode_index == 1 else self._map_panels
        panels.setCurrentIndex(section_index)

    def _retranslate_ui(self) -> None:
        """Retranslate the application shell without replacing native editors."""
        labels = {
            "new": "new_project", "open": "open_project", "save": "save", "save_as": "save_as",
            "save_all": "save_all", "validate": "validate", "export": "export",
            "playtest": "playtest", "import_tileset": "import_tileset", "quit": "quit", "undo": "undo", "redo": "redo",
            "grid": "grid", "frame": "frame",
        }
        for action_key, translation_key in labels.items():
            if action_key in self.actions:
                self.actions[action_key].setText(self.translator(translation_key))
        self._menus["file"].setTitle("&" + self.translator("file"))
        self._menus["edit"].setTitle("&" + self.translator("edit"))
        self._menus["view"].setTitle("&" + self.translator("view"))
        self._language_menu.setTitle(self.translator("language"))
        self._theme_menu.setTitle(self.translator("theme"))
        self.recent_menu.setTitle(self.translator("open_recent"))
        self._settings_action.setText(self.translator("settings"))
        for mode, action in self._theme_actions.items():
            action.setText(self.translator(f"theme_{mode}"))
        for action, translation_key in zip(self.tool_actions, self._tool_keys):
            action.setText(self.translator(translation_key))
        self._toolbar.setWindowTitle(self.translator("tools"))
        self.tileset_library.retranslate(self.translator)
        self.spritesheet_library.retranslate(self.translator)
        self.object_library.retranslate(self.translator)
        self.door_library.retranslate(self.translator)
        self.door_instance_editor.retranslate(self.translator)
        self.object_transition_editor.retranslate(self.translator)
        self.player_library.retranslate(self.translator)
        self.attack_library.retranslate(self.translator)
        self.item_library.retranslate(self.translator)
        self.crafting_library.retranslate(self.translator)
        self.presentation_library.retranslate(self.translator)
        self.smart_terrain.retranslate(self.translator)
        self.scene_editor.retranslate(self.translator)
        self.layers.retranslate(self.translator)
        self.asset_browser.retranslate(self.translator)
        self.diagnostics_panel.retranslate(self.translator)
        self.map_canvas.set_translator(self.translator)
        self.map_browser.set_translator(self.translator)
        self.mode_tabs.setTabText(0, self.translator("maps_mode"))
        self.mode_tabs.setTabText(1, self.translator("content_mode"))
        for index, key in enumerate(self._section_keys(self.mode_tabs.currentIndex())):
            item = self._section_sidebar.item(index)
            if item is not None:
                item.setText(self.translator(key))
        self.map_inspector_empty.setText(self.translator("inspector_empty_hint"))
        self.entity_browser.set_translator(self.translator)
        self.content_browser.set_translator(self.translator)
        self.map_elements.retranslate({
            "player_spawn": self.translator("player_spawn"), "map_transition": self.translator("map_transition"),
            "region": self.translator("region_element"), "hint": self.translator("map_elements_hint"),
        })
        self.delete_map_selection_button.setText(self.translator("delete"))

    def _refresh_all(self) -> None:
        self.map_browser.refresh(
            [document.map_id for document in self.project.maps],
            self.project.active_map.map_id, self._map_folders(),
            entry=self.project.entry_map_id)
        self._refresh_map()
        self.content_browser.set_workspace(self.workspace)
        self.content_browser.set_project(self.project)
        self.entity_browser.set_workspace(self.workspace)
        self.tileset_library.set_workspace(self.workspace, self.project)
        self.tileset_library.set_folder_groups(self._tileset_folders())
        self.tileset_library.set_asset_root(self.asset_root)
        self.tileset_library.set_map_tile_size(self.project.active_map.tile_size)
        self.spritesheet_library.set_context(self.workspace, self.asset_root)
        self.object_library.set_context(self.workspace, self.asset_root)
        self.door_library.set_context(
            self.workspace, self.asset_root,
            self.project.active_map.tile_size)
        self.player_library.set_context(self.workspace, self.asset_root)
        self.attack_library.set_context(self.workspace, self.asset_root)
        self.item_library.set_context(
            self.workspace, self.asset_root, self.project)
        self.crafting_library.set_context(
            self.workspace, self.asset_root)
        self.presentation_library.set_context(
            self.workspace, self.asset_root)
        self.ui_composer.set_context(self.workspace, self.translator)
        self.semantic_palette.set_workspace(self.workspace)
        self.semantic_palette.set_asset_root(self.asset_root)
        self.semantic_editor.set_workspace(self.workspace)
        self.semantic_editor.set_asset_root(self.asset_root)
        self.smart_terrain.set_workspace(self.workspace)
        self.smart_terrain.set_asset_root(self.asset_root)
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
        self.tileset_library.set_map_tile_size(document.tile_size)
        self.door_library.set_map_tile_size(document.tile_size)
        self.layers.set_document(document)
        self.scene_editor.set_document(document)
        for panel in self.map_collections.values():
            panel.set_document(document)
        self.map_browser.refresh(
            [value.map_id for value in self.project.maps], document.map_id,
            self._map_folders(), entry=self.project.entry_map_id)
        if self.map_canvas.selected_entity:
            self._map_selection_changed(self.map_canvas.selected_entity)

    def _refresh_map_content(self) -> None:
        self.map_canvas.update()

        if self.map_canvas.selected_entity:
            self._map_selection_changed(
                self.map_canvas.selected_entity
            )

    def _map_content_changed(self) -> None:
        self.command_coordinator.mark("map")
        self._refresh_map_content()

    def _map_changed(self) -> None:
        self.command_coordinator.mark("map")
        self._refresh_map()

    def _entity_selected(self, definition: ContentDefinition | None) -> None:
        self.selected_definition = definition
        if definition:
            self.content_inspector.set_object(f"{definition.display_name} [{definition.origin}]", definition.data)
            self._refresh_content_field_errors()
            self.preview.show_definition(definition, self.workspace, self.asset_root)
            self._clear_toolbar_tools()
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
            self._refresh_content_field_errors()
            self.preview.show_definition(definition, self.workspace, self.asset_root)

    def _refresh_content_field_errors(self) -> None:
        """Inline validation for the selected definition (audit IN2)."""
        if not (self.workspace and self.selected_definition):
            self.content_inspector.set_field_errors({})
            return
        issues = self.workspace.validate_local(self.selected_definition)
        # validate_local paths start with the category; inspector paths do not.
        category_prefix = f"{self.selected_definition.category}."
        errors: dict[str, str] = {}
        for issue in issues:
            if not issue.path:
                continue
            relative = issue.path[len(category_prefix):] if issue.path.startswith(category_prefix) else issue.path
            errors[relative] = issue.message
        self.content_inspector.set_field_errors(errors)

    def _show_usages(self, definition: object) -> None:
        if not self.workspace or not isinstance(definition, ContentDefinition):
            return
        usages = self.workspace.find_usages(definition.definition_id)
        if not usages:
            self.set_status(self.translator("no_usages_found"))
            return
        labels = [f"{item.display_name} [{item.category}:{item.definition_id}]" for item in usages]
        chosen, accepted = QInputDialog.getItem(self, "Find Usages", "Referenced by:", labels, 0, False)
        if accepted:
            index = labels.index(chosen)
            target = usages[index]
            self.content_browser.select_definition(target.category, target.definition_id)

    def _back_definition(self) -> None:
        if not self._definition_history:
            self.set_status(self.translator("no_previous_definition"))
            return
        category, definition_id = self._definition_history.pop()
        self._last_content_definition = None
        self.content_browser.select_definition(category, definition_id)

    def _asset_selected(self, entry: object) -> None:
        if entry is None:
            self.preview.show_asset(None)
            self.set_status(self.translator("no_asset_selected"))
            return
        self.preview.show_asset(entry)  # type: ignore[arg-type]
        self.set_status(str(entry.relative_path))  # type: ignore[attr-defined]

    def _assign_asset(self, entry: object) -> None:
        if not self.workspace or not self.selected_definition or not hasattr(entry, "relative_path"):
            return
        definition = self.workspace.find(self.selected_definition.category, self.selected_definition.definition_id)
        if definition is None or definition.origin != "project":
            self.set_status(self.translator("builtin_read_only"))
            return
        try:
            if definition.category == "visualImages":
                self.workspace.update(definition, "root", "gameAssets" if entry.root == "gameAssets" else "contentWorkspace")  # type: ignore[attr-defined]
                self.workspace.update(definition, "relativePath", entry.relative_path.as_posix())  # type: ignore[attr-defined]
            elif definition.category == "tilesets":
                if entry.root != "gameAssets":  # type: ignore[attr-defined]
                    self.set_status(self.translator("tileset_asset_root_required"))
                    return
                self.workspace.update(definition, "relativeAssetPath", entry.relative_path.as_posix())  # type: ignore[attr-defined]
            else:
                self.set_status(self.translator("select_visual_or_tileset"))
                return
            self._refresh_all(); self.set_status(self.translator("asset_assigned"))
        except (KeyError, TypeError, ValueError) as error:
            self.set_status(str(error))

    def _edit_content_field(self, path: str, value: object) -> None:
        if self.workspace and self.selected_definition:
            try:
                self.command_coordinator.mark("content")
                self.workspace.update(self.selected_definition, path, value)
                self.set_status(self.translator("definition_edited"))
                self._refresh_diagnostics(self.workspace.validate_local(self.selected_definition))
                self._refresh_all()
                self._refresh_content_field_errors()
            except (KeyError, TypeError, ValueError) as error:
                self.set_status(str(error))

    def _edit_content_collection(self, path: str, action: str) -> None:
        if not self.workspace or not self.selected_definition:
            return
        try:
            self.command_coordinator.mark("content")
            self.workspace.mutate_collection(self.selected_definition, path, action)
            self.set_status(self.translator("collection_updated"))
            self._refresh_all()
        except (KeyError, TypeError, ValueError) as error:
            self.set_status(str(error))

    def _map_selection_changed(self, selection: object) -> None:
        if not selection:
            self.map_inspector.clear()
            self.door_instance_editor.clear()
            self.object_transition_editor.clear()
            self.delete_map_selection_button.setEnabled(False)
            self.map_inspector_empty.setVisible(True)
            self.map_inspector.setVisible(False)
            return

        category, identifier = selection

        value = (
            self.project.active_map.entity(
                category,
                identifier,
            )
            if category in {
                "enemies",
                "npcs",
                "objects",
                "pickups",
            }
            else next(
                (
                    entry
                    for entry
                    in self.project.active_map.all_collection(
                        category
                    )
                    if entry.get("id")
                    == identifier
                ),
                None,
            )
        )

        can_delete = (
            value is not None
        )

        self.delete_map_selection_button.setEnabled(
            can_delete
        )

        if value is None:
            self.door_instance_editor.clear()
            self.object_transition_editor.clear()
            return

        is_door = False

        if (
            category == "objects"
            and self.workspace is not None
        ):
            is_door = self.door_instance_editor.set_context(
                self.project.active_map,
                self.workspace,
                int(identifier),
            )
        else:
            self.door_instance_editor.clear()

        is_object_transition_editable = False

        if category == "objects":
            is_object_transition_editable = self.object_transition_editor.set_context(
                self.project,
                self.project.active_map,
                int(identifier),
            )
        else:
            self.object_transition_editor.clear()

        inspector_value = value

        if (
            (is_door or is_object_transition_editable)
            and isinstance(
                value,
                dict,
            )
        ):
            inspector_value = dict(
                value
            )

            if is_door:
                # Door behavior has a dedicated contextual editor.
                # Keep generic placement fields available without exposing
                # duplicate raw door/persistence controls.
                inspector_value.pop(
                    "door",
                    None,
                )

                inspector_value.pop(
                    "persistence",
                    None,
                )

            if is_object_transition_editable:
                # Transition is authored by ObjectTransitionEditor; hide
                # the raw dict from the generic inspector to avoid
                # duplicate edits of the same capability.
                inspector_value.pop(
                    "transition",
                    None,
                )

        self.map_inspector_empty.setVisible(False)
        self.map_inspector.setVisible(True)
        category_labels = {
            "objects": "category_objects", "enemies": "category_enemies",
            "npcs": "category_npcs", "pickups": "category_pickups",
            "links": "category_links", "playerSpawns": "category_playerSpawns",
            "regions": "category_regions", "worldRules": "category_worldRules",
            "encounters": "category_encounters",
        }
        label_key = category_labels.get(category)
        title = self.translator("inspector_title").format(
            label=self.translator(label_key) if label_key else category,
            id=identifier,
        )
        self.map_inspector.set_object(title, inspector_value)

    def _configure_selected_door(
            self,
            request: object) -> None:
        selection = (
            self.map_canvas.selected_entity
        )

        if (
            not selection
            or self.workspace is None
            or not isinstance(
                request,
                dict,
            )
        ):
            return

        category, identifier = selection

        if category != "objects":
            return

        try:
            def _optional_str(key: str) -> str | None:
                value = request.get(key)
                return value if isinstance(value, str) else None

            DoorInstanceService(
                self.project.active_map,
                self.workspace,
            ).configure(
                int(identifier),
                uses_definition_defaults=bool(
                    request.get(
                        "uses_definition_defaults",
                        True,
                    )
                ),
                initial_state=str(
                    request.get(
                        "initial_state",
                        "closed",
                    )
                ),
                required_item_id=(
                    request.get(
                        "required_item_id"
                    )
                    if isinstance(
                        request.get(
                            "required_item_id"
                        ),
                        str,
                    )
                    else None
                ),
                consume_item=bool(
                    request.get(
                        "consume_item",
                        False,
                    )
                ),
                persistence=str(
                    request.get(
                        "persistence",
                        "persistent",
                    )
                ),
                open_attack_id=_optional_str(
                    "open_attack_id"
                ),
                encounter_id=_optional_str(
                    "encounter_id"
                ),
            )

            self.command_coordinator.mark(
                "map"
            )

            self._refresh_map()

            self.set_status(
                self.translator(
                    "door_configuration_saved"
                )
            )

        except (
            TypeError,
            ValueError,
        ) as error:
            self.set_status(
                str(error)
            )

    def _configure_selected_object_transition(
            self,
            request: object) -> None:
        selection = (
            self.map_canvas.selected_entity
        )

        if (
            not selection
            or not isinstance(
                request,
                dict,
            )
        ):
            return

        category, identifier = selection

        if category != "objects":
            return

        def _optional_str(
                key: str) -> str | None:
            value = request.get(
                key
            )

            return (
                value
                if isinstance(
                    value,
                    str,
                )
                else None
            )

        try:
            ObjectTransitionService(
                self.project,
                self.project.active_map,
            ).configure(
                int(identifier),
                enabled=bool(
                    request.get(
                        "enabled",
                        False,
                    )
                ),
                target_map_id=_optional_str(
                    "target_map_id"
                ),
                target_spawn_id=_optional_str(
                    "target_spawn_id"
                ),
            )

            self.command_coordinator.mark(
                "map"
            )

            self._refresh_map()

            self.set_status(
                self.translator(
                    "object_transition_saved"
                )
            )

        except (
            TypeError,
            ValueError,
        ) as error:
            self.set_status(
                str(error)
            )

    def _delete_map_selection(self) -> None:
        if self.map_canvas.delete_selection():
            self.set_status(self.translator("selection_deleted"))

    def _validated_item_stack(
            self,
            value: object) -> dict[str, object]:
        if self.workspace is None:
            raise ValueError(
                "ItemStack requires a content workspace"
            )

        if not isinstance(
            value,
            dict,
        ):
            raise ValueError(
                "ItemStack must be an object"
            )

        item_id = value.get(
            "itemId"
        )

        quantity = value.get(
            "quantity"
        )

        if not isinstance(
            item_id,
            str,
        ):
            raise ValueError(
                "ItemStack itemId must reference an Item"
            )

        return dict(
            ItemAuthoringService(
                self.workspace
            ).validate_stack(
                item_id,
                quantity,
            )
        )

    def _edit_map_field(self, path: str, value: object) -> None:
        selection = self.map_canvas.selected_entity
        if not selection:
            return
        category, identifier = selection

        if (
            category == "objects"
            and path.startswith(
                "initialContents["
            )
            and path.endswith(
                "]"
            )
        ):
            try:
                value = self._validated_item_stack(
                    value
                )
            except ValueError as error:
                self.set_status(
                    str(error)
                )
                return

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

    def _edit_map_collection(self, path: str, action: str) -> None:
        selection = self.map_canvas.selected_entity
        if not selection:
            return
        category, identifier = selection
        values = self.project.active_map.all_collection(category)
        entry_index = next((index for index, entry in enumerate(values)
                            if isinstance(entry, dict) and entry.get("id") == identifier), -1)
        if entry_index < 0:
            return
        try:
            self.command_coordinator.mark("map")
            self.project.active_map.mutate_collection_entry(
                category, entry_index, path, action)
            self.set_status(self.translator("object_contents_updated"))
            self._refresh_map()
        except (IndexError, KeyError, TypeError, ValueError) as error:
            self.set_status(str(error))

    def _add_map_collection_value(
            self, path: str, value: object) -> None:
        selection = self.map_canvas.selected_entity

        if not selection:
            return

        category, identifier = selection

        if (
            category != "objects"
            or path != "initialContents"
        ):
            self.set_status(
                f"Explicit collection value is unsupported: "
                f"{category}.{path}"
            )
            return

        try:
            stack = self._validated_item_stack(
                value
            )
        except ValueError as error:
            self.set_status(
                str(error)
            )
            return

        item_id = str(
            stack["itemId"]
        )

        quantity = int(
            stack["quantity"]
        )

        try:
            self.command_coordinator.mark(
                "map"
            )

            self.project.active_map.add_object_initial_content(
                int(identifier),
                item_id,
                quantity,
            )

            self.set_status(
                self.translator(
                    "object_contents_updated"
                )
            )

            self._refresh_map()

        except (
            IndexError,
            KeyError,
            TypeError,
            ValueError,
        ) as error:
            self.set_status(
                str(error)
            )

    def _edit_door_animated_collision(
            self,
            definition_id: str) -> None:
        if self.workspace is None:
            self.set_status(
                self.translator(
                    "door_workspace_required"
                )
            )
            return

        try:
            definition = self.workspace.find(
                "objects",
                definition_id,
            )

            entry = DoorAuthoringService(
                self.workspace
            ).entry(
                definition_id,
                self.project.active_map.tile_size,
            )

            if (
                definition is None
                or entry is None
            ):
                raise ValueError(
                    self.translator(
                        "door_definition_invalid"
                    ).format(
                        definition_id=definition_id,
                    )
                )

            static_collision = definition.data.get(
                "collision"
            )

            dialog = AnimatedCollisionEditorDialog(
                self.workspace,
                self.asset_root,
                entry.source_animation_id,
                self.translator,
                self,
                channel=OBJECT_COLLISION_MASK_CHANNEL,
                fallback_mask=(
                    static_collision
                    if isinstance(static_collision, dict)
                    else None
                ),
            )

            if not dialog.exec():
                return

            AnimationFrameMaskService(
                self.workspace
            ).replace_channel(
                entry.source_animation_id,
                OBJECT_COLLISION_MASK_CHANNEL,
                dialog.result_effective_masks(),
            )

        except ValueError as error:
            self.set_status(
                str(error)
            )
            return

        self._content_changed()

        self.set_status(
            self.translator(
                "animated_collision_saved"
            ).format(
                definition_id=definition_id,
            )
        )

    def _place_door_definition(
            self,
            definition_id: str) -> None:
        if self.workspace is None:
            self.set_status(
                self.translator(
                    "door_workspace_required"
                )
            )
            return

        definition = self.workspace.find(
            "objects",
            definition_id,
        )

        if (
            definition is None
            or not isinstance(
                definition.data.get("door"),
                dict,
            )
        ):
            self.set_status(
                self.translator(
                    "door_definition_invalid"
                ).format(
                    definition_id=definition_id,
                )
            )
            return

        local_issues = (
            self.workspace
            .validate_local(
                definition
            )
        )

        if any(
            issue.is_error
            for issue in local_issues
        ):
            self._refresh_diagnostics(
                local_issues
            )
            return

        self.mode_tabs.setCurrentIndex(
            0
        )

        self._clear_toolbar_tools()

        self.map_canvas.set_door_selection(
            definition_id
        )

        self.set_status(
            self.translator(
                "door_placement_active"
            ).format(
                definition_id=definition_id,
            )
        )

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

            if (
                category == "objects"
                and isinstance(
                    definition.data.get("door"),
                    dict,
                )
            ):
                self._place_door_definition(
                    definition_id
                )
                return

        self.mode_tabs.setCurrentIndex(0)
        self._clear_toolbar_tools()
        self.map_canvas.set_entity_selection(category, definition_id)
        self.set_status(self.translator("placement_active").format(definition_id=definition_id))

    def _migrate_legacy_tile_collision(self):
        if self.workspace is None:
            return None

        return LegacyTileCollisionMigrationService().migrate(
            self.project,
            self.workspace,
        )

    def _content_changed(self) -> None:
        self.command_coordinator.mark("content")
        self.semantic_catalog.invalidate()
        self._refresh_all()

    def set_tool(self, tool: str) -> None:
        self.map_canvas.set_tool(tool)
        tool_names = {"select": "tools_select", "erase": "tools_erase", "none": "tool_none"}
        self.set_status(self.translator("tool_status").format(
            tool=self.translator(tool_names.get(tool, "tool_none"))))

    def _select_tool_toggled(self, checked: bool) -> None:
        if checked:
            self.set_tool("select")
        elif self.map_canvas.tool == "select":
            self.set_tool("none")

    def _erase_tool_toggled(self, checked: bool) -> None:
        if checked:
            self.set_tool("erase")
        elif self.map_canvas.tool == "erase":
            self.set_tool("none")

    def _clear_toolbar_tools(self) -> None:
        for key in ("select", "erase_tiles"):
            action = self.actions.get(key)
            if action is not None and action.isChecked():
                action.setChecked(False)

    def _stamp_selected(self, stamp_id: str) -> None:
        self._clear_toolbar_tools()
        self.map_canvas.set_stamp_selection(stamp_id)

    def _terrain_selected(self, selection: object) -> None:
        self._clear_toolbar_tools()
        self.map_canvas.set_terrain_selection(selection)  # type: ignore[arg-type]

    def _pattern_selected(self, selection: object) -> None:
        self._clear_toolbar_tools()
        self.map_canvas.set_terrain_selection(selection)  # type: ignore[arg-type]

    def _room_selected(self, profile: object) -> None:
        self._clear_toolbar_tools()
        self.map_canvas.set_room_profile(profile)  # type: ignore[arg-type]

    def _map_element_selected(self, payload: object) -> None:
        self._clear_toolbar_tools()
        self.map_canvas.set_active_payload(payload)  # type: ignore[arg-type]

    def map_canvas_grid(self, checked: bool) -> None:
        self.map_canvas.set_grid_visible(checked)

    def map_canvas_snap(self, checked: bool) -> None:
        self.map_canvas.set_snap_enabled(checked)

    def import_tileset(self) -> None:
        if self.workspace is None:
            self.show_error(self.translator("content_directory_unavailable"))
            return
        self.tileset_library.add_files()

    def _tile_selected(self, tileset_id: str, source_index: int, flags: int) -> None:
        valid, message = self.map_canvas.editing.can_use_tileset(tileset_id)
        if not valid:
            self.set_status(message)
            return
        self._clear_toolbar_tools()
        self.map_canvas.selected_tile = (tileset_id, source_index, flags)
        self.map_canvas.set_brush(tileset_id, [source_index], flags)
        self.set_status(f"Tile selected: {tileset_id} [{source_index}]")

    def _atlas_tile_selected(self, tileset_id: str, source_index: int, flags: int) -> None:
        self.semantic_editor.set_selection(tileset_id, source_index)

    def _semantic_saved(self) -> None:
        self.command_coordinator.mark("content")
        self.semantic_catalog.invalidate()
        self._refresh_all()
        self.set_status(self.translator("semantic_saved"))

    def _brush_selected(self, tileset_id: str, source_indices: object, flags: int) -> None:
        if isinstance(source_indices, list) and all(isinstance(value, int) for value in source_indices):
            self._clear_toolbar_tools()
            self.map_canvas.set_brush(tileset_id, source_indices, flags)

    def _layer_selected(self, index: int) -> None:
        self.map_canvas.set_layer(index)

    def _select_map(self, map_id: str) -> None:
        try:
            self.project.select_map(map_id); self._refresh_map()
        except ValueError as error:
            self.set_status(str(error))

    def _populate_recent_menu(self) -> None:
        self.recent_menu.clear()
        for path_text in self.preferences.recent_projects:
            action = self.recent_menu.addAction(path_text)
            action.triggered.connect(lambda checked=False, value=path_text: self._open_recent(value))
        if not self.preferences.recent_projects:
            empty = self.recent_menu.addAction(self.translator("no_recent_projects"))
            empty.setEnabled(False)

    def _open_recent(self, path_text: str) -> None:
        path = Path(path_text)
        if not path.is_file():
            self.set_status(self.translator("recent_project_missing").format(path=path_text))
            return
        if not self._confirm_unsaved():
            return
        project, diagnostics = WorldProject.open(path)
        if project is None:
            self._refresh_diagnostics(diagnostics)
            return
        self.project = project
        self.preferences.last_project = path_text
        self._remember_recent_project(path_text)
        save_preferences(self.preferences)
        migration = self._migrate_legacy_tile_collision()
        self._refresh_all()
        self._refresh_diagnostics(list(diagnostics) + list(migration.diagnostics if migration else []))

    def _remember_recent_project(self, path_text: str) -> None:
        entries = [entry for entry in self.preferences.recent_projects if entry != path_text]
        entries.insert(0, path_text)
        self.preferences.recent_projects = entries[:8]

    def new_project(self) -> None:
        if not self._confirm_unsaved():
            return
        self.project = WorldProject.new()
        self.project.path = self.default_project_path
        self._refresh_all(); self.set_status(self.translator("new_blank_project"))

    def new_map(self) -> None:
        folders = self._map_folders()
        dialog = MapPropertiesDialog(
            self.translator, suggested_id=f"map.{len(self.project.maps) + 1}",
            folders=list(folders.values()), parent=self)
        if dialog.exec() != MapPropertiesDialog.DialogCode.Accepted:
            return
        properties = dialog.properties()
        try:
            self.project.add_map(MapDocument.new(
                properties.map_id, properties.width, properties.height,
                properties.tile_size, properties.include_player_spawn))
            self._set_map_folder(properties.map_id, properties.folder)
            self._refresh_all()
        except ValueError as error:
            self.show_error(str(error))

    def edit_map(self, map_id: str) -> None:
        document = self.project.map_by_id(map_id)
        if document is None:
            self.show_error(self.translator("map_not_found"))
            return
        folders = self._map_folders()
        dialog = MapPropertiesDialog(
            self.translator, document=document, folders=list(folders.values()),
            current_folder=folders.get(map_id, ""), parent=self)
        if dialog.exec() != MapPropertiesDialog.DialogCode.Accepted:
            return
        properties = dialog.properties()
        try:
            self.project.set_map_properties(
                map_id, properties.map_id, properties.width,
                properties.height, properties.tile_size)
            if properties.map_id != map_id:
                folders.pop(map_id, None)
            self._set_map_folder(properties.map_id, properties.folder)
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
        self.project = project
        self.preferences.last_project = path
        self._remember_recent_project(path)
        save_preferences(self.preferences)

        migration = self._migrate_legacy_tile_collision()

        self._refresh_all()

        migration_diagnostics = (
            list(migration.diagnostics)
            if migration is not None
            else []
        )

        self._refresh_diagnostics(
            [
                *diagnostics,
                *migration_diagnostics,
            ]
        )

        if migration is not None and migration.changed:
            self.set_status(
                "Legacy tile collision migrated to Pixel Collision"
            )

    def save(self) -> None:
        try:
            if self.project.path is None:
                self.save_as()
                return
            self.project.save()
            if self.workspace: self.workspace.save_all()
            self.set_status(self.translator("saved"))
        except (OSError, ValueError) as error:
            self.show_error(str(error))

    def save_as(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Save Authored Project", "", "World Project (*.uworld);;Map (*.umap)")
        if path:
            try:
                old_key = self._map_folder_key()
                self.project.save_as(Path(path))
                self._remember_recent_project(path)
                new_key = self._map_folder_key()
                if old_key != new_key and old_key in self.preferences.map_folders:
                    self.preferences.map_folders[new_key] = self.preferences.map_folders.pop(old_key)
                    save_preferences(self.preferences)
                self.set_status(self.translator("saved"))
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
        self.set_status(self.translator("validation_passed") if not any(issue.is_error for issue in issues) else self.translator("validation_failed"))

    def export_maps(self) -> None:
        if not self.workspace:
            self.show_error(
                self.translator("workspace_required_export")
            )
            return

        migration = self._migrate_legacy_tile_collision()

        if migration is not None and not migration.ok:
            self._refresh_diagnostics(
                list(
                    migration.diagnostics
                )
            )
            self.set_status(
                "Export blocked: legacy collision requires review"
            )
            return

        directory = QFileDialog.getExistingDirectory(self, "Export DMAP directory")
        if not directory: return
        result, issues = WorldExportService().export(
            self.project, Path(directory), self.workspace)
        self._refresh_diagnostics(issues)
        self.set_status(self.translator("export_completed") if result.ok else self.translator("export_failed"))

    def _update_playtest_icon(self) -> None:
        """Mirror the playtest state on the toolbar icon (play/stop)."""
        running = self.playtest.process is not None and self.playtest.process.poll() is None
        self.actions["playtest"].setIcon(icon("stop" if running else "playtest"))

    def toggle_playtest(self) -> None:
        if self.playtest.process and self.playtest.process.poll() is None:
            self.playtest.stop()
            self._update_playtest_icon()
            self.set_status(self.translator("playtest_stopped"))
            return

        if not self.workspace:
            self.show_error(
                "The repository content directory is unavailable"
            )
            return

        migration = self._migrate_legacy_tile_collision()

        if migration is not None and not migration.ok:
            self._refresh_diagnostics(
                list(
                    migration.diagnostics
                )
            )
            self.set_status(
                "Playtest blocked: legacy collision requires review"
            )
            return

        success, issues = self.playtest.start(self.project, self.workspace, self.asset_root)
        self._refresh_diagnostics(issues); self._update_playtest_icon(); self.set_status(self.translator("playtest_started") if success else self.translator("playtest_failed"))

    def undo(self) -> None:
        changed = self.command_coordinator.undo(self.project.active_map, self.workspace)
        if changed: self._refresh_all()

    def redo(self) -> None:
        changed = self.command_coordinator.redo(self.project.active_map, self.workspace)
        if changed: self._refresh_all()

    def import_map(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Import UMAP",
            "",
            "Authored Map (*.umap)",
        )

        if not path:
            return

        try:
            self.project.import_map(
                Path(path)
            )

            migration = self._migrate_legacy_tile_collision()

            self._refresh_all()

            if migration is not None:
                self._refresh_diagnostics(
                    list(
                        migration.diagnostics
                    )
                )

                if migration.changed:
                    self.set_status(
                        "Imported map collision migrated to Pixel Collision"
                    )
        except ValueError as error:
            self.show_error(
                str(error)
            )

    def remove_map(self, map_id: str) -> None:
        answer = QMessageBox.question(
            self, self.translator("map_remove"),
            self.translator("map_remove_confirm").format(map_id=map_id),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        try:
            self.project.remove_map(map_id)
            self._map_folders().pop(map_id, None)
            save_preferences(self.preferences)
            self._refresh_all()
        except ValueError as error: self.show_error(str(error))

    def set_entry_map(self, map_id: str) -> None:
        try: self.project.set_entry_map(map_id); self._refresh_all()
        except ValueError as error: self.show_error(str(error))

    def set_language(self, language: str) -> None:
        self.translator.set_language(language); self.preferences.language = language; save_preferences(self.preferences)
        self._retranslate_ui()
        self._update_title()

    def open_settings(self) -> None:
        dialog = SettingsDialog(self.preferences, self.translator, self)
        if dialog.exec() != SettingsDialog.DialogCode.Accepted:
            return
        previous_language = self.preferences.language
        previous_theme = self._theme_mode
        previous_asset_root = self.preferences.asset_root
        dialog.commit()
        save_preferences(self.preferences)
        if self.preferences.theme != previous_theme:
            self._theme_mode = self.preferences.theme
            self._apply_theme_mode()
        if self.preferences.language != previous_language:
            self.translator.set_language(self.preferences.language)
            self._retranslate_ui()
            self._update_title()
        new_asset_root = Path(self.preferences.asset_root) if self.preferences.asset_root else None
        if new_asset_root != self.asset_root:
            self.asset_root = new_asset_root
            self._refresh_all()
        self.set_status(self.translator("settings_saved"))

    def set_theme_mode(self, mode: str) -> None:
        if mode not in theme.THEME_MODES or mode == self._theme_mode:
            return
        self._theme_mode = mode
        self.preferences.theme = mode
        save_preferences(self.preferences)
        self._apply_theme_mode()
        self.set_status(f"{self.translator('theme')}: {self.translator(f'theme_{mode}')}")

    def _apply_theme_mode(self) -> None:
        theme.apply_theme(self._theme_mode)
        for mode, action in self._theme_actions.items():
            action.setChecked(mode == self._theme_mode)
        self.map_canvas.update()

    def _system_scheme_changed(self, scheme: object) -> None:
        """Re-apply the resolved scheme while following the system theme."""
        del scheme
        if self._theme_mode == theme.THEME_SYSTEM:
            theme.apply_theme(theme.THEME_SYSTEM)
            self.map_canvas.update()

    def _map_folder_key(self) -> str:
        path = self.project.path or self.default_project_path
        return str(path) if path else "<unsaved-project>"

    def _map_folders(self) -> dict[str, str]:
        return self.preferences.map_folders.setdefault(self._map_folder_key(), {})

    def _set_map_folder(self, map_id: str, folder: str) -> None:
        folders = self._map_folders()
        if folder.strip():
            folders[map_id] = folder.strip()
        else:
            folders.pop(map_id, None)
        save_preferences(self.preferences)

    def _tileset_folder_key(self) -> str:
        return str(self.workspace.root) if self.workspace else "<no-content-workspace>"

    def _tileset_folders(self) -> dict[str, list[str]]:
        return self.preferences.tileset_folders.setdefault(self._tileset_folder_key(), {})

    def _tileset_folders_changed(self, folders: object) -> None:
        if not isinstance(folders, dict):
            return
        self.preferences.tileset_folders[self._tileset_folder_key()] = {
            str(folder): [str(tileset_id) for tileset_id in tileset_ids]
            for folder, tileset_ids in folders.items()
            if str(folder).strip() and isinstance(tileset_ids, list)
        }
        save_preferences(self.preferences)

    def _update_title(self) -> None:
        self.setWindowTitle(self.translator("app") + (" *" if self.has_unsaved_changes() else ""))

    def _autosave(self) -> None:
        if not self.has_unsaved_changes():
            return
        try:
            paths = autosave(self.project, self.workspace)
            if paths:
                self.set_status(self.translator("autosave_written").format(count=len(paths)))
        except OSError as error:
            self._refresh_diagnostics([Diagnostic("error", f"autosave failed: {error}", code="autosave")])

    def set_status(self, message: str) -> None:
        self.statusBar().showMessage(message)

    def _refresh_diagnostics(self, diagnostics: list[Diagnostic]) -> None:
        self.diagnostics = diagnostics
        self.diagnostics_panel.set_diagnostics(diagnostics)

    def _navigate_to_definition(self, definition_id: str) -> None:
        """Open the definition referenced by a diagnostic (audit S2)."""
        if not self.workspace:
            return
        for category in self.workspace.category_counts():
            if self.workspace.find(category, definition_id) is not None:
                self.mode_tabs.setCurrentIndex(1)
                self.content_browser.select_definition(category, definition_id)
                self.set_status(self.translator("definition_opened").format(definition_id=definition_id))
                return
        self.set_status(self.translator("definition_not_found").format(definition_id=definition_id))

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


def _open_repository_workspace(repository_root: Path) -> ContentWorkspace:
    content_root = repository_root / "content" / "definitions"
    if content_root.is_dir() and any(content_root.rglob("*.json")):
        return ContentWorkspace.open(content_root)
    workspace = ContentWorkspace.new(content_root)
    workspace.save_all()
    return workspace


def main(argv: list[str] | None = None) -> int:
    parser = __import__("argparse").ArgumentParser(description="Dungeon Underworld Python Content Studio")
    parser.add_argument("--project-root", type=Path, help="repository root used by the Content Studio")
    parser.add_argument("--project", type=Path, help="authored .uworld or .umap")
    parser.add_argument("--cpp-root", type=Path, help="repository root containing C++ tools")
    args = parser.parse_args(argv)
    app = QApplication(sys.argv if argv is None else [sys.argv[0], *argv])
    # Standard dialogs (OK/Cancel/Yes/No) follow the studio language
    # through Qt's own translations (audit DL4/DL9).
    preferences = load_preferences()
    qt_locale = {"pt-BR": "pt_BR", "en-US": "en_US"}
    qt_translator = QTranslator(app)
    if qt_translator.load(f"qtbase_{qt_locale.get(preferences.language, 'pt_BR')}",
                          QLibraryInfo.path(QLibraryInfo.LibraryPath.TranslationsPath)):
        app.installTranslator(qt_translator)
    repository_root = (args.project_root or Path.cwd()).expanduser().resolve()
    workspace = _open_repository_workspace(repository_root)
    asset_root = repository_root / "assets"
    asset_root.mkdir(parents=True, exist_ok=True)
    project_path = args.project or (repository_root / "content" / "world.uworld")
    project = None
    if project_path.is_file():
        project, diagnostics = WorldProject.open(project_path)
        if project is None:
            print("\n".join(issue.message for issue in diagnostics), file=sys.stderr)
            return 1
    else:
        project = WorldProject.new()
        project.path = project_path
    cpp_root = args.cpp_root or repository_root
    window = MainWindow(project, workspace, asset_root, CppToolchain(cpp_root, asset_root=asset_root))
    window.show()
    return app.exec()
