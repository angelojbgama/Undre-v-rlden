"""Regression tests for reference safety across workspace and world project.

Covers the audit findings: renames must rewrite map placements, map removal
must strip inbound links/transitions, and the playtest service must not
orphan a previous game process when started twice.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.model.world_project import WorldProject
from tools.content_studio.services.toolchain import PlaytestService, CppToolchain


def _make_workspace_with_enemy(root: Path) -> tuple[ContentWorkspace, str]:
    workspace = ContentWorkspace.open(root / "definitions")
    workspace.create_definition("enemies", "enemy.audit")
    definition = workspace.find("enemies", "enemy.audit")
    assert definition is not None
    data = dict(definition.data)
    data["visualSetId"] = ""
    data["behaviorProfileId"] = ""
    workspace.replace_definition(definition, data)
    return workspace, root / "definitions" / "content.json"


def _make_workspace_with_enemy_file(root: Path) -> ContentWorkspace:
    (root / "definitions").mkdir(parents=True, exist_ok=True)
    (root / "definitions" / "content.json").write_text(
        '{"format": "dungeon-underworld-content", "version": 7, "enemies": ['
        '{"id": "enemy.audit", "visualSetId": "", "behaviorProfileId": ""}]}',
        encoding="utf-8",
    )
    workspace = ContentWorkspace.open(root / "definitions")
    assert workspace.find("enemies", "enemy.audit") is not None
    return workspace


class ReferenceSafetyTests(unittest.TestCase):
    def test_rename_updates_map_placements(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = _make_workspace_with_enemy_file(root)
            project = WorldProject.new("map.1", 16, 16)
            document = project.maps[0]
            document.data["enemies"] = [
                {"id": 1, "definitionId": "enemy.audit",
                 "position": {"x": 32, "y": 32}, "facing": "down"}]
            workspace.world_project = project

            renamed = workspace.rename_definition(
                workspace.find("enemies", "enemy.audit"), "enemy.renamed")
            self.assertEqual(renamed.definition_id, "enemy.renamed")
            placement = document.data["enemies"][0]
            self.assertEqual(placement["definitionId"], "enemy.renamed")
            self.assertTrue(document.dirty)
            self.assertTrue(project.dirty)

    def test_remove_map_strips_inbound_links_and_transitions(self) -> None:
        project = WorldProject.new("map.1", 16, 16)
        second = __import__("tools.content_studio.model.map_document",
                            fromlist=["MapDocument"]).MapDocument.new("map.2", 16, 16)
        project.add_map(second)
        first = project.maps[0]
        second.data["links"] = [
            {"id": "link.1", "trigger": {"x": 0, "y": 0, "width": 16, "height": 16},
             "targetMapId": "map.1", "targetSpawnId": "player.start"}]
        second.data["objects"] = [
            {"id": 1, "definitionId": "object.door",
             "position": {"x": 16, "y": 16}, "initialContents": [],
             "persistence": "persistent",
             "transition": {"targetMapId": "map.1", "targetSpawnId": "player.start"}}]

        project.remove_map("map.1")

        self.assertEqual(second.data["links"], [])
        self.assertNotIn("transition", second.data["objects"][0])
        self.assertTrue(second.dirty)
        self.assertEqual(project.entry_map_id, "map.2")

    def test_find_usages_blocks_delete_of_referenced_definition(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = _make_workspace_with_enemy_file(root)
            workspace.create_definition("rewardProfiles", "reward.audit")
            reward = workspace.find("rewardProfiles", "reward.audit")
            assert reward is not None
            data = dict(reward.data)
            data["enemyId"] = "enemy.audit"
            workspace.replace_definition(reward, data)

            usages = workspace.find_usages("enemy.audit")
            self.assertTrue(any(u.category == "rewardProfiles" for u in usages))


class _FakeProcess:
    def __init__(self, exit_code: int | None = 3) -> None:
        self.terminated = False
        self.exit_code = exit_code

    @property
    def returncode(self) -> int | None:
        return self.exit_code

    def poll(self) -> int | None:
        return self.exit_code

    def terminate(self) -> None:
        self.terminated = True
        self.exit_code = -15 if self.exit_code is None else self.exit_code

    def kill(self) -> None:
        self.terminated = True

    def wait(self, timeout: float | None = None) -> int | None:
        return self.exit_code


class _FakeToolchain(CppToolchain):
    def __init__(self) -> None:
        self.process: _FakeProcess | None = None
        self.started = 0

    def launch_playtest(self, map_path, content_root, asset_root, map_root):
        self.started += 1
        self.process = _FakeProcess()
        return self.process


class PlaytestLifecycleTests(unittest.TestCase):
    def test_start_does_not_orphan_previous_process(self) -> None:
        toolchain = _FakeToolchain()
        service = PlaytestService(toolchain)
        service.process = _FakeProcess(exit_code=None)  # a game still running
        previous = service.process

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = WorldProject.new("map.1", 16, 16)
            document = project.maps[0]
            document.data["playerSpawns"] = [
                {"id": "player.start", "position": {"x": 24, "y": 24}, "facing": "down"}]
            workspace = _make_workspace_with_enemy_file(root)
            ok, issues = service.start(project, workspace)

        self.assertTrue(ok, issues)
        self.assertTrue(previous.terminated)
        self.assertEqual(toolchain.started, 1)

    def test_is_running_and_stop_record_exit_code(self) -> None:
        toolchain = _FakeToolchain()
        service = PlaytestService(toolchain)
        service.process = _FakeProcess()
        self.assertFalse(service.is_running())  # already exited (poll != None)
        service.stop()
        self.assertIsNone(service.process)
        self.assertEqual(service.last_exit_code, 3)


if __name__ == "__main__":
    unittest.main()
