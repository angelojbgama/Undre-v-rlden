"""Tests for the spritesheet library list and playback icons (SP1/SP2)."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QApplication = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.services.localization import Translator
from tools.content_studio.ui.spritesheet_library_widget import SpritesheetLibraryWidget


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class SpritesheetLibraryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def _widget(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        root = Path(directory.name)
        content = {"format": "dungeon-underworld-content", "version": 5}
        content.update({category: [] for category in CONTENT_CATEGORIES})
        content["visualImages"] = [
            {"id": "image.chest", "root": "contentWorkspace", "relativePath": "chest.png"},
            {"id": "image.door", "root": "contentWorkspace", "relativePath": "door.png"},
        ]
        content["animations"] = [
            {"id": "animation.chest.idle", "imageId": "image.chest", "displayName": "Idle",
             "loop": True, "frames": [{"source": {"x": 0, "y": 0, "width": 16, "height": 16},
                                       "anchor": {"x": 8, "y": 15}, "drawOffset": {"x": 0, "y": 0},
                                       "durationTicks": 4, "markers": []}]},
            {"id": "animation.door.idle", "imageId": "image.door", "displayName": "Idle",
             "loop": True, "frames": [{"source": {"x": 0, "y": 0, "width": 16, "height": 16},
                                       "anchor": {"x": 8, "y": 15}, "drawOffset": {"x": 0, "y": 0},
                                       "durationTicks": 4, "markers": []}]},
            {"id": "animation.chest.opening", "imageId": "image.chest", "displayName": "Opening",
             "loop": True, "frames": [{"source": {"x": 0, "y": 0, "width": 16, "height": 16},
                                       "anchor": {"x": 8, "y": 15}, "drawOffset": {"x": 0, "y": 0},
                                       "durationTicks": 4, "markers": []}]},
        ]
        (root / "content.json").write_text(encode_json(content), encoding="utf-8")
        workspace = ContentWorkspace.open(root)
        widget = SpritesheetLibraryWidget(workspace, root, Translator("pt-BR"))
        self.addCleanup(widget.deleteLater)
        return widget

    def test_items_cluster_by_spritesheet_and_name_the_sheet(self) -> None:
        widget = self._widget()
        labels = [widget.animations.item(index).text() for index in range(widget.animations.count())]
        self.assertEqual([
            "Idle · image.chest",
            "Opening · image.chest",
            "Idle · image.door",
        ], labels)
        self.assertEqual("animation.chest.idle", widget.animations.item(0).toolTip())

    def test_playback_buttons_carry_registry_icons(self) -> None:
        widget = self._widget()
        self.assertFalse(widget.play_button.icon().isNull())
        self.assertFalse(widget.pause_button.icon().isNull())
        self.assertNotIn("▶", widget.play_button.text())
        self.assertNotIn("⏸", widget.pause_button.text())


if __name__ == "__main__":
    unittest.main()
