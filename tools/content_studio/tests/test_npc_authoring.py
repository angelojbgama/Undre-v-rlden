"""Tests for the first-class NPC authoring service and library widget."""

from __future__ import annotations

import os
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from tools.content_studio.model.content_workspace import ContentWorkspace

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


def _write_synthetic_png(path: Path, width: int = 128, height: int = 16) -> None:
    """A minimal valid grayscale PNG, built with the stdlib only."""
    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    row = bytes([0]) + bytes([0x80]) * width
    raw = row * height
    magic = bytes([0x89]) + b"PNG" + bytes([0x0D, 0x0A, 0x1A, 0x0A])
    path.write_bytes(
        magic + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b""))


def make_workspace(root: Path) -> ContentWorkspace:
    """Workspace with one dialogue and one authored NPC visual (idle art)."""
    (root / "definitions").mkdir(parents=True, exist_ok=True)
    (root / "definitions" / "content.json").write_text(
        "{"
        '"format": "dungeon-underworld-content", "version": 7, '
        '"dialogues": [{"id": "dialogue.test.talk", "entryNodeId": "entry",'
        ' "nodes": [{"id": "entry", "speaker": "Test", "pages": ["Hello"],'
        ' "nextNodeId": "", "choices": []}]}], '
        '"visualImages": [{"id": "image.test.npc", "root": "gameAssets",'
        ' "relativePath": "grub.png"}], '
        '"animations": []'
        "}",
        encoding="utf-8",
    )
    # The animations array is authored through the workspace API below so
    # frame payloads stay consistent.
    workspace = ContentWorkspace.open(root / "definitions")
    for animation_id in ("anim.test.idle", "anim.test.walk"):
        workspace.create_definition("animations", animation_id)
        animation = workspace.find("animations", animation_id)
        assert animation is not None
        workspace.replace_definition(animation, {
            "id": animation_id, "imageId": "image.test.npc", "loop": True,
            "frames": [{
                "source": {"x": 0, "y": 0, "width": 32, "height": 16},
                "anchor": {"x": 16, "y": 15}, "drawOffset": {"x": 0, "y": 0},
                "durationTicks": 12, "markers": [], "flipX": False, "masks": [],
            }],
        })
    workspace.create_definition("npcVisuals", "visual.test.npc")
    visual = workspace.find("npcVisuals", "visual.test.npc")
    assert visual is not None
    workspace.replace_definition(visual, {
        "id": "visual.test.npc",
        "markerColor": {"r": 200, "g": 80, "b": 80, "a": 255},
        "idle": {name: "anim.test.idle"
                 for name in ("down", "up", "left", "right")},
    })
    return workspace


VALID_NPC = {
    "id": "npc.test.elder",
    "visualSetId": "visual.test.npc",
    "interaction": {"x": -14, "y": -28, "width": 28, "height": 22},
    "interactionEnabled": True,
    "defaultDialogueId": "dialogue.test.talk",
    "tags": ["npc", "test"],
}


class NpcAuthoringServiceTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.workspace = make_workspace(Path(self._temporary.name))
        from tools.content_studio.services.npc_authoring_service import (
            NpcAuthoringService,
        )

        self.service = NpcAuthoringService(self.workspace)

    def test_create_configure_and_delete_roundtrip(self) -> None:
        created = self.service.create_npc("test.elder", dict(VALID_NPC))
        self.assertEqual(created.definition_id, "npc.test.elder")

        edited = dict(VALID_NPC, tags=["npc"])
        self.service.configure("npc.test.elder", edited)
        stored = self.service.find("npc.test.elder")
        assert stored is not None
        self.assertEqual(stored.data.get("tags"), ["npc"])

        self.service.delete("npc.test.elder")
        self.assertIsNone(self.service.find("npc.test.elder"))

    def test_create_rejects_unknown_visual_and_dialogue(self) -> None:
        for field, broken in (("visualSetId", "visual.test.missing"),
                              ("defaultDialogueId", "dialogue.test.missing")):
            data = dict(VALID_NPC, id=f"npc.test.{field}", **{field: broken})
            with self.assertRaisesRegex(ValueError, "does not exist"):
                self.service.create_npc(f"test.{field}", data)

    def test_create_rejects_degenerate_interaction_box(self) -> None:
        data = dict(VALID_NPC, id="npc.test.badbox",
                    interaction={"x": 0, "y": 0, "width": 0, "height": 22})
        with self.assertRaisesRegex(ValueError, "positive area"):
            self.service.create_npc("test.badbox", data)

    def test_visual_sets_include_builtin_markers(self) -> None:
        ids = [str(entry.get("id")) for entry in self.service.visual_sets()]
        self.assertIn("visual.npc.scholar", ids)
        self.assertIn("visual.test.npc", ids)

    def test_create_and_configure_visual_set(self) -> None:
        created = self.service.create_visual_set("visual.test.warden", {
            "id": "visual.test.warden",
            "markerColor": {"r": 10, "g": 200, "b": 90, "a": 255},
            "idle": {name: "anim.test.walk"
                     for name in ("down", "up", "left", "right")},
        })
        self.assertEqual(created.definition_id, "visual.test.warden")

        with self.assertRaisesRegex(ValueError, "at least one animation binding"):
            self.service.create_visual_set("visual.test.empty", {
                "id": "visual.test.empty",
                "markerColor": {"r": 0, "g": 0, "b": 0, "a": 255},
                "idle": {},
            })

        self.service.configure_visual("visual.test.warden", {
            "id": "visual.test.warden",
            "markerColor": {"r": 10, "g": 200, "b": 90, "a": 255},
            "idle": {"defaultAnimation": "anim.test.walk"},
        })
        stored = self.workspace.find("npcVisuals", "visual.test.warden")
        assert stored is not None
        self.assertEqual(stored.data["idle"].get("defaultAnimation"),
                         "anim.test.walk")

    def test_verify_visual_reports_marker_only_and_missing_file(self) -> None:
        self.service.create_npc("test.marked", dict(VALID_NPC, id="npc.test.marked",
                                               visualSetId="visual.npc.scholar"))
        diagnostics = self.service.verify_visual("npc.test.marked", None)
        self.assertTrue(any("colored marker" in d["message"] for d in diagnostics))

        self.service.create_npc("test.broken", dict(VALID_NPC, id="npc.test.broken"))
        (self.workspace.root.parent / "assets" / "grub.png").unlink(missing_ok=True)
        # grub.png never existed under the temp assets; point the image at a
        # real synthetic file first, then remove it to force the missing path.
        image = self.workspace.find("visualImages", "image.test.npc")
        assert image is not None
        grub = self.workspace.root.parent / "grub.png"
        grub.parent.mkdir(parents=True, exist_ok=True)
        _write_synthetic_png(grub)
        diagnostics = self.service.verify_visual("npc.test.broken", self.workspace.root.parent)
        self.assertEqual([d for d in diagnostics if d["severity"] == "error"], [])
        grub.unlink()
        diagnostics = self.service.verify_visual("npc.test.broken", self.workspace.root.parent)
        self.assertTrue(any("image file is missing" in d["message"] for d in diagnostics))

    def test_delete_blocked_by_map_placement(self) -> None:
        self.service.create_npc("test.placed", dict(VALID_NPC, id="npc.test.placed"))
        from tools.content_studio.model.map_document import MapDocument
        from tools.content_studio.model.world_project import WorldProject

        project = WorldProject.new("map.1", 16, 16)
        self.workspace.world_project = project
        self.addCleanup(setattr, self.workspace, "world_project", None)
        project.maps[0].add_entity("npcs", "npc.test.placed", 32, 32)

        with self.assertRaisesRegex(ValueError, "placed in maps"):
            self.service.delete("npc.test.placed")


class NpcVisualPipelineTests(unittest.TestCase):
    """The developed-standard spritesheet pipeline for NPC visuals."""

    def test_save_visual_materializes_image_animations_and_set(self) -> None:
        from tools.content_studio.services.npc_authoring_service import (
            NpcAuthoringService,
            NpcVisualRequest,
        )
        from tools.content_studio.services.player_authoring_service import (
            FrameSequenceSpec,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "assets"
            assets.mkdir()
            _write_synthetic_png(assets / "grub.png", width=128, height=16)
            workspace = make_workspace(root)
            workspace.create_definition("visualImages", "image.test.sheet")
            image = workspace.find("visualImages", "image.test.sheet")
            assert image is not None
            workspace.replace_definition(image, {
                "id": "image.test.sheet", "root": "gameAssets",
                "relativePath": "grub.png"})
            service = NpcAuthoringService(workspace)

            spec = FrameSequenceSpec(
                image_id="image.test.sheet", frame_width=32, frame_height=16,
                spacing=0, origin_x=0, origin_y=0, duration_ticks=12,
                loop=True, flip_x=False, frame_indices=(0, 1, 2, 3), columns=4)
            request = NpcVisualRequest(
                visual_id="visual.test.imported",
                marker_color={"r": 10, "g": 120, "b": 200, "a": 255},
                sequences={"default": spec, "down": spec})
            saved = service.save_visual(workspace, request)
            self.assertEqual(saved.definition_id, "visual.test.imported")
            idle = saved.data.get("idle", {})
            self.assertEqual(idle.get("defaultAnimation"),
                             "anim.npc.visual-test-imported.default")
            self.assertEqual(idle.get("down"),
                             "anim.npc.visual-test-imported.down")
            # The materialized animations are real workspace definitions.
            for animation_id in idle.values():
                self.assertIsNotNone(workspace.find("animations", animation_id))

            # Editing round-trip rebuilds the frame sequences.
            marker, sequences = service.visual_request_for(
                workspace, "visual.test.imported", assets)
            self.assertEqual(sequences["default"].frame_indices, (0, 1, 2, 3))
            self.assertEqual(marker.get("r"), 10)

            # Saving without sequences is rejected.
            with self.assertRaisesRegex(ValueError, "pelo menos uma sequência"):
                service.save_visual(workspace, NpcVisualRequest(
                    visual_id="visual.test.imported",
                    marker_color={"r": 0, "g": 0, "b": 0, "a": 255},
                    sequences={}), editing=True)


class NpcLibraryWidgetTests(unittest.TestCase):
    def test_widget_lists_creates_and_previews(self) -> None:
        from PySide6.QtWidgets import QApplication

        _ = QApplication.instance() or QApplication([])
        from tools.content_studio.services.npc_authoring_service import (
            NpcAuthoringService,
        )
        from tools.content_studio.ui.npc_library_widget import NpcLibraryWidget

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "assets"
            assets.mkdir()
            _write_synthetic_png(assets / "grub.png")
            workspace = make_workspace(root)
            service = NpcAuthoringService(workspace)
            service.create_npc("test.listed", dict(VALID_NPC, id="npc.test.listed"))

            widget = NpcLibraryWidget(workspace, asset_root=assets)
            widget.refresh()
            self.assertEqual(widget.npcs_list.count(), 1)
            widget.npcs_list.setCurrentRow(0)
            QApplication.processEvents()
            idle = widget.preview.strip_labels["down"].pixmap()
            self.assertFalse(idle.isNull())
            self.assertNotIn("✗", widget.preview.diagnostics.text())


if __name__ == "__main__":
    unittest.main()
