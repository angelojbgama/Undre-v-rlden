from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import QApplication, QLineEdit, QSizePolicy
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QApplication = None  # type: ignore[assignment]
    QLineEdit = None  # type: ignore[assignment,misc]
    QSizePolicy = None  # type: ignore[assignment,misc]
    Qt = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.model.world_project import WorldProject


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class QtSmokeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def test_blank_startup_has_no_implicit_builtin_selection_and_native_text_editing(self) -> None:
        from tools.content_studio.ui.main_window import MainWindow

        with tempfile.TemporaryDirectory() as directory:
            content = {"format": "dungeon-underworld-content", "version": 5}
            content.update({category: [] for category in CONTENT_CATEGORIES})
            root = Path(directory)
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            window = MainWindow(WorldProject.new(), ContentWorkspace.open(root))
            self.addCleanup(window.close)
            self.assertIsNone(window.selected_definition)
            self.assertEqual("", window.map_canvas.selected_definition_id)
            editor = QLineEdit("map.untitled")
            editor.setCursorPosition(4)
            editor.insert(".edited")
            self.assertEqual("map..editeduntitled", editor.text())

    def test_repository_root_bootstraps_persistent_content_workspace(self) -> None:
        from tools.content_studio.ui.main_window import _open_repository_workspace

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = _open_repository_workspace(root)
            expected = root / "content" / "definitions" / "content.json"
            self.assertEqual(root / "content" / "definitions", workspace.root)
            self.assertTrue(expected.is_file())
            self.assertFalse(workspace.dirty)

    def test_entity_browser_has_search_and_separate_categories(self) -> None:
        from tools.content_studio.ui.widgets import ContentBrowser

        with tempfile.TemporaryDirectory() as directory:
            content = {"format": "dungeon-underworld-content", "version": 5}
            content.update({category: [] for category in CONTENT_CATEGORIES})
            content["enemies"] = [{"id": "enemy.slime", "visualSetId": "", "behaviorProfileId": "", "attackIds": [], "rewardProfileId": None}]
            content["objects"] = [{"id": "object.chest", "visualSetId": ""}]
            root = Path(directory)
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            browser = ContentBrowser(ContentWorkspace.open(root), ("enemies", "npcs", "objects", "pickups"))
            self.addCleanup(browser.deleteLater)
            browser.category.setCurrentIndex(browser.category.findData("enemies"))
            browser.search.setText("slime")
            self.assertEqual(1, browser.list.count())
            self.assertEqual("enemies", browser.category.currentData())
            self.assertTrue(browser.create_button.isEnabled())
            self.assertFalse(browser.delete_button.isEnabled())
            browser.list.setCurrentRow(0)
            self.assertTrue(browser.delete_button.isEnabled())

    def test_context_toolbar_canvas_drop_and_map_elements_are_available(self) -> None:
        from tools.content_studio.interaction.drag_payload import StudioDragPayload
        from tools.content_studio.ui.main_window import MainWindow
        from tools.content_studio.ui.widgets import MapElementsPalette

        with tempfile.TemporaryDirectory() as directory:
            content = {"format": "dungeon-underworld-content", "version": 5}
            content.update({category: [] for category in CONTENT_CATEGORIES})
            root = Path(directory)
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            window = MainWindow(WorldProject.new(), ContentWorkspace.open(root))
            self.addCleanup(window.close)
            visible_actions = [action.text() for action in window._toolbar.actions() if not action.isSeparator()]
            self.assertLessEqual(len(visible_actions), 5)
            self.assertTrue(window.map_canvas.acceptDrops())
            self.assertTrue(MapElementsPalette().elements.dragEnabled())
            payload = StudioDragPayload.content("enemies", "enemy.test")
            self.assertEqual(payload, StudioDragPayload.from_bytes(payload.to_bytes()))

    def test_mode_sections_are_navigation_rows_above_the_workspace(self) -> None:
        from tools.content_studio.ui.main_window import MainWindow

        with tempfile.TemporaryDirectory() as directory:
            content = {"format": "dungeon-underworld-content", "version": 5}
            content.update({category: [] for category in CONTENT_CATEGORIES})
            root = Path(directory)
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            window = MainWindow(WorldProject.new(), ContentWorkspace.open(root))
            self.addCleanup(window.close)

            self.assertEqual("Mapas", window.mode_tabs.tabText(0))
            self.assertEqual("Conteúdos", window.mode_tabs.tabText(1))
            self.assertEqual("Mapas", window._section_tabs.tabText(0))

            window.mode_tabs.setCurrentIndex(1)
            self.assertEqual("Definições", window._section_tabs.tabText(0))
            self.assertEqual("Assets", window._section_tabs.tabText(1))
            self.assertFalse(window._toolbar.isVisible())
            window._section_tabs.setCurrentIndex(1)
            self.assertIs(window.asset_browser, window._content_panels.currentWidget())

            window.mode_tabs.setCurrentIndex(0)
            self.assertEqual("Mapas", window._section_tabs.tabText(0))
            self.assertIs(window._map_split, window._workspace_pages.currentWidget())
            self.assertFalse(window.delete_map_selection_button.isEnabled())

            spawn_id = window.project.active_map.add_player_spawn("spawn.test", 16, 16)
            window.map_canvas.selection_controller.select("playerSpawns", spawn_id)
            self.assertTrue(window.delete_map_selection_button.isEnabled())
            self.assertTrue(window.map_canvas.delete_selection())
            self.assertEqual([], window.project.active_map.data["playerSpawns"])
            window.project.active_map.dirty = False

            for splitter in (window._map_split, window._content_split):
                self.assertTrue(splitter.childrenCollapsible())
                self.assertEqual(8, splitter.handleWidth())
                self.assertTrue(splitter.opaqueResize())
                self.assertTrue(splitter.isCollapsible(0))
                self.assertFalse(splitter.isCollapsible(1))
                self.assertTrue(splitter.isCollapsible(2))
                self.assertEqual(QSizePolicy.Policy.Ignored, splitter.widget(0).sizePolicy().horizontalPolicy())
                self.assertEqual(QSizePolicy.Policy.Ignored, splitter.widget(2).sizePolicy().horizontalPolicy())
                splitter.resize(1200, 500)
                splitter.setSizes([64, 1000, 120])
                self.assertLessEqual(splitter.sizes()[0], 80)
                splitter.setSizes([0, 1000, 120])
                self.assertEqual(0, splitter.sizes()[0])

    def test_tileset_import_dialog_opens_offscreen(self) -> None:
        from tools.content_studio.ui.tileset_import_dialog import TilesetImportDialog

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = {"format": "dungeon-underworld-content", "version": 5}
            content.update({category: [] for category in CONTENT_CATEGORIES})
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            dialog = TilesetImportDialog(ContentWorkspace.open(root), None)
            self.addCleanup(dialog.deleteLater)
            self.assertTrue(dialog.windowTitle())
            self.assertTrue(dialog.source.isEnabled())

    def test_tileset_library_smart_terrain_and_semantic_editor_open_offscreen(self) -> None:
        from tools.content_studio.services.tile_semantic_catalog import TileSemanticCatalog
        from tools.content_studio.ui.terrain.smart_terrain_palette import SmartTerrainPalette
        from tools.content_studio.ui.terrain.terrain_rule_dialog import TerrainRuleDialog
        from tools.content_studio.ui.terrain.tile_semantic_editor import TileSemanticEditor
        from tools.content_studio.ui.tilesets.batch_tileset_import_dialog import BatchTilesetImportDialog
        from tools.content_studio.ui.tilesets.tileset_library_widget import TilesetLibraryWidget
        from tools.content_studio.services.tileset_library import TilesetLibrary

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = {"format": "dungeon-underworld-content", "version": 5}
            content.update({category: [] for category in CONTENT_CATEGORIES})
            content["tilesets"] = [{"id": "tileset.test", "displayName": "Test", "relativeAssetPath": "test.png", "tileSize": 16, "columns": 2, "rows": 2}]
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            workspace = ContentWorkspace.open(root)
            library = TilesetLibrary(workspace)
            widget = TilesetLibraryWidget(workspace, WorldProject.new(), None)
            palette = SmartTerrainPalette(TileSemanticCatalog(workspace))
            editor = TileSemanticEditor(workspace, TileSemanticCatalog(workspace))
            rule_dialog = TerrainRuleDialog(workspace, None, "tileset.test")
            dialog = BatchTilesetImportDialog(library, None)
            for value in (widget, palette, editor, rule_dialog, dialog):
                self.addCleanup(value.deleteLater)
            self.assertTrue(widget.acceptDrops())
            self.assertTrue(palette.room.isEnabled())
            self.assertEqual(9, len(rule_dialog.slots))
            self.assertTrue(palette.collision.isEnabled())
            self.assertGreater(widget.atlas.tiles.maximumWidth(), 100_000)
            self.assertEqual(Qt.ContextMenuPolicy.CustomContextMenu, widget.tilesets.contextMenuPolicy())
            self.assertEqual(Qt.ContextMenuPolicy.CustomContextMenu, widget.atlas.tiles.contextMenuPolicy())
            menu = widget._tileset_context_menu("tileset.test")
            self.addCleanup(menu.deleteLater)
            self.assertEqual(["Gerenciar lógica Smart Terrain..."], [action.text() for action in menu.actions()])
            self.assertTrue(editor.save_button.isEnabled())
            self.assertTrue(dialog.windowTitle())


if __name__ == "__main__":
    unittest.main()
