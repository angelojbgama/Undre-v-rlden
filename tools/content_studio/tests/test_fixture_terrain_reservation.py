from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from tools.content_studio.formats.umap import (
    load_map,
    write_map,
)
from tools.content_studio.interaction.map_editing_service import (
    MapEditingService,
)
from tools.content_studio.model.map_document import (
    MapDocument,
)
from tools.content_studio.model.tile_semantics import (
    TerrainProfile,
    TerrainSelection,
)
from tools.content_studio.services.door_placement_service import (
    DoorPlacementService,
)
from tools.content_studio.services.terrain_painting_service import (
    TerrainPaintingService,
)
from tools.content_studio.services.tile_semantic_catalog import (
    TileSemanticCatalog,
)
from tools.content_studio.tests.test_door_authoring_service import (
    workspace_from,
)
from tools.content_studio.tests.test_door_placement_service import (
    placement_content,
    wall_document,
)


WALL = TerrainSelection(
    "terrain.test",
    "wall",
)

FLOOR = TerrainSelection(
    "terrain.test",
    "floor",
)


class PredictableResolver:
    """Deterministic resolver for reservation integration tests."""

    def resolve(
        self,
        family,
        role,
        position,
        active,
        map_id,
        seed=0,
        tile_size=16,
    ):
        source_index = (
            1
            if role == "floor"
            else 0
        )

        return SimpleNamespace(
            tileset_id="tileset.test",
            source_index=source_index,
            flags=0,
            empty=False,
        )


def make_context(
    root: Path,
):
    workspace = workspace_from(
        placement_content(),
        root,
    )

    document = wall_document()

    return (
        workspace,
        document,
    )


def place_gate(
    document,
    workspace,
):
    return DoorPlacementService(
        document,
        workspace,
    ).place(
        "object.gate",
        (3, 0),
        preferred_layer_index=1,
    )


def painter_for(
    document,
    workspace,
    layer: int = 1,
):
    editing = MapEditingService(
        document,
        workspace=workspace,
    )

    editing.set_layer(
        layer
    )

    return TerrainPaintingService(
        document=document,
        workspace=workspace,
        editing=editing,
        catalog=TileSemanticCatalog(
            workspace
        ),
        resolver=PredictableResolver(),
    )


class FixtureTerrainReservationTests(
    unittest.TestCase
):
    def test_gate_reserves_exact_wall_footprint(
        self,
    ) -> None:
        from tools.content_studio.services.fixture_reservation_service import (
            FixtureTerrainReservationService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace, document = make_context(
                Path(directory)
            )

            placed = place_gate(
                document,
                workspace,
            )

            service = FixtureTerrainReservationService(
                document,
                workspace,
            )

            self.assertEqual(
                {
                    (2, 0),
                    (3, 0),
                    (4, 0),
                },
                service.reserved_cells(
                    "wall"
                ),
            )

            self.assertEqual(
                set(),
                service.reserved_cells(
                    "floor"
                ),
            )

            reservations = (
                service.reservations(
                    "wall"
                )
            )

            self.assertEqual(
                1,
                len(reservations),
            )

            reservation = (
                reservations[0]
            )

            self.assertEqual(
                placed.object_id,
                reservation.owner_id,
            )

            self.assertEqual(
                "object.gate",
                reservation.definition_id,
            )

            self.assertEqual(
                (
                    (2, 0),
                    (3, 0),
                    (4, 0),
                ),
                reservation.cells,
            )

    def test_regular_object_does_not_reserve_terrain(
        self,
    ) -> None:
        from tools.content_studio.services.fixture_reservation_service import (
            FixtureTerrainReservationService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace, document = make_context(
                Path(directory)
            )

            document.add_entity(
                "objects",
                "object.crate",
                56,
                16,
            )

            service = FixtureTerrainReservationService(
                document,
                workspace,
            )

            self.assertEqual(
                set(),
                service.reserved_cells(
                    "wall"
                ),
            )

    def test_smart_wall_cannot_paint_over_gate(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, document = make_context(
                Path(directory)
            )

            place_gate(
                document,
                workspace,
            )

            wall = document.layers[
                1
            ]["cells"]

            wall[1] = None
            wall[5] = None

            result = painter_for(
                document,
                workspace,
            ).paint_terrain(
                {
                    (1, 0),
                    (2, 0),
                    (3, 0),
                    (4, 0),
                    (5, 0),
                },
                WALL,
                layer_index=1,
            )

            self.assertTrue(
                result.changed
            )

            wall = document.layers[
                1
            ]["cells"]

            self.assertIsNotNone(
                wall[1]
            )

            self.assertIsNone(
                wall[2]
            )

            self.assertIsNone(
                wall[3]
            )

            self.assertIsNone(
                wall[4]
            )

            self.assertIsNotNone(
                wall[5]
            )

    def test_gate_reservation_is_virtual_wall_for_autotile_topology(
        self,
    ) -> None:
        class RecordingResolver:
            def __init__(
                self,
            ) -> None:
                self.calls = []

            def resolve(
                self,
                family,
                role,
                position,
                active,
                map_id,
                seed=0,
                tile_size=16,
            ):
                self.calls.append(
                    (
                        position,
                        set(active),
                    )
                )

                return SimpleNamespace(
                    tileset_id="tileset.test",
                    source_index=0,
                    flags=0,
                    empty=False,
                )

        with tempfile.TemporaryDirectory() as directory:
            workspace, document = make_context(
                Path(directory)
            )

            place_gate(
                document,
                workspace,
            )

            editing = MapEditingService(
                document,
                workspace=workspace,
            )

            editing.set_layer(
                1
            )

            resolver = RecordingResolver()

            painter = TerrainPaintingService(
                document=document,
                workspace=workspace,
                editing=editing,
                catalog=TileSemanticCatalog(
                    workspace
                ),
                resolver=resolver,
            )

            painter.paint_terrain(
                {
                    (1, 0),
                    (2, 0),
                    (3, 0),
                    (4, 0),
                    (5, 0),
                },
                WALL,
                layer_index=1,
            )

            topology = next(
                active
                for position, active
                in resolver.calls
                if position
                == (1, 0)
            )

            self.assertTrue(
                {
                    (2, 0),
                    (3, 0),
                    (4, 0),
                }.issubset(
                    topology
                )
            )

            for x in (
                2,
                3,
                4,
            ):
                self.assertIsNone(
                    document.layers[
                        1
                    ]["cells"][x]
                )

    def test_room_gate_reservation_is_virtual_boundary_for_topology(
        self,
    ) -> None:
        class RecordingResolver(
            PredictableResolver
        ):
            def __init__(
                self,
            ) -> None:
                self.calls = []

            def resolve(
                self,
                family,
                role,
                position,
                active,
                map_id,
                seed=0,
                tile_size=16,
            ):
                self.calls.append(
                    (
                        role,
                        position,
                        set(active),
                    )
                )

                return super().resolve(
                    family,
                    role,
                    position,
                    active,
                    map_id,
                    seed,
                    tile_size,
                )

        with tempfile.TemporaryDirectory() as directory:
            workspace, document = make_context(
                Path(directory)
            )

            place_gate(
                document,
                workspace,
            )

            editing = MapEditingService(
                document,
                workspace=workspace,
            )

            editing.set_layer(
                1
            )

            resolver = RecordingResolver()

            painter = TerrainPaintingService(
                document=document,
                workspace=workspace,
                editing=editing,
                catalog=TileSemanticCatalog(
                    workspace
                ),
                resolver=resolver,
            )

            profile = TerrainProfile(
                "terrain.test.room",
                FLOOR,
                WALL,
            )

            painter.paint_room(
                (0, 0),
                (
                    document.width - 1,
                    document.height - 1,
                ),
                profile,
                layer_index=1,
            )

            topology = next(
                active
                for role, position, active
                in resolver.calls
                if role == "wall"
                and position == (1, 0)
            )

            self.assertTrue(
                {
                    (2, 0),
                    (3, 0),
                    (4, 0),
                }.issubset(
                    topology
                )
            )

    def test_fill_wall_respects_gate_reservation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, document = make_context(
                Path(directory)
            )

            place_gate(
                document,
                workspace,
            )

            result = painter_for(
                document,
                workspace,
            ).fill_terrain(
                (0, 1),
                WALL,
                layer_index=1,
            )

            self.assertTrue(
                result.changed
            )

            cells = document.layers[
                1
            ]["cells"]

            self.assertIsNone(
                cells[2]
            )

            self.assertIsNone(
                cells[3]
            )

            self.assertIsNone(
                cells[4]
            )

            self.assertIsNotNone(
                cells[
                    document.width
                ]
            )

    def test_smart_room_boundary_respects_gate_reservation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace, document = make_context(
                Path(directory)
            )

            place_gate(
                document,
                workspace,
            )

            profile = TerrainProfile(
                "terrain.test.room",
                FLOOR,
                WALL,
            )

            result = painter_for(
                document,
                workspace,
            ).paint_room(
                (0, 0),
                (
                    document.width - 1,
                    document.height - 1,
                ),
                profile,
                layer_index=1,
            )

            self.assertTrue(
                result.changed
            )

            cells = document.layers[
                1
            ]["cells"]

            self.assertIsNotNone(
                cells[1]
            )

            self.assertIsNone(
                cells[2]
            )

            self.assertIsNone(
                cells[3]
            )

            self.assertIsNone(
                cells[4]
            )

            self.assertIsNotNone(
                cells[5]
            )

    def test_deleting_gate_removes_reservation(
        self,
    ) -> None:
        from tools.content_studio.services.fixture_reservation_service import (
            FixtureTerrainReservationService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace, document = make_context(
                Path(directory)
            )

            placed = place_gate(
                document,
                workspace,
            )

            service = FixtureTerrainReservationService(
                document,
                workspace,
            )

            self.assertEqual(
                3,
                len(
                    service.reserved_cells(
                        "wall"
                    )
                ),
            )

            document.delete_entity(
                "objects",
                placed.object_id,
            )

            self.assertEqual(
                set(),
                service.reserved_cells(
                    "wall"
                ),
            )

    def test_reservation_survives_umap_save_reload_without_duplicate_metadata(
        self,
    ) -> None:
        from tools.content_studio.services.fixture_reservation_service import (
            FixtureTerrainReservationService,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace, document = make_context(
                root / "content"
            )

            place_gate(
                document,
                workspace,
            )

            target = (
                root
                / "fixture.umap"
            )

            write_map(
                target,
                document.data,
            )

            loaded = load_map(
                target
            )

            self.assertIsNotNone(
                loaded.data
            )

            assert loaded.data is not None

            self.assertNotIn(
                "fixtureReservations",
                loaded.data,
            )

            reloaded = MapDocument(
                loaded.data
            )

            service = FixtureTerrainReservationService(
                reloaded,
                workspace,
            )

            self.assertEqual(
                {
                    (2, 0),
                    (3, 0),
                    (4, 0),
                },
                service.reserved_cells(
                    "wall"
                ),
            )

    def test_undo_redo_dynamically_updates_reservation(
        self,
    ) -> None:
        from tools.content_studio.services.fixture_reservation_service import (
            FixtureTerrainReservationService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace, document = make_context(
                Path(directory)
            )

            place_gate(
                document,
                workspace,
            )

            service = FixtureTerrainReservationService(
                document,
                workspace,
            )

            expected = {
                (2, 0),
                (3, 0),
                (4, 0),
            }

            self.assertEqual(
                expected,
                service.reserved_cells(
                    "wall"
                ),
            )

            self.assertTrue(
                document.undo()
            )

            self.assertEqual(
                set(),
                service.reserved_cells(
                    "wall"
                ),
            )

            self.assertTrue(
                document.redo()
            )

            self.assertEqual(
                expected,
                service.reserved_cells(
                    "wall"
                ),
            )


if __name__ == "__main__":
    unittest.main()
