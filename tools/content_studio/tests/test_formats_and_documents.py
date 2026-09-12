from __future__ import annotations

import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from pathlib import Path

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES, decode_content, write_content
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.formats.umap import decode_map, load_map, new_map, write_map
from tools.content_studio.formats.uworld import decode_world, write_world
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.model.authored_entity_index import AuthoredEntityIndex
from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.model.world_project import WorldProject
from tools.content_studio.model.content_authoring import DefinitionRepository, ReferenceIndex
from tools.content_studio.interaction.command_coordinator import CommandCoordinator
from tools.content_studio.interaction.drag_payload import StudioDragPayload
from tools.content_studio.interaction.interaction_controller import InteractionController
from tools.content_studio.interaction.map_editing_service import MapEditingService
from tools.content_studio.interaction.selection_controller import SelectionController
from tools.content_studio.model.tile_semantics import TerrainSelection
from tools.content_studio.model.scene_timeline import (
    add_clip, add_marker, add_track, evaluate_preview, fit_duration, move_clip,
    new_scene, validate_scene,
)
from tools.content_studio.services.toolchain import (
    CppToolchain, PlaytestService, find_cpp_tool,
)
from tools.content_studio.services.autosave import autosave
from tools.content_studio.services.preferences import load_preferences, save_preferences
from tools.content_studio.services.import_service import ImageDimensions, ImportService, TilesetImporter, TilesetImportRequest, calculate_grid
from tools.content_studio.model.types import ContentReference, ToolResult
from tools.content_studio.model.types import ProjectPreferences


REPOSITORY = Path(__file__).resolve().parents[3]
FIXTURES = REPOSITORY / "tests" / "fixtures"


def content_root(*definitions: tuple[str, dict[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {"format": "dungeon-underworld-content", "version": 5}
    result.update({category: [] for category in CONTENT_CATEGORIES})
    for category, definition in definitions:
        result[category].append(definition)  # type: ignore[union-attr]
    return result


class FormatTests(unittest.TestCase):
    def test_map_folders_round_trip_as_tooling_preferences(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "settings.json"
            preferences = ProjectPreferences(map_folders={
                "/project/world.uworld": {"map.forest.1": "Floresta"}})
            save_preferences(preferences, path)

            reopened = load_preferences(path)

            self.assertEqual(
                "Floresta",
                reopened.map_folders["/project/world.uworld"]["map.forest.1"])

    def test_content_workspace_can_open_a_single_authored_json_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "content.json"
            source.write_text(encode_json(content_root()), encoding="utf-8")
            workspace = ContentWorkspace.open(source)
            self.assertEqual(root, workspace.root)
            self.assertEqual([], workspace.diagnostics)
            self.assertEqual([], workspace.definitions())

    def test_content_v4_round_trip_preserves_unknown_future_to_python_fields(self) -> None:
        source = FIXTURES / "phase16-content-v4" / "content.json"
        original = json.loads(source.read_text(encoding="utf-8"))
        decoded = decode_content(source)
        self.assertIsNotNone(decoded.data)
        self.assertFalse([issue for issue in decoded.diagnostics if issue.is_error])
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

    def test_umap_scene_round_trip_preserves_timeline_fields(self) -> None:
        data = new_map("map.scene", 4, 4)
        data["scenes"] = [new_scene("scene.intro", 90)]
        data["scenes"][0]["tracks"][0]["clips"].append({  # type: ignore[index]
            "kind": "move", "actorSlot": "player", "startTick": 10,
            "durationTicks": 20, "targetPosition": {"x": 16, "y": 16},
            "facing": "down", "emote": "surprise", "heightPixels": 0,
            "waitForCompletion": False,
        })  # type: ignore[index]
        decoded = decode_map(data)
        self.assertFalse([issue for issue in decoded.diagnostics if issue.is_error])
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "scene.umap"
            write_map(target, data)
            self.assertEqual(data, json.loads(target.read_text(encoding="utf-8")))

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

    def test_valid_enemy_candidate_is_placeable_with_unrelated_workspace_error(self) -> None:
        data = content_root(
            ("enemyVisuals", {"id": "visual.slime", "idle": {}}),
            ("behaviors", {"id": "behavior.slime"}),
            ("attacks", {"id": "attack.slime"}),
            ("rewardProfiles", {"id": "reward.slime"}),
            ("enemies", {"id": "enemy.slime", "visualSetId": "visual.slime", "behaviorProfileId": "behavior.slime",
                         "faction": "enemy", "maximumHealth": 3, "movementSpeedSubpixelsPerTick": 1,
                         "collisionBody": {"offsetX": -4, "offsetY": -4, "width": 8, "height": 8},
                         "hurtbox": {"offsetX": -6, "offsetY": -12, "width": 12, "height": 12},
                         "attackIds": ["attack.slime"], "rewardProfileId": "reward.slime"}),
            ("quests", {"id": "quest.broken", "rewardGrantId": "missing.reward"}),
        )
        data["unrelatedGlobalError"] = True
        temporary, workspace = self.make_workspace(data)
        self.addCleanup(temporary.cleanup)
        candidate = AuthoredEntityIndex(workspace).find("enemies", "enemy.slime")
        self.assertIsNotNone(candidate)
        self.assertTrue(candidate.placeable)  # type: ignore[union-attr]
        self.assertTrue(workspace.diagnostics)

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
            with self.assertRaises(ValueError):
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

    def test_generic_repository_duplicate_and_rename_updates_references(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = content_root(
                ("enemies", {"id": "enemy.slime", "visualSetId": "", "behaviorProfileId": "", "attackIds": [], "rewardProfileId": None}),
                ("authoringDescriptors", {"definitionId": "enemy.slime", "displayName": "Slime", "category": "enemy", "tags": []}),
            )
            (root / "content.json").write_text(encode_json(content), encoding="utf-8")
            workspace = ContentWorkspace.open(root)
            repository = DefinitionRepository(workspace)
            enemy = repository.find(ContentReference("enemies", "enemy.slime"))
            self.assertIsNotNone(enemy)
            copy_definition = repository.duplicate(enemy, "enemy.slime.copy")  # type: ignore[arg-type]
            self.assertEqual("enemy.slime.copy", copy_definition.definition_id)
            renamed = repository.rename(copy_definition, "enemy.slime.renamed")
            self.assertEqual("enemy.slime.renamed", renamed.definition_id)
            self.assertTrue(workspace.dirty)
            workspace.save_all()
            saved = json.loads((root / "content.json").read_text(encoding="utf-8"))
            self.assertIn("enemy.slime.renamed", [value["id"] for value in saved["enemies"]])


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

    def test_all_authored_entity_categories_share_typed_placement(self) -> None:
        document = MapDocument.new("map.categories", 8, 8)
        for index, category in enumerate(("enemies", "npcs", "objects", "pickups"), start=1):
            identifier = document.add_entity(category, f"{category}.{index}", index * 8, index * 8)
            self.assertEqual(f"{category}.{index}", document.entity(category, identifier)["definitionId"])  # type: ignore[index]

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

    def test_removing_entry_map_promotes_a_neighbor_and_preserves_active_map(self) -> None:
        project = WorldProject.new("map.a", 2, 2)
        project.add_map(MapDocument.new("map.b", 2, 2))
        project.add_map(MapDocument.new("map.c", 2, 2))
        project.select_map("map.c")

        project.remove_map("map.a")

        self.assertEqual(["map.b", "map.c"], [document.map_id for document in project.maps])
        self.assertEqual("map.b", project.entry_map_id)
        self.assertEqual("map.c", project.active_map.map_id)
        self.assertTrue(project.dirty)

    def test_removing_active_entry_map_selects_and_promotes_neighbor(self) -> None:
        project = WorldProject.new("map.a", 2, 2)
        project.add_map(MapDocument.new("map.b", 2, 2))
        project.set_entry_map("map.b")

        project.remove_map("map.b")

        self.assertEqual(["map.a"], [document.map_id for document in project.maps])
        self.assertEqual("map.a", project.entry_map_id)
        self.assertEqual("map.a", project.active_map.map_id)

    def test_world_project_does_not_remove_its_only_map(self) -> None:
        project = WorldProject.new("map.only", 2, 2)
        with self.assertRaisesRegex(ValueError, "at least one map"):
            project.remove_map("map.only")

    def test_map_properties_resize_grid_and_rename_cross_map_references(self) -> None:
        edited = MapDocument.new("map.old", 3, 2, 16)
        edited.set_tile(0, 1, 1, "tileset.test", 7)
        edited.set_tile(0, 2, 1, "tileset.test", 8)
        edited.set_collision([(1, 1), (2, 1)], True)
        linked = MapDocument.new("map.linked", 2, 2, 16)
        linked.add_link("link.to-old", 0, 0, 16, 16, "map.old", "spawn.start")
        project = WorldProject([edited, linked], "map.old")

        project.set_map_properties("map.old", "map.renamed", 2, 3, 24)

        self.assertEqual("map.renamed", edited.map_id)
        self.assertEqual((2, 3, 24), (edited.width, edited.height, edited.tile_size))
        self.assertEqual(6, len(edited.layers[0]["cells"]))
        self.assertIsNotNone(edited.layers[0]["cells"][3])
        self.assertEqual(1, edited.data["collision"][3])
        self.assertEqual("map.renamed", project.entry_map_id)
        self.assertEqual("map.renamed", linked.data["links"][0]["targetMapId"])

    def test_map_properties_reject_duplicate_id_before_editing(self) -> None:
        first = MapDocument.new("map.a", 2, 2)
        second = MapDocument.new("map.b", 2, 2)
        project = WorldProject([first, second], "map.a")

        with self.assertRaisesRegex(ValueError, "already exists"):
            project.set_map_properties("map.a", "map.b", 4, 4, 16)

        self.assertEqual(("map.a", 2, 2), (first.map_id, first.width, first.height))

    def test_autosave_does_not_clear_dirty_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            project = WorldProject.new("map.autosave", 2, 2)
            project.save_as(Path(directory) / "project.uworld")
            project.active_map.add_entity("enemies", "enemy.slime", 8, 8)
            written = autosave(project, None)
            self.assertEqual(1, len(written))
            self.assertTrue(project.has_unsaved_changes())
            self.assertTrue(written[0].is_file())

    def test_playtest_rejects_map_without_player_spawn(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = ContentWorkspace.new(Path(directory) / "content")
            project = WorldProject.new("map.no_spawn", 2, 2)
            success, diagnostics = PlaytestService(CppToolchain(Path(directory))).start(project, workspace)
            self.assertFalse(success)
            self.assertTrue(any(issue.code == "missing_player_spawn" for issue in diagnostics))

    def test_scene_timeline_editing_and_preview_preserve_authored_contract(self) -> None:
        scene = new_scene("scene.test", 120)
        track = 0
        add_clip(scene, track, {"kind": "move", "actorSlot": "player", "startTick": 7,
                                "durationTicks": 23, "targetPosition": {"x": 32, "y": 16}})
        add_clip(scene, track, {"kind": "face", "actorSlot": "player", "startTick": 65,
                                "durationTicks": 99, "facing": "left"})
        add_marker(scene, "arrival", 73)
        self.assertEqual(5, scene["tracks"][track]["clips"][0]["startTick"])  # type: ignore[index]
        self.assertEqual(0, scene["tracks"][track]["clips"][1]["durationTicks"])  # type: ignore[index]
        move_clip(scene, track, 0, 35)
        self.assertEqual({"x": 32, "y": 16}, evaluate_preview(scene, 40)["actors"][0]["position"])  # type: ignore[index]
        self.assertEqual(75, fit_duration(scene))
        self.assertFalse([issue for issue in validate_scene(scene) if issue.is_error])

    def test_content_collection_entries_are_structured_and_undoable(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "content.json").write_text(encode_json(content_root(("dialogues", {"id": "dialogue.test", "entryNodeId": "node.1", "nodes": []}))), encoding="utf-8")
        workspace = ContentWorkspace.open(root)
        self.addCleanup(temporary.cleanup)
        definition = workspace.find("dialogues", "dialogue.test")
        self.assertIsNotNone(definition)
        workspace.mutate_collection(definition, "nodes", "add")  # type: ignore[arg-type]
        definition = workspace.find("dialogues", "dialogue.test")
        workspace.mutate_collection(definition, "nodes", "add")  # type: ignore[arg-type]
        definition = workspace.find("dialogues", "dialogue.test")
        workspace.mutate_collection(definition, "nodes", "remove_at:0")  # type: ignore[arg-type]
        self.assertEqual(1, len(workspace.find("dialogues", "dialogue.test").data["nodes"]))  # type: ignore[union-attr]
        self.assertTrue(workspace.undo())
        self.assertEqual(2, len(workspace.find("dialogues", "dialogue.test").data["nodes"]))  # type: ignore[union-attr]
        self.assertTrue(workspace.undo())
        self.assertEqual(1, len(workspace.find("dialogues", "dialogue.test").data["nodes"]))  # type: ignore[union-attr]
        self.assertTrue(workspace.undo())
        self.assertEqual([], workspace.find("dialogues", "dialogue.test").data["nodes"])  # type: ignore[union-attr]


class InteractionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.document = MapDocument.new("map.interaction", 5, 4)
        self.editing = MapEditingService(self.document)
        self.selection = SelectionController()
        self.controller = InteractionController(self.editing, self.selection)

    def test_contextual_tile_paint_erase_rectangle_and_fill(self) -> None:
        self.controller.set_active_payload(StudioDragPayload.tile("tileset.test", 1))
        self.controller.press("left", (0, 0), (0, 0))
        self.controller.move(frozenset({"left"}), (1, 0))
        self.controller.release("left", (1, 0))
        self.assertEqual([0, 0], self.document.layers[0]["cells"][:2])
        self.controller.press("left", (2, 0), (32, 0))
        self.controller.release("left", (2, 0))
        self.assertIsNotNone(self.document.layers[0]["cells"][2])
        self.controller.press("right", (1, 0), (16, 0))
        self.assertIsNone(self.document.layers[0]["cells"][1])
        self.controller.press("left", (2, 1), (32, 16), frozenset({"shift"}))
        self.controller.release("left", (3, 2), frozenset({"shift"}))
        self.assertEqual(6, sum(value is not None for value in self.document.layers[0]["cells"]))
        self.controller.press("left", (4, 3), (64, 48), frozenset({"ctrl"}))
        self.assertEqual(self.document.width * self.document.height, sum(value is not None for value in self.document.layers[0]["cells"]))

    def test_tile_erase_tool_erases_selected_rectangle_in_one_operation(self) -> None:
        self.controller.set_active_payload(StudioDragPayload.tile("tileset.test", 1))
        for y in range(2):
            for x in range(3):
                self.controller.press("left", (x, y), (x * 16, y * 16))
                self.controller.release("left", (x, y))
        self.controller.set_tile_erase_mode(True)

        pressed = self.controller.press("right", (0, 0), (0, 0))
        moved = self.controller.move(frozenset({"right"}), (1, 1))
        self.assertFalse(pressed.changed)
        self.assertFalse(moved.changed)
        self.assertIsNotNone(self.document.layers[0]["cells"][0])
        self.assertEqual("erase_rectangle_preview", pressed.status)

        released = self.controller.release("right", (1, 1))
        self.assertTrue(released.changed)
        self.assertIsNone(self.document.layers[0]["cells"][0])
        self.assertIsNone(self.document.layers[0]["cells"][1])
        self.assertIsNotNone(self.document.layers[0]["cells"][2])
        self.assertIsNone(self.document.layers[0]["cells"][5])
        self.assertIsNone(self.document.layers[0]["cells"][6])
        self.assertIsNotNone(self.document.layers[0]["cells"][7])
        self.assertTrue(self.document.undo())
        self.assertIsNotNone(self.document.layers[0]["cells"][0])
        self.assertIsNotNone(self.document.layers[0]["cells"][6])

    def test_smart_terrain_rectangle_keeps_mode_when_release_has_no_shift(self) -> None:
        class RecordingPainter:
            def __init__(self) -> None:
                self.cells: set[tuple[int, int]] = set()

            def paint_terrain(self, cells, selection, erase=False):
                del selection, erase
                self.cells = set(cells)
                return SimpleNamespace(changed=True, warnings=())

        painter = RecordingPainter()
        self.controller.set_terrain_painter(painter)  # type: ignore[arg-type]
        self.controller.set_terrain_selection(TerrainSelection("terrain.test", "wall"))
        self.controller.press("left", (1, 1), (16, 16))
        self.controller.move(frozenset({"left"}), (3, 2), frozenset())
        result = self.controller.release("left", (3, 2), frozenset())
        self.assertTrue(result.changed)
        self.assertEqual({(x, y) for y in range(1, 3) for x in range(1, 4)}, painter.cells)

    def test_collision_is_bound_to_the_tile_layer_and_removed_with_the_tile(self) -> None:
        self.document.add_layer("Wall")
        self.document.set_tile(1, 2, 1, "tileset.wall", 7)
        self.editing.set_layer(1)
        self.editing.set_collision([(2, 1)], True)
        self.assertEqual(1, self.document.data["collision"][7])
        self.assertEqual([{"layer": 1, "x": 2, "y": 1, "tilesetId": "tileset.wall", "sourceIndex": 7, "flags": 0}],
                         self.document.data["collisionBindings"])

        self.editing.erase_tiles([(2, 1)])
        self.assertIsNone(self.document.layers[1]["cells"][7])
        self.assertEqual(0, self.document.data["collision"][7])
        self.assertEqual([], self.document.data["collisionBindings"])

    def test_collision_binding_follows_layer_reorder_and_layer_removal(self) -> None:
        self.document.add_layer("Wall")
        self.document.set_tile(1, 2, 1, "tileset.wall", 7)
        self.editing.set_layer(1)
        self.editing.set_collision([(2, 1)], True)

        self.document.move_layer(1, 0)
        self.assertEqual(0, self.document.data["collisionBindings"][0]["layer"])
        self.document.remove_layer(0)

        self.assertEqual(0, self.document.data["collision"][7])
        self.assertEqual([], self.document.data["collisionBindings"])

    def test_clearing_one_layer_keeps_collision_from_another_layer(self) -> None:
        self.document.add_layer("Wall")
        self.document.set_tile(0, 2, 1, "tileset.floor", 3)
        self.document.set_tile(1, 2, 1, "tileset.wall", 7)
        self.editing.set_layer(0)
        self.editing.set_collision([(2, 1)], True)
        self.editing.set_layer(1)
        self.editing.set_collision([(2, 1)], True)
        self.editing.set_collision([(2, 1)], False)

        self.assertEqual(1, self.document.data["collision"][7])
        self.assertEqual(1, len(self.document.data["collisionBindings"]))
        self.assertEqual(0, self.document.data["collisionBindings"][0]["layer"])

    def test_contextual_collision_left_right_rectangle_and_fill(self) -> None:
        self.document.set_tiles(0, ((x, y) for y in range(self.document.height)
                                    for x in range(self.document.width)), "tileset.test", 1)
        self.controller.set_collision_overlay(True)
        self.controller.press("left", (0, 0), (0, 0))
        self.assertEqual(1, self.document.data["collision"][0])
        self.controller.press("right", (0, 0), (0, 0))
        self.assertEqual(0, self.document.data["collision"][0])
        self.controller.press("left", (1, 1), (16, 16), frozenset({"shift"}))
        self.controller.release("left", (2, 2), frozenset({"shift"}))
        self.assertEqual(4, sum(self.document.data["collision"]))
        self.controller.press("left", (0, 0), (0, 0), frozenset({"ctrl"}))
        self.assertEqual(self.document.width * self.document.height, sum(self.document.data["collision"]))

    def test_collision_paint_on_a_blank_cell_does_not_create_an_orphan(self) -> None:
        self.document.set_collision([(1, 1)], True, 0)
        self.assertEqual(0, self.document.data["collision"][6])
        self.assertEqual([], self.document.data["collisionBindings"])

    def test_content_drop_supports_enemy_npc_object_and_pickup(self) -> None:
        for index, category in enumerate(("enemies", "npcs", "objects", "pickups")):
            result = self.controller.drop(StudioDragPayload.content(category, f"{category}.test"), (index * 16, 0))
            self.assertTrue(result.changed)
            self.assertEqual(category, self.selection.current.category)  # type: ignore[union-attr]
        for category in ("enemies", "npcs", "objects", "pickups"):
            self.assertEqual(1, len(self.document.data[category]))

    def test_map_element_drop_selects_spawn_region_and_transition(self) -> None:
        for element, category in (("player_spawn", "playerSpawns"), ("region", "regions"), ("map_transition", "links")):
            result = self.controller.drop(StudioDragPayload.map_element_payload(element), (16, 16))
            self.assertTrue(result.changed)
            self.assertEqual(category, self.selection.current.category)  # type: ignore[union-attr]
        link = self.document.data["links"][0]
        self.assertEqual("", link["targetMapId"])  # type: ignore[index]
        self.assertEqual("", link["targetSpawnId"])  # type: ignore[index]


class AuthoringInfrastructureTests(unittest.TestCase):
    def test_command_coordinator_uses_last_editing_context(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = ContentWorkspace.new(root / "content")
            definition = workspace.create_definition("enemies", "enemy.test")
            document = MapDocument.new("map.test", 2, 2)
            coordinator = CommandCoordinator()
            document.add_entity("enemies", "enemy.test", 0, 0)
            coordinator.mark("map")
            self.assertTrue(coordinator.undo(document, workspace))
            self.assertEqual([], document.data["enemies"])
            workspace.update(definition, "faction", "neutral")
            coordinator.mark("content")
            self.assertTrue(coordinator.undo(document, workspace))
            self.assertEqual("enemy", workspace.find("enemies", "enemy.test").data["faction"])  # type: ignore[union-attr]
            self.assertEqual([], ReferenceIndex(workspace).usages(ContentReference("enemies", "enemy.test")))

    def test_tileset_import_detects_grid_and_authors_metadata_without_copying(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "licensed"
            assets.mkdir()
            source = assets / "dungeon.png"
            header = b"\x89PNG\r\n\x1a\n" + b"\x00" * 8 + struct.pack(">II", 32, 48)
            source.write_bytes(header)
            workspace = ContentWorkspace.new(root / "content")
            result = ImportService().import_tileset(workspace, TilesetImportRequest(source, "tileset.dungeon", 16, 16, asset_root=assets))
            self.assertTrue(result.ok)
            self.assertEqual((2, 3), (result.columns, result.rows))
            self.assertEqual("gameAssets", result.asset_root)
            self.assertEqual("dungeon.png", result.definition.data["relativeAssetPath"])  # type: ignore[union-attr]
            self.assertNotIn("root", result.definition.data)  # type: ignore[union-attr]
            self.assertNotIn("spacing", result.definition.data)  # type: ignore[union-attr]
            self.assertTrue(source.is_file())

    def test_tileset_import_copies_external_image_into_managed_assets(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets = root / "assets"
            source = root / "downloads" / "dungeon.png"
            source.parent.mkdir()
            header = b"\x89PNG\r\n\x1a\n" + b"\x00" * 8 + struct.pack(">II", 32, 48)
            source.write_bytes(header)
            workspace = ContentWorkspace.new(root / "content" / "definitions")

            result = ImportService().import_tileset(
                workspace,
                TilesetImportRequest(source, "tileset.dungeon", 16, 16, asset_root=assets),
            )

            managed = assets / "tilesets" / "tileset.dungeon.png"
            self.assertTrue(result.ok)
            self.assertEqual("tilesets/tileset.dungeon.png", result.definition.data["relativeAssetPath"])  # type: ignore[union-attr]
            self.assertEqual(source.read_bytes(), managed.read_bytes())
            self.assertTrue(source.is_file())

    def test_tileset_import_rejects_invalid_grid(self) -> None:
        self.assertEqual((2, 3), calculate_grid(ImageDimensions(32, 48), 16, 16))
        with self.assertRaises(ValueError):
            calculate_grid(ImageDimensions(8, 8), 16, 16)
        with self.assertRaises(ValueError):
            TilesetImporter()._validate_request(TilesetImportRequest(Path("tileset.png"), "tileset.rect", 16, 8))
        with self.assertRaises(ValueError):
            TilesetImporter()._validate_request(TilesetImportRequest(Path("tileset.png"), "tileset.spaced", spacing=1))

    def test_playtest_compiles_world_but_launches_active_map(self) -> None:
        class FakeToolchain:
            def __init__(self) -> None:
                self.launched: Path | None = None

            def compile_world(self, source: Path, output: Path, content_root: Path) -> tuple[ToolResult, list[object]]:
                del source, content_root
                output.mkdir(parents=True, exist_ok=True)
                (output / "map.active.dmap").write_bytes(b"dmap")
                return ToolResult(0, "PASS", ""), []

            def launch_playtest(self, map_path: Path, content_root: Path, asset_root: Path | None, map_root: Path):
                del content_root, asset_root, map_root
                self.launched = map_path
                return _FinishedProcess()

        class _FinishedProcess:
            def poll(self) -> int:
                return 0

            def terminate(self) -> None:
                return None

        with tempfile.TemporaryDirectory() as directory:
            workspace = ContentWorkspace.new(Path(directory) / "content")
            project = WorldProject.new("map.entry", 2, 2)
            project.active_map.add_player_spawn("spawn.active", 8, 8)
            project.add_map(MapDocument.new("map.active", 2, 2, include_player_spawn=True))
            project.select_map("map.active")
            service = PlaytestService(FakeToolchain())  # type: ignore[arg-type]
            success, diagnostics = service.start(project, workspace)
            self.assertTrue(success, diagnostics)
            self.assertIsNotNone(service.toolchain.launched)  # type: ignore[attr-defined]
            self.assertIn("map.active", service.toolchain.launched.name)  # type: ignore[union-attr, attr-defined]
            service.stop()


class ToolchainResolutionTests(unittest.TestCase):
    def test_windows_resolution_never_uses_linux_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            linux = root / "build" / "linux" / "content_check"
            windows = root / "build" / "bin" / "content_check.exe"
            linux.parent.mkdir(parents=True)
            windows.parent.mkdir(parents=True)
            linux.write_text("linux", encoding="utf-8")
            windows.write_text("windows", encoding="utf-8")

            with patch("tools.content_studio.services.toolchain.os.name", "nt"):
                self.assertEqual(
                    windows.resolve(),
                    find_cpp_tool(root, "content_check"),
                )

            windows.unlink()
            with (
                patch("tools.content_studio.services.toolchain.os.name", "nt"),
                patch(
                    "tools.content_studio.services.toolchain.shutil.which",
                    return_value=None,
                ),
            ):
                self.assertIsNone(find_cpp_tool(root, "content_check"))


class CppCompatibilityTests(unittest.TestCase):
    def setUp(self) -> None:
        content_check = find_cpp_tool(REPOSITORY, "content_check")
        map_compile = find_cpp_tool(REPOSITORY, "map_compile")
        world_compile = find_cpp_tool(REPOSITORY, "world_compile")
        missing = [
            name for name, path in (
                ("content_check", content_check),
                ("map_compile", map_compile),
                ("world_compile", world_compile),
            )
            if path is None
        ]
        if missing:
            self.skipTest(
                "native C++ compatibility tools have not been built: "
                + ", ".join(missing)
            )
        assert content_check is not None
        assert map_compile is not None
        assert world_compile is not None
        self.content_check = content_check
        self.map_compile = map_compile
        self.world_compile = world_compile

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

    def test_python_scene_map_round_trip_is_accepted_by_cpp_map_compiler(self) -> None:
        source = json.loads((FIXTURES / "phase16-map-v3.umap").read_text(encoding="utf-8"))
        source["version"] = 4
        source["scenes"] = [new_scene("scene.python.compat", 60)]
        source["placementOverrides"] = []
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content_root_path = root / "content"
            shutil.copytree(FIXTURES / "phase16-content-v4", content_root_path)
            authored_map = root / "scene.umap"
            write_map(authored_map, source)
            compiled = subprocess.run(
                [str(self.map_compile), "--content", str(content_root_path), str(authored_map), str(root / "scene.dmap")],
                capture_output=True, text=True, check=False)
            self.assertEqual(0, compiled.returncode, compiled.stderr)
            self.assertTrue((root / "scene.dmap").is_file())

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

    def test_invalid_authored_content_is_rejected_by_cpp_validator(self) -> None:
        result = subprocess.run(
            [str(self.content_check), str(FIXTURES / "phase16-content-invalid-activation")],
            capture_output=True, text=True, check=False)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("missing required field", result.stderr)


if __name__ == "__main__":
    unittest.main()
