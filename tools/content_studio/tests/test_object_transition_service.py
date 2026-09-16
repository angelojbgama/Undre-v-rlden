from __future__ import annotations

import unittest

from tools.content_studio.model.map_document import MapDocument
from tools.content_studio.model.world_project import WorldProject
from tools.content_studio.services.object_transition_service import (
    ObjectTransitionService,
)


class ObjectTransitionServiceTests(unittest.TestCase):
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

        self.project = WorldProject(
            [
                self.source,
                self.target,
            ],
            self.source.map_id,
        )

        self.service = ObjectTransitionService(
            self.project,
            self.source,
        )

    def test_configuration_starts_disabled_and_lists_targets(self) -> None:
        config = self.service.configuration(
            self.object_id
        )

        self.assertFalse(
            config.enabled
        )

        self.assertEqual(
            (
                "map.source",
                "map.target",
            ),
            self.service.available_maps(),
        )

        self.assertEqual(
            (
                "entry.portal",
            ),
            self.service.available_spawns(
                "map.target"
            ),
        )

    def test_configure_and_clear_transition_are_undoable_map_edits(self) -> None:
        config = self.service.configure(
            self.object_id,
            enabled=True,
            target_map_id="map.target",
            target_spawn_id="entry.portal",
        )

        self.assertTrue(
            config.enabled
        )

        placement = self.source.entity(
            "objects",
            self.object_id,
        )
        assert placement is not None

        self.assertEqual(
            {
                "targetMapId": "map.target",
                "targetSpawnId": "entry.portal",
            },
            placement["transition"],
        )

        self.assertTrue(
            self.source.undo()
        )

        placement = self.source.entity(
            "objects",
            self.object_id,
        )
        assert placement is not None

        self.assertNotIn(
            "transition",
            placement,
        )

        self.assertTrue(
            self.source.redo()
        )

        self.service.configure(
            self.object_id,
            enabled=False,
        )

        placement = self.source.entity(
            "objects",
            self.object_id,
        )
        assert placement is not None

        self.assertNotIn(
            "transition",
            placement,
        )

    def test_invalid_target_spawn_is_rejected_without_mutation(self) -> None:
        with self.assertRaisesRegex(
            ValueError,
            "unknown transition target spawn",
        ):
            self.service.configure(
                self.object_id,
                enabled=True,
                target_map_id="map.target",
                target_spawn_id="entry.missing",
            )

        placement = self.source.entity(
            "objects",
            self.object_id,
        )
        assert placement is not None

        self.assertNotIn(
            "transition",
            placement,
        )


if __name__ == "__main__":
    unittest.main()
