"""Tests for the semantics/stamps palette and smart terrain cards (SS1/ST1-ST3)."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtGui import QColor, QImage
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QColor = None  # type: ignore[assignment,misc]
    QImage = None  # type: ignore[assignment,misc]
    QApplication = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.model.world_project import WorldProject
from tools.content_studio.services.localization import Translator
from tools.content_studio.services.tile_semantic_catalog import TileSemanticCatalog
from tools.content_studio.ui.terrain.smart_terrain_palette import SmartTerrainPalette
from tools.content_studio.ui.terrain.tile_semantic_editor import TileSemanticEditor
from tools.content_studio.ui.widgets import SemanticPalette


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class SemanticsUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def _fixture(self, semantic_count: int = 3):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        content = {"format": "dungeon-underworld-content", "version": 5}
        content.update({category: [] for category in CONTENT_CATEGORIES})
        content["tilesets"] = [{
            "id": "tileset.thumbs", "displayName": "Thumbs", "relativeAssetPath": "thumbs.png",
            "tileSize": 16, "columns": 4, "rows": 1,
        }]
        content["tileSemantics"] = [
            {
                "id": f"semantic.thumbs.{index}", "tilesetId": "tileset.thumbs",
                "sourceIndex": index, "family": "terrain.thumbs", "role": "floor",
                "topology": "interior", "north": "unknown", "east": "unknown",
                "south": "unknown", "west": "unknown", "preferredLayer": "",
                "flipXAllowed": False, "variantWeight": 1,
            }
            for index in range(semantic_count)
        ]
        content["stamps"] = [{"id": "stamp.doorway", "displayName": "Doorway"}]
        (root / "content.json").write_text(encode_json(content), encoding="utf-8")
        image = QImage(64, 16, QImage.Format.Format_ARGB32)
        image.fill(QColor(140, 100, 90))
        self.assertTrue(image.save(str(root / "thumbs.png")))
        return root, ContentWorkspace.open(root)

    def test_semantic_items_use_human_labels_and_thumbnails(self) -> None:
        root, workspace = self._fixture()
        palette = SemanticPalette(translator=Translator("pt-BR"))
        self.addCleanup(palette.deleteLater)
        palette.set_workspace(workspace)
        palette.set_asset_root(root)
        self.assertEqual(3, palette.tiles.count())
        item = palette.tiles.item(0)
        self.assertIn("Piso", item.text())
        self.assertIn("Interno", item.text())
        self.assertIn("#0", item.text())
        self.assertNotIn("semantic.thumbs.0", item.text())
        self.assertFalse(item.icon().isNull())
        self.assertIn("semantic.thumbs.0", item.toolTip())

    def test_search_filters_semantics_and_stamps(self) -> None:
        root, workspace = self._fixture()
        palette = SemanticPalette(translator=Translator("pt-BR"))
        self.addCleanup(palette.deleteLater)
        palette.set_workspace(workspace)
        palette.set_asset_root(root)
        self.assertEqual(3, palette.tiles.count())
        self.assertEqual(1, palette.stamps.count())
        palette.search.setText("doorway")
        self.assertEqual(0, palette.tiles.count())
        self.assertEqual(1, palette.stamps.count())
        palette.search.clear()
        self.assertEqual(3, palette.tiles.count())

    def test_smart_terrain_cards_elide_and_status_matches_state(self) -> None:
        root, workspace = self._fixture(semantic_count=9)
        palette = SmartTerrainPalette(TileSemanticCatalog(workspace), Translator("pt-BR"))
        self.addCleanup(palette.deleteLater)
        palette.set_workspace(workspace)
        palette.set_asset_root(root)

        families = list(palette.family_cards.values())
        self.assertEqual(1, len(families))
        card = families[0]
        # Elided label: a single short line, never the wrapped raw family id.
        self.assertLessEqual(len(card.text()), 14)
        self.assertNotIn("\n", card.text())
        self.assertIn("Piso", card.statusTip())

        # With cards visible and nothing selected the status must invite
        # selection instead of claiming no family exists (audit ST2).
        palette.select_family("terrain.thumbs")
        self.assertIn("terrain.thumbs", palette.status.text())
        palette._selected_family = ""
        palette._selection_changed()
        self.assertEqual(
            Translator("pt-BR")("terrain_no_selection"), palette.status.text())

    def test_seed_and_room_have_tooltips(self) -> None:
        root, workspace = self._fixture(semantic_count=9)
        palette = SmartTerrainPalette(TileSemanticCatalog(workspace), Translator("pt-BR"))
        self.addCleanup(palette.deleteLater)
        palette.set_workspace(workspace)
        palette.set_asset_root(root)
        self.assertTrue(palette.seed.toolTip())
        self.assertTrue(palette.room.toolTip())

    def test_semantic_editor_edge_grid_and_tile_header(self) -> None:
        root, workspace = self._fixture(semantic_count=1)
        editor = TileSemanticEditor(workspace, TileSemanticCatalog(workspace), Translator("pt-BR"))
        self.addCleanup(editor.deleteLater)
        editor.set_asset_root(root)

        # No selection yet: header names the placeholder target.
        self.assertNotIn("tileset.thumbs", editor.tile_header.text())
        editor.set_selection("tileset.thumbs", 0)
        self.assertIn("tileset.thumbs", editor.tile_header.text())
        self.assertIn("#0", editor.tile_header.text())
        # SE2: the header shows the actual atlas tile.
        self.assertFalse(editor.tile_preview.pixmap().isNull())

        # SE1: the edge combos live in a compass around the center cell.
        self.assertEqual("unknown", editor.north.currentText())
        self.assertIsNotNone(editor.center_cell)
        editor._set(editor.north, "floor")
        editor.semantic_id.setText("semantic.thumbs.named")
        editor._set_family("terrain.thumbs")
        editor.save_semantic()
        saved = workspace.find("tileSemantics", "semantic.thumbs.named")
        self.assertIsNotNone(saved)
        self.assertEqual("floor", saved.data["north"])
        self.assertEqual("unknown", saved.data["west"])

        # Re-selecting the saved cell loads its persisted edges back.
        editor.set_selection("tileset.thumbs", 0)
        self.assertEqual("floor", editor.north.currentText())


if __name__ == "__main__":
    unittest.main()
