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

from tools.content_studio.model.world_project import (
    WorldProject,
)
from tools.content_studio.tests.test_door_authoring_service import (
    workspace_from,
)
from tools.content_studio.tests.test_door_placement_service import (
    placement_content,
    wall_document,
)


def ui_content() -> dict[str, object]:
    """Complete UI fixture including dependencies validated by MainWindow."""

    data = placement_content()

    data["visualImages"] = [
        {
            "id": "image.gate",
            "root": "contentWorkspace",
            "relativePath": "gate.png",
        },
    ]

    return data


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class DoorPlacementUiTests(
        unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_door_library_place_button_activates_specialized_canvas_mode(
            self,
    ) -> None:
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                ui_content(),
                root,
            )

            project = WorldProject.new()

            window = MainWindow(
                project,
                workspace,
                asset_root=root,
            )

            self.addCleanup(
                window.close
            )

            self.assertEqual(
                1,
                window.door_library.doors.count(),
            )

            window.door_library.doors.setCurrentRow(
                0
            )

            self.assertTrue(
                window.door_library.place_button.isEnabled()
            )

            window.door_library.place_button.click()

            self.assertEqual(
                "door",
                window.map_canvas.tool,
            )

            self.assertEqual(
                "object.gate",
                window.map_canvas.selected_door_definition_id,
            )

            self.assertIsNone(
                window.map_canvas.interaction.active_payload
            )

    def test_canvas_previews_three_tile_wall_footprint_and_places_door(
            self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                ui_content(),
                root,
            )

            document = wall_document()

            canvas = MapCanvas()

            self.addCleanup(
                canvas.close
            )

            canvas.set_context(
                document,
                workspace,
                root,
            )

            # Deliberately leave the Ground layer selected.
            # DoorPlacementService must discover the Wall layer.
            canvas.set_layer(
                0
            )

            canvas.set_door_selection(
                "object.gate"
            )

            self.assertTrue(
                canvas.preview_door_at(
                    (3, 0)
                )
            )

            self.assertEqual(
                (
                    (2, 0),
                    (3, 0),
                    (4, 0),
                ),
                canvas.renderer.door_preview_cells,
            )

            self.assertTrue(
                canvas.renderer.door_preview_valid
            )

            self.assertEqual(
                (56, 16),
                canvas.renderer.door_preview_world,
            )

            selection = canvas.place_door_at(
                (3, 0)
            )

            self.assertIsNotNone(
                selection
            )

            assert selection is not None

            self.assertEqual(
                (
                    "objects",
                    1,
                ),
                selection.as_tuple(),
            )

            self.assertEqual(
                (
                    "objects",
                    1,
                ),
                canvas.selection_controller
                .current
                .as_tuple(),
            )

            placed = document.entity(
                "objects",
                1,
            )

            self.assertIsNotNone(
                placed
            )

            assert placed is not None

            self.assertEqual(
                {
                    "x": 56,
                    "y": 16,
                },
                placed["position"],
            )

            wall = document.layers[1]["cells"]

            self.assertIsNone(
                wall[2]
            )

            self.assertIsNone(
                wall[3]
            )

            self.assertIsNone(
                wall[4]
            )

            # Door mode stays active so several doors can be placed.
            self.assertEqual(
                "door",
                canvas.tool,
            )

    def test_invalid_wall_preview_keeps_full_footprint_and_turns_red(
            self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                ui_content(),
                root,
            )

            document = wall_document()

            floor_reference = (
                document.find_tile_reference(
                    "tileset.test",
                    1,
                )
            )

            assert floor_reference is not None

            document.layers[1]["cells"][3] = (
                floor_reference
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

            canvas.set_door_selection(
                "object.gate"
            )

            self.assertFalse(
                canvas.preview_door_at(
                    (3, 0)
                )
            )

            self.assertEqual(
                (
                    (2, 0),
                    (3, 0),
                    (4, 0),
                ),
                canvas.renderer.door_preview_cells,
            )

            self.assertFalse(
                canvas.renderer.door_preview_valid
            )

            self.assertIn(
                "semantic wall",
                canvas.renderer.door_preview_error,
            )

    def test_escape_style_cancel_clears_door_mode_and_preview(
            self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                ui_content(),
                root,
            )

            document = wall_document()

            canvas = MapCanvas()

            self.addCleanup(
                canvas.close
            )

            canvas.set_context(
                document,
                workspace,
                root,
            )

            canvas.set_door_selection(
                "object.gate"
            )

            canvas.preview_door_at(
                (3, 0)
            )

            canvas.cancel_placement()

            self.assertEqual(
                "select",
                canvas.tool,
            )

            self.assertEqual(
                "",
                canvas.selected_door_definition_id,
            )

            self.assertEqual(
                (),
                canvas.renderer.door_preview_cells,
            )


if __name__ == "__main__":
    unittest.main()
