from __future__ import annotations

import unittest

from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.model.world_project import WorldProject


class ObjectTransitionProjectReferenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = MapDocument.new(
            "map.source",
            4,
            4,
        )

        self.object_id = self.source.add_entity(
            "objects",
            "object.portal.test",
            16,
            16,
        )

        self.target = MapDocument.new(
            "map.target",
            4,
            4,
        )

        self.target.add_player_spawn(
            "entry.portal",
            32,
            32,
        )

        self.source.set_object_transition_configuration(
            self.object_id,
            {
                "targetMapId": "map.target",
                "targetSpawnId": "entry.portal",
            },
        )

        self.project = WorldProject(
            [
                self.source,
                self.target,
            ],
            self.source.map_id,
        )

    def test_cross_map_validation_accepts_valid_object_transition(self) -> None:
        issues = self.project.validate_cross_map()

        self.assertFalse(
            any(
                ".transition." in issue.path
                for issue in issues
            )
        )

    def test_target_map_rename_updates_object_transition_reference(self) -> None:
        self.project.set_map_properties(
            "map.target",
            "map.destination",
            4,
            4,
            16,
        )

        placement = self.source.entity(
            "objects",
            self.object_id,
        )

        assert placement is not None
        transition = placement.get(
            "transition"
        )

        self.assertIsInstance(
            transition,
            dict,
        )

        assert isinstance(
            transition,
            dict,
        )

        self.assertEqual(
            "map.destination",
            transition["targetMapId"],
        )

        self.assertFalse(
            any(
                ".transition." in issue.path
                for issue in self.project.validate_cross_map()
            )
        )

    def test_cross_map_validation_reports_missing_transition_targets(self) -> None:
        self.source.set_object_transition_configuration(
            self.object_id,
            {
                "targetMapId": "map.missing",
                "targetSpawnId": "entry.portal",
            },
        )

        issues = self.project.validate_cross_map()

        self.assertTrue(
            any(
                issue.code == "missing_target_map"
                and issue.path.endswith(
                    ".transition.targetMapId"
                )
                for issue in issues
            )
        )

        self.source.set_object_transition_configuration(
            self.object_id,
            {
                "targetMapId": "map.target",
                "targetSpawnId": "entry.missing",
            },
        )

        issues = self.project.validate_cross_map()

        self.assertTrue(
            any(
                issue.code == "missing_target_spawn"
                and issue.path.endswith(
                    ".transition.targetSpawnId"
                )
                for issue in issues
            )
        )


if __name__ == "__main__":
    unittest.main()
