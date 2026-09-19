from __future__ import annotations

from copy import deepcopy
from pathlib import Path

from PySide6.QtCore import QRect, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPixmap
from PySide6.QtWidgets import (
    QDialog,
    QDialogButtonBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..services.animation_frame_mask_service import (
    AnimationFrameMaskService,
    OBJECT_COLLISION_MASK_CHANNEL,
)
from ..services.localization import Translator
from .shape_mask_editor import ShapeMaskEditorDialog


class AnimatedCollisionEditorDialog(QDialog):
    # PS3 persists effective collision through animation frame masks.
    # The editor presents keyframe/inheritance semantics while storage
    # stays compatible with the existing animation.frames[].masks schema.

    def __init__(
        self,
        workspace: ContentWorkspace,
        asset_root: Path | None,
        animation_id: str,
        translator: Translator,
        parent: QWidget | None = None,
        *,
        channel: str = OBJECT_COLLISION_MASK_CHANNEL,
        fallback_mask: dict[str, object] | None = None,
    ) -> None:
        super().__init__(
            parent
        )

        self.workspace = workspace
        self.asset_root = asset_root
        self.animation_id = animation_id
        self.translate = translator
        self.channel = channel
        self._fallback_mask = (
            deepcopy(fallback_mask)
            if isinstance(fallback_mask, dict)
            else None
        )

        animation = workspace.find(
            "animations",
            animation_id,
        )

        if animation is None:
            raise ValueError(
                self.translate(
                    "animated_collision_animation_missing"
                ).format(
                    animation=animation_id
                )
            )

        frames = animation.data.get(
            "frames",
            [],
        )

        self._frames = [
            frame
            for frame in frames
            if isinstance(frame, dict)
        ] if isinstance(frames, list) else []

        if not self._frames:
            raise ValueError(
                self.translate(
                    "animated_collision_frames_missing"
                ).format(
                    animation=animation_id
                )
            )

        self._source_image = self._animation_image(
            animation.data
        )

        if self._source_image.isNull():
            raise ValueError(
                self.translate(
                    "animated_collision_image_missing"
                ).format(
                    animation=animation_id
                )
            )

        stored = AnimationFrameMaskService(
            workspace
        ).channel_masks(
            animation_id,
            channel,
        )

        self._keyframes = self._compress_effective_masks(
            stored
        )
        self._frame_index = 0

        self.animation_label = QLabel(
            self.translate(
                "animated_collision_animation"
            ).format(
                animation=animation_id
            )
        )

        self.frame_label = QLabel()

        self.frame_label.setAlignment(
            Qt.AlignmentFlag.AlignCenter
        )

        self.preview = QLabel()

        self.preview.setAlignment(
            Qt.AlignmentFlag.AlignCenter
        )

        self.preview.setMinimumSize(
            420,
            420,
        )

        self.preview.setStyleSheet(
            "background: #161b22; "
            "color: #aeb8c4; "
            "border: 1px solid #34404d;"
        )

        self.mask_status = QLabel()

        self.mask_status.setAlignment(
            Qt.AlignmentFlag.AlignCenter
        )

        self.help = QLabel(
            self.translate(
                "animated_collision_persistence_help"
            )
        )

        self.help.setWordWrap(
            True
        )

        self.help.setProperty("muted", True)

        self.previous_button = QPushButton(
            self.translate(
                "animated_collision_previous"
            )
        )

        self.next_button = QPushButton(
            self.translate(
                "animated_collision_next"
            )
        )

        self.edit_button = QPushButton(
            self.translate(
                "animated_collision_edit_frame"
            )
        )

        self.no_collision_button = QPushButton(
            self.translate(
                "animated_collision_set_none"
            )
        )

        self.inherit_button = QPushButton(
            self.translate(
                "animated_collision_inherit_previous"
            )
        )

        self.previous_button.clicked.connect(
            self._previous_frame
        )

        self.next_button.clicked.connect(
            self._next_frame
        )

        self.edit_button.clicked.connect(
            self._edit_current_frame
        )

        self.no_collision_button.clicked.connect(
            self._set_current_none
        )

        self.inherit_button.clicked.connect(
            self._inherit_current
        )

        navigation = QHBoxLayout()

        navigation.addWidget(
            self.previous_button
        )

        navigation.addWidget(
            self.frame_label,
            1,
        )

        navigation.addWidget(
            self.next_button
        )

        actions = QHBoxLayout()

        actions.addWidget(
            self.edit_button
        )

        actions.addWidget(
            self.no_collision_button
        )

        actions.addWidget(
            self.inherit_button
        )

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save
            | QDialogButtonBox.StandardButton.Cancel
        )

        buttons.accepted.connect(
            self.accept
        )

        buttons.rejected.connect(
            self.reject
        )

        layout = QVBoxLayout(
            self
        )

        layout.addWidget(
            self.animation_label
        )

        layout.addLayout(
            navigation
        )

        layout.addWidget(
            self.preview,
            1,
        )

        layout.addWidget(
            self.mask_status
        )

        layout.addLayout(
            actions
        )

        layout.addWidget(
            self.help
        )

        layout.addWidget(
            buttons
        )

        self.setWindowTitle(
            self.translate(
                "animated_collision_editor_title"
            )
        )

        self.resize(
            900,
            720,
        )

        self._refresh()

    def frame_count(
        self,
    ) -> int:
        return len(
            self._frames
        )

    def current_frame_index(
        self,
    ) -> int:
        return self._frame_index

    def keyframes(
        self,
    ) -> dict[
        int,
        dict[str, object] | None,
    ]:
        return deepcopy(
            self._keyframes
        )

    def effective_mask(
        self,
        frame_index: int,
    ) -> dict[str, object] | None:
        if (
            frame_index < 0
            or frame_index >= len(self._frames)
        ):
            raise IndexError(
                "animation frame index out of range"
            )

        keyframe_index = self._source_keyframe_index(
            frame_index
        )

        if keyframe_index is None:
            return None

        return deepcopy(
            self._keyframes[
                keyframe_index
            ]
        )

    def result_effective_masks(
        self,
    ) -> list[
        dict[str, object] | None
    ]:
        return [
            self.effective_mask(
                index
            )
            for index in range(
                len(self._frames)
            )
        ]

    @staticmethod
    def _compress_effective_masks(
        values: list[
            dict[str, object] | None
        ],
    ) -> dict[
        int,
        dict[str, object] | None,
    ]:
        result: dict[
            int,
            dict[str, object] | None,
        ] = {}

        previous: dict[str, object] | None = None

        for index, value in enumerate(
            values
        ):
            current = deepcopy(
                value
            )

            if index == 0:
                if current is not None:
                    result[0] = current

                previous = current
                continue

            if current != previous:
                result[index] = current

            previous = current

        return result

    def _source_keyframe_index(
        self,
        frame_index: int,
    ) -> int | None:
        matches = [
            index
            for index in self._keyframes
            if index <= frame_index
        ]

        if not matches:
            return None

        return max(
            matches
        )

    def _animation_image(
        self,
        animation_data: dict[str, object],
    ) -> QImage:
        image_id = str(
            animation_data.get(
                "imageId",
                "",
            )
        )

        image_definition = self.workspace.find(
            "visualImages",
            image_id,
        )

        if image_definition is None:
            return QImage()

        relative = image_definition.data.get(
            "relativePath"
        )

        if not isinstance(
            relative,
            str,
        ):
            return QImage()

        root = (
            self.asset_root
            if (
                image_definition.data.get(
                    "root"
                )
                == "gameAssets"
            )
            else self.workspace.root
        )

        if root is None:
            return QImage()

        return QImage(
            str(
                root
                / relative
            )
        )

    def _current_frame(
        self,
    ) -> dict[str, object]:
        return self._frames[
            self._frame_index
        ]

    @staticmethod
    def _frame_rect(
        frame: dict[str, object],
    ) -> QRect:
        source = frame.get(
            "source",
            {},
        )

        source = (
            source
            if isinstance(
                source,
                dict,
            )
            else {}
        )

        return QRect(
            int(
                source.get(
                    "x",
                    0,
                )
            ),
            int(
                source.get(
                    "y",
                    0,
                )
            ),
            max(
                1,
                int(
                    source.get(
                        "width",
                        1,
                    )
                ),
            ),
            max(
                1,
                int(
                    source.get(
                        "height",
                        1,
                    )
                ),
            ),
        )

    def _frame_sprite(
        self,
        frame: dict[str, object],
    ) -> QImage:
        return self._source_image.copy(
            self._frame_rect(
                frame
            )
        )

    @staticmethod
    def _default_mask(
        frame: dict[str, object],
    ) -> dict[str, object]:
        source = frame.get(
            "source",
            {},
        )

        anchor = frame.get(
            "anchor",
            {},
        )

        offset = frame.get(
            "drawOffset",
            {},
        )

        source = (
            source
            if isinstance(source, dict)
            else {}
        )

        anchor = (
            anchor
            if isinstance(anchor, dict)
            else {}
        )

        offset = (
            offset
            if isinstance(offset, dict)
            else {}
        )

        width = max(
            1,
            int(
                source.get(
                    "width",
                    1,
                )
            ),
        )

        height = max(
            1,
            int(
                source.get(
                    "height",
                    1,
                )
            ),
        )

        return {
            "width": width,
            "height": height,
            "origin": {
                "x": (
                    -int(
                        anchor.get(
                            "x",
                            0,
                        )
                    )
                    + int(
                        offset.get(
                            "x",
                            0,
                        )
                    )
                ),
                "y": (
                    -int(
                        anchor.get(
                            "y",
                            0,
                        )
                    )
                    + int(
                        offset.get(
                            "y",
                            0,
                        )
                    )
                ),
            },
            "cells": [
                0
            ] * (
                width
                * height
            ),
        }

    def _editable_seed(
        self,
        frame: dict[str, object],
    ) -> dict[str, object]:
        effective = self.effective_mask(
            self._frame_index
        )

        if effective is not None:
            return effective

        if (
            isinstance(
                self._fallback_mask,
                dict,
            )
            and self._mask_matches_frame(
                self._fallback_mask,
                frame,
            )
        ):
            return deepcopy(
                self._fallback_mask
            )

        return self._default_mask(
            frame
        )

    @staticmethod
    def _mask_matches_frame(
        mask: dict[str, object],
        frame: dict[str, object],
    ) -> bool:
        source = frame.get(
            "source",
            {},
        )

        if not isinstance(
            source,
            dict,
        ):
            return False

        return (
            int(
                mask.get(
                    "width",
                    0,
                )
            )
            == max(
                1,
                int(
                    source.get(
                        "width",
                        1,
                    )
                ),
            )
            and int(
                mask.get(
                    "height",
                    0,
                )
            )
            == max(
                1,
                int(
                    source.get(
                        "height",
                        1,
                    )
                ),
            )
        )

    def _previous_frame(
        self,
    ) -> None:
        if self._frame_index <= 0:
            return

        self._frame_index -= 1
        self._refresh()

    def _next_frame(
        self,
    ) -> None:
        if (
            self._frame_index
            >= len(self._frames) - 1
        ):
            return

        self._frame_index += 1
        self._refresh()

    def _edit_current_frame(
        self,
    ) -> None:
        frame = self._current_frame()

        sprite = self._frame_sprite(
            frame
        )

        dialog = ShapeMaskEditorDialog(
            sprite,
            self._editable_seed(
                frame
            ),
            self.translate,
            self,
            title_key=(
                "animated_collision_frame_mask_title"
            ),
        )

        if not dialog.exec():
            return

        result = dialog.result_mask()

        cells = result.get(
            "cells",
            [],
        )

        if (
            isinstance(
                cells,
                list,
            )
            and any(
                int(value) != 0
                for value in cells
            )
        ):
            self._keyframes[
                self._frame_index
            ] = result
        else:
            self._keyframes[
                self._frame_index
            ] = None

        self._refresh()

    def _set_current_none(
        self,
    ) -> None:
        self._keyframes[
            self._frame_index
        ] = None

        self._refresh()

    def _inherit_current(
        self,
    ) -> None:
        if self._frame_index == 0:
            return

        self._keyframes.pop(
            self._frame_index,
            None,
        )

        self._refresh()

    def _preview_pixmap(
        self,
    ) -> QPixmap:
        sprite = self._frame_sprite(
            self._current_frame()
        )

        if sprite.isNull():
            return QPixmap()

        mask = self.effective_mask(
            self._frame_index
        )

        if mask is not None:
            width = int(
                mask.get(
                    "width",
                    0,
                )
            )

            height = int(
                mask.get(
                    "height",
                    0,
                )
            )

            cells = mask.get(
                "cells",
                [],
            )

            if (
                width == sprite.width()
                and height == sprite.height()
                and isinstance(
                    cells,
                    list,
                )
            ):
                overlay = sprite.convertToFormat(
                    QImage.Format.Format_ARGB32
                )

                painter = QPainter(
                    overlay
                )

                painter.setPen(
                    Qt.PenStyle.NoPen
                )

                painter.setBrush(
                    QColor(
                        255,
                        60,
                        60,
                        115,
                    )
                )

                expected = (
                    width
                    * height
                )

                for index, value in enumerate(
                    cells[:expected]
                ):
                    if not int(
                        value
                    ):
                        continue

                    painter.drawRect(
                        index % width,
                        index // width,
                        1,
                        1,
                    )

                painter.end()

                sprite = overlay

        return QPixmap.fromImage(
            sprite
        ).scaled(
            420,
            420,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation,
        )

    def _status_text(
        self,
    ) -> str:
        source = self._source_keyframe_index(
            self._frame_index
        )

        if source is None:
            return self.translate(
                "animated_collision_status_none"
            )

        value = self._keyframes[
            source
        ]

        if source == self._frame_index:
            if value is None:
                return self.translate(
                    "animated_collision_status_none_keyframe"
                )

            return self.translate(
                "animated_collision_status_defined"
            )

        if value is None:
            return self.translate(
                "animated_collision_status_inherited_none"
            ).format(
                frame=source + 1
            )

        return self.translate(
            "animated_collision_status_inherited"
        ).format(
            frame=source + 1
        )

    def _refresh(
        self,
    ) -> None:
        self.frame_label.setText(
            self.translate(
                "animated_collision_frame"
            ).format(
                current=self._frame_index + 1,
                total=len(self._frames),
            )
        )

        pixmap = self._preview_pixmap()

        self.preview.setPixmap(
            pixmap
        )

        self.preview.setText(
            ""
            if not pixmap.isNull()
            else self.translate(
                "image_unavailable"
            )
        )

        self.mask_status.setText(
            self._status_text()
        )

        self.previous_button.setEnabled(
            self._frame_index > 0
        )

        self.next_button.setEnabled(
            self._frame_index
            < len(self._frames) - 1
        )

        self.inherit_button.setEnabled(
            self._frame_index > 0
        )
