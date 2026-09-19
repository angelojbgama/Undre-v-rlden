"""Tests for the humanized generic inspector (audit IN1)."""

from __future__ import annotations

import os
import unittest

try:
    from PySide6.QtWidgets import (
        QApplication, QDoubleSpinBox, QGroupBox, QLabel, QLineEdit, QSpinBox,
    )
except ImportError:  # pragma: no cover - exercised on minimal CI images
    QApplication = None  # type: ignore[assignment,misc]
    QDoubleSpinBox = None  # type: ignore[assignment,misc]
    QGroupBox = None  # type: ignore[assignment,misc]
    QLabel = None  # type: ignore[assignment,misc]
    QLineEdit = None  # type: ignore[assignment,misc]
    QSpinBox = None  # type: ignore[assignment,misc]

from tools.content_studio.services.localization import Translator
from tools.content_studio.ui.widgets import StructuredInspector


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class InspectorFieldTitleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def _labels(self, inspector: StructuredInspector) -> list[str]:
        return [label.text() for label in inspector.findChildren(QLabel)]

    def test_field_titles_are_translated(self) -> None:
        inspector = StructuredInspector(translator=Translator("pt-BR"))
        self.addCleanup(inspector.deleteLater)
        inspector.set_object("Objeto #1", {
            "id": 1,
            "definitionId": "object.chest",
            "visualSetId": "visual.chest",
            "position": {"x": 16, "y": 32},
            "persistence": "persistent",
        })
        labels = self._labels(inspector)
        self.assertIn("Definição", labels)
        self.assertIn("Visual", labels)
        self.assertIn("X", labels)
        self.assertIn("Y", labels)
        self.assertIn("Persistência", labels)
        self.assertNotIn("definitionId", labels)
        # Nested dicts render as group boxes with a humanized title.
        group_titles = [group.title() for group in inspector.findChildren(QGroupBox)]
        self.assertIn("Posição", group_titles)

    def test_unknown_fields_fall_back_to_the_pretty_path(self) -> None:
        inspector = StructuredInspector(translator=Translator("pt-BR"))
        self.addCleanup(inspector.deleteLater)
        inspector.set_object("obj", {"customUnknownField": "value"})
        labels = self._labels(inspector)
        self.assertIn("customUnknownField", labels)

    def test_scalar_editors_stay_compact(self) -> None:
        inspector = StructuredInspector(translator=Translator("pt-BR"))
        self.addCleanup(inspector.deleteLater)
        inspector.set_object("obj", {"id": 7, "name": "gate", "maximumHealth": 3})
        spin_children = (QSpinBox, QDoubleSpinBox)
        for editor in (*inspector.findChildren(QSpinBox), *inspector.findChildren(QLineEdit)):
            # QSpinBox owns an internal QLineEdit: only standalone editors count.
            if isinstance(editor.parent(), spin_children):
                continue
            self.assertLessEqual(editor.maximumWidth(), 240)

    def test_translator_has_distinguishes_missing_keys(self) -> None:
        translator = Translator("pt-BR")
        self.assertTrue(translator.has("field_position"))
        self.assertFalse(translator.has("field_notARealField"))


if __name__ == "__main__":
    unittest.main()
