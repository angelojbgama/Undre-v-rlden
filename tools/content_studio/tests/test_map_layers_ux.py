"""Tests for map browser entry badge / double-click and layer drag (M1/M2/L1)."""

from __future__ import annotations

import os
import unittest

try:
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover - exercised on minimal CI images
    Qt = None  # type: ignore[assignment,misc]
    QApplication = None  # type: ignore[assignment,misc]

from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.services.localization import Translator
from tools.content_studio.ui.widgets import LayersPanel, MapBrowser


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class MapBrowserUxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def _browser(self):
        browser = MapBrowser()
        self.addCleanup(browser.deleteLater)
        return browser

    def _item(self, browser: MapBrowser, map_id: str):
        for row in range(browser.list.topLevelItemCount()):
            item = browser.list.topLevelItem(row)
            if item.data(0, Qt.ItemDataRole.UserRole) == map_id:
                return item
        for row in range(browser.list.topLevelItemCount()):
            parent = browser.list.topLevelItem(row)
            for child in range(parent.childCount()):
                if parent.child(child).data(0, Qt.ItemDataRole.UserRole) == map_id:
                    return parent.child(child)
        raise AssertionError(f"map {map_id!r} not in browser")

    def test_entry_map_gets_a_flag_badge(self) -> None:
        browser = self._browser()
        browser.refresh(["map.a", "map.b"], "map.a", entry="map.b")
        plain = self._item(browser, "map.a")
        entry = self._item(browser, "map.b")
        self.assertTrue(plain.icon(0).isNull())
        self.assertFalse(entry.icon(0).isNull())
        self.assertTrue(entry.toolTip(0))

    def test_double_click_opens_map_properties(self) -> None:
        browser = self._browser()
        browser.refresh(["map.a", "map.b"], "map.a", entry="")
        received: list[str] = []
        browser.edit_requested.connect(received.append)
        browser._item_double_clicked(self._item(browser, "map.b"), 0)
        self.assertEqual(["map.b"], received)


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class LayersPanelUxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def _panel(self):
        document = MapDocument.new("map.layers", 8, 8)
        document.add_layer("Wall")
        document.add_layer("Detail")
        panel = LayersPanel(translator=Translator("pt-BR"))
        self.addCleanup(panel.deleteLater)
        panel.set_document(document)
        return panel, document

    def test_items_are_checkable_and_match_visibility(self) -> None:
        panel, document = self._panel()
        for row, layer in enumerate(document.layers):
            item = panel.list.item(row)
            self.assertEqual(
                Qt.CheckState.Checked if layer.get("visible", True) else Qt.CheckState.Unchecked,
                item.checkState())
            self.assertNotIn("●", item.text())

    def test_unchecking_hides_the_layer(self) -> None:
        panel, document = self._panel()
        changes: list[bool] = []
        panel.changed.connect(lambda: changes.append(True))
        item = panel.list.item(0)
        item.setCheckState(Qt.CheckState.Unchecked)
        self.assertFalse(document.layers[0]["visible"])
        self.assertEqual([True], changes)

    def test_drag_reorder_moves_document_layers(self) -> None:
        panel, document = self._panel()
        # Simulate the widget state after an InternalMove drag: "Detail"
        # (last row) dragged to the top.
        item = panel.list.takeItem(panel.list.count() - 1)
        panel.list.insertItem(0, item)
        panel._rows_moved(0, 0, 0, 0, 0)
        names = [layer["name"] for layer in document.layers]
        self.assertEqual(["Detail", "Ground", "Wall"], names)
        self.assertEqual(["Detail", "Ground", "Wall"],
                         [panel.list.item(row).text() for row in range(panel.list.count())])

    def test_noop_drag_does_not_dirty_the_document(self) -> None:
        panel, document = self._panel()
        document.dirty = False
        panel._rows_moved(0, 0, 0, 0, 0)
        self.assertFalse(document.dirty)


if __name__ == "__main__":
    unittest.main()
