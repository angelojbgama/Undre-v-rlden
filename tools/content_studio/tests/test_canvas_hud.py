"""Tests for the map canvas HUD (zoom controls, coordinates, placement banner)."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtCore import QEvent, Qt
    from PySide6.QtGui import QKeyEvent
    from PySide6.QtWidgets import QApplication, QToolButton
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QEvent = None  # type: ignore[assignment,misc]
    Qt = None  # type: ignore[assignment,misc]
    QKeyEvent = None  # type: ignore[assignment,misc]
    QApplication = None  # type: ignore[assignment,misc]
    QToolButton = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.ui.map_canvas import MapCanvas


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class CanvasHudTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def _canvas(self, workspace: ContentWorkspace | None = None) -> MapCanvas:
        canvas = MapCanvas()
        self.addCleanup(canvas.deleteLater)
        canvas.set_context(MapDocument.new("map.hud", 16, 16, 16), workspace, None)
        return canvas

    def test_zoom_controls_buttons_and_shortcuts(self) -> None:
        canvas = self._canvas()
        buttons = canvas._zoom_buttons.findChildren(QToolButton)
        self.assertEqual(3, len(buttons))
        shortcuts = [action.shortcut().toString() for action in canvas.actions()]
        self.assertIn("Ctrl++", shortcuts)
        self.assertIn("Ctrl+-", shortcuts)
        before = canvas.camera.zoom
        canvas.zoom_in()
        self.assertGreater(canvas.camera.zoom, before)
        canvas.zoom_out()
        self.assertAlmostEqual(canvas.camera.zoom, before, delta=0.001)

    def test_zoom_is_clamped(self) -> None:
        canvas = self._canvas()
        for _ in range(30):
            canvas.zoom_out()
        self.assertAlmostEqual(canvas.camera.zoom, 0.25, delta=0.001)

    def test_placement_banner_follows_the_tool_state(self) -> None:
        canvas = self._canvas()
        self.assertIsNone(canvas._placement_banner_text())

        canvas.set_brush("tileset.audit", [3], 0)
        self.assertIn("tileset.audit", canvas._placement_banner_text())
        self.assertIn("3", canvas._placement_banner_text())

        canvas.set_tool("select")
        self.assertIsNone(canvas._placement_banner_text())

        canvas.set_entity_selection("objects", "object.chest")
        self.assertIn("object.chest", canvas._placement_banner_text())

        canvas.cancel_placement()
        self.assertIsNone(canvas._placement_banner_text())

        canvas.set_tool("spawn")
        self.assertIn("Player Spawn", canvas._placement_banner_text())

    def test_entity_banner_prefers_the_display_name(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = {"format": "dungeon-underworld-content", "version": 5}
            content.update({category: [] for category in CONTENT_CATEGORIES})
            content["objects"] = [{"id": "object.chest", "displayName": "Chest", "visualSetId": ""}]
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            workspace = ContentWorkspace.open(root)

            canvas = self._canvas(workspace)
            canvas.set_entity_selection("objects", "object.chest")
            self.assertIn("Chest", canvas._placement_banner_text())
            self.assertNotIn("object.chest", canvas._placement_banner_text())

    def test_escape_cancels_the_placement_banner(self) -> None:
        canvas = self._canvas()
        canvas.set_entity_selection("objects", "object.chest")
        event = QKeyEvent(QEvent.Type.KeyPress, Qt.Key.Key_Escape, Qt.KeyboardModifier.NoModifier)
        canvas.keyPressEvent(event)
        self.assertEqual("select", canvas.tool)
        self.assertIsNone(canvas._placement_banner_text())


if __name__ == "__main__":
    unittest.main()
