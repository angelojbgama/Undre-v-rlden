from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.content_studio.formats.content_json import (
    CONTENT_CATEGORIES,
    CONTENT_VERSION,
)
from tools.content_studio.model.content_workspace import (
    ContentWorkspace,
)


def content_root() -> dict[str, object]:
    result: dict[str, object] = {
        "format": "dungeon-underworld-content",
        "version": CONTENT_VERSION,
    }

    for category in CONTENT_CATEGORIES:
        result[category] = []

    return result


def workspace_from(
        data: dict[str, object],
        root: Path,
) -> ContentWorkspace:
    root.mkdir(
        parents=True,
        exist_ok=True,
    )

    (
        root / "content.json"
    ).write_text(
        json.dumps(
            data,
            indent=2,
        ),
        encoding="utf-8",
    )

    return ContentWorkspace.open(
        root
    )


def gate_content(
        descriptor_tags: list[str] | None = None,
) -> dict[str, object]:
    data = content_root()

    data["objects"] = [
        {
            "id": "object.gate",
            "visualSetId": "visual.object.gate",
            "door": {
                "initialState": "closed",
            },
        },
        {
            "id": "object.crate",
            "visualSetId": "visual.object.crate",
        },
    ]

    data["objectVisuals"] = [
        {
            "id": "visual.object.gate",
            "idleAnimationId": "animation.gate",
            "doorClosedAnimationId": "animation.gate",
        },
        {
            "id": "visual.object.crate",
            "idleAnimationId": "animation.crate",
        },
    ]

    data["animations"] = [
        {
            "id": "animation.gate",
            "imageId": "image.gate",
            "loop": False,
            "frames": [
                {
                    "source": {
                        "x": 0,
                        "y": 0,
                        "width": 48,
                        "height": 48,
                    },
                    "anchor": {
                        "x": 24,
                        "y": 47,
                    },
                    "drawOffset": {
                        "x": 0,
                        "y": 0,
                    },
                    "durationTicks": 12,
                    "markers": [],
                },
            ],
        },
        {
            "id": "animation.crate",
            "imageId": "image.crate",
            "loop": True,
            "frames": [
                {
                    "source": {
                        "x": 0,
                        "y": 0,
                        "width": 32,
                        "height": 32,
                    },
                    "anchor": {
                        "x": 16,
                        "y": 31,
                    },
                    "drawOffset": {
                        "x": 0,
                        "y": 0,
                    },
                    "durationTicks": 8,
                    "markers": [],
                },
            ],
        },
    ]

    data["authoringDescriptors"] = [
        {
            "definitionId": "object.gate",
            "displayName": "Gate",
            "category": "object",
            "tags": (
                descriptor_tags
                if descriptor_tags is not None
                else [
                    "door",
                    "object-source-animation:animation.gate",
                ]
            ),
        },
        {
            "definitionId": "object.crate",
            "displayName": "Crate",
            "category": "object",
            "tags": [
                "object-source-animation:animation.crate",
            ],
        },
    ]

    return data


def add_gate(
        data: dict[str, object],
        definition_id: str,
        animation_id: str,
        tags: list[str],
) -> None:
    data["objects"].append(
        {
            "id": definition_id,
            "visualSetId": f"visual.{animation_id}",
            "door": {
                "initialState": "closed",
            },
        }
    )

    data["objectVisuals"].append(
        {
            "id": f"visual.{animation_id}",
            "idleAnimationId": animation_id,
            "doorClosedAnimationId": animation_id,
        }
    )

    data["animations"].append(
        {
            "id": animation_id,
            "imageId": f"image.{animation_id}",
            "loop": False,
            "frames": [
                {
                    "source": {
                        "x": 0,
                        "y": 0,
                        "width": 48,
                        "height": 48,
                    },
                    "anchor": {
                        "x": 24,
                        "y": 47,
                    },
                    "drawOffset": {
                        "x": 0,
                        "y": 0,
                    },
                    "durationTicks": 12,
                    "markers": [],
                },
            ],
        }
    )

    data["authoringDescriptors"].append(
        {
            "definitionId": definition_id,
            "displayName": definition_id,
            "category": "object",
            "tags": tags,
        }
    )


def family_content() -> dict[str, object]:
    data = gate_content()

    add_gate(
        data,
        "object.gate.cave.iron",
        "animation.gate.cave.iron",
        [
            "door",
            "object-source-animation:animation.gate.cave.iron",
            "door-family:gate.family.cave",
        ],
    )

    add_gate(
        data,
        "object.gate.cave.wood",
        "animation.gate.cave.wood",
        [
            "door",
            "object-source-animation:animation.gate.cave.wood",
            "door-family:gate.family.cave",
        ],
    )

    return data


class DoorAuthoringServiceTests(
        unittest.TestCase
):
    def test_lists_only_objects_with_door_capability(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                gate_content(),
                Path(directory),
            )

            service = DoorAuthoringService(
                workspace
            )

            entries = service.entries(
                tile_size=16
            )

            self.assertEqual(
                ["object.gate"],
                [
                    entry.definition_id
                    for entry in entries
                ],
            )

    def test_gate_width_infers_three_tile_wall_span(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                gate_content(),
                Path(directory),
            )

            entry = DoorAuthoringService(
                workspace
            ).entry(
                "object.gate",
                tile_size=16,
            )

            self.assertIsNotNone(
                entry
            )

            assert entry is not None

            self.assertEqual(
                "Gate",
                entry.display_name,
            )

            self.assertEqual(
                "animation.gate",
                entry.source_animation_id,
            )

            self.assertEqual(
                (48, 48),
                (
                    entry.frame_width,
                    entry.frame_height,
                ),
            )

            self.assertEqual(
                "wall",
                entry.placement.mode,
            )

            self.assertEqual(
                3,
                entry.placement.span_tiles,
            )

            self.assertEqual(
                1,
                entry.placement.thickness_tiles,
            )

            self.assertEqual(
                "bottom-center",
                entry.placement.anchor,
            )

            self.assertEqual(
                ("horizontal",),
                entry.placement.orientations,
            )

    def test_descriptor_tags_override_inferred_profile(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        tags = [
            "door",
            "object-source-animation:animation.gate",
            "door-placement:wall",
            "door-span-tiles:4",
            "door-thickness-tiles:2",
            "door-anchor:bottom-center",
            "door-orientations:horizontal,vertical",
        ]

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                gate_content(tags),
                Path(directory),
            )

            entry = DoorAuthoringService(
                workspace
            ).entry(
                "object.gate",
                tile_size=16,
            )

            assert entry is not None

            self.assertEqual(
                4,
                entry.placement.span_tiles,
            )

            self.assertEqual(
                2,
                entry.placement.thickness_tiles,
            )

            self.assertEqual(
                (
                    "horizontal",
                    "vertical",
                ),
                entry.placement.orientations,
            )

    def test_rejects_invalid_placement_metadata(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        tags = [
            "door",
            "object-source-animation:animation.gate",
            "door-span-tiles:0",
        ]

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                gate_content(tags),
                Path(directory),
            )

            with self.assertRaisesRegex(
                ValueError,
                "door-span-tiles",
            ):
                DoorAuthoringService(
                    workspace
                ).entry(
                    "object.gate",
                    tile_size=16,
                )

    def test_rejects_non_positive_tile_size(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                gate_content(),
                Path(directory),
            )

            with self.assertRaisesRegex(
                ValueError,
                "tile_size",
            ):
                DoorAuthoringService(
                    workspace
                ).entries(
                    tile_size=0
                )

    def test_door_without_family_tag_remains_valid_and_unfamiliared(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                gate_content(),
                Path(directory),
            )

            service = DoorAuthoringService(
                workspace
            )

            self.assertEqual(
                [],
                service.families(tile_size=16),
            )

            entry = service.entry(
                "object.gate",
                tile_size=16,
            )

            assert entry is not None

            self.assertIsNone(
                entry.family
            )

    def test_family_tag_derives_family_id(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                family_content(),
                Path(directory),
            )

            entry = DoorAuthoringService(
                workspace
            ).entry(
                "object.gate.cave.iron",
                tile_size=16,
            )

            assert entry is not None

            assert entry.family is not None

            self.assertEqual(
                "gate.family.cave",
                entry.family.family_id,
            )

    def test_family_name_falls_back_to_last_id_component(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                family_content(),
                Path(directory),
            )

            entry = DoorAuthoringService(
                workspace
            ).entry(
                "object.gate.cave.iron",
                tile_size=16,
            )

            assert entry is not None

            assert entry.family is not None

            self.assertEqual(
                "Cave",
                entry.family.display_name,
            )

    def test_explicit_family_metadata_is_loaded(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        data = family_content()

        for descriptor in data["authoringDescriptors"]:  # type: ignore[union-attr]
            assert isinstance(descriptor, dict)

            if descriptor["definitionId"] == "object.gate.cave.iron":
                descriptor["tags"] = [
                    "door",
                    "object-source-animation:animation.gate.cave.iron",
                    "door-family:gate.family.cave",
                    "door-family-name:Cavern Gates",
                    "door-family-description:Entradas de caverna",
                    "door-family-thumbnail:Tileset/cave.png",
                    "door-family-sort-order:10",
                ]

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                data,
                Path(directory),
            )

            entry = DoorAuthoringService(
                workspace
            ).entry(
                "object.gate.cave.iron",
                tile_size=16,
            )

            assert entry is not None

            family = entry.family

            assert family is not None

            self.assertEqual(
                "gate.family.cave",
                family.family_id,
            )

            self.assertEqual(
                "Cavern Gates",
                family.display_name,
            )

            self.assertEqual(
                "Entradas de caverna",
                family.description,
            )

            self.assertEqual(
                "Tileset/cave.png",
                family.thumbnail,
            )

            self.assertEqual(
                10,
                family.sort_order,
            )

    def test_two_doors_of_same_family_yield_one_family(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                family_content(),
                Path(directory),
            )

            service = DoorAuthoringService(
                workspace
            )

            families = service.families(
                tile_size=16
            )

            self.assertEqual(
                ["gate.family.cave"],
                [
                    family.family_id
                    for family in families
                ],
            )

    def test_families_sorted_by_sort_order_name_and_id(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        data = family_content()

        add_gate(
            data,
            "object.gate.castle",
            "animation.gate.castle",
            [
                "door",
                "object-source-animation:animation.gate.castle",
                "door-family:gate.family.castle",
                "door-family-sort-order:5",
            ],
        )

        add_gate(
            data,
            "object.gate.mines",
            "animation.gate.mines",
            [
                "door",
                "object-source-animation:animation.gate.mines",
                "door-family:gate.family.mines",
            ],
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                data,
                Path(directory),
            )

            families = DoorAuthoringService(
                workspace
            ).families(
                tile_size=16
            )

            self.assertEqual(
                [
                    "gate.family.castle",
                    "gate.family.cave",
                    "gate.family.mines",
                ],
                [
                    family.family_id
                    for family in families
                ],
            )

    def test_conflicting_family_metadata_is_rejected(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        data = family_content()

        for descriptor in data["authoringDescriptors"]:  # type: ignore[union-attr]
            assert isinstance(descriptor, dict)

            if descriptor["definitionId"] == "object.gate.cave.iron":
                descriptor["tags"] = [
                    "door",
                    "object-source-animation:animation.gate.cave.iron",
                    "door-family:gate.family.cave",
                    "door-family-name:Cave",
                ]

            if descriptor["definitionId"] == "object.gate.cave.wood":
                descriptor["tags"] = [
                    "door",
                    "object-source-animation:animation.gate.cave.wood",
                    "door-family:gate.family.cave",
                    "door-family-name:Castle",
                ]

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                data,
                Path(directory),
            )

            with self.assertRaisesRegex(
                ValueError,
                "conflicting door family metadata: "
                "gate.family.cave",
            ):
                DoorAuthoringService(
                    workspace
                ).entries(
                    tile_size=16
                )

    def test_invalid_family_sort_order_is_rejected(
            self,
    ) -> None:
        from tools.content_studio.services.door_authoring_service import (
            DoorAuthoringService,
        )

        for invalid in ("abc", "-1"):
            data = family_content()

            for descriptor in data["authoringDescriptors"]:  # type: ignore[union-attr]
                assert isinstance(descriptor, dict)

                if descriptor["definitionId"] == "object.gate.cave.iron":
                    descriptor["tags"].append(  # type: ignore[union-attr]
                        f"door-family-sort-order:{invalid}"
                    )

            with tempfile.TemporaryDirectory() as directory:
                workspace = workspace_from(
                    data,
                    Path(directory),
                )

                with self.assertRaisesRegex(
                    ValueError,
                    "door-family-sort-order",
                ):
                    DoorAuthoringService(
                        workspace
                    ).entry(
                        "object.gate.cave.iron",
                        tile_size=16,
                    )


if __name__ == "__main__":
    unittest.main()
