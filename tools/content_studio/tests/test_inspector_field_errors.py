"""Tests for inline field validation in the generic inspector (audit IN2)."""

from __future__ import annotations

import os
import unittest

try:
    from PySide6.QtWidgets import QApplication, QLineEdit
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QApplication = None  # type: ignore[assignment,misc]
    QLineEdit = None  # type: ignore[assignment,misc]

from tools.content_studio.services.localization import Translator
from tools.content_studio.ui.widgets import StructuredInspector


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class InspectorFieldErrorsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def _editor_for(self, inspector: StructuredInspector, path: str) -> QLineEdit:
        for editor_path, editor in inspector._editors:
            if editor_path == path:
                return editor
        raise AssertionError(f"no editor registered for path {path!r}")

    def test_matching_editor_gets_border_and_tooltip(self) -> None:
        inspector = StructuredInspector(translator=Translator("pt-BR"))
        self.addCleanup(inspector.deleteLater)
        inspector.set_object("Enemy", {"visualSetId": "visual.broken", "faction": "enemy"})
        inspector.set_field_errors({"visualSetId": "missing dependency: visual.broken"})

        editor = self._editor_for(inspector, "visualSetId")
        self.assertIn("#e5534b", editor.styleSheet())
        self.assertEqual("missing dependency: visual.broken", editor.toolTip())

        clean = self._editor_for(inspector, "faction")
        self.assertNotIn("#e5534b", clean.styleSheet())

    def test_error_strip_lists_errors_without_editors(self) -> None:
        inspector = StructuredInspector(translator=Translator("pt-BR"))
        self.addCleanup(inspector.deleteLater)
        inspector.set_object("Enemy", {"faction": "enemy"})
        inspector.set_field_errors({"hurtbox": "missing field: hurtbox"})
        self.assertTrue(inspector._error_strip.isVisibleTo(inspector))
        self.assertIn("missing field: hurtbox", inspector._error_strip.text())

    def test_set_object_clears_previous_errors(self) -> None:
        inspector = StructuredInspector(translator=Translator("pt-BR"))
        self.addCleanup(inspector.deleteLater)
        inspector.set_object("A", {"visualSetId": "x"})
        inspector.set_field_errors({"visualSetId": "broken"})
        editor = self._editor_for(inspector, "visualSetId")
        self.assertIn("#e5534b", editor.styleSheet())

        inspector.set_object("B", {"visualSetId": "y"})
        self.assertFalse(inspector._error_strip.isVisibleTo(inspector))
        editor = self._editor_for(inspector, "visualSetId")
        self.assertNotIn("#e5534b", editor.styleSheet())
        self.assertEqual("", editor.toolTip())

    def test_clearing_errors_restores_editors(self) -> None:
        inspector = StructuredInspector(translator=Translator("pt-BR"))
        self.addCleanup(inspector.deleteLater)
        inspector.set_object("Enemy", {"visualSetId": "x"})
        inspector.set_field_errors({"visualSetId": "broken"})
        inspector.set_field_errors({})
        editor = self._editor_for(inspector, "visualSetId")
        self.assertNotIn("#e5534b", editor.styleSheet())
        self.assertFalse(inspector._error_strip.isVisibleTo(inspector))


if __name__ == "__main__":
    unittest.main()
