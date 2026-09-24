"""Tests for the first-class dialogue authoring service and library."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.model.content_workspace import ContentWorkspace

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


def make_workspace(root: Path) -> ContentWorkspace:
    (root / "definitions").mkdir(parents=True, exist_ok=True)
    (root / "definitions" / "content.json").write_text(
        "{"
        '"format": "dungeon-underworld-content", "version": 7, '
        '"dialogues": [], '
        '"quests": [{"id": "quest.test.awakening", "title": "Awakening",'
        ' "objectives": [{"id": "obj.1", "kind": "kill",'
        ' "targetId": "enemy.evil_soldier", "requiredCount": 1,'
        ' "description": "Defeat."}], "tags": ["test"],'
        ' "rewardGrantId": null}], '
        '"shops": [{"id": "shop.test.general", "offers": []}]'
        "}",
        encoding="utf-8",
    )
    return ContentWorkspace.open(root / "definitions")


def sample_nodes() -> list[dict]:
    return [
        {"id": "entry", "speaker": "Elder", "pages": ["Hello, traveler."],
         "nextNodeId": "", "choices": [
             {"label": "Offer help",
              "targetNodeId": "offer",
              "conditions": [{"kind": "flagNotSet",
                              "flagId": "flag.test.awakening.active"}],
              "actions": [{"kind": "startQuest",
                           "targetId": "quest.test.awakening"}]},
             {"label": "Leave", "targetNodeId": "", "conditions": [],
              "actions": []},
         ]},
        {"id": "offer", "speaker": "Elder", "pages": ["Thank you!"],
         "nextNodeId": "", "choices": []},
    ]


VALID_DIALOGUE = {
    "id": "dialogue.test.elder",
    "entryNodeId": "entry",
    "nodes": sample_nodes(),
}


class DialogueAuthoringServiceTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.workspace = make_workspace(Path(self._temporary.name))
        from tools.content_studio.services.dialogue_authoring_service import (
            DialogueAuthoringService,
        )

        self.service = DialogueAuthoringService(self.workspace)

    def test_create_configure_and_delete_roundtrip(self) -> None:
        created = self.service.create_dialogue("test.elder", dict(VALID_DIALOGUE))
        self.assertEqual(created.definition_id, "dialogue.test.elder")

        # An edit that would orphan a choice target must be rejected...
        orphaned = dict(VALID_DIALOGUE, nodes=sample_nodes()[:1])
        with self.assertRaisesRegex(ValueError, "unknown node"):
            self.service.configure("dialogue.test.elder", orphaned)
        # ...while a consistent trimmed dialogue edits fine.
        trimmed = sample_nodes()[:1]
        trimmed[0]["choices"] = []
        edited = dict(VALID_DIALOGUE, nodes=trimmed)
        self.service.configure("dialogue.test.elder", edited)
        stored = self.service.find("dialogue.test.elder")
        assert stored is not None
        self.assertEqual(len(stored.data.get("nodes", [])), 1)

        self.service.delete("dialogue.test.elder")
        self.assertIsNone(self.service.find("dialogue.test.elder"))

    def test_create_normalizes_namespace_and_rejects_mismatch(self) -> None:
        created = self.service.create_dialogue(
            "test.named", dict(VALID_DIALOGUE, id="dialogue.test.named"))
        self.assertEqual(created.definition_id, "dialogue.test.named")
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.service.create_dialogue(
                "test.other", dict(VALID_DIALOGUE, id="dialogue.test.mismatch"))

    def test_create_rejects_broken_chains(self) -> None:
        broken_nodes = sample_nodes()
        broken_nodes[1]["id"] = "renamed"
        data = dict(VALID_DIALOGUE, id="dialogue.test.broken", nodes=broken_nodes)
        with self.assertRaisesRegex(ValueError, "unknown node"):
            self.service.create_dialogue("test.broken", data)

        broken_entry = dict(VALID_DIALOGUE, id="dialogue.test.brokenentry",
                            entryNodeId="missing")
        with self.assertRaisesRegex(ValueError, "entry node"):
            self.service.create_dialogue("test.brokenentry", broken_entry)

    def test_create_rejects_unknown_quest_and_shop_targets(self) -> None:
        nodes = sample_nodes()
        nodes[0]["choices"][0]["actions"] = [
            {"kind": "startQuest", "targetId": "quest.test.missing"}]
        with self.assertRaisesRegex(ValueError, "quest does not exist"):
            self.service.create_dialogue(
                "test.badquest", dict(VALID_DIALOGUE, id="dialogue.test.badquest",
                                      nodes=nodes))

        nodes = sample_nodes()
        nodes[0]["choices"][0]["actions"] = [
            {"kind": "openShop", "targetId": "shop.test.missing"}]
        with self.assertRaisesRegex(ValueError, "shop does not exist"):
            self.service.create_dialogue(
                "test.badshop", dict(VALID_DIALOGUE, id="dialogue.test.badshop",
                                     nodes=nodes))

    def test_create_rejects_empty_flag_condition(self) -> None:
        nodes = sample_nodes()
        nodes[0]["choices"][0]["conditions"] = [{"kind": "flagSet", "flagId": ""}]
        with self.assertRaisesRegex(ValueError, "non-empty flag"):
            self.service.create_dialogue(
                "test.badflag", dict(VALID_DIALOGUE, id="dialogue.test.badflag",
                                     nodes=nodes))

    def test_delete_blocked_when_npc_references_dialogue(self) -> None:
        self.service.create_dialogue("test.used", dict(VALID_DIALOGUE,
                                                       id="dialogue.test.used"))
        from tools.content_studio.services.npc_authoring_service import (
            NpcAuthoringService,
        )

        npc_service = NpcAuthoringService(self.workspace)
        npc = dict(id="npc.test.talker", visualSetId="visual.npc.scholar",
                   interaction={"x": -14, "y": -28, "width": 28, "height": 22},
                   interactionEnabled=True,
                   defaultDialogueId="dialogue.test.used", tags=["npc"])
        npc_service.create_npc("test.talker", npc)

        with self.assertRaisesRegex(ValueError, "referenced by NPCs"):
            self.service.delete("dialogue.test.used")

    def test_quests_and_shops_referenced(self) -> None:
        self.service.create_dialogue("test.links", dict(VALID_DIALOGUE,
                                                        id="dialogue.test.links"))
        self.assertEqual(self.service.quests_referenced("dialogue.test.links"),
                         ("quest.test.awakening",))
        self.assertEqual(self.service.shops_referenced("dialogue.test.links"), ())


class DialogueLibraryWidgetTests(unittest.TestCase):
    def test_widget_lists_and_details(self) -> None:
        from PySide6.QtWidgets import QApplication

        _ = QApplication.instance() or QApplication([])
        from tools.content_studio.services.dialogue_authoring_service import (
            DialogueAuthoringService,
        )
        from tools.content_studio.ui.dialogue_library_widget import (
            DialogueLibraryWidget,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = make_workspace(Path(directory))
            service = DialogueAuthoringService(workspace)
            service.create_dialogue("test.shown",
                                    dict(VALID_DIALOGUE, id="dialogue.test.shown"))

            widget = DialogueLibraryWidget(workspace)
            widget.refresh()
            self.assertEqual(widget.dialogues_list.count(), 1)
            widget.dialogues_list.setCurrentRow(0)
            self.assertIn("dialogue.test.shown", widget.details.text())


if __name__ == "__main__":
    unittest.main()
