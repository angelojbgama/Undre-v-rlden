"""Real-art preview and sprite swapping in the UI Composer (offscreen)."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtGui import QColor, QImage  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

from tools.content_studio.services.localization import Translator  # noqa: E402
from tools.content_studio.ui.studio_visual_resolver import (  # noqa: E402
    ResolvedStudioVisual,
)
from tools.content_studio.ui.ui_composer_widget import (  # noqa: E402
    UiCanvas,
    UiComposerWidget,
)


def _solid_image(color: QColor, width: int = 8, height: int = 8) -> QImage:
    image = QImage(width, height, QImage.Format.Format_ARGB32)
    image.fill(color)
    return image


class FakeResolver:
    """Resolver stub handing every sprite the same solid art."""

    def __init__(self, color: QColor) -> None:
        self.image = _solid_image(color)
        self.requests: list[str] = []

    def resolve_static_sprite(self, sprite_id: str) -> ResolvedStudioVisual:
        self.requests.append(sprite_id)
        return ResolvedStudioVisual(
            image=self.image,
            anchor_x=0,
            anchor_y=0,
            draw_offset_x=0,
            draw_offset_y=0,
            animation_id=sprite_id,
            frame_index=0,
        )


def _art_content(version: int = 5) -> dict:
    result: dict = {
        "format": "dungeon-underworld-content",
        "version": version,
    }
    result.update({category: [] for category in CONTENT_CATEGORIES})
    result["visualImages"] = [
        {"id": "image.art", "root": "contentWorkspace", "relativePath": "art.png"},
        {"id": "image.other", "root": "contentWorkspace", "relativePath": "other.png"},
    ]
    result["staticSprites"] = [
        {"id": "spr.art", "imageId": "image.art",
         "source": {"x": 0, "y": 0, "width": 8, "height": 8},
         "anchor": {"x": 0, "y": 0}},
        {"id": "spr.other", "imageId": "image.other",
         "source": {"x": 0, "y": 0, "width": 8, "height": 8},
         "anchor": {"x": 0, "y": 0}},
    ]
    result["uiScreens"] = [{
        "id": "screen.hud",
        "kind": "hud",
        "root": {
            "id": "hud.root", "component": "group",
            "layout": {"offsetX": 0, "offsetY": 0},
            "children": [{
                "id": "hud.icon", "component": "image", "sprite": "spr.art",
                "layout": {"offsetX": 10, "offsetY": 10},
            }],
        },
    }]
    return result


def _make_workspace(root: Path) -> ContentWorkspace:
    root.mkdir(parents=True, exist_ok=True)
    (root / "art.png").write_text("", encoding="utf-8")  # replaced below
    red = _solid_image(QColor(255, 0, 0))
    green = _solid_image(QColor(0, 200, 0))
    red.save(str(root / "art.png"))
    green.save(str(root / "other.png"))
    (root / "content.json").write_text(encode_json(_art_content()), encoding="utf-8")
    return ContentWorkspace.open(root)


class UiCanvasArtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.application = QApplication.instance() or QApplication([])

    def _grab(self, canvas: UiCanvas) -> QImage:
        canvas.resize(canvas.width(), canvas.height())
        return canvas.grab().toImage()

    def test_image_node_paints_resolved_art(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = UiCanvas()
        self.addCleanup(canvas.deleteLater)
        resolver = FakeResolver(QColor(255, 0, 0))
        canvas.set_visuals(resolver)
        canvas.set_screen_data({
            "id": "screen.x", "kind": "hud",
            "root": {"id": "root", "component": "panel",
                     "layout": {"offsetX": 0, "offsetY": 0, "width": 100, "height": 80},
                     "background": {"r": 20, "g": 40, "b": 60, "a": 255},
                     "children": [
                         {"id": "hero", "component": "image", "sprite": "spr.red",
                          "layout": {"offsetX": 10, "offsetY": 10}}]},
        })
        image = self._grab(canvas)
        # Canvas zoom is 2: logical (14, 14) lands on widget pixel (28, 28).
        self.assertEqual("#ff0000", image.pixelColor(28, 28).name())
        self.assertEqual(QColor(20, 40, 60).name(), image.pixelColor(4, 4).name())

    def test_unresolved_sprite_keeps_the_placeholder_box(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = UiCanvas()
        self.addCleanup(canvas.deleteLater)
        canvas.set_screen_data({
            "id": "screen.x", "kind": "hud",
            "root": {"id": "root", "component": "group",
                     "layout": {"offsetX": 0, "offsetY": 0},
                     "children": [
                         {"id": "hero", "component": "image", "sprite": "spr.missing",
                          "layout": {"offsetX": 10, "offsetY": 10, "width": 16,
                                     "height": 16}}]},
        })
        image = self._grab(canvas)
        # The placeholder box outline sits at the node edge even without art.
        outline = image.pixelColor(20, 21)
        self.assertNotEqual("#000000", outline.name())

    def test_slot_node_paints_background_and_count(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = UiCanvas()
        self.addCleanup(canvas.deleteLater)
        canvas.set_preview_values({"context.item.amount": 3})
        canvas.set_screen_data({
            "id": "screen.x", "kind": "hud",
            "root": {"id": "root", "component": "group",
                     "layout": {"offsetX": 0, "offsetY": 0},
                     "children": [
                         {"id": "slot", "component": "slot",
                          "layout": {"offsetX": 10, "offsetY": 10, "width": 16, "height": 16},
                          "background": {"r": 54, "g": 30, "b": 38, "a": 255},
                          "countOffset": {"x": 1, "y": 4},
                          "bindings": [{"property": "count",
                                        "source": "context.item.amount"}]}]},
        })
        image = self._grab(canvas)
        # Sample the corner right of the inner icon placeholder and below the
        # count text: widget pixel (50, 48) = logical (25, 24).
        self.assertEqual(QColor(54, 30, 38).name(), image.pixelColor(50, 48).name())


class UiComposerSwapArtTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.application = QApplication.instance() or QApplication([])

    def test_sprite_picker_lists_swaps_and_previews_art(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = _make_workspace(root)
            widget = UiComposerWidget(workspace, Translator("pt-BR"))
            self.addCleanup(widget.deleteLater)

            widget.refresh()
            widget.screens_list.setCurrentRow(0)
            self.assertEqual("screen.hud", widget._selected_screen)
            widget._select_node("hud.icon")

            # The picker lists the workspace sprites and previews the
            # referenced art.
            items = [widget._sprite_combo.itemText(index)
                     for index in range(widget._sprite_combo.count())]
            self.assertIn("spr.art", items)
            self.assertIn("spr.other", items)
            self.assertEqual("spr.art", widget._sprite_combo.currentText())
            self.assertFalse(widget._sprite_preview.pixmap().isNull())

            # Swapping the art through the combo commits set_sprite and the
            # canvas repaints with the new art.
            widget.canvas.show()
            widget._sprite_combo.setCurrentText("spr.other")
            screen = workspace.find("uiScreens", "screen.hud")
            assert screen is not None
            node = screen.data["root"]["children"][0]
            self.assertEqual("spr.other", node["sprite"])
            self.assertEqual("spr.other", widget._sprite_combo.currentText())
            grabbed = widget.canvas.grab().toImage()
            # Logical (14,14) -> zoomed widget pixel (28,28) is now green.
            self.assertEqual("#00c800", grabbed.pixelColor(28, 28).name())
            self.assertFalse(widget._sprite_preview.pixmap().isNull())


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
