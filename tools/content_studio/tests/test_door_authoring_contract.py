from __future__ import annotations

import struct
import unittest

from tools.content_studio.formats.dmap import (
    DMAP_MINOR,
    DmapError,
    serialize_dmap,
)
from tools.content_studio.formats.umap import (
    MAP_VERSION,
    decode_map,
    new_map,
)


def transition_map() -> dict:
    data = new_map(
        "map.transition.contract",
        4,
        4,
    )

    data["objects"] = [{
        "id": 7,
        "definitionId": "object.portal.test",
        "position": {
            "x": 16,
            "y": 16,
        },
        "initialContents": [],
        "persistence": "persistent",
        "transition": {
            "targetMapId": "map.destination",
            "targetSpawnId": "entry.portal",
        },
    }]

    return data


def door_map() -> dict:
    data = new_map(
        "map.door.contract",
        4,
        4,
    )

    data["objects"] = [{
        "id": 1,
        "definitionId": "object.door.wood",
        "position": {
            "x": 32,
            "y": 32,
        },
        "initialContents": [],
        "persistence": "resetOnMapEnter",
        "door": {
            "initialState": "locked",
            "requiredItemId": "item.key.blue",
            "consumeItem": False,
        },
    }]

    return data


class DoorPlacementContractTests(unittest.TestCase):
    def test_umap_v5_accepts_per_instance_door_configuration(self) -> None:
        self.assertEqual(
            5,
            MAP_VERSION,
        )

        data = door_map()

        decoded = decode_map(
            data
        )

        self.assertEqual(
            [],
            decoded.diagnostics,
        )

        self.assertEqual(
            "locked",
            decoded.data["objects"][0]["door"]["initialState"],
        )

        self.assertEqual(
            "item.key.blue",
            decoded.data["objects"][0]["door"]["requiredItemId"],
        )

        self.assertFalse(
            decoded.data["objects"][0]["door"]["consumeItem"],
        )

    def test_umap_v4_rejects_new_door_instance_schema(self) -> None:
        data = door_map()
        data["version"] = 4

        decoded = decode_map(
            data
        )

        self.assertTrue(
            any(
                diagnostic.code == "unsupported_version"
                and diagnostic.path == "objects[0].door"
                for diagnostic in decoded.diagnostics
            )
        )

    def test_consume_item_requires_required_item_id(self) -> None:
        data = door_map()

        data["objects"][0]["door"].pop(
            "requiredItemId"
        )

        data["objects"][0]["door"]["consumeItem"] = True

        decoded = decode_map(
            data
        )

        self.assertTrue(
            any(
                diagnostic.code == "invalid_value"
                and diagnostic.path
                == "objects[0].door.consumeItem"
                for diagnostic in decoded.diagnostics
            )
        )

        with self.assertRaisesRegex(
            DmapError,
            "consumeItem",
        ):
            serialize_dmap(
                data
            )

    def test_python_dmap_writer_emits_minor_7_and_key_reference(self) -> None:
        self.assertEqual(
            7,
            DMAP_MINOR,
        )

        payload = serialize_dmap(
            door_map()
        )

        self.assertEqual(
            b"DMAP",
            payload[:4],
        )

        major, minor = struct.unpack_from(
            "<HH",
            payload,
            4,
        )

        self.assertEqual(
            1,
            major,
        )

        self.assertEqual(
            7,
            minor,
        )

        self.assertIn(
            b"item.key.blue",
            payload,
        )

    def test_python_dmap_writer_emits_generic_object_transition_chunk(self) -> None:
        payload = serialize_dmap(
            transition_map()
        )

        self.assertIn(
            b"OTRN",
            payload,
        )

        self.assertIn(
            b"map.destination",
            payload,
        )

        self.assertIn(
            b"entry.portal",
            payload,
        )


if __name__ == "__main__":
    unittest.main()