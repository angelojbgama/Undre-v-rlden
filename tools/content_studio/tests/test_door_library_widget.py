from __future__ import annotations

import os
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

os.environ.setdefault(
    "QT_QPA_PLATFORM",
    "offscreen",
)

try:
    from PySide6.QtCore import Qt
    from PySide6.QtGui import QColor, QImage
    from PySide6.QtWidgets import QApplication
except ImportError:  # pragma: no cover
    QApplication = None  # type: ignore[assignment]
    Qt = None  # type: ignore[assignment]
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


    def test_animated_collision_action_targets_current_door(
            self,
    ) -> None:
        from tools.content_studio.ui.door_library_widget import (
            DoorLibraryWidget,
        )

        assert Qt is not None

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
                Qt.ContextMenuPolicy.CustomContextMenu,
                widget.doors.contextMenuPolicy(),
            )

            requested: list[str] = []
            statuses: list[str] = []

            widget.animated_collision_requested.connect(
                requested.append
            )

            widget.status_changed.connect(
                statuses.append
            )

            widget.doors.setCurrentRow(
                0
            )

            widget._request_animated_collision()

            self.assertEqual(
                ["object.gate"],
                requested,
            )

            self.assertTrue(
                statuses
            )

            self.assertIn(
                "object.gate",
                statuses[-1],
            )


    def test_animation_frame_mask_service_preserves_other_channels(
            self,
    ) -> None:
        from tools.content_studio.services.animation_frame_mask_service import (
            AnimationFrameMaskService,
            OBJECT_COLLISION_MASK_CHANNEL,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = create_workspace(
                root
            )

            animation = workspace.find(
                "animations",
                "animation.gate",
            )

            self.assertIsNotNone(
                animation
            )

            assert animation is not None

            hurtbox = {
                "channel": "hurtbox",
                "width": 48,
                "height": 48,
                "origin": {
                    "x": -24,
                    "y": -47,
                },
                "cells": [1]
                + [0] * (48 * 48 - 1),
            }

            frames = animation.data[
                "frames"
            ]

            assert isinstance(
                frames,
                list,
            )

            first = frames[0]

            assert isinstance(
                first,
                dict,
            )

            first["masks"] = [
                hurtbox
            ]

            object_mask = {
                "width": 48,
                "height": 48,
                "origin": {
                    "x": -24,
                    "y": -47,
                },
                "cells": [0] * (48 * 48),
            }

            object_mask["cells"][-1] = 1

            service = AnimationFrameMaskService(
                workspace
            )

            service.replace_channel(
                "animation.gate",
                OBJECT_COLLISION_MASK_CHANNEL,
                [
                    object_mask,
                    None,
                ],
            )

            updated = workspace.find(
                "animations",
                "animation.gate",
            )

            assert updated is not None

            updated_frames = updated.data[
                "frames"
            ]

            assert isinstance(
                updated_frames,
                list,
            )

            first_masks = updated_frames[0][
                "masks"
            ]

            self.assertEqual(
                {
                    "hurtbox",
                    OBJECT_COLLISION_MASK_CHANNEL,
                },
                {
                    value["channel"]
                    for value in first_masks
                },
            )

            self.assertNotIn(
                "masks",
                updated_frames[1],
            )

            workspace.save_all()

            reopened = ContentWorkspace.open(
                root
            )

            stored = AnimationFrameMaskService(
                reopened
            ).channel_masks(
                "animation.gate",
                OBJECT_COLLISION_MASK_CHANNEL,
            )

            self.assertEqual(
                object_mask,
                stored[0],
            )

            self.assertIsNone(
                stored[1]
            )

    def test_animated_collision_editor_uses_keyframe_inheritance(
            self,
    ) -> None:
        from tools.content_studio.services.localization import Translator
        from tools.content_studio.ui.animated_collision_editor import (
            AnimatedCollisionEditorDialog,
        )

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            workspace = create_workspace(
                root
            )

            dialog = AnimatedCollisionEditorDialog(
                workspace,
                root,
                "animation.gate",
                Translator("pt-BR"),
            )

            self.addCleanup(
                dialog.close
            )

            self.assertEqual(
                2,
                dialog.frame_count(),
            )

            self.assertEqual(
                {},
                dialog.keyframes(),
            )

            authored_mask = {
                "width": 48,
                "height": 48,
                "origin": {
                    "x": -24,
                    "y": -47,
                },
                "cells": [1]
                + [0] * (48 * 48 - 1),
            }

            with patch(
                "tools.content_studio.ui.animated_collision_editor."
                "ShapeMaskEditorDialog"
            ) as editor_type:
                editor_instance = (
                    editor_type.return_value
                )

                editor_instance.exec.return_value = 1
                editor_instance.result_mask.return_value = (
                    authored_mask
                )

                dialog._edit_current_frame()

            self.assertEqual(
                authored_mask,
                dialog.effective_mask(0),
            )

            dialog._next_frame()

            self.assertEqual(
                authored_mask,
                dialog.effective_mask(1),
            )

            self.assertNotIn(
                1,
                dialog.keyframes(),
            )

            dialog._set_current_none()

            self.assertIsNone(
                dialog.effective_mask(1)
            )

            self.assertIn(
                1,
                dialog.keyframes(),
            )

            self.assertEqual(
                [
                    authored_mask,
                    None,
                ],
                dialog.result_effective_masks(),
            )

            dialog._inherit_current()

            self.assertEqual(
                authored_mask,
                dialog.effective_mask(1),
            )

    def test_main_window_persists_door_collision_on_animation_frames(
            self,
    ) -> None:
        from tools.content_studio.services.animation_frame_mask_service import (
            AnimationFrameMaskService,
            OBJECT_COLLISION_MASK_CHANNEL,
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

            authored_mask = {
                "width": 48,
                "height": 48,
                "origin": {
                    "x": -24,
                    "y": -47,
                },
                "cells": [1]
                + [0] * (48 * 48 - 1),
            }

            with patch(
                "tools.content_studio.ui.main_window."
                "AnimatedCollisionEditorDialog"
            ) as dialog_type:
                dialog_instance = (
                    dialog_type.return_value
                )

                dialog_instance.exec.return_value = 1

                dialog_instance.result_effective_masks.return_value = [
                    authored_mask,
                    None,
                ]

                window.door_library.animated_collision_requested.emit(
                    "object.gate"
                )

                dialog_type.assert_called_once()
                dialog_instance.exec.assert_called_once_with()

            stored = AnimationFrameMaskService(
                workspace
            ).channel_masks(
                "animation.gate",
                OBJECT_COLLISION_MASK_CHANNEL,
            )

            self.assertEqual(
                authored_mask,
                stored[0],
            )

            self.assertIsNone(
                stored[1]
            )

            self.assertTrue(
                workspace.dirty
            )

            workspace.save_all()


if __name__ == "__main__":
    unittest.main()
