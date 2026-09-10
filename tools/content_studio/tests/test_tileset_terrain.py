from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.formats.umap import load_map, new_map, write_map
from tools.content_studio.interaction.map_editing_service import MapEditingService
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.model.tile_semantics import TerrainProfile, TerrainSelection
from tools.content_studio.services.autotile_resolver import AutoTileResolver, EAST, NORTH, SOUTH, WEST
from tools.content_studio.services.import_service import TilesetImportRequest
from tools.content_studio.services.tile_semantic_catalog import TileSemanticCatalog
from tools.content_studio.services.terrain_painting_service import TerrainCollisionPolicy, TerrainPaintingService
from tools.content_studio.services.tileset_library import BatchTilesetImportRequest, TilesetLibrary, TilesetUsageIndex


def content_root() -> dict[str, object]:
    result: dict[str, object] = {"format": "dungeon-underworld-content", "version": 5}
    result.update({category: [] for category in CONTENT_CATEGORIES})
    return result


def semantic(semantic_id: str, tileset_id: str, source_index: int, role: str, topology: str,
             family: str = "dungeon.stone") -> dict[str, object]:
    return {"id": semantic_id, "tilesetId": tileset_id, "sourceIndex": source_index,
            "family": family, "role": role, "topology": topology,
            "north": "unknown", "east": "unknown", "south": "unknown", "west": "unknown",
            "preferredLayer": "", "flipXAllowed": False, "visualConfidence": "confirmed",
            "semanticConfidence": "probable", "gameplayConfidence": "unverified"}


def terrain_content() -> dict[str, object]:
    data = content_root()
    data["tilesets"] = [
        {"id": "tileset.floor", "displayName": "Floor", "relativeAssetPath": "floor.png", "tileSize": 16, "columns": 4, "rows": 4},
        {"id": "tileset.wall", "displayName": "Wall", "relativeAssetPath": "wall.png", "tileSize": 16, "columns": 4, "rows": 4},
        {"id": "tileset.corner", "displayName": "Corner", "relativeAssetPath": "corner.png", "tileSize": 16, "columns": 4, "rows": 4},
    ]
    data["tileSemantics"] = [
        semantic("floor.clean", "tileset.floor", 0, "floor", "interior"),
        semantic("floor.variant", "tileset.floor", 1, "floor", "interior"),
        semantic("wall.horizontal", "tileset.wall", 0, "wall", "straightHorizontal"),
        semantic("wall.vertical", "tileset.wall", 1, "wall", "straightVertical"),
        semantic("wall.cap", "tileset.wall", 2, "wall", "cap"),
        semantic("wall.interior", "tileset.wall", 3, "wall", "interior"),
        semantic("wall.corner", "tileset.corner", 0, "wall", "outerCorner"),
        semantic("wall.junction", "tileset.corner", 1, "wall", "junction"),
    ]
    return data


def workspace_from(data: dict[str, object], root: Path) -> ContentWorkspace:
    root.mkdir(parents=True, exist_ok=True)
    (root / "content.json").write_text(encode_json(data), encoding="utf-8")
    return ContentWorkspace.open(root)


def fake_png(path: Path, width: int, height: int) -> None:
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + b"\0" * 8 + struct.pack(">II", width, height))


class MultipleTilesetTests(unittest.TestCase):
    def test_same_layer_and_different_layers_keep_typed_tileset_references(self) -> None:
        document = MapDocument.new("map.mixed", 3, 2)
        document.add_layer("Walls")
        document.add_layer("Decoration")
        document.set_tile(0, 0, 0, "tileset.floor", 1)
        document.set_tile(0, 1, 0, "tileset.wall", 2)
        document.set_tile(1, 0, 0, "tileset.wall", 3)
        document.set_tile(2, 0, 0, "tileset.corner", 4)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "mixed.umap"
            write_map(target, document.data)
            loaded = load_map(target)
            self.assertIsNotNone(loaded.data)
            references = loaded.data["tileReferences"]  # type: ignore[index]
            ids = {value["tilesetId"] for value in references}  # type: ignore[union-attr]
            self.assertEqual({"tileset.floor", "tileset.wall", "tileset.corner"}, ids)
            self.assertEqual(4, sum(value is not None for layer in loaded.data["layers"] for value in layer["cells"]))  # type: ignore[index]

    def test_tile_size_mismatch_is_rejected_before_map_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = workspace_from(terrain_content(), root)
            document = MapDocument.new("map.size", 2, 2, tile_size=16)
            editing = MapEditingService(document, workspace=workspace)
            workspace.find("tilesets", "tileset.floor").data["tileSize"] = 32  # type: ignore[union-attr]
            with self.assertRaisesRegex(ValueError, "requires 16px"):
                editing.paint_tiles([(0, 0)], "tileset.floor", 0)


class TilesetLibraryTests(unittest.TestCase):
    def test_suggestion_discovery_conflicts_and_asset_root(self) -> None:
        self.assertEqual("tileset.dungeon.floor", TilesetLibrary.suggest_id(Path("Dungeon_floor.png")))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); asset_root = root / "assets"; asset_root.mkdir(); outside = root / "outside"; outside.mkdir()
            fake_png(asset_root / "dungeon_floor.png", 32, 16); fake_png(asset_root / "second.png", 16, 16); fake_png(asset_root / "nested.png", 16, 16)
            (asset_root / "folder").mkdir(); fake_png(asset_root / "folder" / "nested.png", 16, 16)
            self.assertEqual(3, len(TilesetLibrary.discover_files(asset_root)))
            self.assertEqual(4, len(TilesetLibrary.discover_files(asset_root, recursive=True)))
            workspace = workspace_from(content_root(), root / "content")
            library = TilesetLibrary(workspace)
            request = TilesetImportRequest(asset_root / "dungeon_floor.png", "tileset.dungeon.floor", asset_root=asset_root)
            self.assertTrue(library.import_batch(BatchTilesetImportRequest((request,))).imported[0].ok)
            skipped = library.import_batch(BatchTilesetImportRequest((request,), "skip"))
            self.assertTrue(any(issue.code == "tileset_conflict" for issue in skipped.diagnostics))
            bad = library.import_batch(BatchTilesetImportRequest((TilesetImportRequest(outside / "missing.png", "tileset.outside", asset_root=asset_root),)))
            self.assertTrue(any(issue.is_error for issue in bad.diagnostics))

    def test_reimport_shrink_and_safe_delete_consult_map_usage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); asset_root = root / "assets"; asset_root.mkdir(); fake_png(asset_root / "atlas.png", 64, 64)
            workspace = workspace_from(content_root(), root / "content")
            document = MapDocument.new("map.uses", 2, 2); document.set_tile(0, 0, 0, "tileset.atlas", 15)
            from tools.content_studio.model.world_project import WorldProject
            project = WorldProject.new("map.uses", 2, 2); project.maps[0] = document
            library = TilesetLibrary(workspace, project)
            initial = library.import_batch(BatchTilesetImportRequest((TilesetImportRequest(asset_root / "atlas.png", "tileset.atlas", asset_root=asset_root),)))
            self.assertTrue(initial.imported[0].ok)
            self.assertTrue(library.usage_index.is_used("tileset.atlas"))
            fake_png(asset_root / "atlas.png", 16, 16)
            shrinking = library.reimport("tileset.atlas", asset_root / "atlas.png", asset_root)
            self.assertFalse(shrinking.ok)
            self.assertTrue(any(issue.code == "tileset_reimport_used_index" for issue in shrinking.diagnostics or []))
            deleted, diagnostics = library.delete("tileset.atlas")
            self.assertFalse(deleted); self.assertTrue(any(issue.code == "tileset_in_use" for issue in diagnostics))


class SemanticAndAutotileTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.workspace = workspace_from(terrain_content(), Path(self.temporary.name))
        self.catalog = TileSemanticCatalog(self.workspace)
        self.resolver = AutoTileResolver(self.catalog)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_catalog_indexes_family_role_topology_and_reference(self) -> None:
        self.assertEqual({"tileset.floor", "tileset.wall", "tileset.corner"}, set(self.catalog.families()[0].tileset_ids))
        self.assertEqual("tileset.wall", self.catalog.by_family_role_topology("dungeon.stone", "wall", "straightHorizontal")[0].tileset_id)
        self.assertEqual("floor.clean", self.catalog.by_reference("tileset.floor", 0)[0].definition_id)
        self.workspace.find("tileSemantics", "floor.clean").data["family"] = "changed"  # type: ignore[union-attr]
        self.catalog.invalidate()
        self.assertEqual("changed", self.catalog.find("floor.clean").family)  # type: ignore[union-attr]

    def test_wall_topologies_can_come_from_three_tilesets_and_variants_are_deterministic(self) -> None:
        horizontal = self.resolver.resolve("dungeon.stone", "wall", (1, 1), {(0, 1), (2, 1)}, "map.a")
        vertical = self.resolver.resolve("dungeon.stone", "wall", (1, 1), {(1, 0), (1, 2)}, "map.a")
        corner = self.resolver.resolve("dungeon.stone", "wall", (1, 1), {(2, 1), (1, 2)}, "map.a")
        single = self.resolver.resolve("dungeon.stone", "wall", (1, 1), (), "map.a")
        floor_a = self.resolver.resolve("dungeon.stone", "floor", (2, 2), (), "map.a")
        self.assertEqual("tileset.wall", horizontal.tileset_id)  # type: ignore[union-attr]
        self.assertEqual(0, horizontal.source_index)  # type: ignore[union-attr]
        self.assertEqual("tileset.wall", vertical.tileset_id)  # type: ignore[union-attr]
        self.assertEqual("tileset.corner", corner.tileset_id)  # type: ignore[union-attr]
        self.assertEqual(2, single.source_index)  # type: ignore[union-attr]
        self.assertEqual("tileset.floor", floor_a.tileset_id)  # type: ignore[union-attr]
        self.assertEqual(self.resolver.resolve("dungeon.stone", "floor", (2, 2), (), "map.a"), floor_a)

    def test_smart_wall_and_room_are_single_undoable_operations(self) -> None:
        document = MapDocument.new("map.smart", 8, 8)
        editing = MapEditingService(document, workspace=self.workspace)
        painter = TerrainPaintingService(document, self.workspace, editing, self.catalog, self.resolver)
        result = painter.paint_terrain([(2, 2), (3, 2), (4, 2)], TerrainSelection("dungeon.stone", "wall"))
        self.assertTrue(result.changed); self.assertEqual(3, sum(value is not None for value in document.layers[0]["cells"]))
        self.assertTrue(document.undo()); self.assertEqual(0, sum(value is not None for value in document.layers[0]["cells"]))
        profile = TerrainProfile("dungeon.stone.room", TerrainSelection("dungeon.stone", "floor"), TerrainSelection("dungeon.stone", "wall"))
        room = painter.paint_room((1, 1), (5, 4), profile)
        self.assertTrue(room.changed); cells = document.layers[0]["cells"]
        self.assertEqual(20, sum(value is not None for value in cells))
        self.assertTrue(document.undo()); self.assertEqual(0, sum(value is not None for value in document.layers[0]["cells"]))

    def test_collision_policy_is_opt_in_and_part_of_the_same_gesture(self) -> None:
        document = MapDocument.new("map.collision.policy", 4, 4)
        editing = MapEditingService(document, workspace=self.workspace)
        painter = TerrainPaintingService(document, self.workspace, editing, self.catalog, self.resolver,
                                         TerrainCollisionPolicy(frozenset({"wall"})))
        result = painter.paint_terrain([(1, 1), (2, 1)], TerrainSelection("dungeon.stone", "wall"))
        self.assertTrue(result.changed); self.assertEqual([1, 1], document.data["collision"][5:7])
        self.assertTrue(document.undo()); self.assertEqual([0, 0], document.data["collision"][5:7])
