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
    QColor = None  # type: ignore[assignment,misc]
    QImage = None  # type: ignore[assignment,misc]

from tools.content_studio.formats.content_json import (
    CONTENT_CATEGORIES,
    CONTENT_VERSION,
)
from tools.content_studio.formats.json_io import encode_json
from tools.content_studio.model.content_workspace import (
    ContentWorkspace,
)
from tools.content_studio.model.world_project import WorldProject


def door_content() -> dict[str, object]:
    data: dict[str, object] = {
        "format": "dungeon-underworld-content",
        "version": CONTENT_VERSION,
    }

    data.update({
        category: []
        for category in CONTENT_CATEGORIES
    })

    data["visualImages"] = [
        {
            "id": "image.gate",
            "root": "contentWorkspace",
            "relativePath": "gate.png",
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
                    "durationTicks": 6,
                    "markers": [],
                },
                {
                    "source": {
                        "x": 48,
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
                    "durationTicks": 6,
                    "markers": [],
                },
            ],
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
            "idleAnimationId": "animation.gate",
        },
    ]

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

    data["authoringDescriptors"] = [
        {
            "definitionId": "object.gate",
            "displayName": "Gate",
            "category": "object",
            "tags": [
                "door",
                "object-source-animation:animation.gate",
            ],
        },
        {
            "definitionId": "object.crate",
            "displayName": "Crate",
            "category": "object",
            "tags": [
                "object-source-animation:animation.gate",
            ],
        },
    ]

    return data


def create_workspace(
        root: Path,
) -> ContentWorkspace:
    (
        root / "content.json"
    ).write_text(
        encode_json(
            door_content()
        ),
        encoding="utf-8",
    )

    assert QImage is not None
    assert QColor is not None

    image = QImage(
        96,
        48,
        QImage.Format.Format_ARGB32,
    )

    image.fill(
        QColor(
            70,
            110,
            160,
        )
    )

    assert image.save(
        str(
            root / "gate.png"
        )
    )

    return ContentWorkspace.open(
        root
    )


@unittest.skipIf(
    QApplication is None,
    "PySide6 is not installed",
)
class DoorLibraryWidgetTests(
        unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        assert QApplication is not None

        cls.application = (
            QApplication.instance()
            or QApplication([])
        )

    def test_library_lists_only_door_capabilities_and_shows_footprint(
            self,
    ) -> None:
        try:
            from tools.content_studio.ui.door_library_widget import (
                DoorLibraryWidget,
            )
        except ModuleNotFoundError as error:
            raise AssertionError(
                "DoorLibraryWidget ainda nao foi implementado"
            ) from error

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = create_workspace(
                root
            )

            widget = DoorLibraryWidget(
                workspace,
                root,
                16,
            )

            self.addCleanup(
                widget.close
            )

            self.assertEqual(
                1,
                widget.doors.count(),
            )

            item = widget.doors.item(
                0
            )

            self.assertEqual(
                "object.gate",
                item.data(
                    widget.definition_id_role
                ),
            )

            widget.doors.setCurrentRow(
                0
            )

            self.assertIn(
                "3 × 1",
                widget.details.text(),
            )

            self.assertIn(
                "object.gate",
                widget.details.text(),
            )

            pixmap = widget.preview.pixmap()

            self.assertIsNotNone(
                pixmap
            )

            assert pixmap is not None

            self.assertFalse(
                pixmap.isNull()
            )

            widget.search.setText(
                "crate"
            )

            self.assertEqual(
                0,
                widget.doors.count(),
            )

            widget.search.setText(
                "gate"
            )

            self.assertEqual(
                1,
                widget.doors.count(),
            )

    def test_main_window_exposes_doors_as_specialized_map_section(
            self,
    ) -> None:
        from tools.content_studio.ui.door_library_widget import (
            DoorLibraryWidget,
        )
        from tools.content_studio.ui.main_window import (
            MainWindow,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = create_workspace(
                root
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

            self.assertIsInstance(
                window.door_library,
                DoorLibraryWidget,
            )

            window.mode_tabs.setCurrentIndex(
                0
            )

            labels = [
                window._section_tabs.tabText(index)
                for index in range(
                    window._section_tabs.count()
                )
            ]

            door_label = window.translator(
                "doors_tab"
            )

            self.assertIn(
                door_label,
                labels,
            )

            door_index = labels.index(
                door_label
            )

            window._section_tabs.setCurrentIndex(
                door_index
            )

            self.assertIs(
                window._map_panels.currentWidget(),
                window.door_library,
            )

            self.assertEqual(
                1,
                window.door_library.doors.count(),
            )

            self.assertEqual(
                3,
                window.door_library.current_entry().placement.span_tiles,
            )


if __name__ == "__main__":
    unittest.main()
