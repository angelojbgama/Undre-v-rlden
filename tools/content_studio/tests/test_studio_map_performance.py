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
    from PySide6.QtGui import (
        QImage,
        QPainter,
    )
    from PySide6.QtWidgets import (
        QApplication,
    )
except ImportError:  # pragma: no cover
    QApplication = None  # type: ignore[assignment]

from tools.content_studio.interaction.selection_controller import (
    SelectionController,
)
from tools.content_studio.model.map_document import (
    MapDocument,
)
from tools.content_studio.model.world_project import (
    WorldProject,
)
from tools.content_studio.tests.test_canvas_door_visuals import (
    visual_workspace,
)
from tools.content_studio.tests.test_door_placement_service import (
    placement_content,
)
from tools.content_studio.tests.test_door_authoring_service import (
    workspace_from,
)
from tools.content_studio.ui.canvas_camera import (
    CanvasCamera,
)
from tools.content_studio.ui.canvas_renderer import (
    CanvasRenderer,
)


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class StudioMapPerformanceTests(
    unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_scaled_tile_frame_is_cached_for_same_zoom_size(
        self,
    ) -> None:
        from tools.content_studio.ui.studio_visual_resolver import (
            StudioVisualResolver,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = visual_workspace(
                root
            )

            resolver = StudioVisualResolver()

            resolver.set_context(
                workspace,
                root,
            )

            reference = {
                "tilesetId": "tileset.test",
                "sourceIndex": 0,
                "flags": 0,
            }

            first = resolver.resolve_scaled_tile(
                reference,
                0,
                16,
                32,
            )

            second = resolver.resolve_scaled_tile(
                reference,
                0,
                16,
                32,
            )

            self.assertIsNotNone(
                first
            )

            self.assertIs(
                first,
                second,
            )

            assert first is not None

            self.assertEqual(
                (32, 32),
                (
                    first.width(),
                    first.height(),
                ),
            )

    def test_entity_visual_resolution_is_culled_outside_viewport(
        self,
    ) -> None:
        class CountingRenderer(
            CanvasRenderer
        ):
            def __init__(
                self,
            ) -> None:
                super().__init__(
                    CanvasCamera(),
                    SelectionController(),
                )

                self.visual_queries = 0

            def _entity_visual(
                self,
                category,
                value,
            ):
                self.visual_queries += 1
                return None

        document = MapDocument.new(
            "map.performance",
            128,
            128,
            16,
        )

        document.data[
            "objects"
        ] = [
            {
                "id": index + 1,
                "definitionId": "object.offscreen",
                "position": {
                    "x": 16,
                    "y": 16,
                },
                "initialContents": [],
                "persistence": "persistent",
            }
            for index in range(
                1000
            )
        ]

        document.data[
            "objects"
        ].append({
            "id": 1001,
            "definitionId": "object.visible",
            "position": {
                "x": 1024,
                "y": 1024,
            },
            "initialContents": [],
            "persistence": "persistent",
        })

        renderer = CountingRenderer()

        renderer.set_context(
            document,
            None,
            None,
        )

        image = QImage(
            320,
            180,
            QImage.Format.Format_ARGB32,
        )

        painter = QPainter(
            image
        )

        try:
            renderer._draw_entities(
                painter,
                320,
                180,
            )
        finally:
            painter.end()

        self.assertEqual(
            1,
            renderer.visual_queries,
        )

    def test_canvas_edits_do_not_rebuild_map_side_panels(
        self,
    ) -> None:
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                placement_content(),
                root,
            )

            document = MapDocument.new(
                "map.performance-window",
                8,
                8,
                16,
            )

            project = WorldProject(
                [document],
                document.map_id,
            )

            document.dirty = False
            project.dirty = False

            window = MainWindow(
                project,
                workspace,
                asset_root=root,
            )

            def close_window() -> None:
                document.dirty = False
                project.dirty = False
                window.close()

            self.addCleanup(
                close_window
            )

            calls = {
                "tiles": 0,
                "doors": 0,
                "layers": 0,
                "maps": 0,
            }

            window.tileset_library.set_map_tile_size = (
                lambda value:
                calls.__setitem__(
                    "tiles",
                    calls["tiles"] + 1,
                )
            )

            window.door_library.set_map_tile_size = (
                lambda value:
                calls.__setitem__(
                    "doors",
                    calls["doors"] + 1,
                )
            )

            window.layers.set_document = (
                lambda value:
                calls.__setitem__(
                    "layers",
                    calls["layers"] + 1,
                )
            )

            window.map_browser.refresh = (
                lambda *args, **kwargs:
                calls.__setitem__(
                    "maps",
                    calls["maps"] + 1,
                )
            )

            window.map_canvas.document_changed.emit()

            self.assertEqual(
                {
                    "tiles": 0,
                    "doors": 0,
                    "layers": 0,
                    "maps": 0,
                },
                calls,
            )

    def test_structural_map_refresh_still_updates_side_panels(
        self,
    ) -> None:
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = workspace_from(
                placement_content(),
                root,
            )

            document = MapDocument.new(
                "map.performance-window",
                8,
                8,
                16,
            )

            project = WorldProject(
                [document],
                document.map_id,
            )

            document.dirty = False
            project.dirty = False

            window = MainWindow(
                project,
                workspace,
                asset_root=root,
            )

            def close_window() -> None:
                document.dirty = False
                project.dirty = False
                window.close()

            self.addCleanup(
                close_window
            )

            calls = {
                "tiles": 0,
                "doors": 0,
                "layers": 0,
                "maps": 0,
            }

            window.tileset_library.set_map_tile_size = (
                lambda value:
                calls.__setitem__(
                    "tiles",
                    calls["tiles"] + 1,
                )
            )

            window.door_library.set_map_tile_size = (
                lambda value:
                calls.__setitem__(
                    "doors",
                    calls["doors"] + 1,
                )
            )

            window.layers.set_document = (
                lambda value:
                calls.__setitem__(
                    "layers",
                    calls["layers"] + 1,
                )
            )

            window.map_browser.refresh = (
                lambda *args, **kwargs:
                calls.__setitem__(
                    "maps",
                    calls["maps"] + 1,
                )
            )

            window._map_changed()

            self.assertEqual(
                {
                    "tiles": 1,
                    "doors": 1,
                    "layers": 1,
                    "maps": 1,
                },
                calls,
            )


if __name__ == "__main__":
    unittest.main()
