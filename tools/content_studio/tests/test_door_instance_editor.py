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
except ImportError:  # pragma: no cover
    QApplication = None  # type: ignore[assignment]

from tools.content_studio.model.world_project import (
    WorldProject,
)
from tools.content_studio.services.localization import (
    Translator,
)
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
class DoorInstanceEditorTests(
        unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_editor_loads_definition_defaults_and_only_key_items(
            self,
    ) -> None:
        try:
            from tools.content_studio.ui.door_instance_editor import (
                DoorInstanceEditor,
            )
        except ModuleNotFoundError as error:
            raise AssertionError(
                "DoorInstanceEditor ainda nao existe"
            ) from error

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            editor = DoorInstanceEditor(
                Translator("en-US")
            )

            self.addCleanup(
                editor.close
            )

            self.assertTrue(
                editor.set_context(
                    document,
                    workspace,
                    object_id,
                )
            )

            self.assertFalse(
                editor.isHidden()
            )

            self.assertEqual(
                "defaults",
                editor.mode.currentData(),
            )

            self.assertEqual(
                "closed",
                editor.initial_state.currentData(),
            )

            self.assertFalse(
                editor.initial_state.isEnabled()
            )

            self.assertFalse(
                editor.required_key.isEnabled()
            )

            self.assertFalse(
                editor.consume_key.isEnabled()
            )

            self.assertTrue(
                editor.persistence.isEnabled()
            )

            values = [
                editor.required_key.itemData(index)
                for index in range(
                    editor.required_key.count()
                )
            ]

            self.assertIn(
                "item.key.castle",
                values,
            )

            self.assertNotIn(
                "item.potion",
                values,
            )

    def test_custom_locked_configuration_emits_complete_request(
            self,
    ) -> None:
        from tools.content_studio.ui.door_instance_editor import (
            DoorInstanceEditor,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            editor = DoorInstanceEditor(
                Translator("en-US")
            )

            self.addCleanup(
                editor.close
            )

            editor.set_context(
                document,
                workspace,
                object_id,
            )

            requests: list[object] = []

            editor.configuration_requested.connect(
                requests.append
            )

            editor.mode.setCurrentIndex(
                editor.mode.findData(
                    "custom"
                )
            )

            editor.initial_state.setCurrentIndex(
                editor.initial_state.findData(
                    "locked"
                )
            )

            editor.required_key.setCurrentIndex(
                editor.required_key.findData(
                    "item.key.castle"
                )
            )

            editor.consume_key.setChecked(
                True
            )

            editor.persistence.setCurrentIndex(
                editor.persistence.findData(
                    "resetOnMapEnter"
                )
            )

            editor.apply_button.click()

            self.assertEqual(
                1,
                len(requests),
            )

            request = requests[0]

            self.assertIsInstance(
                request,
                dict,
            )

            assert isinstance(
                request,
                dict,
            )

            self.assertEqual(
                {
                    "uses_definition_defaults": False,
                    "initial_state": "locked",
                    "required_item_id": "item.key.castle",
                    "consume_item": True,
                    "persistence": "resetOnMapEnter",
                },
                request,
            )

    def test_non_locked_state_clears_key_and_consume(
            self,
    ) -> None:
        from tools.content_studio.ui.door_instance_editor import (
            DoorInstanceEditor,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            editor = DoorInstanceEditor(
                Translator("en-US")
            )

            self.addCleanup(
                editor.close
            )

            editor.set_context(
                document,
                workspace,
                object_id,
            )

            editor.mode.setCurrentIndex(
                editor.mode.findData(
                    "custom"
                )
            )

            editor.initial_state.setCurrentIndex(
                editor.initial_state.findData(
                    "locked"
                )
            )

            editor.required_key.setCurrentIndex(
                editor.required_key.findData(
                    "item.key.castle"
                )
            )

            editor.consume_key.setChecked(
                True
            )

            editor.initial_state.setCurrentIndex(
                editor.initial_state.findData(
                    "closed"
                )
            )

            self.assertIsNone(
                editor.required_key.currentData()
            )

            self.assertFalse(
                editor.consume_key.isChecked()
            )

            self.assertFalse(
                editor.required_key.isEnabled()
            )

    def test_main_window_applies_door_editor_configuration(
            self,
    ) -> None:
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                door_instance_content(),
                root,
            )

            document, object_id = (
                door_document()
            )

            project = WorldProject(
                [document],
                document.map_id,
            )

            # Avoid the close confirmation in the offscreen test.
            document.dirty = False
            project.dirty = False

            window = MainWindow(
                project,
                workspace,
                asset_root=root,
            )

            def close_window() -> None:
                document.dirty = False
                project.dirty = False
                window.close()

            self.addCleanup(
                close_window
            )

            window.map_canvas.selection_controller.select(
                "objects",
                object_id,
            )

            self.assertFalse(
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
                "door",
                inspector_root,
            )

            self.assertNotIn(
                "persistence",
                inspector_root,
            )

            editor = (
                window.door_instance_editor
            )

            editor.mode.setCurrentIndex(
                editor.mode.findData(
                    "custom"
                )
            )

            editor.initial_state.setCurrentIndex(
                editor.initial_state.findData(
                    "locked"
                )
            )

            editor.required_key.setCurrentIndex(
                editor.required_key.findData(
                    "item.key.castle"
                )
            )

            editor.consume_key.setChecked(
                False
            )

            editor.persistence.setCurrentIndex(
                editor.persistence.findData(
                    "resetOnMapEnter"
                )
            )

            editor.apply_button.click()

            placement = document.entity(
                "objects",
                object_id,
            )

            self.assertIsNotNone(
                placement
            )

            assert placement is not None

            self.assertEqual(
                {
                    "initialState": "locked",
                    "requiredItemId": "item.key.castle",
                    "consumeItem": False,
                },
                placement["door"],
            )

            self.assertEqual(
                "resetOnMapEnter",
                placement["persistence"],
            )

            self.assertEqual(
                5,
                document.data["version"],
            )

    def test_non_door_selection_hides_door_editor(
            self,
    ) -> None:
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                door_instance_content(),
                root,
            )

            document, unused_door_id = (
                door_document()
            )

            del unused_door_id

            crate_id = document.add_entity(
                "objects",
                "object.crate",
                32,
                32,
            )

            project = WorldProject(
                [document],
                document.map_id,
            )

            document.dirty = False
            project.dirty = False

            window = MainWindow(
                project,
                workspace,
                asset_root=root,
            )

            def close_window() -> None:
                document.dirty = False
                project.dirty = False
                window.close()

            self.addCleanup(
                close_window
            )

            window.map_canvas.selection_controller.select(
                "objects",
                crate_id,
            )

            self.assertTrue(
                window.door_instance_editor.isHidden()
            )


if __name__ == "__main__":
    unittest.main()
