from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.services.player_authoring_service import (
    FrameSequenceSpec,
    PlayerAuthoringRequest,
    PlayerAuthoringService,
    PlayerCollisionMaskSpec,
)


def content_root() -> dict[str, object]:
    result: dict[str, object] = {
        "format": "dungeon-underworld-content",
        "version": 5,
    }
    result.update({category: [] for category in CONTENT_CATEGORIES})
    return result


def fake_png(path: Path, width: int, height: int) -> None:
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + b"\0" * 8
        + struct.pack(">II", width, height)
    )


def movement_mask(
    *,
    width: int = 32,
    height: int = 32,
) -> PlayerCollisionMaskSpec:
    cells = [0] * (width * height)
    # Stable ground footprint: only the bottom part of the sprite blocks.
    for y in range(max(0, height - 8), height):
        for x in range(max(0, width // 2 - 8),
                       min(width, width // 2 + 8)):
            cells[y * width + x] = 1
    return PlayerCollisionMaskSpec(
        width=width,
        height=height,
        origin_x=-(width // 2),
        origin_y=-(height - 1),
        cells=tuple(cells),
    )


def hurtbox_mask(
    *,
    width: int = 32,
    height: int = 32,
) -> PlayerCollisionMaskSpec:
    cells = [0] * (width * height)
    body_width = max(1, round(width * 14 / 32))
    body_height = max(1, round(height * 22 / 32))
    start_x = max(0, (width - body_width) // 2)
    end_x = min(width, start_x + body_width)
    end_y = max(1, height - 1)
    start_y = max(0, end_y - body_height)
    for y in range(start_y, end_y):
        for x in range(start_x, end_x):
            cells[y * width + x] = 1
    return PlayerCollisionMaskSpec(
        width=width,
        height=height,
        origin_x=-(width // 2),
        origin_y=-(height - 1),
        cells=tuple(cells),
    )


def sequence(
    indices: tuple[int, ...],
    *,
    flip_x: bool = False,
    duration_ticks: int = 8,
) -> FrameSequenceSpec:
    return FrameSequenceSpec(
        image_id="image.player",
        frame_width=32,
        frame_height=32,
        spacing=0,
        origin_x=0,
        origin_y=0,
        duration_ticks=duration_ticks,
        loop=True,
        flip_x=flip_x,
        frame_indices=indices,
        columns=4,
    )


class PlayerAuthoringReopenTests(unittest.TestCase):
    def make_workspace(
        self,
    ) -> tuple[tempfile.TemporaryDirectory[str], ContentWorkspace]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        data = content_root()
        data["visualImages"] = [{
            "id": "image.player",
            "root": "contentWorkspace",
            "relativePath": "player.png",
        }]
        (root / "content.json").write_text(
            encode_json(data), encoding="utf-8")
        fake_png(root / "player.png", 128, 96)
        return temporary, ContentWorkspace.open(root)

    def make_request(self) -> PlayerAuthoringRequest:
        return PlayerAuthoringRequest(
            player_id="player.hero",
            display_name="Hero",
            progression_id="progression.player.default",
            sequences={
                "idle": {
                    "down": sequence((0, 1)),
                    "up": sequence((4, 5)),
                    "left": sequence((8, 9)),
                    "right": sequence((8, 9), flip_x=True),
                },
                "walk": {
                    "down": sequence((0, 1, 2, 3)),
                    "up": sequence((4, 5, 6, 7)),
                    "left": sequence((8, 9, 10, 11)),
                    "right": sequence((8, 9, 10, 11), flip_x=True),
                },
                "sword": {
                    "left": sequence((8, 9, 10, 11)),
                    "right": sequence((8, 9, 10, 11), flip_x=True),
                },
            },
            movement_collision_enabled=True,
            movement_collision={
                "down": movement_mask(),
                "up": movement_mask(),
                "left": movement_mask(),
                "right": movement_mask(),
            },
            hurtbox_enabled=True,
            hurtbox=hurtbox_mask(),
        )

    def test_saved_player_reopens_with_same_frame_sequences(self) -> None:
        temporary, workspace = self.make_workspace()
        self.addCleanup(temporary.cleanup)
        service = PlayerAuthoringService()

        created = service.save(workspace, self.make_request())
        reopened = service.request_for(workspace, created)

        self.assertEqual("player.hero", reopened.player_id)
        self.assertEqual("Hero", reopened.display_name)
        self.assertEqual(
            "progression.player.default", reopened.progression_id)
        self.assertEqual(
            (0, 1, 2, 3),
            reopened.sequences["walk"]["down"].frame_indices,
        )
        self.assertEqual(
            (8, 9, 10, 11),
            reopened.sequences["walk"]["left"].frame_indices,
        )
        self.assertFalse(reopened.sequences["walk"]["left"].flip_x)
        self.assertTrue(reopened.sequences["walk"]["right"].flip_x)
        self.assertEqual(
            4, reopened.sequences["walk"]["right"].columns)
        self.assertEqual(
            32, reopened.sequences["walk"]["right"].frame_width)
        self.assertTrue(reopened.movement_collision_enabled)
        self.assertEqual(
            movement_mask(),
            reopened.movement_collision["down"],
        )
        self.assertEqual(
            movement_mask(),
            reopened.movement_collision["left"],
        )
        self.assertEqual(
            movement_mask(),
            reopened.movement_collision["right"],
        )
        self.assertTrue(reopened.hurtbox_enabled)
        self.assertEqual(hurtbox_mask(), reopened.hurtbox)

    def test_legacy_side_refs_expand_to_left_and_right(self) -> None:
        refs = PlayerAuthoringService._refs({
            "down": "animation.down",
            "up": "animation.up",
            "side": "animation.side",
        })
        self.assertEqual("animation.side", refs["left"])
        self.assertEqual("animation.side", refs["right"])
        self.assertNotIn("side", refs)

    def test_legacy_side_collision_reopens_as_left_and_mirrored_right(
            self) -> None:
        side = PlayerCollisionMaskSpec(
            width=4,
            height=2,
            origin_x=-3,
            origin_y=-1,
            cells=(1, 0, 0, 0, 1, 1, 0, 0),
        )
        payload = {
            "width": side.width,
            "height": side.height,
            "origin": {"x": side.origin_x, "y": side.origin_y},
            "cells": list(side.cells),
        }
        enabled, masks = PlayerAuthoringService._movement_collision_specs({
            "down": payload,
            "up": payload,
            "side": payload,
        })
        self.assertTrue(enabled)
        self.assertEqual(side, masks["left"])
        self.assertEqual(
            PlayerAuthoringService.mirror_mask_horizontal(side),
            masks["right"],
        )

    def test_editing_reopened_player_updates_existing_content(self) -> None:
        temporary, workspace = self.make_workspace()
        self.addCleanup(temporary.cleanup)
        service = PlayerAuthoringService()

        created = service.save(workspace, self.make_request())
        original_visual_id = str(created.data["visualSetId"])
        reopened = service.request_for(workspace, created)
        edited_sequences = {
            state: dict(directions)
            for state, directions in reopened.sequences.items()
        }
        edited_sequences["walk"]["down"] = sequence((3, 2, 1, 0))

        service.save(
            workspace,
            PlayerAuthoringRequest(
                player_id=reopened.player_id,
                display_name="Hero editado",
                progression_id=reopened.progression_id,
                sequences=edited_sequences,
                movement_collision_enabled=(
                    reopened.movement_collision_enabled),
                movement_collision=dict(reopened.movement_collision),
                hurtbox_enabled=reopened.hurtbox_enabled,
                hurtbox=reopened.hurtbox,
            ),
            editing=True,
        )

        player = workspace.find("players", "player.hero")
        self.assertIsNotNone(player)
        self.assertEqual(
            original_visual_id,
            str(player.data["visualSetId"]),  # type: ignore[union-attr]
        )
        reopened_again = service.request_for(
            workspace, player)  # type: ignore[arg-type]
        self.assertEqual("Hero editado", reopened_again.display_name)
        self.assertEqual(
            (3, 2, 1, 0),
            reopened_again.sequences["walk"]["down"].frame_indices,
        )
        self.assertEqual(
            1,
            len([
                value for value in workspace.definitions("players")
                if value.definition_id == "player.hero"
            ]),
        )


    def test_disabling_movement_collision_removes_optional_field(self) -> None:
        temporary, workspace = self.make_workspace()
        self.addCleanup(temporary.cleanup)
        service = PlayerAuthoringService()

        created = service.save(workspace, self.make_request())
        reopened = service.request_for(workspace, created)
        service.save(
            workspace,
            PlayerAuthoringRequest(
                player_id=reopened.player_id,
                display_name=reopened.display_name,
                progression_id=reopened.progression_id,
                sequences=reopened.sequences,
                movement_collision_enabled=False,
                movement_collision={},
                hurtbox_enabled=reopened.hurtbox_enabled,
                hurtbox=reopened.hurtbox,
            ),
            editing=True,
        )

        player = workspace.find("players", "player.hero")
        self.assertIsNotNone(player)
        self.assertNotIn(
            "movementCollision",
            player.data,  # type: ignore[union-attr]
        )


    def test_disabling_hurtbox_removes_optional_field(self) -> None:
        temporary, workspace = self.make_workspace()
        self.addCleanup(temporary.cleanup)
        service = PlayerAuthoringService()

        created = service.save(workspace, self.make_request())
        reopened = service.request_for(workspace, created)
        service.save(
            workspace,
            PlayerAuthoringRequest(
                player_id=reopened.player_id,
                display_name=reopened.display_name,
                progression_id=reopened.progression_id,
                sequences=reopened.sequences,
                movement_collision_enabled=(
                    reopened.movement_collision_enabled),
                movement_collision=dict(reopened.movement_collision),
                hurtbox_enabled=False,
                hurtbox=None,
            ),
            editing=True,
        )

        player = workspace.find("players", "player.hero")
        self.assertIsNotNone(player)
        self.assertNotIn(
            "hurtbox",
            player.data,  # type: ignore[union-attr]
        )


if __name__ == "__main__":
    unittest.main()
