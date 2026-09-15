from __future__ import annotations

import copy
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.model.map_document import (
    MapDocument,
)
from tools.content_studio.tests.test_door_authoring_service import (
    gate_content,
    workspace_from,
)


def door_instance_content() -> dict[str, object]:
    data = gate_content()

    data["items"] = [
        {
            "id": "item.key.castle",
            "visualId": "visual.item.key.castle",
            "category": "key",
            "stackLimit": 1,
        },
        {
            "id": "item.potion",
            "visualId": "visual.item.potion",
            "category": "consumable",
            "stackLimit": 66,
        },
    ]

    data["authoringDescriptors"].extend([
        {
            "definitionId": "item.key.castle",
            "displayName": "Castle Key",
            "category": "item",
            "tags": [],
        },
        {
            "definitionId": "item.potion",
            "displayName": "Potion",
            "category": "item",
            "tags": [],
        },
    ])

    return data


def door_document() -> tuple[MapDocument, int]:
    document = MapDocument.new(
        "map.door-instance",
        8,
        6,
        16,
    )

    # Simulate an existing authored map created before UMAP v5.
    document.data["version"] = 4

    object_id = document.add_entity(
        "objects",
        "object.gate",
        48,
        16,
    )

    return (
        document,
        object_id,
    )


class DoorInstanceServiceTests(
        unittest.TestCase
):
    def test_reads_definition_defaults_without_authoring_override(
            self,
    ) -> None:
        from tools.content_studio.services.door_instance_service import (
            DoorInstanceService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            config = DoorInstanceService(
                document,
                workspace,
            ).configuration(
                object_id
            )

            self.assertTrue(
                config.uses_definition_defaults
            )

            self.assertEqual(
                "closed",
                config.initial_state,
            )

            self.assertIsNone(
                config.required_item_id
            )

            self.assertFalse(
                config.consume_item
            )

            self.assertEqual(
                "persistent",
                config.persistence,
            )

    def test_available_keys_filters_items_by_key_category(
            self,
    ) -> None:
        from tools.content_studio.services.door_instance_service import (
            DoorInstanceService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, unused = (
                door_document()
            )

            del unused

            keys = DoorInstanceService(
                document,
                workspace,
            ).available_keys()

            self.assertEqual(
                [
                    "item.key.castle",
                ],
                [
                    value.definition_id
                    for value in keys
                ],
            )

    def test_configure_locked_keyed_door_upgrades_v4_map_and_undoes_atomically(
            self,
    ) -> None:
        from tools.content_studio.services.door_instance_service import (
            DoorInstanceService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            before = copy.deepcopy(
                document.data
            )

            service = DoorInstanceService(
                document,
                workspace,
            )

            config = service.configure(
                object_id,
                uses_definition_defaults=False,
                initial_state="locked",
                required_item_id="item.key.castle",
                consume_item=False,
                persistence="resetOnMapEnter",
            )

            self.assertFalse(
                config.uses_definition_defaults
            )

            self.assertEqual(
                5,
                document.data["version"],
            )

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

            self.assertTrue(
                document.undo()
            )

            self.assertEqual(
                before,
                document.data,
            )

    def test_locked_without_key_is_valid_for_script_unlock(
            self,
    ) -> None:
        from tools.content_studio.services.door_instance_service import (
            DoorInstanceService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            service = DoorInstanceService(
                document,
                workspace,
            )

            service.configure(
                object_id,
                uses_definition_defaults=False,
                initial_state="locked",
                required_item_id=None,
                consume_item=False,
                persistence="persistent",
            )

            placement = document.entity(
                "objects",
                object_id,
            )

            assert placement is not None

            self.assertEqual(
                {
                    "initialState": "locked",
                    "consumeItem": False,
                },
                placement["door"],
            )

    def test_required_key_is_rejected_for_non_locked_state(
            self,
    ) -> None:
        from tools.content_studio.services.door_instance_service import (
            DoorInstanceService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            before = copy.deepcopy(
                document.data
            )

            with self.assertRaisesRegex(
                ValueError,
                "locked",
            ):
                DoorInstanceService(
                    document,
                    workspace,
                ).configure(
                    object_id,
                    uses_definition_defaults=False,
                    initial_state="closed",
                    required_item_id="item.key.castle",
                    consume_item=False,
                    persistence="persistent",
                )

            self.assertEqual(
                before,
                document.data,
            )

    def test_non_key_item_cannot_be_used_as_required_key(
            self,
    ) -> None:
        from tools.content_studio.services.door_instance_service import (
            DoorInstanceService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            with self.assertRaisesRegex(
                ValueError,
                "category key",
            ):
                DoorInstanceService(
                    document,
                    workspace,
                ).configure(
                    object_id,
                    uses_definition_defaults=False,
                    initial_state="locked",
                    required_item_id="item.potion",
                    consume_item=False,
                    persistence="persistent",
                )

    def test_consume_item_requires_required_key(
            self,
    ) -> None:
        from tools.content_studio.services.door_instance_service import (
            DoorInstanceService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            with self.assertRaisesRegex(
                ValueError,
                "consumeItem",
            ):
                DoorInstanceService(
                    document,
                    workspace,
                ).configure(
                    object_id,
                    uses_definition_defaults=False,
                    initial_state="locked",
                    required_item_id=None,
                    consume_item=True,
                    persistence="persistent",
                )

    def test_definition_defaults_remove_instance_override_but_keep_persistence(
            self,
    ) -> None:
        from tools.content_studio.services.door_instance_service import (
            DoorInstanceService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document, object_id = (
                door_document()
            )

            service = DoorInstanceService(
                document,
                workspace,
            )

            service.configure(
                object_id,
                uses_definition_defaults=False,
                initial_state="locked",
                required_item_id="item.key.castle",
                consume_item=True,
                persistence="persistent",
            )

            service.configure(
                object_id,
                uses_definition_defaults=True,
                initial_state="closed",
                required_item_id=None,
                consume_item=False,
                persistence="resetOnMapEnter",
            )

            placement = document.entity(
                "objects",
                object_id,
            )

            assert placement is not None

            self.assertNotIn(
                "door",
                placement,
            )

            self.assertEqual(
                "resetOnMapEnter",
                placement["persistence"],
            )

    def test_non_door_world_object_is_rejected(
            self,
    ) -> None:
        from tools.content_studio.services.door_instance_service import (
            DoorInstanceService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                door_instance_content(),
                Path(directory),
            )

            document = MapDocument.new(
                "map.not-door",
                5,
                5,
                16,
            )

            object_id = document.add_entity(
                "objects",
                "object.crate",
                32,
                32,
            )

            with self.assertRaisesRegex(
                ValueError,
                "door capability",
            ):
                DoorInstanceService(
                    document,
                    workspace,
                ).configuration(
                    object_id
                )


if __name__ == "__main__":
    unittest.main()
