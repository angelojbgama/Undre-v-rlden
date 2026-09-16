from __future__ import annotations

import os
import unittest

os.environ.setdefault(
    "QT_QPA_PLATFORM",
    "offscreen",
)

try:
    from PySide6.QtWidgets import QApplication
except ImportError:
    QApplication = None  # type: ignore[assignment]

from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.model.world_project import WorldProject
from tools.content_studio.services.localization import Translator


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class ObjectTransitionEditorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None
        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def setUp(self) -> None:
        self.source = MapDocument.new(
            "map.source",
            4,
            4,
        )

        self.object_id = self.source.add_entity(
            "objects",
            "object.portal.test",
            16,
            16,
        )

        self.target = MapDocument.new(
            "map.target",
            4,
            4,
        )

        self.target.add_player_spawn(
            "entry.portal",
            32,
            32,
        )

        self.project = WorldProject(
            [
                self.source,
                self.target,
            ],
            self.source.map_id,
        )

    def test_editor_is_generic_and_loads_map_spawn_choices(self) -> None:
        from tools.content_studio.ui.object_transition_editor import (
            ObjectTransitionEditor,
        )

        editor = ObjectTransitionEditor(
            Translator("en-US")
        )

        self.addCleanup(
            editor.close
        )

        self.assertTrue(
            editor.set_context(
                self.project,
                self.source,
                self.object_id,
            )
        )

        self.assertFalse(
            editor.isHidden()
        )

        self.assertFalse(
            editor.enabled.isChecked()
        )

        editor.enabled.setChecked(
            True
        )

        editor.target_map.setCurrentIndex(
            editor.target_map.findData(
                "map.target"
            )
        )

        self.assertEqual(
            "entry.portal",
            editor.target_spawn.currentData(),
        )

        self.assertTrue(
            editor.target_map.isEnabled()
        )

        self.assertTrue(
            editor.target_spawn.isEnabled()
        )

    def test_apply_emits_complete_transition_request(self) -> None:
        from tools.content_studio.ui.object_transition_editor import (
            ObjectTransitionEditor,
        )

        editor = ObjectTransitionEditor(
            Translator("en-US")
        )

        self.addCleanup(
            editor.close
        )

        editor.set_context(
            self.project,
            self.source,
            self.object_id,
        )

        requests: list[object] = []

        editor.configuration_requested.connect(
            requests.append
        )

        editor.enabled.setChecked(
            True
        )

        editor.target_map.setCurrentIndex(
            editor.target_map.findData(
                "map.target"
            )
        )

        editor.apply_button.click()

        self.assertEqual(
            [
                {
                    "enabled": True,
                    "target_map_id": "map.target",
                    "target_spawn_id": "entry.portal",
                },
            ],
            requests,
        )


if __name__ == "__main__":
    unittest.main()
