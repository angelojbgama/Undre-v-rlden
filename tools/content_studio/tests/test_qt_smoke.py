from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QColor, QImage
    from PySide6.QtWidgets import QApplication, QLineEdit, QSizePolicy
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QApplication = None  # type: ignore[assignment]
    QColor = None  # type: ignore[assignment,misc]
    QImage = None  # type: ignore[assignment,misc]
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
            content["tilesets"] = [{
                "id": "tileset.startup", "displayName": "Startup",
                "relativeAssetPath": "startup.png", "tileSize": 16,
                "columns": 1, "rows": 1,
            }]
            root = Path(directory)
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            window = MainWindow(WorldProject.new(), ContentWorkspace.open(root))
            self.addCleanup(window.close)
            self.assertIsNone(window.selected_definition)
            self.assertEqual("", window.map_canvas.selected_definition_id)
            self.assertTrue(window.actions["select"].isChecked())
            self.assertEqual("select", window.map_canvas.tool)
            window.actions["select"].trigger()
            self.assertFalse(window.actions["select"].isChecked())
            self.assertEqual("none", window.map_canvas.tool)
            window.actions["select"].trigger()
            self.assertTrue(window.actions["select"].isChecked())
            self.assertEqual("select", window.map_canvas.tool)
            editor = QLineEdit("map.untitled")
            editor.setCursorPosition(4)
            editor.insert(".edited")
            self.assertEqual("map..editeduntitled", editor.text())

    def test_tileset_pixel_collision_context_menu_and_badge(self) -> None:
        from tools.content_studio.services.localization import Translator
        from tools.content_studio.services.tile_collision_service import TileCollisionService
        from tools.content_studio.ui.tilesets.tile_atlas_widget import PIXEL_COLLISION_ROLE
        from tools.content_studio.ui.tilesets.tileset_library_widget import TilesetLibraryWidget

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            content = {
                "format": "dungeon-underworld-content",
                "version": 5,
            }

            content.update({
                category: []
                for category in CONTENT_CATEGORIES
            })

            content["tilesets"] = [{
                "id": "tileset.pixel.collision",
                "displayName": "Pixel Collision",
                "relativeAssetPath": "tiles.png",
                "tileSize": 16,
                "columns": 2,
                "rows": 1,
            }]

            (root / "content.json").write_text(
                encode_json(content),
                encoding="utf-8",
            )

            image = QImage(
                32,
                16,
                QImage.Format.Format_ARGB32,
            )

            image.fill(
                QColor(
                    80,
                    80,
                    80,
                )
            )

            self.assertTrue(
                image.save(
                    str(root / "tiles.png")
                )
            )

            workspace = ContentWorkspace.open(
                root
            )

            translator = Translator(
                "pt-BR"
            )

            widget = TilesetLibraryWidget(
                workspace,
                WorldProject.new(),
                root,
                translator,
            )

            self.addCleanup(
                widget.close
            )

            widget.atlas.set_tileset(
                "tileset.pixel.collision"
            )

            item = widget.atlas.tiles.item(0)

            self.assertIsNotNone(item)

            self.assertFalse(
                bool(
                    item.data(
                        PIXEL_COLLISION_ROLE
                    )
                )
            )

            menu = widget._atlas_tile_context_menu(
                "tileset.pixel.collision",
                0,
            )

            self.addCleanup(
                menu.close
            )

            action_texts = [
                action.text()
                for action in menu.actions()
                if not action.isSeparator()
            ]

            self.assertIn(
                translator(
                    "define_tile_pixel_collision"
                ),
                action_texts,
            )

            service = TileCollisionService(
                workspace
            )

            # Explicitly configured but empty masks are still authored
            # Pixel Collision and therefore receive the visual marker.
            service.set_mask(
                "tileset.pixel.collision",
                0,
                [0] * 256,
            )

            widget.atlas.refresh()

            item = widget.atlas.tiles.item(0)

            self.assertIsNotNone(item)

            self.assertTrue(
                bool(
                    item.data(
                        PIXEL_COLLISION_ROLE
                    )
                )
            )

            self.assertIn(
                "Pixel Collision",
                item.toolTip(),
            )

            menu = widget._atlas_tile_context_menu(
                "tileset.pixel.collision",
                0,
            )

            self.addCleanup(
                menu.close
            )

            action_texts = [
                action.text()
                for action in menu.actions()
                if not action.isSeparator()
            ]

            self.assertIn(
                translator(
                    "edit_tile_pixel_collision"
                ),
                action_texts,
            )

            self.assertIn(
                translator(
                    "remove_tile_pixel_collision"
                ),
                action_texts,
            )

            service.remove_mask(
                "tileset.pixel.collision",
                0,
            )

            widget.atlas.refresh()

            item = widget.atlas.tiles.item(0)

            self.assertFalse(
                bool(
                    item.data(
                        PIXEL_COLLISION_ROLE
                    )
                )
            )

    def test_map_properties_dialog_is_shared_and_map_browser_has_context_edit(self) -> None:
        from tools.content_studio.model.map_document import MapDocument
        from tools.content_studio.services.localization import Translator
        from tools.content_studio.ui.map_properties_dialog import MapPropertiesDialog
        from tools.content_studio.ui.widgets import MapBrowser

        create_dialog = MapPropertiesDialog(
            Translator("pt-BR"), suggested_id="map.new", folders=["Floresta", "Deserto"])
        self.addCleanup(create_dialog.close)
        self.assertEqual(("map.new", 32, 24, 16), (
            create_dialog.properties().map_id, create_dialog.properties().width,
            create_dialog.properties().height, create_dialog.properties().tile_size))
        self.assertTrue(create_dialog.player_spawn.isVisibleTo(create_dialog))
        create_dialog.folder.setCurrentText("Floresta")
        self.assertEqual("Floresta", create_dialog.properties().folder)

        document = MapDocument.new("map.edit", 8, 6, 24)
        edit_dialog = MapPropertiesDialog(Translator("pt-BR"), document=document)
        self.addCleanup(edit_dialog.close)
        self.assertEqual(("map.edit", 8, 6, 24), (
            edit_dialog.properties().map_id, edit_dialog.properties().width,
            edit_dialog.properties().height, edit_dialog.properties().tile_size))
        self.assertFalse(edit_dialog.player_spawn.isVisibleTo(edit_dialog))

        browser = MapBrowser()
        self.addCleanup(browser.close)
        browser.refresh(
            ["map.forest.1", "map.forest.2", "map.desert.1"], "map.forest.2",
            {"map.forest.1": "Floresta", "map.forest.2": "Floresta", "map.desert.1": "Deserto"})
        self.assertEqual(Qt.ContextMenuPolicy.CustomContextMenu, browser.list.contextMenuPolicy())
        self.assertEqual(2, browser.list.topLevelItemCount())
        self.assertEqual("Floresta", browser.list.topLevelItem(0).text(0))
        self.assertEqual(2, browser.list.topLevelItem(0).childCount())
        self.assertEqual("map.forest.2", browser._current_map_id())

    def test_select_tool_hits_and_moves_player_start_with_drag_preview(self) -> None:
        from tools.content_studio.model.map_document import MapDocument
        from tools.content_studio.ui.map_canvas import MapCanvas

        document = MapDocument.new("map.spawn-move", 8, 8)
        document.add_player_spawn("player.start", 16, 16)
        canvas = MapCanvas()
        self.addCleanup(canvas.close)
        canvas.set_context(document, None, None)
        canvas.set_tool("select")
        messages: list[str] = []
        canvas.status_changed.connect(messages.append)

        selection = canvas._hit_selection((16, 16))
        self.assertIsNotNone(selection)
        self.assertEqual(("playerSpawns", "player.start"), selection.as_tuple())  # type: ignore[union-attr]
        canvas._moving = selection
        canvas.renderer.moving_selection = selection.as_tuple()  # type: ignore[union-attr]
        canvas.renderer.moving_world = (48, 32)
        self.assertEqual((48, 32), canvas.renderer.moving_world)

        canvas._move_selection((48, 32))

        self.assertEqual(
            {"x": 48, "y": 32}, document.data["playerSpawns"][0]["position"])
        self.assertEqual("Player Start movido", messages[-1])
        self.assertTrue(document.undo())
        self.assertEqual(
            {"x": 16, "y": 16}, document.data["playerSpawns"][0]["position"])

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
            self.assertLessEqual(len(visible_actions), 6)
            self.assertEqual(["Playtest", "Apagar"], visible_actions[-2:])
            self.assertTrue(window.actions["erase_tiles"].isCheckable())
            window.actions["erase_tiles"].trigger()
            self.assertTrue(window.actions["erase_tiles"].isChecked())
            self.assertEqual("erase", window.map_canvas.tool)
            window.actions["erase_tiles"].trigger()
            self.assertFalse(window.actions["erase_tiles"].isChecked())
            self.assertEqual("none", window.map_canvas.tool)
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
            window.delete_map_selection_button.click()
            self.assertEqual([], window.project.active_map.data["playerSpawns"])
            self.assertFalse(window.delete_map_selection_button.isEnabled())
            self.assertEqual("Seleção excluída", window.statusBar().currentMessage())
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

    def test_player_frame_sequence_dialog_exposes_independent_pixel_mask_rows(self) -> None:
        from tools.content_studio.services.localization import Translator
        from tools.content_studio.services.player_authoring_service import (
            PlayerAuthoringService,
            PlayerCollisionMaskSpec,
        )
        from tools.content_studio.ui.player_library_widget import (
            FrameSequenceDialog,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = {
                "format": "dungeon-underworld-content",
                "version": 5,
            }
            content.update({
                category: [] for category in CONTENT_CATEGORIES
            })
            (root / "content.json").write_text(
                encode_json(content), encoding="utf-8")
            workspace = ContentWorkspace.open(root)
            translator = Translator("pt-BR")
            dialog = FrameSequenceDialog(
                workspace, None, translator, "Idle — Baixo")
            self.addCleanup(dialog.deleteLater)

            channels = PlayerAuthoringService.FRAME_MASK_CHANNELS
            self.assertIs(translator, dialog.translate)
            self.assertFalse(hasattr(dialog, "mask_channel"))
            self.assertEqual(set(channels), set(dialog.frame_mask_summaries))
            self.assertEqual(
                set(channels), set(dialog.edit_frame_mask_buttons))
            self.assertEqual(
                set(channels), set(dialog.copy_previous_mask_buttons))
            self.assertEqual(
                set(channels), set(dialog.clear_frame_mask_buttons))
            self.assertTrue(all(
                button.text() == "Editar por pixel..."
                for button in dialog.edit_frame_mask_buttons.values()
            ))

            mask = PlayerCollisionMaskSpec(
                width=1,
                height=1,
                origin_x=0,
                origin_y=0,
                cells=(1,),
            )
            dialog._selected_indices = [0]
            dialog._selected_masks = [{
                channel: mask for channel in channels
            }]
            dialog.sequence.addItem("frame #0")
            dialog.sequence.setCurrentRow(0)
            dialog._refresh_frame_mask_summaries()

            self.assertTrue(all(
                "Definida" in dialog.frame_mask_summaries[channel].text()
                for channel in channels
            ))
            self.assertTrue(all(
                dialog.clear_frame_mask_buttons[channel].isEnabled()
                for channel in channels
            ))

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
        from tools.content_studio.ui.tilesets.tileset_properties_dialog import TilesetPropertiesDialog
        from tools.content_studio.ui.tilesets.tileset_library_widget import TilesetLibraryWidget
        from tools.content_studio.services.tileset_library import TilesetLibrary

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = {"format": "dungeon-underworld-content", "version": 5}
            content.update({category: [] for category in CONTENT_CATEGORIES})
            content["tilesets"] = [{"id": "tileset.test", "displayName": "Test", "relativeAssetPath": "test.png", "tileSize": 16, "columns": 2, "rows": 2}]
            content["tileSemantics"] = [
                {
                    "id": f"semantic.rule.terrain_test.floor.tileset_test.interior.{slot}.{index}",
                    "tilesetId": "tileset.test", "sourceIndex": index % 4,
                    "family": "terrain.test", "role": "floor", "topology": "interior",
                    "north": "unknown", "east": "unknown", "south": "unknown", "west": "unknown",
                    "preferredLayer": "", "flipXAllowed": False, "variantWeight": 1,
                }
                for index, slot in enumerate((
                    "north_west", "north", "north_east", "west", "center",
                    "east", "south_west", "south", "south_east",
                ))
            ]
            content["tileSemantics"].extend([
                {
                    "id": "semantic.test.wall", "tilesetId": "tileset.test", "sourceIndex": 0,
                    "family": "terrain.test", "role": "wall", "topology": "interior",
                    "north": "unknown", "east": "unknown", "south": "unknown", "west": "unknown",
                    "preferredLayer": "", "flipXAllowed": False, "variantWeight": 1,
                },
                {
                    "id": "semantic.solid.wall", "tilesetId": "tileset.test", "sourceIndex": 1,
                    "family": "terrain.zz_solid", "role": "wall", "topology": "interior",
                    "north": "unknown", "east": "unknown", "south": "unknown", "west": "unknown",
                    "preferredLayer": "", "flipXAllowed": False, "variantWeight": 1,
                },
            ])
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            image = QImage(32, 32, QImage.Format.Format_RGBA8888)
            image.fill(QColor("#7aa2c8"))
            self.assertTrue(image.save(str(root / "test.png")))
            workspace = ContentWorkspace.open(root)
            library = TilesetLibrary(workspace)
            widget = TilesetLibraryWidget(workspace, WorldProject.new(), None)
            palette = SmartTerrainPalette(TileSemanticCatalog(workspace))
            editor = TileSemanticEditor(workspace, TileSemanticCatalog(workspace))
            rule_dialog = TerrainRuleDialog(workspace, None, "tileset.test")
            dialog = BatchTilesetImportDialog(library, None)
            properties = TilesetPropertiesDialog(library, "tileset.test", root)
            for value in (widget, palette, editor, rule_dialog, dialog, properties):
                self.addCleanup(value.deleteLater)
            self.assertTrue(widget.acceptDrops())
            self.assertFalse(palette.room.isEnabled())
            self.assertFalse(hasattr(palette, "role"))
            self.assertFalse(hasattr(palette, "family"))
            self.assertFalse(hasattr(palette, "collision"))
            palette.set_asset_root(root)
            palette.set_workspace(workspace)
            self.assertIsNone(palette.selection())
            self.assertEqual({"terrain.test", "terrain.zz_solid"}, set(palette.family_cards))
            self.assertTrue(all(card.size().width() == card.size().height() == 96
                                for card in palette.family_cards.values()))
            palette.select_family("terrain.test")
            self.assertTrue(palette.room.isEnabled())
            floor_selection = palette.selection()
            self.assertIsNotNone(floor_selection)
            self.assertEqual("floor", floor_selection.role)
            self.assertFalse(hasattr(floor_selection, "collision"))
            self.assertIn(
                "Piso + parede",
                palette.family_cards["terrain.test"].statusTip(),
            )
            preview_tiles = palette.preview_tiles("terrain.test")
            self.assertEqual(9, len(preview_tiles))
            self.assertTrue(all(tile is not None and not tile.isNull() for tile in preview_tiles))
            palette._show_preview("terrain.test", palette.family_cards["terrain.test"])
            self.assertEqual(9, len(palette.preview.cells))
            self.assertIn("terrain.test", palette.preview.title.text())
            palette.preview.hide()
            palette.select_family("terrain.zz_solid")
            wall_selection = palette.selection()
            self.assertIsNotNone(wall_selection)
            self.assertEqual("wall", wall_selection.role)
            self.assertFalse(hasattr(wall_selection, "collision"))
            palette.select_family("terrain.test")
            room_profile = palette.profile()
            self.assertEqual(
                ("floor", "wall"),
                (
                    room_profile.floor.role,
                    room_profile.boundary.role,
                ),
            )
            self.assertFalse(
                hasattr(
                    room_profile.floor,
                    "collision",
                )
            )
            self.assertFalse(
                hasattr(
                    room_profile.boundary,
                    "collision",
                )
            )
            self.assertEqual(9, len(rule_dialog.slots))
            self.assertTrue(all(button.minimumWidth() == 42 and button.maximumWidth() == 42
                                and button.minimumHeight() == 42 and button.maximumHeight() == 42
                                for button in rule_dialog.slots.values()))
            self.assertEqual(QSizePolicy.Policy.Fixed, rule_dialog.slot_panel.sizePolicy().horizontalPolicy())
            self.assertEqual(Qt.Orientation.Horizontal, rule_dialog.content_splitter.orientation())
            self.assertEqual(0, rule_dialog.content_splitter.indexOf(rule_dialog.controls_panel))
            self.assertEqual(1, rule_dialog.content_splitter.indexOf(rule_dialog.atlas))
            rule_dialog._atlas_selected("tileset.test", 0, 0)
            self.assertEqual("", rule_dialog.slots["center"].text())
            self.assertIn("#0", rule_dialog.slots["center"].toolTip())
            self.assertGreater(widget.atlas.tiles.maximumWidth(), 100_000)
            atlas = widget.atlas.tiles
            atlas.resize(80, 120)
            self.application.processEvents()
            self.assertEqual((0, 0), (atlas.row(atlas.item(0)), atlas.column(atlas.item(0))))
            self.assertEqual((0, 1), (atlas.row(atlas.item(1)), atlas.column(atlas.item(1))))
            self.assertEqual((1, 0), (atlas.row(atlas.item(2)), atlas.column(atlas.item(2))))
            atlas.resize(500, 120)
            self.application.processEvents()
            self.assertEqual((1, 0), (atlas.row(atlas.item(2)), atlas.column(atlas.item(2))))
            widget.set_map_tile_size(16)
            atlas.setCurrentRow(3)
            self.assertEqual(3, atlas.currentItem().data(Qt.ItemDataRole.UserRole))
            widget.set_map_tile_size(16)
            self.assertEqual(3, atlas.currentItem().data(Qt.ItemDataRole.UserRole))
            widget.refresh()
            self.assertEqual(3, atlas.currentItem().data(Qt.ItemDataRole.UserRole))
            self.assertEqual(Qt.ContextMenuPolicy.CustomContextMenu, widget.tilesets.contextMenuPolicy())
            self.assertEqual(Qt.ContextMenuPolicy.CustomContextMenu, widget.atlas.tiles.contextMenuPolicy())
            menu = widget._tileset_context_menu("tileset.test")
            self.addCleanup(menu.deleteLater)
            self.assertEqual(
                ["Gerenciar tileset...", "Adicionar à pasta", "", "Gerenciar lógica Smart Terrain..."],
                [action.text() for action in menu.actions()],
            )
            self.assertEqual(0, properties.folder_memberships.count())
            properties.name.setText("Tileset Renomeado")
            properties._save()
            self.assertEqual(
                "Tileset Renomeado",
                workspace.find("tilesets", "tileset.test").data["displayName"],
            )
            tile_menu = widget._atlas_tile_context_menu("tileset.test", 3)
            self.addCleanup(tile_menu.deleteLater)
            self.assertEqual(
                [
                    "Editar nome e família do tile...",
                    "",
                    "Definir colisão por pixel...",
                    "",
                    "Gerenciar lógica Smart Terrain...",
                ],
                [action.text() for action in tile_menu.actions()],
            )
            editor.set_selection("tileset.test", 3)
            editor.semantic_id.setText("tile.test.named")
            self.assertIn("terrain.test", [editor.family.itemText(index) for index in range(editor.family.count())])
            editor.family.setCurrentText("terrain.named")
            editor.save_semantic()
            saved = workspace.find("tileSemantics", "tile.test.named")
            self.assertIsNotNone(saved)
            self.assertEqual("terrain.named", saved.data["family"])
            self.assertTrue(editor.save_button.isEnabled())
            self.assertTrue(dialog.windowTitle())



    def test_item_visual_picker_lists_static_sprites_and_animation_frames(self) -> None:
        from tools.content_studio.ui.item_visual_picker import (
            ItemVisualPickerDialog,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            content = {
                "format": "dungeon-underworld-content",
                "version": 5,
            }

            content.update({
                category: []
                for category in CONTENT_CATEGORIES
            })

            content["visualImages"] = [{
                "id": "image.items",
                "root": "gameAssets",
                "relativePath": "items.png",
            }]

            content["staticSprites"] = [{
                "id": "visual.item.existing",
                "imageId": "image.items",
                "source": {
                    "x": 0,
                    "y": 0,
                    "width": 16,
                    "height": 16,
                },
                "anchor": {
                    "x": 8,
                    "y": 15,
                },
            }]

            content["animations"] = [{
                "id": "animation.items",
                "imageId": "image.items",
                "loop": True,
                "frames": [
                    {
                        "source": {
                            "x": 16,
                            "y": 0,
                            "width": 16,
                            "height": 16,
                        },
                        "anchor": {
                            "x": 8,
                            "y": 15,
                        },
                        "drawOffset": {
                            "x": 0,
                            "y": 0,
                        },
                        "durationTicks": 4,
                        "markers": [],
                    },
                    {
                        "source": {
                            "x": 32,
                            "y": 0,
                            "width": 16,
                            "height": 16,
                        },
                        "anchor": {
                            "x": 7,
                            "y": 14,
                        },
                        "drawOffset": {
                            "x": 0,
                            "y": 0,
                        },
                        "durationTicks": 4,
                        "markers": [],
                    },
                ],
            }]

            (root / "content.json").write_text(
                encode_json(content),
                encoding="utf-8",
            )

            workspace = ContentWorkspace.open(
                root
            )

            dialog = ItemVisualPickerDialog(
                workspace,
                "item.potion",
            )

            self.addCleanup(
                dialog.deleteLater
            )

            self.assertEqual(
                3,
                dialog.visuals.count(),
            )

            entries = [
                dialog.visuals.itemText(index)
                for index in range(
                    dialog.visuals.count()
                )
            ]

            self.assertTrue(
                any(
                    "visual.item.existing" in value
                    for value in entries
                )
            )

            self.assertTrue(
                any(
                    "animation.items #0" in value
                    for value in entries
                )
            )

            self.assertTrue(
                any(
                    "animation.items #1" in value
                    for value in entries
                )
            )

            dialog.visuals.setCurrentIndex(
                0
            )

            self.assertEqual(
                "visual.item.existing",
                dialog.commit_selection(),
            )

            frame_index = next(
                index
                for index in range(
                    dialog.visuals.count()
                )
                if dialog.visuals.itemData(index)[0] == "animation"
                and dialog.visuals.itemData(index)[2] == 1
            )

            dialog.visuals.setCurrentIndex(
                frame_index
            )

            self.assertEqual(
                "visual.item.potion",
                dialog.commit_selection(),
            )

            created = workspace.find(
                "staticSprites",
                "visual.item.potion",
            )

            self.assertIsNone(
                created
            )

            selection = (
                dialog.selected_selection()
            )

            self.assertIsNotNone(
                selection
            )

            assert selection is not None

            self.assertEqual(
                "animation",
                selection.kind,
            )

            self.assertEqual(
                "animation.items",
                selection.definition_id,
            )

            self.assertEqual(
                1,
                selection.frame_index,
            )



    def test_item_library_authors_searches_and_places_generated_pickup(self) -> None:
        try:
            from tools.content_studio.ui.item_library_widget import (
                ItemDefinitionDialog,
                ItemLibraryWidget,
            )
        except ModuleNotFoundError as error:
            raise AssertionError(
                "ItemLibraryWidget ainda nao foi implementado"
            ) from error

        from tools.content_studio.services.localization import (
            Translator,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            content = {
                "format": "dungeon-underworld-content",
                "version": 5,
            }

            content.update({
                category: []
                for category in CONTENT_CATEGORIES
            })

            content["visualImages"] = [{
                "id": "image.item.base",
                "root": "contentWorkspace",
                "relativePath": "item.png",
            }]

            content["staticSprites"] = [{
                "id": "visual.item.base",
                "imageId": "image.item.base",
                "source": {
                    "x": 0,
                    "y": 0,
                    "width": 16,
                    "height": 16,
                },
                "anchor": {
                    "x": 8,
                    "y": 15,
                },
            }]

            (root / "content.json").write_text(
                encode_json(content),
                encoding="utf-8",
            )

            image = QImage(
                16,
                16,
                QImage.Format.Format_ARGB32,
            )

            image.fill(
                QColor(
                    100,
                    140,
                    190,
                )
            )

            self.assertTrue(
                image.save(
                    str(root / "item.png")
                )
            )

            workspace = ContentWorkspace.open(
                root
            )

            translator = Translator(
                "pt-BR"
            )

            dialog = ItemDefinitionDialog(
                workspace,
                root,
                translator,
            )

            self.addCleanup(
                dialog.close
            )

            self.assertEqual(
                66,
                dialog.stack_limit.value(),
            )

            dialog.name.setText(
                "Armadura de Teste"
            )

            dialog.item_id.setText(
                "item.armor_test"
            )

            dialog.set_visual_id(
                "visual.item.base"
            )

            equipment_index = dialog.category.findData(
                "equipment"
            )

            self.assertGreaterEqual(
                equipment_index,
                0,
            )

            dialog.category.setCurrentIndex(
                equipment_index
            )

            self.assertEqual(
                1,
                dialog.stack_limit.value(),
            )

            self.assertFalse(
                dialog.stack_limit.isEnabled()
            )

            created = dialog.commit()

            self.assertEqual(
                "item.armor_test",
                created.definition_id,
            )

            self.assertEqual(
                1,
                created.data["stackLimit"],
            )

            widget = ItemLibraryWidget(
                workspace,
                root,
                translator,
            )

            self.addCleanup(
                widget.close
            )

            self.assertEqual(
                1,
                widget.items.count(),
            )

            self.assertIn(
                "Armadura de Teste",
                widget.items.item(0).text(),
            )

            self.assertIn(
                "item.armor_test",
                widget.items.item(0).text(),
            )

            widget.search.setText(
                "Armadura"
            )

            self.assertEqual(
                1,
                widget.items.count(),
            )

            widget.search.clear()

            placements = []

            widget.place_requested.connect(
                lambda category, definition_id:
                placements.append(
                    (
                        category,
                        definition_id,
                    )
                )
            )

            widget.place_current()

            self.assertEqual(
                [
                    (
                        "pickups",
                        "pickup.armor_test",
                    )
                ],
                placements,
            )

            payload = widget._drag_payload(
                [
                    widget.items.currentItem()
                ]
            )

            self.assertIsNotNone(
                payload
            )

            self.assertEqual(
                "pickups",
                payload.category,
            )

            self.assertEqual(
                "pickup.armor_test",
                payload.definition_id,
            )

            self.assertTrue(
                widget.delete_current(
                    confirm=False
                )
            )

            self.assertIsNone(
                workspace.find(
                    "items",
                    "item.armor_test",
                )
            )

            self.assertIsNone(
                workspace.find(
                    "pickups",
                    "pickup.armor_test",
                )
            )

    def test_main_window_exposes_items_section_and_activates_pickup_placement(self) -> None:
        try:
            from tools.content_studio.ui.item_library_widget import (
                ItemLibraryWidget,
            )
        except ModuleNotFoundError as error:
            raise AssertionError(
                "ItemLibraryWidget ainda nao foi implementado"
            ) from error

        from tools.content_studio.services.item_authoring_service import (
            ItemAuthoringService,
        )
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            content = {
                "format": "dungeon-underworld-content",
                "version": 5,
            }

            content.update({
                category: []
                for category in CONTENT_CATEGORIES
            })

            content["visualImages"] = [{
                "id": "image.item.potion",
                "root": "contentWorkspace",
                "relativePath": "potion.png",
            }]

            content["staticSprites"] = [{
                "id": "visual.item.potion",
                "imageId": "image.item.potion",
                "source": {
                    "x": 0,
                    "y": 0,
                    "width": 16,
                    "height": 16,
                },
                "anchor": {
                    "x": 8,
                    "y": 15,
                },
            }]

            (root / "content.json").write_text(
                encode_json(content),
                encoding="utf-8",
            )

            image = QImage(
                16,
                16,
                QImage.Format.Format_ARGB32,
            )

            image.fill(
                QColor(
                    180,
                    60,
                    80,
                )
            )

            self.assertTrue(
                image.save(
                    str(root / "potion.png")
                )
            )

            workspace = ContentWorkspace.open(
                root
            )

            ItemAuthoringService(
                workspace
            ).create_item(
                "Pocao",
                "item.potion",
                "visual.item.potion",
                "consumable",
            )

            # The MainWindow close path correctly asks about unsaved content.
            # Persist this temporary fixture first so offscreen cleanup never
            # opens the interactive Unsaved Changes dialog.
            workspace.save_all()

            project = WorldProject.new()

            window = MainWindow(
                project,
                workspace,
                asset_root=root,
            )

            self.addCleanup(
                window.close
            )

            self.assertIsInstance(
                window.item_library,
                ItemLibraryWidget,
            )

            window.mode_tabs.setCurrentIndex(
                0
            )

            labels = [
                window._section_tabs.tabText(index)
                for index in range(
                    window._section_tabs.count()
                )
            ]

            item_label = window.translator(
                "items_tab"
            )

            self.assertIn(
                item_label,
                labels,
            )

            item_index = labels.index(
                item_label
            )

            window._section_tabs.setCurrentIndex(
                item_index
            )

            self.assertIs(
                window._map_panels.currentWidget(),
                window.item_library,
            )

            self.assertEqual(
                1,
                window.item_library.items.count(),
            )

            window.item_library.place_current()

            self.assertEqual(
                "pickups",
                window.map_canvas.selected_entity_category,
            )

            self.assertEqual(
                "pickup.potion",
                window.map_canvas.selected_definition_id,
            )

            self.assertEqual(
                "entity",
                window.map_canvas.tool,
            )



    def test_initial_contents_inspector_emits_only_valid_item_stacks(self) -> None:
        from PySide6.QtWidgets import (
            QComboBox,
            QPushButton,
        )

        from tools.content_studio.services.item_authoring_service import (
            ItemAuthoringService,
        )
        from tools.content_studio.ui.widgets import (
            StructuredInspector,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            content = {
                "format": "dungeon-underworld-content",
                "version": 5,
            }

            content.update({
                category: []
                for category in CONTENT_CATEGORIES
            })

            content["visualImages"] = [{
                "id": "image.item.chest_test",
                "root": "contentWorkspace",
                "relativePath": "item.png",
            }]

            content["staticSprites"] = [{
                "id": "visual.item.chest_test",
                "imageId": "image.item.chest_test",
                "source": {
                    "x": 0,
                    "y": 0,
                    "width": 16,
                    "height": 16,
                },
                "anchor": {
                    "x": 8,
                    "y": 15,
                },
            }]

            (root / "content.json").write_text(
                encode_json(content),
                encoding="utf-8",
            )

            workspace = ContentWorkspace.open(
                root
            )

            ItemAuthoringService(
                workspace
            ).create_item(
                "Pocao",
                "item.potion",
                "visual.item.chest_test",
                "consumable",
            )

            inspector = StructuredInspector()

            self.addCleanup(
                inspector.deleteLater
            )

            inspector.set_workspace(
                workspace
            )

            chest = {
                "id": 1,
                "definitionId": "object.chest",
                "position": {
                    "x": 16,
                    "y": 16,
                },
                "initialContents": [],
                "persistence": "persistent",
            }

            inspector.set_object(
                "Chest",
                chest,
            )

            picker = inspector.findChild(
                QComboBox,
                "initialContentsItemPicker",
            )

            add_button = inspector.findChild(
                QPushButton,
                "initialContentsAddButton",
            )

            self.assertIsNotNone(
                picker
            )

            self.assertIsNotNone(
                add_button
            )

            self.assertTrue(
                add_button.isEnabled()
            )

            self.assertEqual(
                "item.potion",
                picker.currentData(),
            )

            emitted = []

            inspector.collection_value_requested.connect(
                lambda path, value:
                emitted.append(
                    (
                        path,
                        value,
                    )
                )
            )

            add_button.click()

            self.assertEqual(
                [
                    (
                        "initialContents",
                        {
                            "itemId": "item.potion",
                            "quantity": 1,
                        },
                    )
                ],
                emitted,
            )

            chest["initialContents"] = [{
                "itemId": "",
                "quantity": 1,
            }]

            inspector.set_object(
                "Legacy Chest",
                chest,
            )

            required_reference = inspector.findChild(
                QComboBox,
                "initialContentsRequiredItemReference",
            )

            self.assertIsNotNone(
                required_reference
            )

            self.assertEqual(
                -1,
                required_reference.findData(
                    ""
                ),
            )

            self.assertEqual(
                -1,
                required_reference.currentIndex(),
            )

            empty_root = root / "empty"

            empty_root.mkdir()

            empty_content = {
                "format": "dungeon-underworld-content",
                "version": 5,
            }

            empty_content.update({
                category: []
                for category in CONTENT_CATEGORIES
            })

            (empty_root / "content.json").write_text(
                encode_json(
                    empty_content
                ),
                encoding="utf-8",
            )

            empty_workspace = ContentWorkspace.open(
                empty_root
            )

            inspector.set_workspace(
                empty_workspace
            )

            chest["initialContents"] = []

            inspector.set_object(
                "Empty Workspace Chest",
                chest,
            )

            add_button = inspector.findChild(
                QPushButton,
                "initialContentsAddButton",
            )

            self.assertIsNotNone(
                add_button
            )

            self.assertFalse(
                add_button.isEnabled()
            )

    def test_main_window_adds_initial_contents_from_typed_inspector(self) -> None:
        from PySide6.QtWidgets import (
            QPushButton,
        )

        from tools.content_studio.services.item_authoring_service import (
            ItemAuthoringService,
        )
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            content = {
                "format": "dungeon-underworld-content",
                "version": 5,
            }

            content.update({
                category: []
                for category in CONTENT_CATEGORIES
            })

            content["visualImages"] = [{
                "id": "image.item.chest_main",
                "root": "contentWorkspace",
                "relativePath": "item.png",
            }]

            content["staticSprites"] = [{
                "id": "visual.item.chest_main",
                "imageId": "image.item.chest_main",
                "source": {
                    "x": 0,
                    "y": 0,
                    "width": 16,
                    "height": 16,
                },
                "anchor": {
                    "x": 8,
                    "y": 15,
                },
            }]

            (root / "content.json").write_text(
                encode_json(content),
                encoding="utf-8",
            )

            workspace = ContentWorkspace.open(
                root
            )

            ItemAuthoringService(
                workspace
            ).create_item(
                "Pocao",
                "item.potion",
                "visual.item.chest_main",
                "consumable",
            )

            workspace.save_all()

            project = WorldProject.new()

            chest_id = project.active_map.add_entity(
                "objects",
                "object.chest",
                32,
                32,
            )

            window = MainWindow(
                project,
                workspace,
                asset_root=root,
            )

            self.addCleanup(
                window.deleteLater
            )

            window.map_canvas.selected_entity = (
                "objects",
                chest_id,
            )

            window._map_selection_changed(
                (
                    "objects",
                    chest_id,
                )
            )

            add_button = window.map_inspector.findChild(
                QPushButton,
                "initialContentsAddButton",
            )

            self.assertIsNotNone(
                add_button
            )

            self.assertTrue(
                add_button.isEnabled()
            )

            add_button.click()

            chest = project.active_map.entity(
                "objects",
                chest_id,
            )

            self.assertIsNotNone(
                chest
            )

            self.assertEqual(
                [
                    {
                        "itemId": "item.potion",
                        "quantity": 1,
                    }
                ],
                chest["initialContents"],
            )

            self.assertTrue(
                project.active_map.undo()
            )

            reverted_chest = project.active_map.entity(
                "objects",
                chest_id,
            )

            self.assertIsNotNone(
                reverted_chest
            )

            self.assertEqual(
                [],
                reverted_chest["initialContents"],
            )


if __name__ == "__main__":
    unittest.main()
