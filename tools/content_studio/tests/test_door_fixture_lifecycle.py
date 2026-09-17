from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path

os.environ.setdefault(
    "QT_QPA_PLATFORM",
    "offscreen",
)

try:
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover
    QApplication = None  # type: ignore[assignment]

from tools.content_studio.formats.umap import (
    load_map,
    write_map,
)
from tools.content_studio.interaction.selection_controller import (
    Selection,
)
from tools.content_studio.model.fixture_cutout import (
    read_fixture_cutout,
)
from tools.content_studio.model.map_document import (
    MapDocument,
)
from tools.content_studio.services.door_placement_service import (
    DoorPlacementService,
)
from tools.content_studio.tests.test_canvas_door_visuals import (
    visual_workspace,
)
from tools.content_studio.tests.test_door_authoring_service import (
    workspace_from,
)
from tools.content_studio.tests.test_door_placement_service import (
    placement_content,
    wall_document,
)


def gate_workspace(root: Path):
    return workspace_from(
        placement_content(),
        root,
    )


class DoorFixtureLifecycleServiceTests(
    unittest.TestCase
):
    def test_placement_persists_fixture_cutout(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = gate_workspace(
                Path(directory)
            )

            document = wall_document()

            placed = DoorPlacementService(
                document,
                workspace,
            ).place(
                "object.gate",
                (3, 0),
                preferred_layer_index=1,
            )

            cutout = read_fixture_cutout(
                document.data,
                placed.object_id,
            )

            self.assertIsNotNone(
                cutout
            )

            assert cutout is not None

            self.assertEqual(
                1,
                cutout.layer_index,
            )

            self.assertEqual(
                "wall",
                cutout.terrain_role,
            )

            self.assertEqual(
                (
                    (2, 0),
                    (3, 0),
                    (4, 0),
                ),
                tuple(
                    (
                        cell.x,
                        cell.y,
                    )
                    for cell in cutout.cells
                ),
            )

    def test_delete_restores_wall_and_collision_in_one_undo(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = gate_workspace(
                Path(directory)
            )

            document = wall_document()

            wall_reference = (
                document.layers[1][
                    "cells"
                ][0]
            )

            service = DoorPlacementService(
                document,
                workspace,
            )

            placed = service.place(
                "object.gate",
                (3, 0),
                preferred_layer_index=1,
            )

            service.delete(
                placed.object_id
            )

            self.assertIsNone(
                document.entity(
                    "objects",
                    placed.object_id,
                )
            )

            wall = document.layers[
                1
            ]["cells"]

            for x in (
                2,
                3,
                4,
            ):
                self.assertEqual(
                    wall_reference,
                    wall[x],
                )

                self.assertEqual(
                    1,
                    document.data[
                        "collision"
                    ][x],
                )

            self.assertTrue(
                document.undo()
            )

            self.assertIsNotNone(
                document.entity(
                    "objects",
                    placed.object_id,
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

            self.assertTrue(
                document.redo()
            )

            self.assertIsNone(
                document.entity(
                    "objects",
                    placed.object_id,
                )
            )

    def test_move_restores_old_wall_and_opens_new_wall(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = gate_workspace(
                Path(directory)
            )

            document = wall_document()

            wall_reference = (
                document.layers[1][
                    "cells"
                ][0]
            )

            service = DoorPlacementService(
                document,
                workspace,
            )

            placed = service.place(
                "object.gate",
                (3, 0),
                preferred_layer_index=1,
            )

            placement = document.entity(
                "objects",
                placed.object_id,
            )

            assert placement is not None

            placement[
                "persistence"
            ] = "resetOnMapEnter"

            moved = service.move(
                placed.object_id,
                (4, 0),
                preferred_layer_index=1,
            )

            self.assertEqual(
                placed.object_id,
                moved.object_id,
            )

            placement = document.entity(
                "objects",
                placed.object_id,
            )

            assert placement is not None

            self.assertEqual(
                {
                    "x": 72,
                    "y": 16,
                },
                placement["position"],
            )

            self.assertEqual(
                "resetOnMapEnter",
                placement[
                    "persistence"
                ],
            )

            wall = document.layers[
                1
            ]["cells"]

            self.assertEqual(
                wall_reference,
                wall[2],
            )

            for x in (
                3,
                4,
                5,
            ):
                self.assertIsNone(
                    wall[x]
                )

            self.assertTrue(
                document.undo()
            )

            placement = document.entity(
                "objects",
                placed.object_id,
            )

            assert placement is not None

            self.assertEqual(
                {
                    "x": 56,
                    "y": 16,
                },
                placement["position"],
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

            self.assertEqual(
                wall_reference,
                document.layers[
                    1
                ]["cells"][5],
            )

            self.assertTrue(
                document.redo()
            )

            placement = document.entity(
                "objects",
                placed.object_id,
            )

            assert placement is not None

            self.assertEqual(
                {
                    "x": 72,
                    "y": 16,
                },
                placement["position"],
            )

    def test_cutout_survives_save_reload(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = gate_workspace(
                root / "content"
            )

            document = wall_document()

            wall_reference = (
                document.layers[1][
                    "cells"
                ][0]
            )

            placed = DoorPlacementService(
                document,
                workspace,
            ).place(
                "object.gate",
                (3, 0),
                preferred_layer_index=1,
            )

            target = (
                root
                / "fixture.umap"
            )

            write_map(
                target,
                document.data,
            )

            decoded = load_map(
                target
            )

            self.assertIsNotNone(
                decoded.data
            )

            assert decoded.data is not None

            reloaded = MapDocument(
                decoded.data
            )

            cutout = read_fixture_cutout(
                reloaded.data,
                placed.object_id,
            )

            self.assertIsNotNone(
                cutout
            )

            DoorPlacementService(
                reloaded,
                workspace,
            ).delete(
                placed.object_id
            )

            for x in (
                2,
                3,
                4,
            ):
                self.assertEqual(
                    wall_reference,
                    reloaded.layers[
                        1
                    ]["cells"][x],
                )

    def test_legacy_gate_without_cutout_can_be_deleted(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            workspace = gate_workspace(
                Path(directory)
            )

            document = wall_document()

            placed = DoorPlacementService(
                document,
                workspace,
            ).place(
                "object.gate",
                (3, 0),
                preferred_layer_index=1,
            )

            overrides = document.data.get(
                "placementOverrides",
                [],
            )

            assert isinstance(
                overrides,
                list,
            )

            # Simulate a Gate authored before fixture-cutout ownership.
            overrides[:] = []

            DoorPlacementService(
                document,
                workspace,
            ).delete(
                placed.object_id
            )

            self.assertIsNone(
                document.entity(
                    "objects",
                    placed.object_id,
                )
            )

            for x in (
                2,
                3,
                4,
            ):
                self.assertIsNotNone(
                    document.layers[
                        1
                    ]["cells"][x]
                )


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class DoorFixtureLifecycleCanvasTests(
    unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_visible_gate_sprite_is_selectable(
        self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = visual_workspace(
                root
            )

            document = wall_document()

            placed = DoorPlacementService(
                document,
                workspace,
            ).place(
                "object.gate",
                (3, 0),
                preferred_layer_index=1,
            )

            canvas = MapCanvas()

            self.addCleanup(
                canvas.close
            )

            canvas.set_context(
                document,
                workspace,
                root,
            )

            # y=0 is inside the visible 48x48 sprite, but 16 pixels
            # away from its bottom-center anchor. The old radius-based
            # selection therefore misses it.
            selection = (
                canvas._hit_selection(
                    (56, 0)
                )
            )

            self.assertEqual(
                Selection(
                    "objects",
                    placed.object_id,
                ),
                selection,
            )

    def test_canvas_delete_restores_wall(
        self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = visual_workspace(
                root
            )

            document = wall_document()

            placed = DoorPlacementService(
                document,
                workspace,
            ).place(
                "object.gate",
                (3, 0),
                preferred_layer_index=1,
            )

            canvas = MapCanvas()

            self.addCleanup(
                canvas.close
            )

            canvas.set_context(
                document,
                workspace,
                root,
            )

            selection = canvas._hit_selection(
                (56, 0)
            )

            canvas.selection_controller.select_value(
                selection
            )

            self.assertTrue(
                canvas.delete_selection()
            )

            self.assertIsNone(
                document.entity(
                    "objects",
                    placed.object_id,
                )
            )

            for x in (
                2,
                3,
                4,
            ):
                self.assertIsNotNone(
                    document.layers[
                        1
                    ]["cells"][x]
                )

    def test_canvas_move_uses_door_fixture_lifecycle(
        self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = visual_workspace(
                root
            )

            document = wall_document()

            placed = DoorPlacementService(
                document,
                workspace,
            ).place(
                "object.gate",
                (3, 0),
                preferred_layer_index=1,
            )

            canvas = MapCanvas()

            self.addCleanup(
                canvas.close
            )

            canvas.set_context(
                document,
                workspace,
                root,
            )

            canvas._moving = Selection(
                "objects",
                placed.object_id,
            )

            # Snapped world x=64 corresponds to target wall tile x=4.
            canvas._move_selection(
                (64, 0)
            )

            placement = document.entity(
                "objects",
                placed.object_id,
            )

            assert placement is not None

            self.assertEqual(
                {
                    "x": 72,
                    "y": 16,
                },
                placement["position"],
            )

            self.assertIsNotNone(
                document.layers[
                    1
                ]["cells"][2]
            )

            for x in (
                3,
                4,
                5,
            ):
                self.assertIsNone(
                    document.layers[
                        1
                    ]["cells"][x]
                )

    def test_door_tool_click_existing_gate_enters_select_and_drags(
        self,
    ) -> None:
        from PySide6.QtCore import Qt
        from PySide6.QtTest import QTest
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = visual_workspace(
                root
            )

            document = wall_document()

            placed = DoorPlacementService(
                document,
                workspace,
            ).place(
                "object.gate",
                (3, 0),
                preferred_layer_index=1,
            )

            canvas = MapCanvas()

            self.addCleanup(
                canvas.close
            )

            canvas.resize(
                500,
                400,
            )

            canvas.set_context(
                document,
                workspace,
                root,
            )

            canvas.set_door_selection(
                "object.gate"
            )

            canvas.show()

            self.application.processEvents()

            start = canvas.world_to_screen(
                56,
                0,
            )

            QTest.mousePress(
                canvas,
                Qt.MouseButton.LeftButton,
                pos=start,
            )

            self.application.processEvents()

            self.assertEqual(
                "select",
                canvas.tool,
            )

            self.assertEqual(
                Selection(
                    "objects",
                    placed.object_id,
                ),
                canvas.selection_controller.current,
            )

            self.assertEqual(
                Selection(
                    "objects",
                    placed.object_id,
                ),
                canvas._moving,
            )

            destination = (
                canvas.world_to_screen(
                    64,
                    0,
                )
            )

            QTest.mouseMove(
                canvas,
                destination,
            )

            QTest.mouseRelease(
                canvas,
                Qt.MouseButton.LeftButton,
                pos=destination,
            )

            self.application.processEvents()

            placement = document.entity(
                "objects",
                placed.object_id,
            )

            assert placement is not None

            self.assertEqual(
                {
                    "x": 72,
                    "y": 16,
                },
                placement["position"],
            )


if __name__ == "__main__":
    unittest.main()
