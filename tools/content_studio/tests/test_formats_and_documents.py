from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES, decode_content, write_content
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.formats.umap import decode_map, load_map, new_map, write_map
from tools.content_studio.formats.uworld import decode_world, write_world
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.model.world_project import WorldProject
from tools.content_studio.services.toolchain import CppToolchain
from tools.content_studio.services.autosave import autosave


REPOSITORY = Path(__file__).resolve().parents[3]
FIXTURES = REPOSITORY / "tests" / "fixtures"


def content_root(*definitions: tuple[str, dict[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {"format": "dungeon-underworld-content", "version": 5}
    result.update({category: [] for category in CONTENT_CATEGORIES})
    for category, definition in definitions:
        result[category].append(definition)  # type: ignore[union-attr]
    return result


class FormatTests(unittest.TestCase):
    def test_content_v4_round_trip_preserves_unknown_future_to_python_fields(self) -> None:
        source = FIXTURES / "phase16-content-v4" / "content.json"
        original = json.loads(source.read_text(encoding="utf-8"))
        decoded = decode_content(source)
        self.assertIsNotNone(decoded.data)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "content.json"
            write_content(target, decoded.data or {})
            self.assertEqual(original, json.loads(target.read_text(encoding="utf-8")))

    def test_umap_v3_round_trip_preserves_map_semantics(self) -> None:
        source = FIXTURES / "phase16-map-v3.umap"
        original = json.loads(source.read_text(encoding="utf-8"))
        decoded = load_map(source)
        self.assertIsNotNone(decoded.data)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "map.umap"
            write_map(target, decoded.data or {})
            self.assertEqual(original, json.loads(target.read_text(encoding="utf-8")))

    def test_world_round_trip_keeps_map_order_and_links(self) -> None:
        map_data = new_map("map.a", 2, 2)
        map_data["playerSpawns"] = [{"id": "spawn.a", "position": {"x": 8, "y": 8}, "facing": "down"}]
        world = {"format": "dungeon-underworld-world-project", "version": 1, "entryMapId": "map.a", "maps": [map_data]}
        decoded = decode_world(world)
        self.assertFalse([issue for issue in decoded.diagnostics if issue.is_error])
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "world.uworld"
            write_world(target, world)
            self.assertEqual(world, json.loads(target.read_text(encoding="utf-8")))

    def test_invalid_json_is_reported_without_partial_model(self) -> None:
        decoded = decode_map({"format": "wrong"})
        self.assertIsNotNone(decoded.data)
        self.assertTrue(any(issue.code == "missing_field" for issue in decoded.diagnostics))


class ContentAuthoringTests(unittest.TestCase):
    def make_workspace(self, data: dict[str, object]) -> tuple[tempfile.TemporaryDirectory[str], ContentWorkspace]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "content.json").write_text(encode_json(data), encoding="utf-8")
        return temporary, ContentWorkspace.open(root)

    def test_authored_enemy_index_survives_unrelated_invalid_definition(self) -> None:
        temporary, workspace = self.make_workspace(content_root(
            ("enemies", {"id": "enemy.slime", "visualSetId": "", "behaviorProfileId": "", "attackIds": [], "rewardProfileId": None}),
            ("quests", {"id": "quest.broken", "title": "Broken", "objectives": [{"target": "missing"}]}),
        ))
        self.addCleanup(temporary.cleanup)
        self.assertEqual(["enemy.slime"], [item.definition_id for item in workspace.definitions("enemies")])

    def test_invalid_enemy_remains_visible_but_is_not_locally_placeable(self) -> None:
        temporary, workspace = self.make_workspace(content_root(
            ("enemies", {"id": "enemy.broken", "visualSetId": "visual.missing", "behaviorProfileId": "", "attackIds": [], "rewardProfileId": None}),
        ))
        self.addCleanup(temporary.cleanup)
        definition = workspace.find("enemies", "enemy.broken")
        self.assertIsNotNone(definition)
        self.assertTrue(any(issue.code == "missing_dependency" for issue in workspace.validate_local(definition)))  # type: ignore[arg-type]

    def test_search_and_category_are_typed(self) -> None:
        temporary, workspace = self.make_workspace(content_root(
            ("enemies", {"id": "enemy.slime", "visualSetId": "", "behaviorProfileId": "", "attackIds": [], "rewardProfileId": None}),
            ("objects", {"id": "object.chest", "visualSetId": ""}),
            ("pickups", {"id": "pickup.slime_drop", "visualId": ""}),
        ))
        self.addCleanup(temporary.cleanup)
        self.assertEqual(["enemy.slime"], [item.definition_id for item in workspace.definitions("enemies", "slime")])
        self.assertEqual([], workspace.definitions("objects", "slime"))
        self.assertEqual(["pickup.slime_drop"], [item.definition_id for item in workspace.definitions("pickups", "slime")])

    def test_builtin_origin_survives_index_rebuild(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "builtin.json"
            source.write_text(encode_json(content_root(("enemies", {"id": "enemy.training", "visualSetId": "", "behaviorProfileId": "", "attackIds": [], "rewardProfileId": None}))), encoding="utf-8")
            workspace = ContentWorkspace.from_builtin_json(source)
            definition = workspace.find("enemies", "enemy.training")
            self.assertEqual("builtin", definition.origin)  # type: ignore[union-attr]
            workspace.update(definition, "visualSetId", "")  # type: ignore[arg-type]
            self.assertEqual("builtin", workspace.find("enemies", "enemy.training").origin)  # type: ignore[union-attr]

    def test_definition_edit_undo_redo_and_save(self) -> None:
        temporary, workspace = self.make_workspace(content_root(("enemies", {"id": "enemy.slime", "visualSetId": ""})))
        self.addCleanup(temporary.cleanup)
        definition = workspace.find("enemies", "enemy.slime")
        self.assertIsNotNone(definition)
        workspace.update(definition, "visualSetId", "visual.enemy.slime")  # type: ignore[arg-type]
        self.assertEqual("visual.enemy.slime", workspace.find("enemies", "enemy.slime").data["visualSetId"])  # type: ignore[union-attr]
        self.assertTrue(workspace.undo())
        self.assertEqual("", workspace.find("enemies", "enemy.slime").data["visualSetId"])  # type: ignore[union-attr]
        self.assertTrue(workspace.redo())
        workspace.save_all()
        self.assertEqual("visual.enemy.slime", json.loads((Path(temporary.name) / "content.json").read_text(encoding="utf-8"))["enemies"][0]["visualSetId"])


class MapAuthoringTests(unittest.TestCase):
    def test_blank_map_has_no_implicit_enemy_or_player_spawn(self) -> None:
        document = MapDocument.new("map.blank", 8, 6)
        self.assertEqual([], document.data["enemies"])
        self.assertEqual([], document.data["playerSpawns"])

    def test_entity_placement_repeat_ids_and_undo_redo(self) -> None:
        document = MapDocument.new("map.place", 8, 8)
        first = document.add_entity("enemies", "enemy.slime", 16, 32, "left")
        second = document.add_entity("enemies", "enemy.slime", 32, 32, "left")
        self.assertEqual(2, len(document.data["enemies"]))
        self.assertNotEqual(first, second)
        self.assertEqual("enemy.slime", document.entity("enemies", first)["definitionId"])  # type: ignore[index]
        self.assertEqual({"x": 16, "y": 32}, document.entity("enemies", first)["position"])  # type: ignore[index]
        self.assertTrue(document.undo())
        self.assertEqual(1, len(document.data["enemies"]))
        self.assertTrue(document.redo())
        self.assertEqual(2, len(document.data["enemies"]))
        document.move_entity("enemies", first, 48, 64)
        self.assertEqual({"x": 48, "y": 64}, document.entity("enemies", first)["position"])  # type: ignore[index]
        document.delete_entity("enemies", first)
        self.assertIsNone(document.entity("enemies", first))

    def test_project_preserves_map_order_and_active_map(self) -> None:
        project = WorldProject.new("map.a", 2, 2)
        project.add_map(MapDocument.new("map.b", 3, 3))
        project.select_map("map.b")
        self.assertEqual(["map.a", "map.b"], [document.map_id for document in project.maps])
        self.assertEqual("map.b", project.active_map.map_id)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "world.uworld"
            project.save_as(target)
            reopened, diagnostics = WorldProject.open(target)
            self.assertFalse([issue for issue in diagnostics if issue.is_error])
            self.assertIsNotNone(reopened)
            self.assertEqual(["map.a", "map.b"], [document.map_id for document in reopened.maps])  # type: ignore[union-attr]

    def test_autosave_does_not_clear_dirty_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            project = WorldProject.new("map.autosave", 2, 2)
            project.save_as(Path(directory) / "project.uworld")
            project.active_map.add_entity("enemies", "enemy.slime", 8, 8)
            written = autosave(project, None)
            self.assertEqual(1, len(written))
            self.assertTrue(project.has_unsaved_changes())
            self.assertTrue(written[0].is_file())


class CppCompatibilityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.content_check = REPOSITORY / "build" / "linux" / "content_check"
        self.map_compile = REPOSITORY / "build" / "linux" / "map_compile"
        self.world_compile = REPOSITORY / "build" / "linux" / "world_compile"
        if not all(path.is_file() for path in (self.content_check, self.map_compile, self.world_compile)):
            self.skipTest("C++ compatibility tools have not been built")

    def test_python_round_trip_is_accepted_by_cpp_tools(self) -> None:
        content_source = FIXTURES / "phase16-content-v4" / "content.json"
        map_source = FIXTURES / "phase16-map-v3.umap"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = ContentWorkspace.open(content_source.parent)
            content.save_all()
            copied_content = root / "content"
            copied_content.mkdir()
            (copied_content / "content.json").write_text(encode_json(content.files[0].data), encoding="utf-8")
            copied_map = root / "map.umap"
            document, diagnostics = MapDocument.open(map_source)
            self.assertIsNotNone(document)
            self.assertFalse([issue for issue in diagnostics if issue.is_error])
            document.save(copied_map)  # type: ignore[union-attr]
            validation = subprocess.run([str(self.content_check), str(copied_content)], capture_output=True, text=True, check=False)
            self.assertEqual(0, validation.returncode, validation.stderr)
            compiled = subprocess.run([str(self.map_compile), "--content", str(copied_content), str(copied_map), str(root / "map.dmap")], capture_output=True, text=True, check=False)
            self.assertEqual(0, compiled.returncode, compiled.stderr)

    def test_multi_map_world_is_compiled_by_cpp_world_tool(self) -> None:
        document, diagnostics = MapDocument.open(FIXTURES / "phase16-map-v3.umap")
        self.assertIsNotNone(document)
        self.assertFalse([issue for issue in diagnostics if issue.is_error])
        project = WorldProject([document], document.map_id)  # type: ignore[arg-type]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            world = root / "project.uworld"
            world.write_text(json.dumps(project.authored_data(), ensure_ascii=False, indent=2), encoding="utf-8")
            output = root / "dmap"
            result = subprocess.run([str(self.world_compile), "--content", str(FIXTURES / "phase16-content-v4"), str(world), str(output)], capture_output=True, text=True, check=False)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertTrue(any(output.glob("*.dmap")))


if __name__ == "__main__":
    unittest.main()
