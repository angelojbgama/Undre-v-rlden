from __future__ import annotations

import copy
import importlib
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace


def content_root() -> dict[str, object]:
    result: dict[str, object] = {
        "format": "dungeon-underworld-content",
        "version": 5,
    }
    result.update({
        category: []
        for category in CONTENT_CATEGORIES
    })
    result["visualImages"] = [{
        "id": "image.items",
        "root": "gameAssets",
        "relativePath": "items.png",
    }]
    result["staticSprites"] = [
        {
            "id": "visual.item.a",
            "imageId": "image.items",
            "source": {
                "x": 0,
                "y": 0,
                "width": 16,
                "height": 16,
            },
            "anchor": {
                "x": 0,
                "y": 0,
            },
        },
        {
            "id": "visual.item.b",
            "imageId": "image.items",
            "source": {
                "x": 16,
                "y": 0,
                "width": 16,
                "height": 16,
            },
            "anchor": {
                "x": 0,
                "y": 0,
            },
        },
    ]
    return result


def make_workspace(root: Path) -> ContentWorkspace:
    root.mkdir(
        parents=True,
        exist_ok=True,
    )
    (root / "content.json").write_text(
        encode_json(content_root()),
        encoding="utf-8",
    )
    return ContentWorkspace.open(root)


def item_service(workspace: ContentWorkspace):
    try:
        module = importlib.import_module(
            "tools.content_studio.services.item_authoring_service"
        )
    except ModuleNotFoundError as error:
        raise AssertionError(
            "ItemAuthoringService ainda nao foi implementado"
        ) from error

    return module.ItemAuthoringService(
        workspace
    )


class ItemAuthoringServiceTests(unittest.TestCase):
    def test_create_item_creates_owned_pickup_atomically(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )
            service = item_service(
                workspace
            )

            created = service.create_item(
                "Pocao de Vida",
                "item.life_potion",
                "visual.item.a",
                "consumable",
            )

            self.assertEqual(
                "item.life_potion",
                created.definition_id,
            )

            self.assertEqual(
                66,
                created.data["stackLimit"],
            )

            self.assertEqual(
                "Pocao de Vida",
                created.display_name,
            )

            pickup = service.pickup_for_item(
                "item.life_potion"
            )

            self.assertIsNotNone(
                pickup
            )

            self.assertEqual(
                "pickup.life_potion",
                pickup.definition_id,
            )

            self.assertEqual(
                "visual.item.a",
                pickup.data["visualId"],
            )

            self.assertEqual(
                {
                    "kind": "item",
                    "itemId": "item.life_potion",
                    "quantity": 1,
                },
                pickup.data["payload"],
            )

            descriptor = workspace.find(
                "authoringDescriptors",
                "item.life_potion",
            )

            self.assertIsNotNone(
                descriptor
            )

            self.assertEqual(
                "Pocao de Vida",
                descriptor.data["displayName"],
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
                    "authoringDescriptors",
                    "item.life_potion",
                )
            )

    def test_stack_defaults_and_equipment_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )
            service = item_service(
                workspace
            )

            misc = service.create_item(
                "Pedra",
                "item.stone",
                "visual.item.a",
                "misc",
            )

            key = service.create_item(
                "Chave Azul",
                "item.key.blue",
                "visual.item.a",
                "key",
            )

            equipment = service.create_item(
                "Armadura",
                "item.armor",
                "visual.item.b",
                "equipment",
                66,
            )

            self.assertEqual(
                66,
                misc.data["stackLimit"],
            )

            self.assertEqual(
                1,
                key.data["stackLimit"],
            )

            self.assertEqual(
                1,
                equipment.data["stackLimit"],
            )

            self.assertEqual(
                "armor",
                equipment.data["equipment"]["slot"],
            )

            self.assertEqual(
                {
                    "maximumHealthBonus": 0,
                    "playerAttackDamageBonus": 0,
                },
                equipment.data["equipment"]["modifiers"],
            )

    def test_invalid_create_is_rejected_before_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )
            service = item_service(
                workspace
            )

            before = workspace.snapshot()

            with self.assertRaisesRegex(
                ValueError,
                "visual",
            ):
                service.create_item(
                    "Sem visual",
                    "item.invalid_visual",
                    "visual.missing",
                    "misc",
                )

            self.assertEqual(
                before,
                workspace.snapshot(),
            )

            with self.assertRaisesRegex(
                ValueError,
                "stack",
            ):
                service.create_item(
                    "Stack zero",
                    "item.zero",
                    "visual.item.a",
                    "misc",
                    0,
                )

            self.assertEqual(
                before,
                workspace.snapshot(),
            )

            service.create_item(
                "Primeiro",
                "item.duplicate",
                "visual.item.a",
                "misc",
            )

            duplicate_before = workspace.snapshot()

            with self.assertRaisesRegex(
                ValueError,
                "already exists",
            ):
                service.create_item(
                    "Segundo",
                    "item.duplicate",
                    "visual.item.a",
                    "misc",
                )

            self.assertEqual(
                duplicate_before,
                workspace.snapshot(),
            )

    def test_update_keeps_owned_pickup_synchronized(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )
            service = item_service(
                workspace
            )

            service.create_item(
                "Item",
                "item.test",
                "visual.item.a",
                "misc",
            )

            updated = service.update_item(
                "item.test",
                display_name="Armadura Teste",
                visual_id="visual.item.b",
                category="equipment",
                stack_limit=66,
            )

            self.assertEqual(
                "equipment",
                updated.data["category"],
            )

            self.assertEqual(
                1,
                updated.data["stackLimit"],
            )

            self.assertEqual(
                "visual.item.b",
                updated.data["visualId"],
            )

            self.assertIsNotNone(
                updated.data["equipment"]
            )

            pickup = service.pickup_for_item(
                "item.test"
            )

            self.assertEqual(
                "visual.item.b",
                pickup.data["visualId"],
            )

            self.assertEqual(
                "Armadura Teste",
                updated.display_name,
            )

    def test_pickup_ownership_is_discovered_by_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )
            service = item_service(
                workspace
            )

            service.create_item(
                "Item",
                "item.test",
                "visual.item.a",
                "misc",
            )

            pickup = service.pickup_for_item(
                "item.test"
            )

            renamed = workspace.rename_definition(
                pickup,
                "pickup.custom_name",
            )

            self.assertEqual(
                "pickup.custom_name",
                renamed.definition_id,
            )

            resolved = service.pickup_for_item(
                "item.test"
            )

            self.assertEqual(
                "pickup.custom_name",
                resolved.definition_id,
            )

    def test_rename_updates_item_pickup_and_external_references(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )
            service = item_service(
                workspace
            )

            service.create_item(
                "Chave",
                "item.key.old",
                "visual.item.a",
                "key",
            )

            workspace.create_definition_bundle(
                "Create Item References",
                [
                    (
                        "rewardGrants",
                        "reward.rename",
                        {
                            "id": "reward.rename",
                            "experience": 0,
                            "gold": 0,
                            "items": [{
                                "itemId": "item.key.old",
                                "quantity": 1,
                            }],
                        },
                    ),
                    (
                        "rewardProfiles",
                        "reward.pickup",
                        {
                            "id": "reward.pickup",
                            "experience": 0,
                            "loot": [{
                                "pickupDefinitionId": "pickup.key.old",
                                "chanceBasisPoints": 10000,
                                "minimumCount": 1,
                                "maximumCount": 1,
                            }],
                        },
                    ),
                ],
            )

            renamed = service.rename_item(
                "item.key.old",
                "item.key.new",
            )

            self.assertEqual(
                "item.key.new",
                renamed.definition_id,
            )

            self.assertIsNone(
                workspace.find(
                    "items",
                    "item.key.old",
                )
            )

            pickup = service.pickup_for_item(
                "item.key.new"
            )

            self.assertEqual(
                "pickup.key.new",
                pickup.definition_id,
            )

            self.assertEqual(
                "item.key.new",
                workspace.find(
                    "rewardGrants",
                    "reward.rename",
                ).data["items"][0]["itemId"],
            )

            self.assertEqual(
                "pickup.key.new",
                workspace.find(
                    "rewardProfiles",
                    "reward.pickup",
                ).data["loot"][0]["pickupDefinitionId"],
            )

            self.assertTrue(
                workspace.undo()
            )

            self.assertIsNotNone(
                workspace.find(
                    "items",
                    "item.key.old",
                )
            )

            self.assertEqual(
                "pickup.key.old",
                service.pickup_for_item(
                    "item.key.old"
                ).definition_id,
            )

    def test_delete_blocks_external_references_then_removes_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )
            service = item_service(
                workspace
            )

            service.create_item(
                "Pocao",
                "item.potion",
                "visual.item.a",
                "consumable",
            )

            workspace.create_definition_bundle(
                "Create Reference",
                [(
                    "rewardGrants",
                    "reward.uses_potion",
                    {
                        "id": "reward.uses_potion",
                        "experience": 0,
                        "gold": 0,
                        "items": [{
                            "itemId": "item.potion",
                            "quantity": 1,
                        }],
                    },
                )],
            )

            before = workspace.snapshot()

            with self.assertRaisesRegex(
                ValueError,
                "in use",
            ):
                service.delete_item(
                    "item.potion"
                )

            self.assertEqual(
                before,
                workspace.snapshot(),
            )

            workspace.delete_definition(
                workspace.find(
                    "rewardGrants",
                    "reward.uses_potion",
                )
            )

            self.assertTrue(
                service.delete_item(
                    "item.potion"
                )
            )

            self.assertIsNone(
                workspace.find(
                    "items",
                    "item.potion",
                )
            )

            self.assertIsNone(
                service.pickup_for_item(
                    "item.potion"
                )
            )

            self.assertIsNone(
                workspace.find(
                    "authoringDescriptors",
                    "item.potion",
                )
            )

            self.assertTrue(
                workspace.undo()
            )

            self.assertIsNotNone(
                workspace.find(
                    "items",
                    "item.potion",
                )
            )

            self.assertIsNotNone(
                service.pickup_for_item(
                    "item.potion"
                )
            )



class ItemVisualServiceTests(unittest.TestCase):
    @staticmethod
    def _service(workspace: ContentWorkspace):
        try:
            module = importlib.import_module(
                "tools.content_studio.services.item_visual_service"
            )
        except ModuleNotFoundError as error:
            raise AssertionError(
                "ItemVisualService ainda nao foi implementado"
            ) from error

        return module.ItemVisualService(
            workspace
        )

    @staticmethod
    def _add_animation(
        workspace: ContentWorkspace,
    ) -> None:
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

    def test_existing_static_sprite_is_reused_without_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            service = self._service(
                workspace
            )

            before = workspace.snapshot()

            selected = service.select_static_sprite(
                "visual.item.a"
            )

            self.assertEqual(
                "visual.item.a",
                selected.definition_id,
            )

            self.assertEqual(
                before,
                workspace.snapshot(),
            )

    def test_animation_frame_creates_deterministic_static_sprite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            self._add_animation(
                workspace
            )

            service = self._service(
                workspace
            )

            created = service.materialize_animation_frame(
                "item.life_potion",
                "animation.item.sheet",
                1,
            )

            self.assertEqual(
                "visual.item.life_potion",
                created.definition_id,
            )

            self.assertEqual(
                "image.items",
                created.data["imageId"],
            )

            self.assertEqual(
                {
                    "x": 16,
                    "y": 16,
                    "width": 16,
                    "height": 16,
                },
                created.data["source"],
            )

            self.assertEqual(
                {
                    "x": 7,
                    "y": 14,
                },
                created.data["anchor"],
            )

            self.assertNotIn(
                "drawOffset",
                created.data,
            )

            self.assertEqual(
                1,
                len(
                    workspace.definitions(
                        "visualImages"
                    )
                ),
            )

    def test_materializing_same_frame_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            self._add_animation(
                workspace
            )

            service = self._service(
                workspace
            )

            first = service.materialize_animation_frame(
                "item.key.blue",
                "animation.item.sheet",
                0,
            )

            before = workspace.snapshot()

            second = service.materialize_animation_frame(
                "item.key.blue",
                "animation.item.sheet",
                0,
            )

            self.assertEqual(
                first.definition_id,
                second.definition_id,
            )

            self.assertEqual(
                before,
                workspace.snapshot(),
            )

    def test_materialization_rejects_conflicting_generated_visual(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            self._add_animation(
                workspace
            )

            workspace.create_definition_bundle(
                "Create Conflict",
                [(
                    "staticSprites",
                    "visual.item.conflict",
                    {
                        "id": "visual.item.conflict",
                        "imageId": "image.items",
                        "source": {
                            "x": 99,
                            "y": 99,
                            "width": 1,
                            "height": 1,
                        },
                        "anchor": {
                            "x": 0,
                            "y": 0,
                        },
                    },
                )],
            )

            service = self._service(
                workspace
            )

            before = workspace.snapshot()

            with self.assertRaisesRegex(
                ValueError,
                "conflict",
            ):
                service.materialize_animation_frame(
                    "item.conflict",
                    "animation.item.sheet",
                    0,
                )

            self.assertEqual(
                before,
                workspace.snapshot(),
            )

    def test_animation_frame_choices_expose_valid_frames(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            self._add_animation(
                workspace
            )

            service = self._service(
                workspace
            )

            choices = service.animation_frames()

            self.assertEqual(
                2,
                len(choices),
            )

            self.assertEqual(
                (
                    "animation.item.sheet",
                    0,
                    "image.items",
                ),
                (
                    choices[0].animation_id,
                    choices[0].frame_index,
                    choices[0].image_id,
                ),
            )



class ItemPickupPlacementTests(unittest.TestCase):
    def test_authored_item_pickup_placement_uses_generated_definition(self) -> None:
        from tools.content_studio.interaction.map_editing_service import (
            MapEditingService,
        )
        from tools.content_studio.model.map_document import (
            MapDocument,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(
                Path(directory)
            )

            service = item_service(
                workspace
            )

            service.create_item(
                "Pocao",
                "item.potion",
                "visual.item.a",
                "consumable",
            )

            generated = service.pickup_for_item(
                "item.potion"
            )

            self.assertIsNotNone(
                generated
            )

            document = MapDocument.new(
                "map.item.pickup",
                4,
                4,
            )

            editing = MapEditingService(
                document,
                workspace=workspace,
            )

            selection = editing.place_entity(
                "pickups",
                generated.definition_id,
                16,
                32,
            )

            placed = document.entity(
                "pickups",
                int(selection.identifier),
            )

            self.assertIsNotNone(
                placed
            )

            self.assertEqual(
                generated.definition_id,
                placed["definitionId"],
            )

            self.assertEqual(
                generated.data["visualId"],
                placed["visualId"],
            )

            self.assertEqual(
                generated.data["collectionBounds"],
                placed["collectionBounds"],
            )

            self.assertEqual(
                generated.data["payload"],
                placed["payload"],
            )

            self.assertEqual(
                {
                    "kind": "item",
                    "itemId": "item.potion",
                    "quantity": 1,
                },
                placed["payload"],
            )

            self.assertTrue(
                document.undo()
            )

            self.assertEqual(
                [],
                document.data["pickups"],
            )


if __name__ == "__main__":
    unittest.main()