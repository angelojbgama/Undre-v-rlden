from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import (
    CONTENT_CATEGORIES,
    CONTENT_VERSION,
)
from tools.content_studio.model.content_workspace import (
    ContentWorkspace,
)


def attack_content() -> dict[str, object]:
    data: dict[str, object] = {
        "format": "dungeon-underworld-content",
        "version": CONTENT_VERSION,
    }

    data.update({
        category: []
        for category in CONTENT_CATEGORIES
    })

    data["projectiles"] = [
        {
            "id": "projectile.player.arrow",
            "visualId": "visual.projectile.arrow",
            "canonicalFacing": "right",
            "speedPixelsPerTick": 4,
            "lifetimeTicks": 60,
            "hitboxWidth": 8,
            "hitboxHeight": 4,
            "spawnOffsets": {"down": {"x": 0, "y": 0}, "up": {"x": 0, "y": 0}, "left": {"x": 0, "y": 0}, "right": {"x": 0, "y": 0}},
        },
    ]

    data["enemies"] = [
        {
            "id": "enemy.slime",
            "visualSetId": "visual.enemy.slime",
            "behaviorProfileId": "behavior.slime.basic",
            "faction": "enemy",
            "maximumHealth": 2,
            "movementSpeedSubpixelsPerTick": 128,
            "collisionBody": {"offsetX": -5, "offsetY": -8, "width": 10, "height": 8},
            "hurtbox": {"offsetX": -7, "offsetY": -16, "width": 14, "height": 16},
            "attackIds": ["attack.slime.bounce"],
        },
    ]

    data["attacks"] = [
        {
            "id": "attack.slime.bounce",
            "kind": "meleeHitbox",
            "damage": {"amount": 1, "knockbackPixels": 4},
            "totalTicks": 20,
            "cooldownTicks": 30,
            "minimumRangePixels": 0,
            "maximumRangePixels": 12,
            "visualActionId": "attack",
            "meleeHitboxes": {
                "down": {"offsetX": -8, "offsetY": 0, "width": 16, "height": 8},
                "up": {"offsetX": -8, "offsetY": -12, "width": 16, "height": 8},
                "left": {"offsetX": -12, "offsetY": -8, "width": 8, "height": 16},
                "right": {"offsetX": 4, "offsetY": -8, "width": 8, "height": 16},
            },
            "projectileDefinitionId": None,
            "timeline": [
                {"tick": 4, "kind": "activateHitbox"},
                {"tick": 14, "kind": "deactivateHitbox"},
            ],
        },
    ]

    return data


def workspace_from(data: dict[str, object]) -> ContentWorkspace:
    temporary = tempfile.TemporaryDirectory()

    root = Path(temporary.name)

    (root / "content.json").write_text(
        json.dumps(data, indent=2),
        encoding="utf-8",
    )

    workspace = ContentWorkspace.open(root)

    workspace.addCleanup = None  # type: ignore[attr-defined]

    return workspace


class AttackAuthoringServiceCleanupMixin:
    pass


def make_workspace(data: dict[str, object]) -> tuple[tempfile.TemporaryDirectory, ContentWorkspace]:
    temporary = tempfile.TemporaryDirectory()

    root = Path(temporary.name)

    (root / "content.json").write_text(
        json.dumps(data, indent=2),
        encoding="utf-8",
    )

    return temporary, ContentWorkspace.open(root)


class AttackAuthoringServiceTests(unittest.TestCase):
    def test_entries_list_authored_attacks(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )

        temporary, workspace = make_workspace(attack_content())

        try:
            entries = AttackAuthoringService(
                workspace
            ).entries()

            by_id = {
                entry.definition_id: entry
                for entry in entries
            }

            # Authored entries plus the builtin player attacks, listed
            # read-only from the engine registry.
            self.assertEqual(
                {
                    "attack.slime.bounce": "authored",
                    "attack.player.bow": "builtin",
                    "attack.player.sword": "builtin",
                },
                {
                    definition_id: entry.status
                    for definition_id, entry in by_id.items()
                },
            )

            self.assertEqual(
                ("enemy.slime",),
                by_id["attack.slime.bounce"].referenced_by,
            )
        finally:
            temporary.cleanup()

    def test_configuration_rejects_unknown_attack(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )

        temporary, workspace = make_workspace(attack_content())

        try:
            with self.assertRaises(ValueError):
                AttackAuthoringService(
                    workspace
                ).configuration(
                    "attack.ghost"
                )
            # Builtin attacks resolve read-only for the editor.
            configuration = AttackAuthoringService(
                workspace
            ).configuration(
                "attack.player.sword"
            )
            self.assertEqual(
                "meleeHitbox",
                configuration["kind"],
            )
        finally:
            temporary.cleanup()

    def test_configure_updates_authored_and_delete_removes(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )

        temporary, workspace = make_workspace(attack_content())

        try:
            service = AttackAuthoringService(workspace)

            data = service.configuration(
                "attack.slime.bounce"
            )

            data["damage"]["amount"] = 3

            service.configure(
                "attack.slime.bounce",
                data,
            )

            override = workspace.find(
                "attacks",
                "attack.slime.bounce",
            )

            self.assertIsNotNone(override)

            assert override is not None

            self.assertEqual(
                3,
                override.data["damage"]["amount"],
            )

            with self.assertRaises(ValueError):
                service.delete("attack.slime.bounce")

            self.assertIsNotNone(
                workspace.find(
                    "attacks",
                    "attack.slime.bounce",
                )
            )
        finally:
            temporary.cleanup()

    def test_play_effect_event_round_trip_and_validation(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )

        temporary, workspace = make_workspace(attack_content())

        try:
            service = AttackAuthoringService(workspace)

            base = service.configuration("attack.slime.bounce")

            data = json.loads(json.dumps(base))

            workspace.create_definition("animations", "animation.arrow")

            data["timeline"] = [
                {"tick": 2, "kind": "playEffect",
                 "animationId": "animation.missing", "offsetX": 0, "offsetY": -8},
                {"tick": 6, "kind": "activateHitbox"},
                {"tick": 18, "kind": "deactivateHitbox"},
            ]

            with self.assertRaisesRegex(
                ValueError,
                "requires an existing animation",
            ):
                service.configure("attack.slime.bounce", data)

            data["timeline"][0]["animationId"] = "animation.arrow"

            service.configure("attack.slime.bounce", data)

            stored = workspace.find("attacks", "attack.slime.bounce")

            assert stored is not None

            self.assertEqual(
                {
                    "tick": 2,
                    "kind": "playEffect",
                    "animationId": "animation.arrow",
                    "offsetX": 0,
                    "offsetY": -8,
                },
                stored.data["timeline"][0],
            )

            reloaded = service.configuration("attack.slime.bounce")

            self.assertEqual(
                "animation.arrow",
                reloaded["timeline"][0]["animationId"],
            )
        finally:
            temporary.cleanup()

    def test_configure_preserves_shapes_and_unknown_fields(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )

        temporary, workspace = make_workspace(attack_content())

        try:
            service = AttackAuthoringService(workspace)

            data = service.configuration(
                "attack.slime.bounce"
            )

            data["shapes"] = [
                {"facing": "down", "frameIndex": 0, "boxes": []},
            ]

            data["customFutureField"] = {"keep": True}

            data["damage"]["amount"] = 2

            service.configure(
                "attack.slime.bounce",
                data,
            )

            stored = workspace.find(
                "attacks",
                "attack.slime.bounce",
            )

            assert stored is not None

            self.assertEqual(
                [
                    {"facing": "down", "frameIndex": 0, "boxes": []},
                ],
                stored.data["shapes"],
            )

            self.assertEqual(
                {"keep": True},
                stored.data["customFutureField"],
            )

            self.assertEqual(
                2,
                stored.data["damage"]["amount"],
            )
        finally:
            temporary.cleanup()

    def test_validation_rejects_invalid_configurations(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )

        temporary, workspace = make_workspace(attack_content())

        try:
            service = AttackAuthoringService(workspace)

            base = service.configuration(
                "attack.slime.bounce"
            )

            def rejects(mutate, message: str) -> None:
                data = json.loads(json.dumps(base))

                mutate(data)

                with self.assertRaisesRegex(
                    ValueError,
                    message,
                ):
                    service.configure(
                        "attack.slime.bounce",
                        data,
                    )

            rejects(
                lambda data: data.update(totalTicks=0),
                "totalTicks must be positive",
            )

            rejects(
                lambda data: (
                    data.update(minimumRangePixels=10),
                    data.update(maximumRangePixels=2),
                ),
                "range values are invalid",
            )

            rejects(
                lambda data: data.update(meleeHitboxes=None),
                "melee attack requires meleeHitboxes",
            )

            rejects(
                lambda data: data["meleeHitboxes"]["down"].update(width=0),
                "positive size",
            )

            rejects(
                lambda data: data["timeline"].reverse(),
                "ordered by tick",
            )

            rejects(
                lambda data: data["timeline"].append(
                    {"tick": 999, "kind": "deactivateHitbox"}
                ),
                "within totalTicks",
            )

            bow = json.loads(json.dumps(base))
            bow.update(kind="projectile", meleeHitboxes=None)
            bow["timeline"] = [
                {"tick": 8, "kind": "spawnProjectile"},
            ]

            bow["projectileDefinitionId"] = "projectile.missing"

            with self.assertRaisesRegex(
                ValueError,
                "unknown projectile",
            ):
                service.configure(
                    "attack.slime.bounce",
                    bow,
                )

            bow["timeline"] = [
                {"tick": 6, "kind": "activateHitbox"},
            ]

            with self.assertRaisesRegex(
                ValueError,
                "spawnProjectile",
            ):
                service.configure(
                    "attack.slime.bounce",
                    bow,
                )
        finally:
            temporary.cleanup()

    def test_configure_attack_ammo(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )

        data = attack_content()
        data["items"] = [
            {
                "id": "item.arrow",
                "visualId": "visual.item.arrow",
                "category": "misc",
                "stackLimit": 99,
            }
        ]

        temporary, workspace = make_workspace(data)

        try:
            bow = {
                "id": "attack.player.bow",
                "kind": "projectile",
                "damage": {"amount": 1, "knockbackPixels": 32},
                "totalTicks": 16,
                "cooldownTicks": 0,
                "minimumRangePixels": 0,
                "maximumRangePixels": 512,
                "visualActionId": "visual.player.bow",
                "projectileDefinitionId": "projectile.player.arrow",
                "timeline": [{"tick": 8, "kind": "spawnProjectile"}],
                "ammo": {"itemId": "item.arrow", "amount": 1},
            }

            AttackAuthoringService(workspace).configure(
                "attack.player.bow",
                bow,
            )

            stored = workspace.find(
                "attacks",
                "attack.player.bow",
            )

            self.assertIsNotNone(stored)

            assert stored is not None

            self.assertEqual(
                {"itemId": "item.arrow", "amount": 1},
                stored.data["ammo"],
            )

            # Removing the ammo reference drops the field.
            without_ammo = dict(bow)
            without_ammo["ammo"] = None

            AttackAuthoringService(workspace).configure(
                "attack.player.bow",
                without_ammo,
            )

            stored = workspace.find(
                "attacks",
                "attack.player.bow",
            )

            assert stored is not None

            self.assertNotIn("ammo", stored.data)

            # Unknown items and non-positive amounts are rejected.
            for invalid in (
                {"itemId": "item.missing", "amount": 1},
                {"itemId": "item.arrow", "amount": 0},
            ):
                with self.assertRaises(ValueError):
                    AttackAuthoringService(workspace).configure(
                        "attack.player.bow",
                        {**bow, "ammo": invalid},
                    )

            # Melee attacks cannot spend ammo.
            melee = dict(bow)
            melee["kind"] = "meleeHitbox"

            with self.assertRaises(ValueError):
                AttackAuthoringService(workspace).configure(
                    "attack.player.bow",
                    melee,
                )
        finally:
            temporary.cleanup()

    def test_configure_attack_presentation_effect(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )

        data = attack_content()
        data["presentationEffects"] = [
            {
                "id": "effect.world.heavy_impact",
                "lifetime": "transient",
                "durationTicks": 24,
                "priority": 80,
            }
        ]

        temporary, workspace = make_workspace(data)

        try:
            sword = {
                "id": "attack.player.sword",
                "kind": "meleeHitbox",
                "damage": {"amount": 1, "knockbackPixels": 4},
                "totalTicks": 12,
                "cooldownTicks": 0,
                "minimumRangePixels": 0,
                "maximumRangePixels": 24,
                "visualActionId": "visual.player.sword",
                "meleeHitboxes": {
                    direction: {
                        "offsetX": 0,
                        "offsetY": 0,
                        "width": 8,
                        "height": 8,
                    }
                    for direction in ("down", "up", "left", "right")
                },
                "timeline": [],
                "presentationEffectId": "effect.world.heavy_impact",
            }

            AttackAuthoringService(workspace).configure(
                "attack.player.sword",
                sword,
            )

            stored = workspace.find(
                "attacks",
                "attack.player.sword",
            )

            self.assertIsNotNone(stored)

            assert stored is not None

            self.assertEqual(
                "effect.world.heavy_impact",
                stored.data["presentationEffectId"],
            )

            # Clearing the effect drops the field.
            without_effect = dict(sword)
            without_effect["presentationEffectId"] = None

            AttackAuthoringService(workspace).configure(
                "attack.player.sword",
                without_effect,
            )

            stored = workspace.find(
                "attacks",
                "attack.player.sword",
            )

            assert stored is not None

            self.assertNotIn(
                "presentationEffectId",
                stored.data,
            )

            # Unknown effects and invalid values are rejected.
            with self.assertRaisesRegex(
                ValueError,
                "unknown presentation effect",
            ):
                AttackAuthoringService(workspace).configure(
                    "attack.player.sword",
                    {
                        **sword,
                        "presentationEffectId": "effect.missing",
                    },
                )

            with self.assertRaisesRegex(
                ValueError,
                "non-empty string",
            ):
                AttackAuthoringService(workspace).configure(
                    "attack.player.sword",
                    {
                        **sword,
                        "presentationEffectId": 7,
                    },
                )
        finally:
            temporary.cleanup()

    def test_delete_blocks_referenced_attack(self) -> None:
        from tools.content_studio.services.attack_authoring_service import (
            AttackAuthoringService,
        )

        temporary, workspace = make_workspace(attack_content())

        try:
            with self.assertRaisesRegex(
                ValueError,
                "referenced by enemies",
            ):
                AttackAuthoringService(
                    workspace
                ).delete(
                    "attack.slime.bounce"
                )
        finally:
            temporary.cleanup()


if __name__ == "__main__":
    unittest.main()
