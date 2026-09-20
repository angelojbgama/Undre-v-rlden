"""Offscreen Qt tests for the strategy-aware Smart Terrain editors."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtGui import QColor, QIcon, QImage
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QColor = None  # type: ignore[assignment,misc]
    QIcon = None  # type: ignore[assignment,misc]
    QImage = None  # type: ignore[assignment,misc]
    QApplication = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.model.tile_semantics import TerrainSelection
from tools.content_studio.services.localization import Translator
from tools.content_studio.services.tile_semantic_catalog import TileSemanticCatalog
from tools.content_studio.ui.terrain.smart_terrain_palette import SmartTerrainPalette
from tools.content_studio.ui.terrain.terrain_rule_dialog import TerrainRuleDialog


def _slot_semantic(slot: str, source_index: int, weight: int = 1) -> dict[str, object]:
    return {
        "id": f"semantic.rule.terrain.ui.floor.tileset.ui.interior.{slot}.{source_index}",
        "tilesetId": "tileset.ui", "sourceIndex": source_index,
        "family": "terrain.ui", "role": "floor", "topology": "interior",
        "north": "unknown", "east": "unknown", "south": "unknown", "west": "unknown",
        "preferredLayer": "Ground", "flipXAllowed": False,
        "visualConfidence": "confirmed", "semanticConfidence": "probable",
        "gameplayConfidence": "unverified", "variantWeight": weight,
    }


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class TerrainRuleUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def _fixture(self, stamp_cells: list[dict[str, object]] | None = None):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        content: dict[str, object] = {"format": "dungeon-underworld-content", "version": 5}
        content.update({category: [] for category in CONTENT_CATEGORIES})
        content["tilesets"] = [{
            "id": "tileset.ui", "displayName": "UI", "relativeAssetPath": "ui.png",
            "tileSize": 16, "columns": 4, "rows": 4,
        }]
        # A legacy nine-slot floor rule keeps loading through the new editor.
        content["tileSemantics"] = [_slot_semantic(slot, index)
                                    for index, slot in enumerate((
                                        "north_west", "north", "north_east",
                                        "west", "center", "east",
                                        "south_west", "south", "south_east"))]
        content["tileSemantics"].append({
            "id": "semantic.rule.terrain.ui.floor.tileset.ui.interior.extra.10",
            "tilesetId": "tileset.ui", "sourceIndex": 10,
            "family": "terrain.ui", "role": "floor", "topology": "interior",
            "north": "unknown", "east": "unknown", "south": "unknown", "west": "unknown",
            "preferredLayer": "Ground", "flipXAllowed": False,
            "visualConfidence": "confirmed", "semanticConfidence": "probable",
            "gameplayConfidence": "unverified", "variantWeight": 3,
        })
        content["stamps"] = [{
            "id": "stamp.ui.pair_1x2", "displayName": "Pair", "width": 1, "height": 2,
            "cells": stamp_cells or [
                {"x": 0, "y": 0, "tileId": _slot_semantic("center", 4)["id"]},
                {"x": 0, "y": 1, "tileId": _slot_semantic("north", 1)["id"]},
            ],
            "anchor": {"x": 0, "y": 0}, "flipXAllowed": False, "atomic": True,
            "confidence": "confirmed",
        }]
        (root / "content.json").write_text(encode_json(content), encoding="utf-8")
        image = QImage(64, 64, QImage.Format.Format_ARGB32)
        image.fill(QColor(120, 120, 140))
        self.assertTrue(image.save(str(root / "ui.png")))
        return root, ContentWorkspace.open(root)

    def _dialog(self, workspace: ContentWorkspace) -> TerrainRuleDialog:
        dialog = TerrainRuleDialog(workspace, None, "tileset.ui", Translator("pt-BR"))
        self.addCleanup(dialog.deleteLater)
        return dialog

    def test_floor_editor_is_not_limited_to_nine_variants(self) -> None:
        root, workspace = self._fixture()
        dialog = self._dialog(workspace)
        self.assertEqual("floor", dialog.role.currentData())
        dialog.variant_editor.set_variants([(source, 1) for source in range(12)])
        self.assertEqual(12, len(dialog.variant_editor.variants()))
        self.assertEqual(12, dialog.variant_editor.variant_list.count())

    def test_add_remove_and_weight_flow_updates_the_variant_list(self) -> None:
        root, workspace = self._fixture()
        dialog = self._dialog(workspace)
        # Legacy rule loads as ten variants (nine slots plus the extra tile).
        self.assertEqual(10, len(dialog.variant_editor.variants()))
        dialog.variant_editor.remove_variant(0)
        self.assertEqual(9, len(dialog.variant_editor.variants()))
        # Clicking the atlas adds the tile back as a fresh variant...
        dialog._atlas_selected("tileset.ui", 0, 0)
        self.assertIn(0, [source for source, _ in dialog.variant_editor.variants()])
        # ...and the weight spinbox edits the selected entry.
        dialog.variant_editor.select_source(0)
        dialog.variant_editor.weight.setValue(9)
        self.assertEqual(9, dict(dialog.variant_editor.variants())[0])

    def test_saving_floor_writes_stable_variant_ids_and_migrates_legacy(self) -> None:
        root, workspace = self._fixture()
        dialog = self._dialog(workspace)
        dialog.variant_editor.select_source(0)
        dialog.variant_editor.weight.setValue(4)
        dialog.variant_editor.remove_variant(3)
        dialog._save()
        definitions = workspace.definitions("tileSemantics")
        variant_ids = [str(value.data.get("id")) for value in definitions
                       if str(value.data.get("id", "")).startswith("semantic.variant.")]
        legacy_ids = [str(value.data.get("id")) for value in definitions
                      if str(value.data.get("id", "")).startswith("semantic.rule.terrain.ui.floor")]
        self.assertEqual(9, len(variant_ids))
        self.assertEqual([], legacy_ids)
        weights = {int(value.data["sourceIndex"]): int(value.data["variantWeight"])
                   for value in definitions
                   if str(value.data.get("id", "")).startswith("semantic.variant.")}
        self.assertEqual(4, weights[0])
        self.assertNotIn(3, weights)

    def test_wall_rule_still_uses_the_connectivity_editor(self) -> None:
        root, workspace = self._fixture()
        dialog = self._dialog(workspace)
        self.assertEqual(9, len(dialog.slots))
        dialog._set_role("wall")
        dialog._select_slot("north_west")
        # Source 13 is not classified by the floor rule of this tileset.
        dialog._atlas_selected("tileset.ui", 13, 0)
        self.assertEqual(13, dialog.assignments["north_west"])
        dialog._save()
        saved = [value for value in workspace.definitions("tileSemantics")
                 if value.data.get("family") == "terrain.ui" and value.data.get("role") == "wall"]
        # The connectivity editor still persists a wall rule carrying the
        # freshly assigned atlas tile.
        self.assertIn(13, {int(value.data["sourceIndex"]) for value in saved})
        self.assertTrue(all(str(value.data["id"]).startswith("semantic.rule.")
                            for value in saved))

    def test_pattern_editor_lists_family_stamps(self) -> None:
        root, workspace = self._fixture()
        dialog = self._dialog(workspace)
        dialog._refresh_patterns("terrain.ui")
        self.assertEqual(1, dialog.pattern_editor.pattern_list.count())
        dialog._refresh_patterns("terrain.other")
        self.assertEqual(0, dialog.pattern_editor.pattern_list.count())

    def test_palette_exposes_patterns_and_strategy_previews(self) -> None:
        root, workspace = self._fixture()
        palette = SmartTerrainPalette(TileSemanticCatalog(workspace), Translator("pt-BR"))
        self.addCleanup(palette.deleteLater)
        palette.set_workspace(workspace)
        palette.set_asset_root(root)
        palette.select_family("terrain.ui")

        sections = {section.kind: section for section in palette.preview_sections("terrain.ui")}
        self.assertIn("connectivity", sections)
        self.assertEqual(9, len(sections["connectivity"].tiles))

        received: list[TerrainSelection] = []
        palette.pattern_selected.connect(received.append)
        palette._rebuild_pattern_row()
        layout = palette.pattern_layout
        self.assertEqual(1, layout.count())
        button = layout.itemAt(0).widget()
        button.click()
        self.assertEqual(1, len(received))
        self.assertEqual("stamp.ui.pair_1x2", received[0].pattern_id)
        self.assertEqual("terrain.ui", received[0].family)

    def test_variant_preview_shows_more_than_nine_entries(self) -> None:
        root, workspace = self._fixture()
        palette = SmartTerrainPalette(TileSemanticCatalog(workspace), Translator("pt-BR"))
        self.addCleanup(palette.deleteLater)
        palette.set_workspace(workspace)
        palette.set_asset_root(root)
        sections = {section.kind: section for section in palette.preview_sections("terrain.ui")}
        self.assertIn("variant", sections)
        self.assertGreaterEqual(len(sections["variant"].tiles), 10)


if __name__ == "__main__":
    unittest.main()
