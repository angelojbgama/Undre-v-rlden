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
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover
    QApplication = None  # type: ignore[assignment]

from tools.content_studio.interaction.drag_payload import (
    StudioDragPayload,
)
from tools.content_studio.model.world_project import (
    WorldProject,
)
from tools.content_studio.services.localization import (
    Translator,
)
from tools.content_studio.tests.test_door_authoring_service import (
    gate_content,
    workspace_from,
)
from tools.content_studio.tests.test_door_placement_service import (
    wall_document,
)


def complete_gate_content() -> dict[str, object]:
    data = gate_content()

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
    ]

    return data


def list_item_by_id(
        widget,
        definition_id: str,
):
    for index in range(
        widget.objects.count()
    ):
        item = widget.objects.item(
            index
        )

        if (
            item.data(
                Qt.ItemDataRole.UserRole
            )
            == definition_id
        ):
            return item

    raise AssertionError(
        f"definition not found: {definition_id}"
    )


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class DoorPlacementBoundaryTests(
        unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_object_library_keeps_door_configurable_but_not_generically_placeable(
            self,
    ) -> None:
        from tools.content_studio.ui.object_library_widget import (
            ObjectLibraryWidget,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                complete_gate_content(),
                Path(directory),
            )

            widget = ObjectLibraryWidget(
                workspace,
                None,
                Translator("en-US"),
            )

            self.addCleanup(
                widget.close
            )

            gate = list_item_by_id(
                widget,
                "object.gate",
            )

            widget.objects.setCurrentItem(
                gate
            )

            self.assertTrue(
                widget.configure_button.isEnabled()
            )

            self.assertFalse(
                widget.place_button.isEnabled()
            )

            self.assertIsNone(
                widget._drag_payload(
                    [gate]
                )
            )

            emitted: list[
                tuple[str, str]
            ] = []

            widget.place_requested.connect(
                lambda category, definition_id:
                emitted.append(
                    (
                        category,
                        definition_id,
                    )
                )
            )

            widget.place_current()

            self.assertEqual(
                [],
                emitted,
            )

            crate = list_item_by_id(
                widget,
                "object.crate",
            )

            widget.objects.setCurrentItem(
                crate
            )

            self.assertTrue(
                widget.place_button.isEnabled()
            )

            self.assertIsNotNone(
                widget._drag_payload(
                    [crate]
                )
            )

            widget.place_current()

            self.assertEqual(
                [
                    (
                        "objects",
                        "object.crate",
                    )
                ],
                emitted,
            )

    def test_content_browser_does_not_generate_generic_door_drag(
            self,
    ) -> None:
        from tools.content_studio.ui.widgets import (
            ContentBrowser,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                complete_gate_content(),
                Path(directory),
            )

            browser = ContentBrowser(
                workspace,
                ("objects",),
                translator=Translator(
                    "en-US"
                ),
            )

            self.addCleanup(
                browser.close
            )

            browser.select_definition(
                "objects",
                "object.gate",
            )

            self.assertFalse(
                browser.place_button.isEnabled()
            )

            gate_item = (
                browser.list.currentItem()
            )

            self.assertIsNotNone(
                gate_item
            )

            assert gate_item is not None

            self.assertIsNone(
                browser._drag_payload(
                    [gate_item]
                )
            )

            emitted: list[
                tuple[str, str]
            ] = []

            browser.place_requested.connect(
                lambda category, definition_id:
                emitted.append(
                    (
                        category,
                        definition_id,
                    )
                )
            )

            browser._place()

            self.assertEqual(
                [],
                emitted,
            )

            browser.select_definition(
                "objects",
                "object.crate",
            )

            self.assertTrue(
                browser.place_button.isEnabled()
            )

            crate_item = (
                browser.list.currentItem()
            )

            assert crate_item is not None

            self.assertIsNotNone(
                browser._drag_payload(
                    [crate_item]
                )
            )

    def test_map_canvas_defensively_routes_door_entity_selection(
            self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            workspace = workspace_from(
                complete_gate_content(),
                Path(directory),
            )

            canvas = MapCanvas()

            self.addCleanup(
                canvas.close
            )

            canvas.set_context(
                wall_document(),
                workspace,
                Path(directory),
            )

            canvas.set_entity_selection(
                "objects",
                "object.gate",
            )

            self.assertEqual(
                "door",
                canvas.tool,
            )

            self.assertEqual(
                "object.gate",
                canvas.selected_door_definition_id,
            )

            self.assertIsNone(
                canvas.interaction.active_payload
            )

            canvas.cancel_placement()

            canvas.set_active_payload(
                StudioDragPayload.content(
                    "objects",
                    "object.gate",
                )
            )

            self.assertEqual(
                "door",
                canvas.tool,
            )

            self.assertEqual(
                "object.gate",
                canvas.selected_door_definition_id,
            )

            self.assertIsNone(
                canvas.interaction.active_payload
            )

    def test_main_window_generic_request_routes_to_door_tool(
            self,
    ) -> None:
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                complete_gate_content(),
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

            window._place_definition(
                "objects",
                "object.gate",
            )

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


if __name__ == "__main__":
    unittest.main()
