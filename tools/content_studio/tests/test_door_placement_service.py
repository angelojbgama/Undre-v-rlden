from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.tests.test_door_authoring_service import (
    gate_content,
    workspace_from,
)


def placement_content() -> dict[str, object]:
    data = gate_content()

    data["tilesets"] = [
        {
            "id": "tileset.test",
            "displayName": "Test",
            "imageId": "image.tiles",
            "tileSize": 16,
            "columns": 2,
            "rows": 1,
        },
    ]

    data["tileSemantics"] = [
        {
            "id": "semantic.test.wall",
            "tilesetId": "tileset.test",
            "sourceIndex": 0,
            "family": "terrain.test",
            "role": "wall",
            "topology": "straightHorizontal",
            "north": "masonry",
            "east": "masonry",
            "south": "floor",
            "west": "masonry",
            "preferredLayer": "Wall",
            "flipXAllowed": False,
            "variantWeight": 1,
        },
        {
            "id": "semantic.test.floor",
            "tilesetId": "tileset.test",
            "sourceIndex": 1,
            "family": "terrain.test",
            "role": "floor",
            "topology": "interior",
            "north": "floor",
            "east": "floor",
            "south": "floor",
            "west": "floor",
            "preferredLayer": "Ground",
            "flipXAllowed": False,
            "variantWeight": 1,
        },
    ]

    return data


def wall_document() -> MapDocument:
    document = MapDocument.new(
        "map.door-placement",
        7,
        4,
        16,
    )

    cell_count = (
        document.width
        * document.height
    )

    document.layers[0]["name"] = "Ground"

    document.layers[0]["cells"] = [
        None
        for _ in range(cell_count)
    ]

    document.layers.append({
        "name": "Wall",
        "visible": True,
        "cells": [
            None
            for _ in range(cell_count)
        ],
    })

    wall_reference = document.tile_reference(
        "tileset.test",
        0,
    )

    floor_reference = document.tile_reference(
        "tileset.test",
        1,
    )

    ground = document.layers[0]["cells"]
    wall = document.layers[1]["cells"]

    assert isinstance(
        ground,
        list,
    )

    assert isinstance(
        wall,
        list,
    )

    for index in range(cell_count):
        ground[index] = floor_reference

    for x in range(document.width):
        index = x

        wall[index] = wall_reference

        document.data["collision"][index] = 1

        document.data.setdefault(
            "collisionBindings",
            [],
        ).append({
            "layer": 1,
            "x": x,
            "y": 0,
            "tilesetId": "tileset.test",
            "sourceIndex": 0,
            "flags": 0,
        })

    return document


class DoorPlacementServiceTests(
        unittest.TestCase
):
    def test_plan_finds_wall_layer_and_centers_three_tile_gate(
            self,
    ) -> None:
        from tools.content_studio.services.door_placement_service import (
            DoorPlacementService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                placement_content(),
                Path(directory),
            )

            document = wall_document()

            plan = DoorPlacementService(
                document,
                workspace,
            ).plan(
                "object.gate",
                (3, 0),
                preferred_layer_index=0,
            )

            self.assertEqual(
                1,
                plan.layer_index,
            )

            self.assertEqual(
                (
                    (2, 0),
                    (3, 0),
                    (4, 0),
                ),
                plan.cells,
            )

            self.assertEqual(
                (56, 16),
                plan.position,
            )

            self.assertEqual(
                "horizontal",
                plan.orientation,
            )

    def test_place_clears_only_wall_footprint_and_is_one_undo(
            self,
    ) -> None:
        from tools.content_studio.services.door_placement_service import (
            DoorPlacementService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                placement_content(),
                Path(directory),
            )

            document = wall_document()

            before = document.snapshot()

            wall_reference = (
                document.layers[1]["cells"][0]
            )

            result = DoorPlacementService(
                document,
                workspace,
            ).place(
                "object.gate",
                (3, 0),
                preferred_layer_index=0,
            )

            self.assertEqual(
                1,
                result.object_id,
            )

            wall = document.layers[1]["cells"]

            self.assertEqual(
                wall_reference,
                wall[1],
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

            self.assertEqual(
                wall_reference,
                wall[5],
            )

            placed = document.entity(
                "objects",
                result.object_id,
            )

            self.assertIsNotNone(
                placed
            )

            assert placed is not None

            self.assertEqual(
                "object.gate",
                placed["definitionId"],
            )

            self.assertEqual(
                {
                    "x": 56,
                    "y": 16,
                },
                placed["position"],
            )

            self.assertEqual(
                [],
                placed["initialContents"],
            )

            self.assertEqual(
                "persistent",
                placed["persistence"],
            )

            self.assertNotIn(
                "door",
                placed,
            )

            for x in (
                2,
                3,
                4,
            ):
                self.assertEqual(
                    0,
                    document.data["collision"][x],
                )

            remaining_bindings = (
                document.data.get(
                    "collisionBindings",
                    [],
                )
            )

            self.assertFalse(
                any(
                    isinstance(value, dict)
                    and value.get("layer") == 1
                    and value.get("y") == 0
                    and value.get("x") in {
                        2,
                        3,
                        4,
                    }
                    for value
                    in remaining_bindings
                )
            )

            self.assertTrue(
                document.undo()
            )

            self.assertEqual(
                before,
                document.data,
            )

            self.assertFalse(
                document.undo()
            )

    def test_non_wall_footprint_is_rejected_without_mutation(
            self,
    ) -> None:
        from tools.content_studio.services.door_placement_service import (
            DoorPlacementService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                placement_content(),
                Path(directory),
            )

            document = wall_document()

            floor_reference = document.find_tile_reference(
                "tileset.test",
                1,
            )

            assert floor_reference is not None

            document.layers[1]["cells"][3] = (
                floor_reference
            )

            before = document.snapshot()

            with self.assertRaisesRegex(
                ValueError,
                "semantic wall",
            ):
                DoorPlacementService(
                    document,
                    workspace,
                ).place(
                    "object.gate",
                    (3, 0),
                )

            self.assertEqual(
                before,
                document.data,
            )

            self.assertEqual(
                [],
                document.data["objects"],
            )

    def test_footprint_cannot_extend_past_map_width(
            self,
    ) -> None:
        from tools.content_studio.services.door_placement_service import (
            DoorPlacementService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                placement_content(),
                Path(directory),
            )

            document = wall_document()

            before = document.snapshot()

            with self.assertRaisesRegex(
                ValueError,
                "outside the map",
            ):
                DoorPlacementService(
                    document,
                    workspace,
                ).place(
                    "object.gate",
                    (0, 0),
                )

            self.assertEqual(
                before,
                document.data,
            )


if __name__ == "__main__":
    unittest.main()
