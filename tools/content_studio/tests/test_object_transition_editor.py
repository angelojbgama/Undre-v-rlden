from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

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
from tools.content_studio.tests.test_door_authoring_service import (
    workspace_from,
)
from tools.content_studio.tests.test_door_instance_service import (
    door_document,
    door_instance_content,
)


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

    def _transition_project(self) -> WorldProject:
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

        return WorldProject(
            [
                self.source,
                self.target,
            ],
            self.source.map_id,
        )

    def setUp(self) -> None:
        self.project = self._transition_project()

    def _make_window(self, project: WorldProject, workspace_root: Path):
        from tools.content_studio.ui.main_window import MainWindow

        for document in project.maps:
            document.dirty = False

        project.dirty = False

        workspace = workspace_from(
            door_instance_content(),
            workspace_root / "definitions",
        )

        window = MainWindow(
            project,
            workspace,
            asset_root=workspace_root,
        )

        def close_window() -> None:
            for document in project.maps:
                document.dirty = False

            project.dirty = False
            window.close()

        self.addCleanup(close_window)

        return window

    def test_main_window_shows_transition_for_non_door_object(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            window = self._make_window(
                self.project,
                Path(directory),
            )

            window.map_canvas.selection_controller.select(
                "objects",
                self.object_id,
            )

            self.assertFalse(
                window.object_transition_editor.isHidden()
            )

            self.assertTrue(
                window.door_instance_editor.isHidden()
            )

            inspector_root = (
                window.map_inspector._root
            )

            self.assertIsInstance(
                inspector_root,
                dict,
            )

            assert isinstance(
                inspector_root,
                dict,
            )

            self.assertNotIn(
                "transition",
                inspector_root,
            )

    def test_main_window_applies_and_removes_transition(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            window = self._make_window(
                self.project,
                Path(directory),
            )

            window.map_canvas.selection_controller.select(
                "objects",
                self.object_id,
            )

            editor = (
                window.object_transition_editor
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

            placement = self.source.entity(
                "objects",
                self.object_id,
            )

            self.assertIsNotNone(
                placement
            )

            assert placement is not None

            self.assertEqual(
                {
                    "targetMapId": "map.target",
                    "targetSpawnId": "entry.portal",
                },
                placement["transition"],
            )

            editor.enabled.setChecked(
                False
            )

            editor.apply_button.click()

            placement = self.source.entity(
                "objects",
                self.object_id,
            )

            assert placement is not None

            self.assertNotIn(
                "transition",
                placement,
            )

    def test_main_window_door_shows_both_editors(self) -> None:
        document, door_id = (
            door_document()
        )

        project = WorldProject(
            [document],
            document.map_id,
        )

        with tempfile.TemporaryDirectory() as directory:
            window = self._make_window(
                project,
                Path(directory),
            )

            window.map_canvas.selection_controller.select(
                "objects",
                door_id,
            )

            self.assertFalse(
                window.door_instance_editor.isHidden()
            )

            self.assertFalse(
                window.object_transition_editor.isHidden()
            )

            inspector_root = (
                window.map_inspector._root
            )

            assert isinstance(
                inspector_root,
                dict,
            )

            self.assertNotIn(
                "transition",
                inspector_root,
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
