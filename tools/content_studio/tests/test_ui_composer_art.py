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

    def test_repeater_paints_one_instance_per_previewed_slot(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = UiCanvas()
        self.addCleanup(canvas.deleteLater)
        canvas.set_preview_values({"player.inventory.slots": 30})
        canvas.set_screen_data({
            "id": "screen.x", "kind": "hud",
            "root": {"id": "root", "component": "group",
                     "layout": {"offsetX": 0, "offsetY": 0},
                     "children": [
                         {"id": "grid", "component": "repeater",
                          "layout": {"offsetX": 8, "offsetY": 40},
                          "columns": 10, "cellWidth": 26, "cellHeight": 15,
                          "bindings": [{"property": "source",
                                        "source": "player.inventory.slots"}],
                          "children": [
                              {"id": "cell", "component": "slot",
                               "layout": {"width": 24, "height": 13},
                               "background": {"r": 200, "g": 120, "b": 40, "a": 255}}]}]},
        })
        image = self._grab(canvas)
        # Instance 0 fills cell (8,40)..(32,53); sample logical (20,45).
        self.assertEqual(QColor(200, 120, 40).name(), image.pixelColor(40, 90).name())
        # Instance 9 sits in column 9: cell (242,40)..(266,53); logical (250,45).
        self.assertEqual(QColor(200, 120, 40).name(), image.pixelColor(500, 90).name())
        # Instance 10 wraps to row 2, column 0: cell origin (8, 55).
        self.assertEqual(QColor(200, 120, 40).name(), image.pixelColor(40, 120).name())

    def test_repeater_without_previewed_instances_keeps_extent_only(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = UiCanvas()
        self.addCleanup(canvas.deleteLater)
        canvas.set_preview_values({"player.inventory.slots": 0})
        canvas.set_screen_data({
            "id": "screen.x", "kind": "hud",
            "root": {"id": "root", "component": "repeater",
                     "layout": {"offsetX": 8, "offsetY": 40},
                     "columns": 10, "cellWidth": 26, "cellHeight": 15,
                     "bindings": [{"property": "source",
                                   "source": "player.inventory.slots"}],
                     "children": [
                         {"id": "cell", "component": "slot",
                          "layout": {"width": 24, "height": 13},
                          "background": {"r": 200, "g": 120, "b": 40, "a": 255}}]},
        })
        image = self._grab(canvas)
        # No instances: the cell area stays canvas-dark; the extent outline
        # marks where the grid would be.
        self.assertNotEqual(QColor(200, 120, 40).name(), image.pixelColor(40, 90).name())
        self.assertNotEqual("#000000", image.pixelColor(16, 80).name())


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


class UiCanvasFidelityTests(unittest.TestCase):
    """State/visibility evaluation, tint, bitmap font and selection."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.application = QApplication.instance() or QApplication([])

    def _canvas(self) -> UiCanvas:
        canvas = UiCanvas()
        self.addCleanup(canvas.deleteLater)
        return canvas

    def test_hidden_nodes_and_state_gates_evaluate(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = self._canvas()
        canvas.set_preview_values({"player.ammo.present": 0})
        canvas.set_screen_data({
            "id": "screen.x", "kind": "hud",
            "root": {"id": "root", "component": "group",
                     "layout": {"offsetX": 0, "offsetY": 0},
                     "children": [
                         # Authored-hidden from the start (layout gate).
                         {"id": "ghost", "component": "panel",
                          "layout": {"offsetX": 10, "offsetY": 10, "width": 40,
                                     "height": 20, "visible": False},
                          "background": {"r": 200, "g": 120, "b": 40, "a": 255}},
                         # State-gated: hidden until the preview flips to 1.
                         {"id": "ammo", "component": "panel",
                          "layout": {"offsetX": 60, "offsetY": 10, "width": 40,
                                     "height": 20, "visible": False},
                          "background": {"r": 90, "g": 200, "b": 90, "a": 255},
                          "states": [{
                              "id": "present", "condition": {
                                  "source": "player.ammo.present",
                                  "operator": "equal", "value": 1},
                              "visual": {"visible": True}}]}]},
        })
        image = canvas.grab().toImage()
        # Both gated panels stay hidden with the gate at 0.
        self.assertNotEqual(QColor(200, 120, 40).name(), image.pixelColor(40, 40).name())
        self.assertNotEqual(QColor(90, 200, 90).name(), image.pixelColor(160, 40).name())

        canvas.set_preview_values({"player.ammo.present": 1})
        image = canvas.grab().toImage()
        # The state gate reveals the ammo panel; the layout gate stays off.
        self.assertEqual(QColor(90, 200, 90).name(), image.pixelColor(160, 40).name())
        self.assertNotEqual(QColor(200, 120, 40).name(), image.pixelColor(40, 40).name())

    def test_state_tint_multiplies_art_like_the_runtime(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = self._canvas()
        canvas.set_visuals(FakeResolver(QColor(255, 0, 0)))
        canvas.set_preview_values({"player.hurt": 1})
        canvas.set_screen_data({
            "id": "screen.x", "kind": "hud",
            "root": {"id": "root", "component": "group",
                     "layout": {"offsetX": 0, "offsetY": 0},
                     "children": [
                         {"id": "hero", "component": "image", "sprite": "spr.hero",
                          "layout": {"offsetX": 10, "offsetY": 10},
                          "states": [{
                              "id": "hurt", "condition": {
                                  "source": "player.hurt",
                                  "operator": "equal", "value": 1},
                              "visual": {"tint": {"r": 0, "g": 255, "b": 0,
                                                  "a": 255}}}]}]},
        })
        image = canvas.grab().toImage()
        # Red art under a green tint multiplies to black, like
        # drawImageRegionTinted.
        self.assertEqual("#000000", image.pixelColor(28, 28).name())

    def test_bitmap_font_draws_atlas_glyphs(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = self._canvas()
        # 26x3 atlas of 7x9 cells: black sheet, white 'A' cell (0,0) and
        # white '3' cell (digits row y=18, x=3*7).
        font = QImage(182, 27, QImage.Format.Format_ARGB32)
        font.fill(QColor(0, 0, 0, 255))
        from PySide6.QtGui import QPainter as GuiPainter
        painter = GuiPainter(font)
        painter.fillRect(0, 0, 7, 9, QColor(255, 255, 255))
        painter.fillRect(3 * 7, 18, 7, 9, QColor(255, 255, 255))
        painter.end()
        canvas._font_image = font
        canvas.set_screen_data({
            "id": "screen.x", "kind": "hud",
            "root": {"id": "root", "component": "group",
                     "layout": {"offsetX": 0, "offsetY": 0},
                     "children": [
                         {"id": "label", "component": "text", "text": "A 3",
                          "layout": {"offsetX": 8, "offsetY": 8}}]},
        })
        image = canvas.grab().toImage()
        # Glyph A at logical (8..14, 8..16), space advances 7, glyph 3 at
        # logical (22..28); zoom 2 -> widget pixels. Glyph centers are white
        # from the atlas; the space column between them stays background.
        self.assertEqual("#ffffff", image.pixelColor(2 * 11, 2 * 12).name())
        self.assertEqual("#ffffff", image.pixelColor(2 * 25, 2 * 12).name())
        self.assertNotEqual("#ffffff", image.pixelColor(2 * 18, 2 * 12).name())

    def test_selected_node_draws_its_outline(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = self._canvas()
        canvas.set_screen_data({
            "id": "screen.x", "kind": "hud",
            "root": {"id": "root", "component": "group",
                     "layout": {"offsetX": 0, "offsetY": 0},
                     "children": [
                         {"id": "hero", "component": "image", "sprite": "spr.hero",
                          "layout": {"offsetX": 10, "offsetY": 10,
                                     "width": 8, "height": 8}}]},
        })
        plain = canvas.grab().toImage()
        canvas.set_selected_node("hero")
        selected = canvas.grab().toImage()
        # The selection outline paints orange around the node box (the box
        # starts at logical (10,10) -> device (20,20), outline one device
        # pixel outside it).
        outline = selected.pixelColor(19, 19)
        self.assertGreater(outline.red(), 200)
        self.assertGreater(outline.green(), 120)
        self.assertLess(outline.blue(), 60)
        self.assertEqual(255, outline.alpha())
        self.assertNotEqual(plain.pixelColor(19, 19).name(), outline.name())


class UiFrameNineSliceTests(unittest.TestCase):
    """Authored 9-slice frames in the canvas and the authoring service."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.application = QApplication.instance() or QApplication([])

    @staticmethod
    def _frame_image() -> QImage:
        from PySide6.QtGui import QPainter as Paint
        image = QImage(12, 12, QImage.Format.Format_ARGB32)
        image.fill(QColor(200, 40, 40))
        painter = Paint(image)
        painter.fillRect(4, 4, 4, 4, QColor(40, 80, 200))
        painter.fillRect(4, 0, 4, 4, QColor(40, 200, 80))
        painter.fillRect(4, 8, 4, 4, QColor(40, 200, 80))
        painter.fillRect(0, 4, 4, 4, QColor(40, 200, 80))
        painter.fillRect(8, 4, 4, 4, QColor(40, 200, 80))
        painter.end()
        return image

    def test_canvas_paints_nine_slice_frame(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        canvas = UiCanvas()
        self.addCleanup(canvas.deleteLater)

        class FrameResolver:
            def __init__(self) -> None:
                self.image = self._make()
                self.asset_root = None

            @staticmethod
            def _make() -> QImage:
                return UiFrameNineSliceTests._frame_image()

            def resolve_static_sprite(self, sprite_id: str) -> ResolvedStudioVisual:
                return ResolvedStudioVisual(
                    image=self.image, anchor_x=0, anchor_y=0,
                    draw_offset_x=0, draw_offset_y=0,
                    animation_id=sprite_id, frame_index=0)

        canvas.set_visuals(FrameResolver())
        canvas.set_screen_data({
            "id": "screen.x", "kind": "screen",
            "root": {"id": "panel", "component": "panel",
                     "layout": {"offsetX": 10, "offsetY": 10,
                                "width": 20, "height": 16},
                     "backgroundImage": {"sprite": "spr.frame", "border": 4}},
        })
        image = canvas.grab().toImage()
        # Corners 1:1 at both extremes; edges/center stretched (zoom 2).
        self.assertEqual(QColor(200, 40, 40).name(), image.pixelColor(22, 22).name())
        self.assertEqual(QColor(200, 40, 40).name(), image.pixelColor(56, 48).name())
        self.assertEqual(QColor(40, 200, 80).name(), image.pixelColor(36, 22).name())
        self.assertEqual(QColor(40, 80, 200).name(), image.pixelColor(36, 36).name())

    def test_service_authors_and_clears_frames(self) -> None:
        app = QApplication.instance() or QApplication([])
        assert app is not None
        with tempfile.TemporaryDirectory() as directory:
            from tools.content_studio.services.ui_authoring_service import (
                UiAuthoringService,
            )
            root = Path(directory)
            workspace = _make_workspace(root)
            service = UiAuthoringService(workspace)
            service.create_screen("screen.frames")
            service.add_node("screen.frames", "panel", "panel", offset=(10, 10))
            service.set_layout("screen.frames", "panel", width=20, height=16)
            service.set_background_image("screen.frames", "panel", "spr.art", 4)
            screen = workspace.find("uiScreens", "screen.frames")
            assert screen is not None
            self.assertEqual({"sprite": "spr.art", "border": 4},
                             screen.data["root"]["children"][0]["backgroundImage"])
            with self.assertRaises(ValueError):
                service.set_background_image("screen.frames", "panel", "spr.absent", 4)
            service.set_background_image("screen.frames", "panel", None)
            self.assertNotIn("backgroundImage",
                             screen.data["root"]["children"][0])


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
