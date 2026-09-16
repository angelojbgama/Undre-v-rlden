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
    from PySide6.QtGui import QColor, QImage
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover
    QApplication = None  # type: ignore[assignment]

from tools.content_studio.interaction.selection_controller import (
    SelectionController,
)
from tools.content_studio.tests.test_door_authoring_service import (
    workspace_from,
)
from tools.content_studio.tests.test_door_placement_service import (
    placement_content,
    wall_document,
)
from tools.content_studio.ui.canvas_camera import (
    CanvasCamera,
)


def visual_workspace(root: Path):
    data = placement_content()

    data["visualImages"] = [
        {
            "id": "image.gate",
            "root": "contentWorkspace",
            "relativePath": "gate.png",
        },
    ]

    tilesets = data["tilesets"]
    assert isinstance(tilesets, list)
    assert isinstance(tilesets[0], dict)

    tilesets[0][
        "relativeAssetPath"
    ] = "tiles.png"

    workspace = workspace_from(
        data,
        root,
    )

    gate = QImage(
        48,
        48,
        QImage.Format.Format_ARGB32,
    )
    gate.fill(
        QColor("#d9a441")
    )

    if not gate.save(
        str(root / "gate.png")
    ):
        raise RuntimeError(
            "could not create gate fixture image"
        )

    tiles = QImage(
        32,
        16,
        QImage.Format.Format_ARGB32,
    )
    tiles.fill(
        QColor("#7d4d4d")
    )

    if not tiles.save(
        str(root / "tiles.png")
    ):
        raise RuntimeError(
            "could not create tiles fixture image"
        )

    return workspace


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class CanvasDoorVisualTests(
    unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_gate_resolves_real_frame_and_anchor(
        self,
    ) -> None:
        from tools.content_studio.ui.studio_visual_resolver import (
            StudioVisualResolver,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = visual_workspace(root)

            resolver = StudioVisualResolver()
            resolver.set_context(
                workspace,
                root,
            )

            visual = resolver.resolve_object(
                "object.gate"
            )

            self.assertIsNotNone(visual)
            assert visual is not None

            self.assertEqual(
                (48, 48),
                (
                    visual.image.width(),
                    visual.image.height(),
                ),
            )

            self.assertEqual(
                (24, 47),
                (
                    visual.anchor_x,
                    visual.anchor_y,
                ),
            )

            self.assertEqual(
                (0, 0),
                (
                    visual.draw_offset_x,
                    visual.draw_offset_y,
                ),
            )

    def test_resolver_reuses_cached_animation_frame(
        self,
    ) -> None:
        from tools.content_studio.ui.studio_visual_resolver import (
            StudioVisualResolver,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = visual_workspace(root)

            calls: list[str] = []

            def loader(path: str) -> QImage:
                calls.append(path)
                return QImage(path)

            resolver = StudioVisualResolver(
                image_loader=loader
            )

            resolver.set_context(
                workspace,
                root,
            )

            first = resolver.resolve_object(
                "object.gate"
            )

            second = resolver.resolve_object(
                "object.gate"
            )

            self.assertIsNotNone(first)
            self.assertIs(
                first,
                second,
            )

            self.assertEqual(
                1,
                len(calls),
            )

    def test_tile_atlas_is_not_reloaded_per_tile(
        self,
    ) -> None:
        from tools.content_studio.ui.studio_visual_resolver import (
            StudioVisualResolver,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = visual_workspace(root)

            calls: list[str] = []

            def loader(path: str) -> QImage:
                calls.append(path)
                return QImage(path)

            resolver = StudioVisualResolver(
                image_loader=loader
            )

            resolver.set_context(
                workspace,
                root,
            )

            reference = {
                "tilesetId": "tileset.test",
                "sourceIndex": 0,
                "flags": 0,
            }

            first = resolver.resolve_tile(
                reference,
                fallback_index=0,
                default_tile_size=16,
            )

            second = resolver.resolve_tile(
                reference,
                fallback_index=0,
                default_tile_size=16,
            )

            self.assertIsNotNone(first)
            self.assertIs(
                first,
                second,
            )

            self.assertEqual(
                1,
                len(calls),
            )

    def test_missing_object_visual_falls_back_cleanly(
        self,
    ) -> None:
        from tools.content_studio.ui.studio_visual_resolver import (
            StudioVisualResolver,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = visual_workspace(root)

            resolver = StudioVisualResolver()
            resolver.set_context(
                workspace,
                root,
            )

            self.assertIsNone(
                resolver.resolve_object(
                    "object.does.not.exist"
                )
            )

    def test_door_preview_owns_real_visual(
        self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = visual_workspace(root)
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

            self.assertTrue(
                canvas.preview_door_at(
                    (3, 0)
                )
            )

            self.assertEqual(
                "object.gate",
                canvas.renderer
                .door_preview_definition_id,
            )

            visual = (
                canvas.renderer
                .door_preview_visual()
            )

            self.assertIsNotNone(visual)
            assert visual is not None

            self.assertEqual(
                (48, 48),
                (
                    visual.image.width(),
                    visual.image.height(),
                ),
            )

            self.assertEqual(
                (24, 47),
                (
                    visual.anchor_x,
                    visual.anchor_y,
                ),
            )

    def test_placed_gate_resolves_as_sprite_not_generic_marker(
        self,
    ) -> None:
        from tools.content_studio.ui.map_canvas import (
            MapCanvas,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = visual_workspace(root)
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

            placed = canvas.place_door_at(
                (3, 0)
            )

            self.assertIsNotNone(placed)

            entity = document.entity(
                "objects",
                1,
            )

            assert entity is not None

            visual = (
                canvas.renderer
                ._entity_visual(
                    "objects",
                    entity,
                )
            )

            self.assertIsNotNone(visual)

    def test_repeated_renderer_context_does_not_rescan_assets(
        self,
    ) -> None:
        from tools.content_studio.ui.canvas_renderer import (
            CanvasRenderer,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = visual_workspace(root)
            document = wall_document()

            renderer = CanvasRenderer(
                CanvasCamera(),
                SelectionController(),
            )

            calls: list[
                tuple[Path | None, Path | None]
            ] = []

            original = renderer.assets.refresh

            def recording_refresh(
                game_root,
                content_root,
            ):
                calls.append(
                    (
                        game_root,
                        content_root,
                    )
                )
                original(
                    game_root,
                    content_root,
                )

            renderer.assets.refresh = (
                recording_refresh
            )

            renderer.set_context(
                document,
                workspace,
                root,
            )

            renderer.set_context(
                document,
                workspace,
                root,
            )

            self.assertEqual(
                1,
                len(calls),
            )


if __name__ == "__main__":
    unittest.main()
