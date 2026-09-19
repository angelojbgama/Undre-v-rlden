"""Tests for the Tabler Icons registry used by the Content Studio UI."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtGui import QIcon
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QIcon = None  # type: ignore[assignment,misc]
    QApplication = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.world_project import WorldProject
from tools.content_studio.ui.icon_registry import (
    ICONS_DIR,
    TABLER_ICONS,
    IconRegistry,
    icon,
)

ACTIONS_WITH_ICONS = (
    "new", "open", "save", "save_as", "save_all", "validate", "export",
    "playtest", "import_tileset", "quit", "undo", "redo", "grid", "snap",
    "frame", "select", "erase_tiles",
)


def _write_empty_workspace(root: Path) -> None:
    content = {"format": "dungeon-underworld-content", "version": 5}
    content.update({category: [] for category in CONTENT_CATEGORIES})
    (root / "content.json").write_text(encode_json(content), encoding="utf-8")


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class IconRegistryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def test_every_registered_icon_maps_to_an_existing_svg(self) -> None:
        registry = IconRegistry()
        self.assertTrue(len(TABLER_ICONS) > 0)
        for name, file_name in TABLER_ICONS.items():
            resolved = registry.resolve(name)
            self.assertIsNotNone(resolved, f"no file mapping for icon '{name}'")
            self.assertTrue(resolved.is_file(), f"missing SVG for icon '{name}': {resolved}")

    def test_versioned_assets_exactly_match_the_registry(self) -> None:
        versioned = {path.name for path in ICONS_DIR.glob("*.svg")}
        referenced = set(TABLER_ICONS.values())
        self.assertEqual(set(), versioned - referenced,
                         "unused SVGs are versioned; only used icons may be versioned")
        self.assertEqual(set(), referenced - versioned,
                         "registry references SVGs that are not versioned")

    def test_registered_icons_render_valid_non_null_qicons(self) -> None:
        registry = IconRegistry()
        for name in TABLER_ICONS:
            loaded = registry.icon(name)
            self.assertFalse(loaded.isNull(), f"icon '{name}' resolved to a null QIcon")
            self.assertFalse(loaded.pixmap(24, 24).isNull(),
                             f"icon '{name}' did not rasterize through the SVG engine")

    def test_icons_are_cached_per_semantic_name(self) -> None:
        self.assertIs(icon("save"), icon("save"))
        registry = IconRegistry()
        self.assertIs(registry.icon("open"), registry.icon("open"))

    def test_unknown_names_and_missing_files_degrade_to_null_icons(self) -> None:
        registry = IconRegistry()
        self.assertTrue(registry.icon("not-a-registered-icon").isNull())
        missing_root = IconRegistry(root=Path(tempfile.gettempdir()) / "missing-icons-dir")
        for name in TABLER_ICONS:
            self.assertTrue(missing_root.icon(name).isNull(),
                            f"icon '{name}' should be null when its file is absent")

    def test_main_window_actions_carry_non_null_icons(self) -> None:
        from tools.content_studio.model.content_workspace import ContentWorkspace
        from tools.content_studio.ui.main_window import MainWindow

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _write_empty_workspace(root)
            window = MainWindow(WorldProject.new(), ContentWorkspace.open(root))
            self.addCleanup(window.close)
            for key in ACTIONS_WITH_ICONS:
                action = window.actions[key]
                self.assertFalse(action.icon().isNull(),
                                 f"action '{key}' has no icon")
            toolbar = window._toolbar
            self.assertEqual(24, toolbar.iconSize().width())
            self.assertEqual(24, toolbar.iconSize().height())

    def test_playtest_icon_follows_the_running_state(self) -> None:
        from tools.content_studio.model.content_workspace import ContentWorkspace
        from tools.content_studio.ui import main_window as main_window_module
        from tools.content_studio.ui.main_window import MainWindow

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _write_empty_workspace(root)
            window = MainWindow(WorldProject.new(), ContentWorkspace.open(root))
            self.addCleanup(window.close)

            requested: list[str] = []
            original_icon = main_window_module.icon

            def recording_icon(name: str) -> QIcon:
                requested.append(name)
                return original_icon(name)

            main_window_module.icon = recording_icon
            try:
                running_process = type("FakeProcess", (), {"poll": staticmethod(lambda: None)})()
                window.playtest.process = running_process
                window._update_playtest_icon()
                self.assertEqual("stop", requested[-1])

                window.playtest.process = None
                window._update_playtest_icon()
                self.assertEqual("playtest", requested[-1])
            finally:
                main_window_module.icon = original_icon


if __name__ == "__main__":
    unittest.main()
