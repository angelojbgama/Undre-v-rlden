from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

try:
    from PySide6.QtGui import QColor, QImage
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover
    QApplication = None  # type: ignore[assignment]
    QColor = None  # type: ignore[assignment,misc]
    QImage = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import ContentWorkspace


@unittest.skipIf(QApplication is None, "PySide6 is not installed")
class AnimationFrameAlignmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        cls.application = QApplication.instance() or QApplication([])

    def test_animation_alignment_uses_anchor_and_suggests_idle_reference(
            self) -> None:
        from tools.content_studio.services.localization import Translator
        from tools.content_studio.ui.animation_frame_alignment_dialog import (
            AnimationFrameAlignmentDialog,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = QImage(96, 96, QImage.Format.Format_ARGB32)
            image.fill(QColor(255, 255, 255, 255))
            image_path = root / "player.png"
            self.assertTrue(image.save(str(image_path)))

            def frame(
                    x: int, y: int, width: int, height: int,
                    anchor_x: int, anchor_y: int) -> dict[str, object]:
                return {
                    "source": {
                        "x": x, "y": y,
                        "width": width, "height": height,
                    },
                    "anchor": {"x": anchor_x, "y": anchor_y},
                    "drawOffset": {"x": 0, "y": 0},
                    "durationTicks": 4,
                    "markers": [],
                    "flipX": False,
                }

            idle_id = "animation.player.hero.idle.down"
            sword_id = "animation.player.hero.sword.down"
            content = {
                "format": "dungeon-underworld-content",
                "version": 5,
            }
            content.update({category: [] for category in CONTENT_CATEGORIES})
            content["visualImages"] = [{
                "id": "image.player.test",
                "root": "contentWorkspace",
                "relativePath": "player.png",
            }]
            content["animations"] = [
                {
                    "id": idle_id,
                    "imageId": "image.player.test",
                    "loop": True,
                    "frames": [frame(0, 0, 32, 32, 16, 31)],
                },
                {
                    "id": sword_id,
                    "imageId": "image.player.test",
                    "loop": False,
                    "frames": [
                        frame(32, 0, 48, 48, 24, 47),
                        frame(32, 48, 48, 48, 24, 47),
                    ],
                },
            ]
            content["playerVisuals"] = [{
                "id": "visual.player.hero",
                "idle": {
                    "down": idle_id, "up": idle_id,
                    "left": idle_id, "right": idle_id,
                },
                "walk": {
                    "down": idle_id, "up": idle_id,
                    "left": idle_id, "right": idle_id,
                },
                "actions": [{
                    "actionId": "sword",
                    "clips": {
                        "down": sword_id, "up": sword_id,
                        "left": sword_id, "right": sword_id,
                    },
                }],
            }]

            (root / "content.json").write_text(
                encode_json(content), encoding="utf-8")
            workspace = ContentWorkspace.open(root)
            animation = workspace.find("animations", sword_id)
            self.assertIsNotNone(animation)

            dialog = AnimationFrameAlignmentDialog(
                workspace,
                animation,  # type: ignore[arg-type]
                image,
                image_path,
                root,
                Translator("pt-BR"),
            )
            self.addCleanup(dialog.deleteLater)

            self.assertEqual(idle_id, dialog.reference_animation.currentData())
            self.assertEqual(24, dialog.anchor_x.value())
            self.assertEqual(47, dialog.anchor_y.value())

            dialog.anchor_y.setValue(31)
            self.assertEqual(
                {"x": 24, "y": 31},
                dialog.frames[0]["anchor"],
            )

            dialog._apply_anchor_to_all_frames()
            self.assertEqual(
                [{"x": 24, "y": 31}, {"x": 24, "y": 31}],
                [value["anchor"] for value in dialog.frames],
            )

            dialog._set_anchor_from_reference()
            self.assertEqual(31, dialog.anchor_y.value())
            self.assertFalse(dialog.canvas._reference_frame.isNull())
