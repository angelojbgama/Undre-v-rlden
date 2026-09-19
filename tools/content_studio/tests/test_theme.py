"""Tests for the Studio light/dark theme and themed Tabler icons."""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtCore import QSize
    from PySide6.QtGui import QIcon, QPalette
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QSize = None  # type: ignore[assignment,misc]
    QIcon = None  # type: ignore[assignment,misc]
    QPalette = None  # type: ignore[assignment,misc]
    QApplication = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.types import ProjectPreferences
from tools.content_studio.services.preferences import load_preferences, save_preferences
from tools.content_studio.ui.icon_registry import TablerIconEngine, icon
from tools.content_studio.ui import theme


def _write_workspace(root: Path, theme_mode: str | None = None) -> Path:
    content = {"format": "dungeon-underworld-content", "version": 5}
    content.update({category: [] for category in CONTENT_CATEGORIES})
    (root / "content.json").write_text(encode_json(content), encoding="utf-8")
    return root


def _opaque_lightness(pixmap) -> int:
    """Lightness of the first fully opaque pixel (icons are monochrome)."""
    image = pixmap.toImage()
    for y in range(image.height()):
        for x in range(image.width()):
            color = image.pixelColor(x, y)
            if color.alpha() > 200:
                return color.lightness()
    return -1


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class ThemeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def test_apply_theme_builds_light_and_dark_palettes(self) -> None:
        self.assertEqual("dark", theme.apply_theme("dark"))
        window = self.application.palette().color(QPalette.ColorRole.Window)
        text = self.application.palette().color(QPalette.ColorRole.WindowText)
        self.assertLess(window.lightness(), 100)
        self.assertGreater(text.lightness(), 180)

        self.assertEqual("light", theme.apply_theme("light"))
        window = self.application.palette().color(QPalette.ColorRole.Window)
        text = self.application.palette().color(QPalette.ColorRole.WindowText)
        self.assertGreater(window.lightness(), 200)
        self.assertLess(text.lightness(), 100)

    def test_mode_normalization_and_resolution(self) -> None:
        self.assertEqual("system", theme.normalize_mode("bogus"))
        self.assertEqual("dark", theme.normalize_mode("dark"))
        self.assertEqual("light", theme.normalize_scheme("weird"))
        self.assertEqual(theme.system_scheme(), theme.resolved_scheme("system"))
        self.assertIn(theme.system_scheme(), ("light", "dark"))

    def test_canvas_chrome_differs_per_scheme(self) -> None:
        dark = theme.canvas_chrome("dark")
        light = theme.canvas_chrome("light")
        self.assertLess(dark["canvas"].lightness(), light["canvas"].lightness())
        self.assertGreater(dark["canvas_text"].lightness(), light["canvas_text"].lightness())
        self.assertIsNot(dark["contrast"], light["contrast"])

    def test_preferences_round_trip_theme(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            save_preferences(ProjectPreferences(theme="dark"), path)
            self.assertEqual("dark", json.loads(path.read_text(encoding="utf-8"))["theme"])
            self.assertEqual("dark", load_preferences(path).theme)

            path.write_text(json.dumps({"theme": "neon"}), encoding="utf-8")
            self.assertEqual("system", load_preferences(path).theme)

            path.write_text(json.dumps({"language": "en-US"}), encoding="utf-8")
            self.assertEqual("system", load_preferences(path).theme)

    def test_icons_recolor_when_the_theme_changes(self) -> None:
        loaded = icon("save")
        self.assertEqual("dark", theme.apply_theme("dark"))
        dark_lightness = _opaque_lightness(loaded.pixmap(QSize(32, 32)))
        self.assertEqual("light", theme.apply_theme("light"))
        light_lightness = _opaque_lightness(loaded.pixmap(QSize(32, 32)))
        self.assertGreater(dark_lightness, 150, "icon must be light on the dark theme")
        self.assertLess(light_lightness, 100, "icon must be dark on the light theme")

    def test_icon_engine_resolves_mode_colors_from_the_palette(self) -> None:
        theme.apply_theme("dark")
        engine = TablerIconEngine("<svg xmlns='http://www.w3.org/2000/svg'/>")
        normal = engine._color(QIcon.Mode.Normal)
        disabled = engine._color(QIcon.Mode.Disabled)
        selected = engine._color(QIcon.Mode.Selected)
        self.assertLess(disabled.lightness(), normal.lightness())
        self.assertNotEqual(selected, normal)
        clone = engine.clone()
        self.assertIsInstance(clone, TablerIconEngine)
        self.assertIsNot(clone, engine)

    def test_theme_actions_switch_mode_and_persist(self) -> None:
        import tools.content_studio.ui.main_window as main_window_module
        from tools.content_studio.model.content_workspace import ContentWorkspace
        from tools.content_studio.model.world_project import WorldProject
        from tools.content_studio.ui.main_window import MainWindow

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _write_workspace(root)
            saved: list[str] = []
            original_save = main_window_module.save_preferences
            main_window_module.save_preferences = (
                lambda preferences, path=None: saved.append(preferences.theme)
            )
            try:
                window = MainWindow(WorldProject.new(), ContentWorkspace.open(root))
                self.addCleanup(window.close)
                self.assertEqual(set(theme.THEME_MODES), set(window._theme_actions))
                self.assertTrue(window._theme_actions[window._theme_mode].isChecked())

                window._theme_actions["dark"].trigger()
                self.assertEqual("dark", window._theme_mode)
                self.assertEqual(["dark"], saved)
                self.assertTrue(window._theme_actions["dark"].isChecked())
                self.assertFalse(window._theme_actions["system"].isChecked())
                self.assertLess(
                    QApplication.palette().color(QPalette.ColorRole.Window).lightness(), 100
                )
                for key in ("save", "open", "undo", "redo", "playtest"):
                    self.assertFalse(window.actions[key].icon().isNull())
                for mode in theme.THEME_MODES:
                    self.assertFalse(window._theme_actions[mode].icon().isNull())

                window._theme_actions["light"].trigger()
                self.assertEqual(["dark", "light"], saved)
                self.assertGreater(
                    QApplication.palette().color(QPalette.ColorRole.Window).lightness(), 200
                )
            finally:
                main_window_module.save_preferences = original_save

    def test_system_scheme_changes_are_followed_only_in_system_mode(self) -> None:
        import tools.content_studio.ui.main_window as main_window_module
        from tools.content_studio.model.content_workspace import ContentWorkspace
        from tools.content_studio.model.world_project import WorldProject
        from tools.content_studio.ui.main_window import MainWindow

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _write_workspace(root)
            applied: list[str] = []
            original_apply = main_window_module.theme.apply_theme
            main_window_module.theme.apply_theme = (
                lambda mode: applied.append(mode) or original_apply(mode)
            )
            try:
                window = MainWindow(WorldProject.new(), ContentWorkspace.open(root))
                self.addCleanup(window.close)
                applied.clear()

                window._theme_mode = theme.THEME_SYSTEM
                window._system_scheme_changed(None)
                self.assertEqual([theme.THEME_SYSTEM], applied)

                applied.clear()
                window._theme_mode = theme.THEME_DARK
                window._system_scheme_changed(None)
                self.assertEqual([], applied)
            finally:
                main_window_module.theme.apply_theme = original_apply


if __name__ == "__main__":
    unittest.main()
