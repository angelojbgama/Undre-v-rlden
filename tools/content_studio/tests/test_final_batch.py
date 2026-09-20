"""Tests for the final audit batch: T2/T3, DL1/DL5/DL6, G10-G12."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QImage, QColor
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover - exercised on minimal CI images
    Qt = None  # type: ignore[assignment,misc]
    QImage = None  # type: ignore[assignment,misc]
    QColor = None  # type: ignore[assignment,misc]
    QApplication = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.model.types import ProjectPreferences
from tools.content_studio.services.preferences import load_preferences, save_preferences
from tools.content_studio.services.localization import Translator
from tools.content_studio.ui.map_properties_dialog import MapPropertiesDialog
from tools.content_studio.ui.settings_dialog import SettingsDialog
from tools.content_studio.ui.tilesets.tile_atlas_widget import TileAtlasWidget


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class FinalBatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    # --- DL1: presets only in create mode ---------------------------------
    def test_map_dialog_presets_only_when_creating(self) -> None:
        create = MapPropertiesDialog(Translator("pt-BR"), suggested_id="map.new")
        self.addCleanup(create.deleteLater)
        self.assertTrue(hasattr(create, "presets"))
        # Editing mode has no presets.
        document = MapDocument.new("map.edit", 16, 16, 16)
        edit = MapPropertiesDialog(Translator("pt-BR"), document=document)
        self.addCleanup(edit.deleteLater)
        self.assertFalse(hasattr(edit, "presets"))

    def test_map_dialog_preset_applies_dimensions(self) -> None:
        dialog = MapPropertiesDialog(Translator("pt-BR"), suggested_id="map.new")
        self.addCleanup(dialog.deleteLater)
        index = dialog.presets.findText("Sala 16 — 20 × 15")
        self.assertGreaterEqual(index, 0)
        dialog.presets.setCurrentIndex(index)
        self.assertEqual((20, 15, 16), (dialog.width_tiles.value(), dialog.height_tiles.value(), dialog.tile_size.value()))
        # Custom keeps user values untouched.
        dialog.presets.setCurrentIndex(0)
        self.assertEqual((20, 15, 16), (dialog.width_tiles.value(), dialog.height_tiles.value(), dialog.tile_size.value()))

    # --- G10: recent projects round trip -----------------------------------
    def test_recent_projects_persist_and_normalize(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            preferences = ProjectPreferences()
            preferences.recent_projects = ["/tmp/a.uworld", "", "/tmp/b.uworld"]
            save_preferences(preferences, path)
            loaded = load_preferences(path)
            self.assertEqual(["/tmp/a.uworld", "/tmp/b.uworld"], loaded.recent_projects)

    # --- G11: settings dialog commits into preferences ----------------------
    def test_settings_dialog_commits(self) -> None:
        preferences = ProjectPreferences(theme="light", language="pt-BR")
        dialog = SettingsDialog(preferences, Translator("pt-BR"))
        self.addCleanup(dialog.deleteLater)
        index = dialog.theme_mode.findData("dark")
        dialog.theme_mode.setCurrentIndex(index)
        dialog.commit()
        self.assertEqual("dark", preferences.theme)

    # --- T2: used tile counting --------------------------------------------
    def test_used_indices_count_only_opaque_tiles(self) -> None:
        image = QImage(64, 16, QImage.Format.Format_ARGB32)
        image.fill(0)  # fully transparent
        used = TileAtlasWidget._used_indices(image, 4, 1, 16)
        self.assertEqual([], used)
        for y in range(16):
            for x in range(16):
                image.setPixelColor(16 + x, y, QColor(120, 40, 40, 255))
        used = TileAtlasWidget._used_indices(image, 4, 1, 16)
        self.assertEqual([1], used)


from tools.content_studio.model.content_workspace import ContentWorkspace  # noqa: E402
from tools.content_studio.tests.test_attack_authoring_service import (  # noqa: E402
    attack_content,
    make_workspace,
)
from tools.content_studio.ui.attack_library_widget import AttackDefinitionDialog  # noqa: E402


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class AttackDialogMeleeVisibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def test_melee_group_hidden_for_projectile_attacks(self) -> None:
        temporary, workspace = make_workspace(attack_content())
        self.addCleanup(temporary.cleanup)
        dialog = AttackDefinitionDialog(workspace, None, "attack.slime.bounce", Translator("pt-BR"))
        self.addCleanup(dialog.deleteLater)
        dialog.kind.setCurrentIndex(dialog.kind.findData("projectile"))
        dialog._sync_enabled()
        self.assertFalse(dialog.melee_group.isVisibleTo(dialog))
        dialog.kind.setCurrentIndex(dialog.kind.findData("meleeHitbox"))
        dialog._sync_enabled()
        self.assertTrue(dialog.melee_group.isVisibleTo(dialog))


if __name__ == "__main__":
    unittest.main()
