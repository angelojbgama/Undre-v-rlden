"""Qt smoke tests for the UI Composer widget (offscreen)."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.model.content_workspace import ContentWorkspace

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication  # noqa: E402

from tools.content_studio.services.localization import Translator  # noqa: E402
from tools.content_studio.ui.ui_composer_widget import (  # noqa: E402
    LOGICAL_HEIGHT,
    LOGICAL_WIDTH,
    UiComposerWidget,
)

from .test_ui_authoring import make_workspace  # noqa: E402


class UiComposerWidgetTests(unittest.TestCase):
    def test_composer_edits_screens_and_previews(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = make_workspace(root)
            widget = UiComposerWidget(workspace, Translator("pt-BR"))
            service = widget.service

            service.create_screen("screen.hud")
            widget.refresh()
            widget.screens_list.setCurrentRow(0)
            self.assertEqual("screen.hud", widget._selected_screen)

            service.add_node("screen.hud", "hud.health", "meter", offset=(3, 2))
            service.set_meter("screen.hud", "hud.health", mode="segmented",
                              spacing=1, sprites={"full": "spr.heart.full"})
            service.bind_property("screen.hud", "hud.health", "value",
                                  "player.health.current")
            service.bind_property("screen.hud", "hud.health", "maximum",
                                  "player.health.max")
            service.add_state("screen.hud", "hud.health", "lowHealth",
                              visual={"tint": {"r": 255, "g": 64, "b": 64, "a": 255}},
                              condition={"source": "player.health.percentage",
                                         "operator": "lessOrEqual", "value": 30})
            widget.refresh()
            widget.screens_list.setCurrentRow(0)

            root_item = widget.hierarchy.topLevelItem(0)
            self.assertIsNotNone(root_item)
            self.assertEqual(1, root_item.childCount())
            flags = __import__("PySide6.QtCore", fromlist=["Qt"]).Qt.MatchFlag.MatchExactly
            self.assertEqual(1, len(widget.validation_list.findItems("OK", flags)))

            widget.canvas.set_preview_values({"player.health.current": 1,
                                              "player.health.max": 5})
            widget.canvas.repaint()

            workspace.save_all()
            reloaded = ContentWorkspace.open(root)
            screen = reloaded.find("uiScreens", "screen.hud")
            self.assertIsNotNone(screen)
            self.assertEqual("lowHealth",
                             screen.data["root"]["children"][0]["states"][0]["id"])

    def test_composer_lists_builtin_and_drag_commit_moves_subtree(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        with tempfile.TemporaryDirectory() as directory:
            widget = UiComposerWidget(make_workspace(Path(directory)), Translator("pt-BR"))
            # No workspace screens yet: the three builtin screens are listed.
            self.assertEqual(3, widget.screens_list.count())
            self.assertTrue(widget.service.is_builtin_only("screen.hud"))
            widget.service.override_builtin("screen.hud")
            widget.refresh()
            widget.screens_list.setCurrentRow(0)
            self.assertEqual("screen.hud", widget._selected_screen)
            self.assertFalse(widget.service.is_builtin_only("screen.hud"))
            widget.hierarchy.setCurrentItem(widget.hierarchy.topLevelItem(0))
            # A canvas drag commits the subtree delta through the service.
            widget._commit_node_move("hud.health", 5, 4)
            node = widget.service.find("screen.hud").data["root"]
            self.assertEqual(8, node["layout"]["offsetX"])
            self.assertEqual(6, node["layout"]["offsetY"])

    def test_canvas_geometry_is_the_logical_screen(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        with tempfile.TemporaryDirectory() as directory:
            widget = UiComposerWidget(make_workspace(Path(directory)), Translator("en-US"))
            self.assertEqual(LOGICAL_WIDTH * 2, widget.canvas.width())
            self.assertEqual(LOGICAL_HEIGHT * 2, widget.canvas.height())


if __name__ == "__main__":
    unittest.main()
