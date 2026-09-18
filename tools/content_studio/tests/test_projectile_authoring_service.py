from __future__ import annotations

import unittest

from tools.content_studio.services.projectile_authoring_service import (
    ProjectileAuthoringService,
)
from tools.content_studio.tests.test_attack_authoring_service import (
    attack_content,
    make_workspace,
)


def workspace_data() -> dict[str, object]:
    data = attack_content()
    data["visualImages"].append({  # type: ignore[union-attr]
        "id": "image.arrow",
        "root": "gameAssets",
        "relativePath": "Characters/Player/attacking/arrow.png",
    })
    data["projectiles"][0]["visualId"] = (  # type: ignore[index]
        "visual.projectile.player.arrow"
    )
    data["staticSprites"].append({  # type: ignore[union-attr]
        "id": "visual.projectile.player.arrow",
        "imageId": "image.arrow",
        "source": {"x": 0, "y": 0, "width": 16, "height": 16},
        "anchor": {"x": 8, "y": 8},
    })
    data["animations"].append({  # type: ignore[union-attr]
        "id": "animation.arrow",
        "imageId": "image.arrow",
        "loop": False,
        "frames": [
            {
                "source": {"x": 0, "y": 0, "width": 16, "height": 16},
                "anchor": {"x": 8, "y": 15},
                "durationTicks": 8,
                "markers": [],
            }
        ],
    })
    return data


class ProjectileAuthoringServiceTests(unittest.TestCase):
    def test_entries_only_list_projectiles_with_visuals(
        self,
    ) -> None:
        temporary, workspace = make_workspace(workspace_data())

        try:
            entries = ProjectileAuthoringService(
                workspace
            ).entries()
            definition_ids = [
                entry.definition_id for entry in entries
            ]

            self.assertIn(
                "projectile.player.arrow",
                definition_ids,
            )
            self.assertNotIn(
                "projectile.skull.arrow",
                definition_ids,
            )
        finally:
            temporary.cleanup()

    def test_create_from_animation_generates_sprite_and_projectile(
        self,
    ) -> None:
        temporary, workspace = make_workspace(workspace_data())

        try:
            projectile_id = ProjectileAuthoringService(
                workspace
            ).create_from_animation(
                "projectile.test.arrow",
                "animation.arrow",
            )

            self.assertEqual(
                "projectile.test.arrow",
                projectile_id,
            )

            projectile = workspace.find(
                "projectiles",
                projectile_id,
            )
            sprite = workspace.find(
                "staticSprites",
                "visual.test.arrow",
            )

            self.assertIsNotNone(projectile)
            self.assertIsNotNone(sprite)

            assert projectile is not None
            assert sprite is not None

            self.assertEqual(
                "visual.test.arrow",
                projectile.data["visualId"],
            )
            self.assertEqual(
                {"x": 8, "y": 15},
                sprite.data["anchor"],
            )
            self.assertEqual(
                {"x": 0, "y": 0, "width": 16, "height": 16},
                sprite.data["source"],
            )
            self.assertEqual(
                8,
                projectile.data["hitboxWidth"],
            )
        finally:
            temporary.cleanup()

    def test_update_spawn_offsets_updates_authored(self) -> None:
        temporary, workspace = make_workspace(workspace_data())
        service = ProjectileAuthoringService(workspace)

        try:
            service.update_spawn_offsets(
                "projectile.player.arrow",
                {
                    direction: {"x": 3, "y": -4}
                    for direction in (
                        "down",
                        "up",
                        "left",
                        "right",
                    )
                },
                "right",
                "world",
            )

            projectile = workspace.find(
                "projectiles",
                "projectile.player.arrow",
            )

            self.assertIsNotNone(projectile)

            assert projectile is not None

            self.assertEqual(
                {
                    direction: {"x": 3, "y": -4}
                    for direction in (
                        "down",
                        "up",
                        "left",
                        "right",
                    )
                },
                projectile.data["spawnOffsets"],
            )
            self.assertEqual(
                "right",
                projectile.data["canonicalFacing"],
            )
            self.assertEqual(
                "world",
                projectile.data["renderLayer"],
            )
        finally:
            temporary.cleanup()

    def test_update_spawn_render_layers_per_direction(self) -> None:
        temporary, workspace = make_workspace(workspace_data())
        service = ProjectileAuthoringService(workspace)

        try:
            service.update_spawn_offsets(
                "projectile.player.arrow",
                {
                    direction: {"x": 0, "y": 0}
                    for direction in ("down", "up", "left", "right")
                },
                None,
                None,
                {"down": "actor", "up": "world"},
            )

            projectile = workspace.find(
                "projectiles", "projectile.player.arrow")

            assert projectile is not None

            self.assertEqual(
                {"down": "actor", "up": "world"},
                projectile.data["renderLayers"],
            )

            with self.assertRaises(ValueError):
                service.update_spawn_offsets(
                    "projectile.player.arrow",
                    {
                        direction: {"x": 0, "y": 0}
                        for direction in ("down", "up", "left", "right")
                    },
                    None,
                    None,
                    {"down": "behind"},
                )
        finally:
            temporary.cleanup()

    def test_update_maximum_distance(self) -> None:
        temporary, workspace = make_workspace(workspace_data())
        service = ProjectileAuthoringService(workspace)

        try:
            service.update_spawn_offsets(
                "projectile.player.arrow",
                {
                    direction: {"x": 0, "y": 0}
                    for direction in ("down", "up", "left", "right")
                },
                None,
                None,
                None,
                90,
            )

            projectile = workspace.find(
                "projectiles", "projectile.player.arrow")

            assert projectile is not None

            self.assertEqual(90, projectile.data["maximumDistancePixels"])

            # Zero removes the cap (unlimited).
            service.update_spawn_offsets(
                "projectile.player.arrow",
                {
                    direction: {"x": 0, "y": 0}
                    for direction in ("down", "up", "left", "right")
                },
                None,
                None,
                None,
                0,
            )

            stored = workspace.find(
                "projectiles", "projectile.player.arrow")

            assert stored is not None

            self.assertNotIn(
                "maximumDistancePixels", stored.data)

            with self.assertRaises(ValueError):
                service.update_spawn_offsets(
                    "projectile.player.arrow",
                    {
                        direction: {"x": 0, "y": 0}
                        for direction in ("down", "up", "left", "right")
                    },
                    None,
                    None,
                    None,
                    -5,
                )
        finally:
            temporary.cleanup()

    def test_update_end_animation(self) -> None:
        temporary, workspace = make_workspace(workspace_data())
        service = ProjectileAuthoringService(workspace)

        try:
            end_anim = workspace.create_definition(
                "animations", "anim.end.poof")

            workspace.update(end_anim, "loop", False)

            service.update_spawn_offsets(
                "projectile.player.arrow",
                {
                    direction: {"x": 0, "y": 0}
                    for direction in ("down", "up", "left", "right")
                },
                end_animation="anim.end.poof",
            )

            projectile = workspace.find(
                "projectiles", "projectile.player.arrow")

            assert projectile is not None

            self.assertEqual(
                "anim.end.poof",
                projectile.data["endAnimationId"],
            )

            # Unknown animation is rejected.
            with self.assertRaises(ValueError):
                service.update_spawn_offsets(
                    "projectile.player.arrow",
                    {
                        direction: {"x": 0, "y": 0}
                        for direction in ("down", "up", "left", "right")
                    },
                    end_animation="anim.missing",
                )

            # Empty removes the field.
            service.update_spawn_offsets(
                "projectile.player.arrow",
                {
                    direction: {"x": 0, "y": 0}
                    for direction in ("down", "up", "left", "right")
                },
                end_animation="",
            )

            stored = workspace.find(
                "projectiles", "projectile.player.arrow")

            assert stored is not None

            self.assertNotIn(
                "endAnimationId", stored.data)
        finally:
            temporary.cleanup()

    def test_update_rejects_invalid_render_layer(self) -> None:
        temporary, workspace = make_workspace(workspace_data())
        service = ProjectileAuthoringService(workspace)

        try:
            with self.assertRaises(ValueError):
                service.update_spawn_offsets(
                    "projectile.player.arrow",
                    {
                        direction: {"x": 0, "y": 0}
                        for direction in (
                            "down",
                            "up",
                            "left",
                            "right",
                        )
                    },
                    None,
                    "background",
                )
        finally:
            temporary.cleanup()

    def test_update_rejects_invalid_canonical_facing(self) -> None:
        temporary, workspace = make_workspace(workspace_data())
        service = ProjectileAuthoringService(workspace)

        try:
            with self.assertRaises(ValueError):
                service.update_spawn_offsets(
                    "projectile.player.arrow",
                    {
                        direction: {"x": 0, "y": 0}
                        for direction in (
                            "down",
                            "up",
                            "left",
                            "right",
                        )
                    },
                    "sideways",
                )
        finally:
            temporary.cleanup()

    def test_update_spawn_offsets_rejects_unknown_projectile(
        self,
    ) -> None:
        temporary, workspace = make_workspace(workspace_data())
        service = ProjectileAuthoringService(workspace)

        try:
            with self.assertRaises(ValueError):
                service.update_spawn_offsets(
                    "projectile.unknown",
                    {},
                )
        finally:
            temporary.cleanup()

    def test_create_rejects_duplicate_projectile(self) -> None:
        temporary, workspace = make_workspace(workspace_data())
        service = ProjectileAuthoringService(workspace)

        try:
            service.create_from_animation(
                "projectile.test.arrow",
                "animation.arrow",
            )

            with self.assertRaises(ValueError):
                service.create_from_animation(
                    "projectile.test.arrow",
                    "animation.arrow",
                )
        finally:
            temporary.cleanup()


if __name__ == "__main__":
    unittest.main()
