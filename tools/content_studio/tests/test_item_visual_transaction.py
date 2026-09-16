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

from tools.content_studio.services.localization import Translator
from tools.content_studio.tests.test_item_authoring import make_workspace


def add_animation(workspace) -> None:
    workspace.create_definition_bundle(
        "Create Animation",
        [(
            "animations",
            "animation.item.sheet",
            {
                "id": "animation.item.sheet",
                "imageId": "image.items",
                "loop": True,
                "frames": [
                    {
                        "source": {
                            "x": 0,
                            "y": 16,
                            "width": 16,
                            "height": 16,
                        },
                        "anchor": {
                            "x": 8,
                            "y": 15,
                        },
                        "drawOffset": {
                            "x": 0,
                            "y": 0,
                        },
                        "durationTicks": 4,
                        "markers": [],
                    },
                    {
                        "source": {
                            "x": 16,
                            "y": 16,
                            "width": 16,
                            "height": 16,
                        },
                        "anchor": {
                            "x": 7,
                            "y": 14,
                        },
                        "drawOffset": {
                            "x": 0,
                            "y": 0,
                        },
                        "durationTicks": 4,
                        "markers": [],
                    },
                ],
            },
        )],
    )


def select_animation_frame(
        picker,
        frame_index: int = 1,
) -> None:
    index = next(
        index
        for index in range(
            picker.visuals.count()
        )
        if (
            picker.visuals.itemData(index)[0]
            == "animation"
            and picker.visuals.itemData(index)[2]
            == frame_index
        )
    )

    picker.visuals.setCurrentIndex(
        index
    )


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class ItemVisualTransactionTests(
        unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_picker_selection_is_side_effect_free(
            self,
    ) -> None:
        from tools.content_studio.ui.item_visual_picker import (
            ItemVisualPickerDialog,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            add_animation(
                workspace
            )

            picker = ItemVisualPickerDialog(
                workspace,
                "item.life_potion",
            )

            self.addCleanup(
                picker.close
            )

            select_animation_frame(
                picker
            )

            before = workspace.snapshot()

            selected = picker.commit_selection()

            self.assertEqual(
                "visual.item.life_potion",
                selected,
            )

            self.assertEqual(
                before,
                workspace.snapshot(),
                "choosing an animation frame must not mutate the workspace",
            )

            self.assertIsNone(
                workspace.find(
                    "staticSprites",
                    "visual.item.life_potion",
                )
            )

    def test_item_commit_materializes_visual_atomically(
            self,
    ) -> None:
        from tools.content_studio.ui.item_library_widget import (
            ItemDefinitionDialog,
        )
        from tools.content_studio.ui.item_visual_picker import (
            ItemVisualPickerDialog,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            add_animation(
                workspace
            )

            picker = ItemVisualPickerDialog(
                workspace,
                "item.life_potion",
            )

            self.addCleanup(
                picker.close
            )

            select_animation_frame(
                picker
            )

            picker.commit_selection()

            selection_method = getattr(
                picker,
                "selected_selection",
                None,
            )

            self.assertTrue(
                callable(selection_method),
                "picker must expose a pending selection descriptor",
            )

            selection = selection_method()

            dialog = ItemDefinitionDialog(
                workspace,
                None,
                Translator("en-US"),
            )

            self.addCleanup(
                dialog.close
            )

            dialog.name.setText(
                "Life Potion"
            )

            dialog.item_id.setText(
                "item.life_potion"
            )

            setter = getattr(
                dialog,
                "set_visual_selection",
                None,
            )

            self.assertTrue(
                callable(setter),
                "Item dialog must accept a pending visual selection",
            )

            setter(
                selection
            )

            self.assertIsNone(
                workspace.find(
                    "staticSprites",
                    "visual.item.life_potion",
                )
            )

            result = dialog.commit()

            self.assertEqual(
                "item.life_potion",
                result.definition_id,
            )

            self.assertEqual(
                "visual.item.life_potion",
                result.data["visualId"],
            )

            self.assertIsNotNone(
                workspace.find(
                    "staticSprites",
                    "visual.item.life_potion",
                )
            )

            self.assertIsNotNone(
                workspace.find(
                    "pickups",
                    "pickup.life_potion",
                )
            )

            self.assertTrue(
                workspace.undo()
            )

            self.assertIsNone(
                workspace.find(
                    "items",
                    "item.life_potion",
                )
            )

            self.assertIsNone(
                workspace.find(
                    "pickups",
                    "pickup.life_potion",
                )
            )

            self.assertIsNone(
                workspace.find(
                    "staticSprites",
                    "visual.item.life_potion",
                )
            )

            self.assertIsNotNone(
                workspace.find(
                    "animations",
                    "animation.item.sheet",
                )
            )

    def test_failed_item_commit_leaves_no_orphan_visual(
            self,
    ) -> None:
        from tools.content_studio.services.item_authoring_service import (
            ItemAuthoringService,
        )
        from tools.content_studio.ui.item_library_widget import (
            ItemDefinitionDialog,
        )
        from tools.content_studio.ui.item_visual_picker import (
            ItemVisualPickerDialog,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            add_animation(
                workspace
            )

            ItemAuthoringService(
                workspace
            ).create_item(
                "Existing",
                "item.life_potion",
                "visual.item.a",
                "consumable",
            )

            picker = ItemVisualPickerDialog(
                workspace,
                "item.life_potion",
            )

            self.addCleanup(
                picker.close
            )

            select_animation_frame(
                picker
            )

            picker.commit_selection()

            selection = (
                picker.selected_selection()
                if hasattr(
                    picker,
                    "selected_selection",
                )
                else None
            )

            self.assertIsNotNone(
                selection
            )

            dialog = ItemDefinitionDialog(
                workspace,
                None,
                Translator("en-US"),
            )

            self.addCleanup(
                dialog.close
            )

            dialog.name.setText(
                "Duplicate"
            )

            dialog.item_id.setText(
                "item.life_potion"
            )

            self.assertTrue(
                hasattr(
                    dialog,
                    "set_visual_selection",
                )
            )

            dialog.set_visual_selection(
                selection
            )

            with self.assertRaisesRegex(
                ValueError,
                "already exists",
            ):
                dialog.commit()

            self.assertIsNone(
                workspace.find(
                    "staticSprites",
                    "visual.item.life_potion",
                )
            )

    def test_edit_visual_and_item_update_share_one_undo(
            self,
    ) -> None:
        from tools.content_studio.services.item_authoring_service import (
            ItemAuthoringService,
        )
        from tools.content_studio.ui.item_library_widget import (
            ItemDefinitionDialog,
        )
        from tools.content_studio.ui.item_visual_picker import (
            ItemVisualPickerDialog,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            add_animation(
                workspace
            )

            service = ItemAuthoringService(
                workspace
            )

            service.create_item(
                "Life Potion",
                "item.life_potion",
                "visual.item.a",
                "consumable",
            )

            original = workspace.find(
                "items",
                "item.life_potion",
            )

            assert original is not None

            picker = ItemVisualPickerDialog(
                workspace,
                "item.life_potion",
            )

            self.addCleanup(
                picker.close
            )

            select_animation_frame(
                picker
            )

            picker.commit_selection()

            if not hasattr(
                picker,
                "selected_selection",
            ):
                self.fail(
                    "picker must expose pending visual selection"
                )

            dialog = ItemDefinitionDialog(
                workspace,
                None,
                Translator("en-US"),
                original,
            )

            self.addCleanup(
                dialog.close
            )

            self.assertTrue(
                hasattr(
                    dialog,
                    "set_visual_selection",
                )
            )

            dialog.set_visual_selection(
                picker.selected_selection()
            )

            dialog.commit()

            updated = workspace.find(
                "items",
                "item.life_potion",
            )

            assert updated is not None

            self.assertEqual(
                "visual.item.life_potion",
                updated.data["visualId"],
            )

            self.assertIsNotNone(
                workspace.find(
                    "staticSprites",
                    "visual.item.life_potion",
                )
            )

            self.assertTrue(
                workspace.undo()
            )

            restored = workspace.find(
                "items",
                "item.life_potion",
            )

            assert restored is not None

            self.assertEqual(
                "visual.item.a",
                restored.data["visualId"],
            )

            self.assertIsNone(
                workspace.find(
                    "staticSprites",
                    "visual.item.life_potion",
                )
            )


if __name__ == "__main__":
    unittest.main()
