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

from tools.content_studio.formats.content_json import (
    CONTENT_CATEGORIES,
    CONTENT_VERSION,
)
from tools.content_studio.formats.json_io import (
    encode_json,
)
from tools.content_studio.model.content_workspace import (
    ContentWorkspace,
)
from tools.content_studio.model.world_project import (
    WorldProject,
)


def item_stack_content() -> dict[str, object]:
    data: dict[str, object] = {
        "format": "dungeon-underworld-content",
        "version": CONTENT_VERSION,
    }

    data.update({
        category: []
        for category in CONTENT_CATEGORIES
    })

    data["items"] = [
        {
            "id": "item.potion",
            "visualId": "visual.item.potion",
            "category": "consumable",
            "stackLimit": 66,
        },
        {
            "id": "item.key.castle",
            "visualId": "visual.item.key.castle",
            "category": "key",
            "stackLimit": 1,
        },
        {
            "id": "item.armor",
            "visualId": "visual.item.armor",
            "category": "equipment",
            "stackLimit": 1,
            "equipment": {
                "slot": "armor",
                "modifiers": {
                    "maximumHealthBonus": 0,
                    "playerAttackDamageBonus": 0,
                },
            },
        },
    ]

    data["authoringDescriptors"] = [
        {
            "definitionId": "item.potion",
            "displayName": "Potion",
            "category": "item",
            "tags": [],
        },
        {
            "definitionId": "item.key.castle",
            "displayName": "Castle Key",
            "category": "item",
            "tags": [],
        },
        {
            "definitionId": "item.armor",
            "displayName": "Armor",
            "category": "item",
            "tags": [],
        },
    ]

    return data


def create_workspace(
        root: Path,
) -> ContentWorkspace:
    (
        root / "content.json"
    ).write_text(
        encode_json(
            item_stack_content()
        ),
        encoding="utf-8",
    )

    return ContentWorkspace.open(
        root
    )


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class ItemStackEditorTests(
        unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_item_service_validates_quantity_against_stack_limit(
            self,
    ) -> None:
        from tools.content_studio.services.item_authoring_service import (
            ItemAuthoringService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = create_workspace(
                Path(directory)
            )

            service = ItemAuthoringService(
                workspace
            )

            self.assertEqual(
                66,
                service.stack_limit(
                    "item.potion"
                ),
            )

            self.assertEqual(
                1,
                service.stack_limit(
                    "item.key.castle"
                ),
            )

            self.assertEqual(
                {
                    "itemId": "item.potion",
                    "quantity": 66,
                },
                service.validate_stack(
                    "item.potion",
                    66,
                ),
            )

            with self.assertRaisesRegex(
                ValueError,
                "stackLimit",
            ):
                service.validate_stack(
                    "item.key.castle",
                    2,
                )

    def test_add_editor_uses_selected_item_stack_limit(
            self,
    ) -> None:
        from tools.content_studio.ui.item_stack_editor import (
            ItemStackEditor,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = create_workspace(
                Path(directory)
            )

            editor = ItemStackEditor(
                workspace,
                add_mode=True,
            )

            self.addCleanup(
                editor.close
            )

            potion_index = (
                editor.item_picker.findData(
                    "item.potion"
                )
            )

            key_index = (
                editor.item_picker.findData(
                    "item.key.castle"
                )
            )

            editor.item_picker.setCurrentIndex(
                potion_index
            )

            self.assertEqual(
                66,
                int(
                    editor.quantity.maximum()
                ),
            )

            editor.quantity.setValue(
                66
            )

            emitted: list[object] = []

            editor.add_requested.connect(
                emitted.append
            )

            editor.action_button.click()

            self.assertEqual(
                [
                    {
                        "itemId": "item.potion",
                        "quantity": 66,
                    },
                ],
                emitted,
            )

            editor.item_picker.setCurrentIndex(
                key_index
            )

            self.assertEqual(
                1,
                int(
                    editor.quantity.maximum()
                ),
            )

            self.assertEqual(
                1,
                int(
                    editor.quantity.value()
                ),
            )

    def test_existing_stack_clamps_atomically_when_item_changes(
            self,
    ) -> None:
        from tools.content_studio.ui.item_stack_editor import (
            ItemStackEditor,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = create_workspace(
                Path(directory)
            )

            editor = ItemStackEditor(
                workspace,
                {
                    "itemId": "item.potion",
                    "quantity": 50,
                },
            )

            self.addCleanup(
                editor.close
            )

            emitted: list[object] = []

            editor.stack_changed.connect(
                emitted.append
            )

            editor.item_picker.setCurrentIndex(
                editor.item_picker.findData(
                    "item.key.castle"
                )
            )

            self.assertEqual(
                1,
                int(
                    editor.quantity.value()
                ),
            )

            self.assertEqual(
                [
                    {
                        "itemId": "item.key.castle",
                        "quantity": 1,
                    },
                ],
                emitted,
            )

    def test_legacy_blank_stack_is_repairable_and_removable(
            self,
    ) -> None:
        from tools.content_studio.ui.item_stack_editor import (
            ItemStackEditor,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = create_workspace(
                Path(directory)
            )

            editor = ItemStackEditor(
                workspace,
                {
                    "itemId": "",
                    "quantity": 1,
                },
            )

            self.addCleanup(
                editor.close
            )

            self.assertEqual(
                -1,
                editor.item_picker.currentIndex(),
            )

            self.assertFalse(
                editor.quantity.isEnabled()
            )

            changed: list[object] = []
            removed: list[bool] = []

            editor.stack_changed.connect(
                changed.append
            )

            editor.remove_requested.connect(
                lambda: removed.append(
                    True
                )
            )

            editor.item_picker.setCurrentIndex(
                editor.item_picker.findData(
                    "item.potion"
                )
            )

            self.assertEqual(
                {
                    "itemId": "item.potion",
                    "quantity": 1,
                },
                changed[-1],
            )

            editor.action_button.click()

            self.assertEqual(
                [True],
                removed,
            )

    def test_structured_inspector_emits_complete_stack_edit(
            self,
    ) -> None:
        from tools.content_studio.ui.item_stack_editor import (
            ItemStackEditor,
        )
        from tools.content_studio.ui.widgets import (
            StructuredInspector,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = create_workspace(
                Path(directory)
            )

            inspector = StructuredInspector()

            self.addCleanup(
                inspector.close
            )

            inspector.set_workspace(
                workspace
            )

            chest = {
                "id": 1,
                "definitionId": "object.chest",
                "position": {
                    "x": 16,
                    "y": 16,
                },
                "initialContents": [{
                    "itemId": "item.potion",
                    "quantity": 50,
                }],
                "persistence": "persistent",
            }

            emitted: list[
                tuple[str, object]
            ] = []

            inspector.changed.connect(
                lambda path, value:
                emitted.append(
                    (
                        path,
                        value,
                    )
                )
            )

            inspector.set_object(
                "Chest",
                chest,
            )

            editors = [
                value
                for value in inspector.findChildren(
                    ItemStackEditor
                )
                if not value.add_mode
            ]

            self.assertEqual(
                1,
                len(editors),
            )

            editor = editors[0]

            editor.item_picker.setCurrentIndex(
                editor.item_picker.findData(
                    "item.key.castle"
                )
            )

            self.assertEqual(
                [
                    (
                        "initialContents[0]",
                        {
                            "itemId": "item.key.castle",
                            "quantity": 1,
                        },
                    ),
                ],
                emitted,
            )

    def test_main_window_rejects_over_limit_stack(
            self,
    ) -> None:
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = create_workspace(
                root
            )

            project = WorldProject.new()

            chest_id = (
                project.active_map.add_entity(
                    "objects",
                    "object.chest",
                    32,
                    32,
                )
            )

            window = MainWindow(
                project,
                workspace,
                asset_root=root,
            )

            self.addCleanup(
                window.deleteLater
            )

            window.map_canvas.selected_entity = (
                "objects",
                chest_id,
            )

            window._add_map_collection_value(
                "initialContents",
                {
                    "itemId": "item.key.castle",
                    "quantity": 2,
                },
            )

            chest = project.active_map.entity(
                "objects",
                chest_id,
            )

            assert chest is not None

            self.assertEqual(
                [],
                chest["initialContents"],
            )

            window._add_map_collection_value(
                "initialContents",
                {
                    "itemId": "item.potion",
                    "quantity": 66,
                },
            )

            # item-stack-snapshot-reference-fix-v1
            # MapDocument mutations restore an authored snapshot, so entity
            # references acquired before a mutation must not be reused.
            chest = project.active_map.entity(
                "objects",
                chest_id,
            )

            assert chest is not None

            self.assertEqual(
                [
                    {
                        "itemId": "item.potion",
                        "quantity": 66,
                    },
                ],
                chest["initialContents"],
            )

            window._edit_map_field(
                "initialContents[0]",
                {
                    "itemId": "item.key.castle",
                    "quantity": 2,
                },
            )

            self.assertEqual(
                {
                    "itemId": "item.potion",
                    "quantity": 66,
                },
                chest["initialContents"][0],
            )

            window._edit_map_field(
                "initialContents[0]",
                {
                    "itemId": "item.key.castle",
                    "quantity": 1,
                },
            )

            chest = project.active_map.entity(
                "objects",
                chest_id,
            )

            assert chest is not None

            self.assertEqual(
                {
                    "itemId": "item.key.castle",
                    "quantity": 1,
                },
                chest["initialContents"][0],
            )


if __name__ == "__main__":
    unittest.main()
