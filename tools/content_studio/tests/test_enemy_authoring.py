"""Tests for the first-class enemy authoring service and library widget."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.model.content_workspace import ContentWorkspace

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


def make_workspace_with_references(root: Path) -> ContentWorkspace:
    """A workspace with one behavior, visual, attack and reward profile."""
    (root / "definitions").mkdir(parents=True, exist_ok=True)
    (root / "definitions" / "content.json").write_text(
        "{"
        '"format": "dungeon-underworld-content", "version": 7, '
        '"behaviors": [{"id": "behavior.test.melee", "detectionRangePixels": 56,'
        ' "disengageRangePixels": 96, "idleDurationTicks": 600,'
        ' "wanderDurationTicks": 45}], '
        '"attacks": [{"id": "attack.test.contact", "kind": "meleeHitbox",'
        ' "damage": {"amount": 1, "knockbackPixels": 8}, "totalTicks": 24,'
        ' "cooldownTicks": 45, "minimumRangePixels": 0, "maximumRangePixels": 12,'
        ' "visualActionId": "visual.action.test", "meleeHitboxes": {'
        ' "down": {"offsetX": -7, "offsetY": -2, "width": 14, "height": 10}}}], '
        '"enemyVisuals": [], '
        '"enemies": [], '
        '"rewardProfiles": [{"id": "reward.test.enemy", "experience": 10, "loot": []}]'
        "}",
        encoding="utf-8",
    )
    workspace = ContentWorkspace.open(root / "definitions")
    return workspace


def make_enemy_visual(workspace: ContentWorkspace) -> None:
    """An enemy visual set needs two authored animations to point at."""
    workspace.create_definition("visualImages", "image.test.enemy")
    image = workspace.find("visualImages", "image.test.enemy")
    assert image is not None
    workspace.replace_definition(image, {
        "id": "image.test.enemy", "root": "gameAssets",
        "relativePath": "Characters/Enemies/Slime/slime_idle.png",
    })
    for animation_id in ("anim.test.enemy.idle", "anim.test.enemy.death"):
        workspace.create_definition("animations", animation_id)
        animation = workspace.find("animations", animation_id)
        assert animation is not None
        workspace.replace_definition(animation, {
            "id": animation_id, "imageId": "image.test.enemy", "loop": True,
            "frames": [{
                "source": {"x": 0, "y": 0, "width": 32, "height": 16},
                "anchor": {"x": 16, "y": 15}, "drawOffset": {"x": 0, "y": 0},
                "durationTicks": 12, "markers": [], "flipX": False, "masks": [],
            }],
        })
    workspace.create_definition("enemyVisuals", "visual.test.enemy")
    visual = workspace.find("enemyVisuals", "visual.test.enemy")
    assert visual is not None
    workspace.replace_definition(visual, {
        "id": "visual.test.enemy",
        "idle": {name: "anim.test.enemy.idle"
                 for name in ("down", "up", "left", "right")},
        "death": {name: "anim.test.enemy.death"
                  for name in ("down", "up", "left", "right")},
        "actions": [],
    })


VALID_ENEMY = {
    "id": "enemy.test.grub",
    "visualSetId": "visual.test.enemy",
    "behaviorProfileId": "behavior.test.melee",
    "faction": "enemy",
    "maximumHealth": 3,
    "movementSpeedSubpixelsPerTick": 96,
    "collisionBody": {"offsetX": -5, "offsetY": -8, "width": 10, "height": 8},
    "hurtbox": {"offsetX": -7, "offsetY": -22, "width": 14, "height": 22},
    "attackIds": ["attack.test.contact"],
    "rewardProfileId": "reward.test.enemy",
}


class EnemyAuthoringServiceTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.workspace = make_workspace_with_references(Path(self._temporary.name))
        make_enemy_visual(self.workspace)
        from tools.content_studio.services.enemy_authoring_service import (
            EnemyAuthoringService,
        )

        self.service = EnemyAuthoringService(self.workspace)

    def test_create_configure_and_delete_roundtrip(self) -> None:
        created = self.service.create_enemy("test.grub", dict(VALID_ENEMY))
        self.assertEqual(created.definition_id, "enemy.test.grub")

        edited = dict(VALID_ENEMY)
        edited["maximumHealth"] = 7
        self.service.configure("enemy.test.grub", edited)
        stored = self.service.find("enemy.test.grub")
        assert stored is not None
        self.assertEqual(stored.data.get("maximumHealth"), 7)

        self.service.delete("enemy.test.grub")
        self.assertIsNone(self.service.find("enemy.test.grub"))

    def test_create_normalizes_the_enemy_namespace(self) -> None:
        data = dict(VALID_ENEMY)
        data["id"] = "enemy.test.grub"
        created = self.service.create_enemy("test.grub", data)
        self.assertEqual(created.definition_id, "enemy.test.grub")

    def test_create_rejects_unknown_references(self) -> None:
        for field, broken in (
            ("visualSetId", "visual.test.missing"),
            ("behaviorProfileId", "behavior.test.missing"),
            ("rewardProfileId", "reward.test.missing"),
        ):
            data = dict(VALID_ENEMY, **{field: broken})
            with self.assertRaisesRegex(ValueError, "does not exist"):
                self.service.create_enemy(f"test.{field}", data)

    def test_create_rejects_empty_attack_list(self) -> None:
        data = dict(VALID_ENEMY, attackIds=[])
        with self.assertRaisesRegex(ValueError, "at least one attack"):
            self.service.create_enemy("test.noattack", data)

    def test_create_rejects_non_positive_stats(self) -> None:
        for field in ("maximumHealth", "movementSpeedSubpixelsPerTick"):
            data = dict(VALID_ENEMY, **{field: 0})
            with self.assertRaisesRegex(ValueError, "positive"):
                self.service.create_enemy(f"test.bad{field}", data)

    def test_create_rejects_degenerate_footprints(self) -> None:
        data = dict(VALID_ENEMY, hurtbox={"offsetX": 0, "offsetY": 0,
                                          "width": 0, "height": 10})
        with self.assertRaisesRegex(ValueError, "hurtbox"):
            self.service.create_enemy("test.badbox", data)

    def test_delete_blocked_by_map_placement(self) -> None:
        self.service.create_enemy("test.placed", dict(VALID_ENEMY, id="enemy.test.placed"))
        from tools.content_studio.model.map_document import MapDocument
        from tools.content_studio.model.world_project import WorldProject

        project = WorldProject.new("map.1", 16, 16)
        self.workspace.world_project = project
        self.addCleanup(setattr, self.workspace, "world_project", None)
        document = project.maps[0]
        document.add_entity("enemies", "enemy.test.placed", 32, 32)

        with self.assertRaisesRegex(ValueError, "placed in maps"):
            self.service.delete("enemy.test.placed")


class EnemyLibraryWidgetTests(unittest.TestCase):
    def test_widget_lists_creates_and_edits(self) -> None:
        from PySide6.QtWidgets import QApplication

        _ = QApplication.instance() or QApplication([])
        from tools.content_studio.ui.enemy_library_widget import EnemyLibraryWidget

        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace_with_references(Path(directory))
            make_enemy_visual(workspace)
            widget = EnemyLibraryWidget(workspace)
            widget.refresh()
            self.assertEqual(widget.enemies_list.count(), 0)

            from tools.content_studio.services.enemy_authoring_service import (
                EnemyAuthoringService,
            )

            service = EnemyAuthoringService(workspace)
            service.create_enemy("listed", dict(VALID_ENEMY, id="enemy.test.listed"))
            widget.refresh()
            self.assertEqual(widget.enemies_list.count(), 1)


if __name__ == "__main__":
    unittest.main()
