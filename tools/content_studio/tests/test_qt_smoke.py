from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtWidgets import QApplication, QLineEdit
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QApplication = None  # type: ignore[assignment]
    QLineEdit = None  # type: ignore[assignment,misc]

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


if __name__ == "__main__":
    unittest.main()
