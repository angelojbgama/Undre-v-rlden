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
                    "side": sequence((8, 9), flip_x=True),
                },
                "walk": {
                    "down": sequence((0, 1, 2, 3)),
                    "up": sequence((4, 5, 6, 7)),
                    "side": sequence((8, 9, 10, 11), flip_x=True),
                },
                "sword": {
                    "side": sequence((8, 9, 10, 11), flip_x=True),
                },
            },
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
            reopened.sequences["walk"]["side"].frame_indices,
        )
        self.assertTrue(reopened.sequences["walk"]["side"].flip_x)
        self.assertEqual(
            4, reopened.sequences["walk"]["side"].columns)
        self.assertEqual(
            32, reopened.sequences["walk"]["side"].frame_width)

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


if __name__ == "__main__":
    unittest.main()
