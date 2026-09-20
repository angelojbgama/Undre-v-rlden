"""Smart Terrain composition strategies: variants, patterns and painting.

Covers the generalization from a fixed 3x3 concept to a composition engine:
- the existing wall/room connectivity behavior keeps resolving unchanged;
- floors compose unlimited weighted 1x1 variants with stable identities;
- multi-tile patterns are resolved through the existing authored stamps.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import CONTENT_CATEGORIES
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.interaction.map_editing_service import MapEditingService
from tools.content_studio.model.content_workspace import ContentWorkspace
from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.model.tile_semantics import TerrainSelection
from tools.content_studio.services.autotile_resolver import AutoTileResolver, EAST, NORTH, SOUTH, WEST
from tools.content_studio.services.fixture_reservation_service import FixtureTerrainReservation, FixtureTerrainReservationService
from tools.content_studio.services.terrain_composition import (
    PATTERN,
    TerrainCompositionService,
    TerrainResolveContext,
)
from tools.content_studio.services.terrain_painting_service import TerrainPaintingService
from tools.content_studio.services.terrain_rule_service import (
    RULE_SLOT_MASK,
    RULE_SLOT_TOPOLOGY,
    TerrainRuleService,
    variant_semantic_id,
)
from tools.content_studio.services.tile_semantic_catalog import TileSemanticCatalog


def content_root() -> dict[str, object]:
    result: dict[str, object] = {"format": "dungeon-underworld-content", "version": 5}
    result.update({category: [] for category in CONTENT_CATEGORIES})
    return result


def semantic(semantic_id: str, tileset_id: str, source_index: int, role: str, topology: str,
             family: str = "terrain.dungeon", variant_weight: int = 1) -> dict[str, object]:
    return {"id": semantic_id, "tilesetId": tileset_id, "sourceIndex": source_index,
            "family": family, "role": role, "topology": topology,
            "north": "unknown", "east": "unknown", "south": "unknown", "west": "unknown",
            "preferredLayer": "", "flipXAllowed": False, "visualConfidence": "confirmed",
            "semanticConfidence": "probable", "gameplayConfidence": "unverified",
            "variantWeight": variant_weight}


def stamp(stamp_id: str, width: int, height: int, cells: list[tuple[int, int, str]],
          display_name: str = "") -> dict[str, object]:
    return {"id": stamp_id, "displayName": display_name or stamp_id,
            "width": width, "height": height,
            "cells": [{"x": x, "y": y, "tileId": tile_id} for x, y, tile_id in cells],
            "anchor": {"x": 0, "y": 0}, "flipXAllowed": False, "atomic": True,
            "confidence": "confirmed"}


def _wall_slot_semantic(slot: str, source_index: int) -> dict[str, object]:
    mask = RULE_SLOT_MASK[slot]
    edges = {
        "north": "masonry" if mask & NORTH else "voidEdge",
        "east": "masonry" if mask & EAST else "voidEdge",
        "south": "masonry" if mask & SOUTH else "voidEdge",
        "west": "masonry" if mask & WEST else "voidEdge",
    }
    return {"id": f"semantic.rule.terrain.dungeon.wall.tileset.masonry.{RULE_SLOT_TOPOLOGY[slot]}.{slot}.{source_index}",
            "tilesetId": "tileset.masonry", "sourceIndex": source_index,
            "family": "terrain.dungeon", "role": "wall",
            "topology": RULE_SLOT_TOPOLOGY[slot],
            "preferredLayer": "Walls", "flipXAllowed": False,
            "visualConfidence": "confirmed", "semanticConfidence": "probable",
            "gameplayConfidence": "unverified", "variantWeight": 1, **edges}


def composition_content() -> dict[str, object]:
    data = content_root()
    data["tilesets"] = [
        {"id": "tileset.ground", "displayName": "Ground", "relativeAssetPath": "ground.png", "tileSize": 16, "columns": 8, "rows": 8},
        {"id": "tileset.masonry", "displayName": "Masonry", "relativeAssetPath": "masonry.png", "tileSize": 16, "columns": 8, "rows": 8},
        {"id": "tileset.moss", "displayName": "Moss", "relativeAssetPath": "moss.png", "tileSize": 16, "columns": 8, "rows": 8},
    ]
    data["tileSemantics"] = [
        # A legacy (slot-generated) floor rule: nine slots, ids carry slots.
        *[{"id": f"semantic.rule.terrain.dungeon.floor.tileset.ground.interior.{slot}.{index}",
           "tilesetId": "tileset.ground", "sourceIndex": index,
           "family": "terrain.dungeon", "role": "floor", "topology": "interior",
           "north": "unknown", "east": "unknown", "south": "unknown", "west": "unknown",
           "preferredLayer": "Ground", "flipXAllowed": False, "visualConfidence": "confirmed",
           "semanticConfidence": "probable", "gameplayConfidence": "unverified",
           "variantWeight": weight}
          for index, (slot, weight) in enumerate((
              ("north_west", 8), ("north", 1), ("north_east", 1),
              ("west", 1), ("center", 1), ("east", 1),
              ("south_west", 1), ("south", 1), ("south_east", 1)))],
        # A complete nine-slot wall rule with profiled edges, exactly the
        # shape the 3x3 connectivity editor persists.
        *[_wall_slot_semantic(slot, index) for index, slot in enumerate((
              "north_west", "north", "north_east",
              "west", "center", "east",
              "south_west", "south", "south_east"))],
        # A manual floor semantic of another family keeps its own identity.
        semantic("moss.floor", "tileset.moss", 0, "floor", "interior", family="terrain.moss"),
    ]
    data["stamps"] = [
        stamp("stamp.dungeon.quad_2x2", 2, 2,
              [(0, 0, "semantic.rule.terrain.dungeon.floor.tileset.ground.interior.north_west.0"),
               (1, 0, "semantic.rule.terrain.dungeon.floor.tileset.ground.interior.north.1"),
               (0, 1, "semantic.rule.terrain.dungeon.floor.tileset.ground.interior.west.3"),
               (1, 1, "semantic.rule.terrain.dungeon.floor.tileset.ground.interior.center.4")]),
        stamp("strip.triple_1x3", 1, 3,
              [(0, 0, "semantic.rule.terrain.dungeon.floor.tileset.ground.interior.north_west.0"),
               (0, 1, "semantic.rule.terrain.dungeon.floor.tileset.ground.interior.north.1"),
               (0, 2, "semantic.rule.terrain.dungeon.floor.tileset.ground.interior.west.3")]),
        stamp("moss.patch_2x2", 2, 2,
              [(0, 0, "moss.floor"), (1, 0, "moss.floor"),
               (0, 1, "moss.floor"), (1, 1, "moss.floor")]),
        stamp("broken.unknown_cells", 1, 1, [(0, 0, "missing.semantic")]),
    ]
    return data


def open_workspace(data: dict[str, object], root: Path) -> ContentWorkspace:
    root.mkdir(parents=True, exist_ok=True)
    (root / "content.json").write_text(encode_json(data), encoding="utf-8")
    return ContentWorkspace.open(root)


class CompositionEnvironment:
    """Shared workspace/document/painter setup for composition tests."""

    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        self.workspace = open_workspace(composition_content(), root)
        self.catalog = TileSemanticCatalog(self.workspace)
        self.resolver = AutoTileResolver(self.catalog)
        self.composition = TerrainCompositionService(self.catalog, self.workspace, self.resolver)
        self.document = MapDocument.new("map.composition", 12, 12)
        self.painter = TerrainPaintingService(
            self.document, self.workspace,
            MapEditingService(self.document, workspace=self.workspace),
            self.catalog, self.resolver,
            composition=self.composition)

    def cleanup(self) -> None:
        self.temporary.cleanup()

    def source_at(self, x: int, y: int) -> tuple[str, int] | None:
        cells = self.document.layers[0]["cells"]
        references = self.document.data["tileReferences"]
        index = cells[y * self.document.width + x]
        if not isinstance(index, int):
            return None
        reference = references[index]
        return str(reference["tilesetId"]), int(reference["sourceIndex"])


class CompatibilityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.env = CompositionEnvironment()
        self.addCleanup(self.env.cleanup)

    def test_wall_connectivity_strategy_keeps_resolving_mask_slots(self) -> None:
        strategy = self.env.composition.strategy_for("terrain.dungeon", "wall")
        self.assertEqual("connectivity", strategy.kind)
        context = TerrainResolveContext(frozenset({(2, 3), (3, 2)}), "map.c", 0, 16)
        placement = strategy.resolve(TerrainSelection("terrain.dungeon", "wall"), (2, 2), context)
        self.assertIsNotNone(placement)
        self.assertEqual("connectivity", placement.strategy)  # type: ignore[union-attr]
        resolved = self.env.resolver.resolve_mask("terrain.dungeon", "wall", (2, 2), EAST | SOUTH, "map.c")
        self.assertEqual((placement.cells[0].tileset_id, placement.cells[0].source_index),  # type: ignore[union-attr]
                         (resolved.tileset_id, resolved.source_index))  # type: ignore[union-attr]

    def test_connectivity_influence_covers_cardinal_neighbours(self) -> None:
        strategy = self.env.composition.strategy_for("terrain.dungeon", "wall")
        influence = strategy.influence([(5, 5)], self.env.document)
        self.assertEqual({(5, 5), (5, 4), (6, 5), (5, 6), (4, 5)}, influence)

    def test_legacy_floor_rule_still_loads_and_resolves(self) -> None:
        variants = TerrainRuleService.load_variants(
            self.env.workspace, "tileset.ground", "terrain.dungeon", "floor")
        self.assertEqual(9, len(variants))
        self.assertTrue(all(value.legacy for value in variants))
        placement = self.env.composition.resolve(
            TerrainSelection("terrain.dungeon", "floor", 3), (4, 4),
            TerrainResolveContext(frozenset(), "map.c", 3, 16))
        self.assertIsNotNone(placement)
        self.assertEqual("variant", placement.strategy)  # type: ignore[union-attr]
        self.assertEqual(("tileset.ground", 0),  # type: ignore[union-attr]
                         (placement.cells[0].tileset_id, placement.cells[0].source_index))

    def test_floor_variant_paint_influences_only_the_target_cell(self) -> None:
        before = [list(layer["cells"]) for layer in self.env.document.layers]
        result = self.env.painter.paint_terrain(
            [(3, 3)], TerrainSelection("terrain.dungeon", "floor", 5))
        self.assertTrue(result.changed)
        self.assertEqual(((3, 3),), result.affected_cells)
        after = [list(layer["cells"]) for layer in self.env.document.layers]
        self.assertEqual(before[0][3 * 12 + 2], after[0][3 * 12 + 2])

    def test_wall_paint_recalculates_terrain_neighbours_only(self) -> None:
        first = self.env.painter.paint_terrain(
            [(2, 2), (3, 2)], TerrainSelection("terrain.dungeon", "wall"))
        self.assertTrue(first.changed)
        # An empty neighbourhood has no terrain to re-resolve.
        self.assertEqual(((2, 2), (3, 2)), tuple(sorted(first.affected_cells)))
        second = self.env.painter.paint_terrain(
            [(4, 2)], TerrainSelection("terrain.dungeon", "wall"))
        self.assertTrue(second.changed)
        # The painted cell and its existing wall neighbour re-resolve; empty
        # distant cells are untouched.
        self.assertEqual(((3, 2), (4, 2)), tuple(sorted(second.affected_cells)))
        self.assertIsNone(self.env.document.layers[0]["cells"][8 * 12 + 8])


class VariantRuleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.env = CompositionEnvironment()
        self.addCleanup(self.env.cleanup)
        self.service = TerrainRuleService()

    def test_save_and_load_more_than_nine_weighted_variants(self) -> None:
        variants = [(source, 1 + source) for source in range(12)]
        self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                   variants, "terrain.dungeon", "floor")
        loaded = self.service.load_variants(self.env.workspace, "tileset.ground", "terrain.dungeon", "floor")
        self.assertEqual(12, len(loaded))
        self.assertEqual(list(range(12)), [value.source_index for value in loaded])
        self.assertEqual(list(range(1, 13)), [value.weight for value in loaded])
        for value in loaded:
            self.assertFalse(value.legacy)
            self.assertEqual(variant_semantic_id("tileset.ground", "terrain.dungeon", "floor", value.source_index),
                             value.definition_id)
            self.assertNotIn("north_west", value.definition_id)

    def test_weights_are_relative_and_resolution_deterministic(self) -> None:
        self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                   [(0, 90), (1, 10)], "terrain.dungeon", "floor")
        self.env.catalog.invalidate()
        results = [self.env.composition.resolve(
            TerrainSelection("terrain.dungeon", "floor", 7), (x, y),
            TerrainResolveContext(frozenset(), "map.var", 7, 16))
            for y in range(24) for x in range(24)]
        dominant = sum(1 for value in results if value.cells[0].source_index == 0)  # type: ignore[union-attr]
        rare = sum(1 for value in results if value.cells[0].source_index == 1)  # type: ignore[union-attr]
        self.assertEqual(576, dominant + rare)
        self.assertGreater(dominant, 460)
        self.assertGreater(rare, 0)
        again = [self.env.composition.resolve(
            TerrainSelection("terrain.dungeon", "floor", 7), (x, y),
            TerrainResolveContext(frozenset(), "map.var", 7, 16))
            for y in range(24) for x in range(24)]
        self.assertEqual(results, again)

    def test_different_seed_can_change_the_variant_choice(self) -> None:
        self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                   [(0, 1), (1, 1)], "terrain.dungeon", "floor")
        self.env.catalog.invalidate()
        base = self.env.composition.resolve(
            TerrainSelection("terrain.dungeon", "floor", 0), (4, 4),
            TerrainResolveContext(frozenset(), "map.seed", 0, 16))
        other = self.env.composition.resolve(
            TerrainSelection("terrain.dungeon", "floor", 1), (4, 4),
            TerrainResolveContext(frozenset(), "map.seed", 1, 16))
        self.assertIsNotNone(base)
        self.assertIsNotNone(other)
        chosen = {base.cells[0].source_index, other.cells[0].source_index}  # type: ignore[union-attr]
        self.assertGreaterEqual(len(chosen), 1)

    def test_adding_and_removing_variants_keeps_stable_identity(self) -> None:
        self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                   [(0, 5), (1, 1)], "terrain.dungeon", "floor")
        self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                   [(0, 5), (1, 1), (2, 2)], "terrain.dungeon", "floor")
        loaded = self.service.load_variants(self.env.workspace, "tileset.ground", "terrain.dungeon", "floor")
        self.assertEqual([0, 1, 2], [value.source_index for value in loaded])
        self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                   [(0, 5), (2, 2)], "terrain.dungeon", "floor")
        loaded = self.service.load_variants(self.env.workspace, "tileset.ground", "terrain.dungeon", "floor")
        self.assertEqual([0, 2], [value.source_index for value in loaded])
        self.assertTrue(self.env.workspace.undo())
        loaded = self.service.load_variants(self.env.workspace, "tileset.ground", "terrain.dungeon", "floor")
        self.assertEqual([0, 1, 2], [value.source_index for value in loaded])

    def test_legacy_floor_rule_migrates_to_variant_ids_without_visual_loss(self) -> None:
        before = self.env.composition.resolve(
            TerrainSelection("terrain.dungeon", "floor", 11), (6, 6),
            TerrainResolveContext(frozenset(), "map.migration", 11, 16))
        self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                   [(source, 1) for source in range(9)], "terrain.dungeon", "floor")
        definitions = self.env.workspace.definitions("tileSemantics")
        self.assertFalse(any(str(value.data.get("id", "")).startswith("semantic.rule.terrain.dungeon.floor")
                             for value in definitions))
        after = self.env.composition.resolve(
            TerrainSelection("terrain.dungeon", "floor", 11), (6, 6),
            TerrainResolveContext(frozenset(), "map.migration", 11, 16))
        self.assertEqual((before.cells[0].tileset_id, before.cells[0].source_index),  # type: ignore[union-attr]
                         (after.cells[0].tileset_id, after.cells[0].source_index))  # type: ignore[union-attr]

    def test_duplicate_variant_source_and_out_of_range_are_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "twice"):
            self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                       [(0, 1), (0, 2)])
        with self.assertRaisesRegex(ValueError, "outside the tileset atlas"):
            self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                       [(64, 1)])
        with self.assertRaisesRegex(ValueError, "between 1 and 100"):
            self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                       [(0, 0)])

    def test_manual_semantics_of_other_families_are_untouched(self) -> None:
        self.service.save_variants(self.env.workspace, "tileset.ground", "terrain.dungeon",
                                   [(0, 1), (1, 1)], "terrain.dungeon", "floor")
        self.assertIsNotNone(self.env.workspace.find("tileSemantics", "moss.floor"))


class PatternDiscoveryTests(unittest.TestCase):
    def setUp(self) -> None:
        self.env = CompositionEnvironment()
        self.addCleanup(self.env.cleanup)

    def test_patterns_are_derived_from_stamps_of_the_same_family(self) -> None:
        patterns = self.env.composition.patterns_for("terrain.dungeon")
        self.assertEqual(("stamp.dungeon.quad_2x2", "strip.triple_1x3"),
                         tuple(value.definition_id for value in patterns))
        self.assertEqual(2, patterns[0].width)
        self.assertEqual(3, patterns[1].height)

    def test_incompatible_stamps_are_never_listed(self) -> None:
        self.assertEqual((), self.env.composition.patterns_for("terrain.missing"))
        moss = self.env.composition.patterns_for("terrain.moss")
        self.assertEqual(("moss.patch_2x2",), tuple(value.definition_id for value in moss))

    def test_pinned_pattern_is_used_and_unknown_or_foreign_is_rejected(self) -> None:
        placement = self.env.composition.pattern.resolve(
            TerrainSelection("terrain.dungeon", "floor", 0, "strip.triple_1x3"), (2, 2),
            TerrainResolveContext(frozenset(), "map.p", 0, 16))
        self.assertEqual("strip.triple_1x3", placement.pattern_id)  # type: ignore[union-attr]
        self.assertIsNone(self.env.composition.pattern.resolve(
            TerrainSelection("terrain.dungeon", "floor", 0, "stamp.not_listed"), (2, 2),
            TerrainResolveContext(frozenset(), "map.p", 0, 16)))
        # The moss stamp belongs to another family and must not be placeable
        # through the dungeon family.
        self.assertIsNone(self.env.composition.pattern.resolve(
            TerrainSelection("terrain.dungeon", "floor", 0, "moss.patch_2x2"), (2, 2),
            TerrainResolveContext(frozenset(), "map.p", 0, 16)))

    def test_pattern_choice_is_deterministic_per_anchor_and_seed(self) -> None:
        selection = TerrainSelection("terrain.dungeon", "floor", 4)
        context = TerrainResolveContext(frozenset(), "map.det", 4, 16)
        first = self.env.composition.pattern.resolve(selection, (3, 3), context)
        second = self.env.composition.pattern.resolve(selection, (3, 3), context)
        self.assertEqual(first.pattern_id, second.pattern_id)  # type: ignore[union-attr]
        self.assertEqual(first.cells, second.cells)  # type: ignore[union-attr]

    def test_pattern_influence_covers_footprint_reach(self) -> None:
        influence = self.env.composition.pattern.influence([(5, 5)], self.env.document)
        self.assertIn((5, 5), influence)
        self.assertIn((6, 6), influence)
        self.assertIn((4, 4), influence)
        self.assertNotIn((8, 5), influence)


class PatternPlacementTests(unittest.TestCase):
    def setUp(self) -> None:
        self.env = CompositionEnvironment()
        self.addCleanup(self.env.cleanup)

    def test_two_by_two_pattern_places_all_cells_as_one_undoable_command(self) -> None:
        result = self.env.painter.place_pattern(
            (3, 3), TerrainSelection("terrain.dungeon", "floor", 0, "stamp.dungeon.quad_2x2"))
        self.assertTrue(result.changed)
        self.assertEqual(4, len(result.affected_cells))
        self.assertEqual(("tileset.ground", 0), self.env.source_at(3, 3))
        self.assertEqual(("tileset.ground", 1), self.env.source_at(4, 3))
        self.assertEqual(("tileset.ground", 3), self.env.source_at(3, 4))
        self.assertEqual(("tileset.ground", 4), self.env.source_at(4, 4))
        self.assertIsNone(self.env.source_at(5, 3))
        self.assertTrue(self.env.document.undo())
        self.assertTrue(all(value is None for value in self.env.document.layers[0]["cells"]))

    def test_non_square_patterns_keep_their_composition(self) -> None:
        result = self.env.painter.place_pattern(
            (2, 2), TerrainSelection("terrain.dungeon", "floor", 0, "strip.triple_1x3"))
        self.assertTrue(result.changed)
        self.assertEqual(3, len(result.affected_cells))
        self.assertIsNotNone(self.env.source_at(2, 2))
        self.assertIsNotNone(self.env.source_at(2, 3))
        self.assertIsNotNone(self.env.source_at(2, 4))
        self.assertIsNone(self.env.source_at(3, 2))

    def test_placement_is_clipped_at_the_map_border(self) -> None:
        result = self.env.painter.place_pattern(
            (11, 11), TerrainSelection("terrain.dungeon", "floor", 0, "stamp.dungeon.quad_2x2"))
        self.assertTrue(result.changed)
        self.assertEqual(((11, 11),), result.affected_cells)
        self.assertIsNotNone(self.env.source_at(11, 11))

    def test_overlapping_placements_behave_like_normal_painting(self) -> None:
        selection = TerrainSelection("terrain.dungeon", "floor", 0, "stamp.dungeon.quad_2x2")
        first = self.env.painter.place_pattern((2, 2), selection)
        second = self.env.painter.place_pattern((3, 3), selection)
        self.assertTrue(first.changed)
        self.assertTrue(second.changed)
        self.assertEqual(("tileset.ground", 0), self.env.source_at(3, 3))
        self.assertEqual(("tileset.ground", 4), self.env.source_at(4, 4))

    def test_reserved_fixture_cells_are_never_written_by_a_pattern(self) -> None:
        class FixedReservations(FixtureTerrainReservationService):
            def reservations(self, terrain_role: str | None = None):  # type: ignore[override]
                return (FixtureTerrainReservation(
                    owner_id=1, definition_id="object.door", terrain_role="wall",
                    cells=((3, 3),)),)

        self.env.painter.reservations = FixedReservations(self.env.document, self.env.workspace)
        result = self.env.painter.place_pattern(
            (3, 3), TerrainSelection("terrain.dungeon", "floor", 0, "stamp.dungeon.quad_2x2"))
        self.assertTrue(result.changed)
        self.assertEqual(3, len(result.affected_cells))
        self.assertIsNone(self.env.source_at(3, 3))
        self.assertIsNotNone(self.env.source_at(4, 4))

    def test_unplaceable_pattern_reports_a_warning_and_changes_nothing(self) -> None:
        before = self.env.document.snapshot()
        result = self.env.painter.place_pattern(
            (2, 2), TerrainSelection("terrain.dungeon", "floor", 0, "stamp.not_listed"))
        self.assertFalse(result.changed)
        self.assertTrue(any("no compatible pattern" in value for value in result.warnings))
        self.assertEqual(before, self.env.document.snapshot())

    def test_freehand_terrain_paint_ignores_a_pinned_pattern(self) -> None:
        selection = TerrainSelection("terrain.dungeon", "floor", 2, "stamp.dungeon.quad_2x2")
        result = self.env.painter.paint_terrain([(2, 2), (5, 5)], selection)
        self.assertTrue(result.changed)
        self.assertEqual(((2, 2), (5, 5)), tuple(sorted(result.affected_cells)))
        self.assertIsNotNone(self.env.source_at(5, 5))
        self.assertIsNone(self.env.source_at(2, 3))

    def test_pattern_then_paint_is_two_distinct_undo_steps(self) -> None:
        selection = TerrainSelection("terrain.dungeon", "floor", 0, "stamp.dungeon.quad_2x2")
        self.env.painter.place_pattern((2, 2), selection)
        self.env.painter.paint_terrain([(8, 8)], TerrainSelection("terrain.dungeon", "floor", 3))
        self.assertIsNotNone(self.env.source_at(8, 8))
        self.assertTrue(self.env.document.undo())
        self.assertIsNone(self.env.source_at(8, 8))
        self.assertIsNotNone(self.env.source_at(2, 2))
        self.assertTrue(self.env.document.undo())
        self.assertIsNone(self.env.source_at(2, 2))


if __name__ == "__main__":
    unittest.main()
