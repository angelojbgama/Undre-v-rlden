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

from tools.content_studio.interaction.selection_controller import (
    Selection,
)
from tools.content_studio.model.world_project import (
    WorldProject,
)
from tools.content_studio.services.localization import (
    Translator,
)
from tools.content_studio.tests.test_door_authoring_service import (
    workspace_from,
)
from tools.content_studio.tests.test_door_placement_service import (
    placement_content,
    wall_document,
)


def movement_content() -> dict[str, object]:
    data = placement_content()

    data["visualImages"] = [
        {
            "id": "image.gate",
            "root": "contentWorkspace",
            "relativePath": "gate.png",
        },
        {
            "id": "image.crate",
            "root": "contentWorkspace",
            "relativePath": "crate.png",
        },
        {
            "id": "image.tiles",
            "root": "contentWorkspace",
            "relativePath": "tiles.png",
        },
    ]

    return data


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class MapCanvasMoveRefreshTests(
        unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_programmatic_door_refresh_does_not_emit_user_selection(
            self,
    ) -> None:
        from tools.content_studio.ui.door_library_widget import (
            DoorLibraryWidget,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                movement_content(),
                root,
            )

            widget = DoorLibraryWidget(
                workspace,
                root,
                16,
                Translator("en-US"),
            )

            self.addCleanup(
                widget.deleteLater
            )

            selections: list[object] = []

            widget.selected.connect(
                selections.append
            )

            widget.refresh()

            self.assertEqual(
                [],
                selections,
                "programmatic refresh must not behave like a user selection",
            )

    def test_map_refresh_during_entity_move_does_not_cancel_move(
            self,
    ) -> None:
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                movement_content(),
                root,
            )

            document = wall_document()

            object_id = document.add_entity(
                "objects",
                "object.crate",
                32,
                32,
            )

            project = WorldProject(
                [document],
                document.map_id,
            )

            window = MainWindow(
                project,
                workspace,
                asset_root=root,
            )

            self.addCleanup(
                window.deleteLater
            )

            select_action = (
                window.actions["select"]
            )

            if not select_action.isChecked():
                select_action.setChecked(
                    True
                )

            canvas = window.map_canvas

            self.assertEqual(
                "select",
                canvas.tool,
            )

            selection = Selection(
                "objects",
                object_id,
            )

            canvas._moving = selection
            canvas.renderer.moving_selection = (
                selection.as_tuple()
            )
            canvas.renderer.moving_world = (
                48,
                48,
            )

            # This used to crash after document_changed:
            #
            # DoorLibrary.refresh()
            # -> selected.emit()
            # -> _clear_toolbar_tools()
            # -> set_tool("none")
            # -> canvas._moving = None
            canvas._move_selection(
                (
                    48,
                    48,
                )
            )

            moved = document.entity(
                "objects",
                object_id,
            )

            self.assertIsNotNone(
                moved
            )

            assert moved is not None

            self.assertEqual(
                {
                    "x": 48,
                    "y": 48,
                },
                moved["position"],
            )

            self.assertEqual(
                "select",
                canvas.tool,
            )

            self.assertEqual(
                selection,
                canvas._moving,
            )


if __name__ == "__main__":
    unittest.main()
