from __future__ import annotations

import copy
import re
from pathlib import Path

from PySide6.QtCore import QPoint, QRect, Qt, Signal
from PySide6.QtGui import QColor, QImage, QMouseEvent, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QHBoxLayout, QLabel,
    QMessageBox, QPushButton, QSpinBox, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.localization import Translator
from .preview import load_definition_image


class FrameAlignmentCanvas(QWidget):
    # Preview fiel ao runtime: logicalPosition - anchor + drawOffset.

    offset_dragged = Signal(int, int)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._frame = QImage()
        self._anchor = QPoint()
        self._offset = QPoint()
        self._reference_frame = QImage()
        self._reference_anchor = QPoint()
        self._reference_offset = QPoint()
        self._canvas_width = 1
        self._canvas_height = 1
        self._scale = 1.0
        self._drag_origin: QPoint | None = None
        self._drag_offset = QPoint()
        self.setMinimumSize(420, 300)
        self.setCursor(Qt.CursorShape.OpenHandCursor)

    def set_frame(
            self, frame: QImage, offset_x: int, offset_y: int,
            canvas_width: int, canvas_height: int,
            anchor_x: int = 0, anchor_y: int = 0,
            reference_frame: QImage | None = None,
            reference_anchor: QPoint | None = None,
            reference_offset: QPoint | None = None) -> None:
        self._frame = frame
        self._anchor = QPoint(anchor_x, anchor_y)
        self._offset = QPoint(offset_x, offset_y)
        self._reference_frame = (
            QImage(reference_frame)
            if reference_frame is not None else QImage())
        self._reference_anchor = (
            QPoint(reference_anchor)
            if reference_anchor is not None else QPoint())
        self._reference_offset = (
            QPoint(reference_offset)
            if reference_offset is not None else QPoint())
        self._canvas_width = max(1, canvas_width)
        self._canvas_height = max(1, canvas_height)
        self.update()

    def _destination(
            self, image: QImage, anchor: QPoint,
            offset: QPoint, origin: QPoint) -> QRect:
        return QRect(
            origin.x() + round((-anchor.x() + offset.x()) * self._scale),
            origin.y() + round((-anchor.y() + offset.y()) * self._scale),
            max(1, round(image.width() * self._scale)),
            max(1, round(image.height() * self._scale)),
        )

    def paintEvent(self, unused: object) -> None:  # type: ignore[override]
        del unused
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#161b22"))
        if self._frame.isNull():
            painter.setPen(QColor("#aeb8c4"))
            painter.drawText(
                self.rect(), Qt.AlignmentFlag.AlignCenter, "No frame")
            return

        reference_width = (
            self._reference_frame.width()
            if not self._reference_frame.isNull() else 0)
        reference_height = (
            self._reference_frame.height()
            if not self._reference_frame.isNull() else 0)
        logical_padding = max(
            8,
            max(
                self._frame.width(), self._frame.height(),
                reference_width, reference_height,
                self._canvas_width, self._canvas_height,
            ) // 2,
        )
        logical_width = max(
            self._frame.width(), reference_width,
            self._canvas_width) + logical_padding * 2
        logical_height = max(
            self._frame.height(), reference_height,
            self._canvas_height) + logical_padding * 2
        self._scale = max(
            0.05,
            min(
                (self.width() - 24) / max(1, logical_width),
                (self.height() - 24) / max(1, logical_height),
            ),
        )

        origin = QPoint(self.width() // 2, self.height() // 2)
        canvas_width = max(1, round(self._canvas_width * self._scale))
        canvas_height = max(1, round(self._canvas_height * self._scale))
        canvas_left = origin.x() - canvas_width // 2
        canvas_top = origin.y() - canvas_height // 2

        painter.setRenderHint(
            QPainter.RenderHint.SmoothPixmapTransform, False)

        if not self._reference_frame.isNull():
            painter.save()
            painter.setOpacity(0.30)
            painter.drawImage(
                self._destination(
                    self._reference_frame,
                    self._reference_anchor,
                    self._reference_offset,
                    origin,
                ),
                self._reference_frame,
            )
            painter.restore()

        painter.drawImage(
            self._destination(
                self._frame, self._anchor, self._offset, origin),
            self._frame,
        )

        guide = QPen(QColor(0, 190, 255, 220))
        guide.setWidth(1)
        guide.setCosmetic(True)
        painter.setPen(guide)
        painter.drawRect(QRect(
            canvas_left, canvas_top,
            canvas_width - 1, canvas_height - 1))

        origin_pen = QPen(QColor(255, 210, 75, 210))
        origin_pen.setWidth(1)
        origin_pen.setCosmetic(True)
        origin_pen.setStyle(Qt.PenStyle.DashLine)
        painter.setPen(origin_pen)
        painter.drawLine(
            origin.x(), canvas_top,
            origin.x(), canvas_top + canvas_height)
        painter.drawLine(
            canvas_left, origin.y(),
            canvas_left + canvas_width, origin.y())
        painter.drawEllipse(origin, 3, 3)

    def mousePressEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if (
            event.button() == Qt.MouseButton.LeftButton
            and not self._frame.isNull()
        ):
            self._drag_origin = event.position().toPoint()
            self._drag_offset = QPoint(self._offset)
            self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if self._drag_origin is None:
            return
        delta = event.position().toPoint() - self._drag_origin
        x = self._drag_offset.x() + round(delta.x() / self._scale)
        y = self._drag_offset.y() + round(delta.y() / self._scale)
        self.offset_dragged.emit(x, y)

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_origin = None
            self.setCursor(Qt.CursorShape.OpenHandCursor)


class FrameSourceContextCanvas(QWidget):
    """Aspect-preserving spritesheet view used to move one source rectangle."""

    source_dragged = Signal(int, int)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._image = QImage()
        self._rects: list[QRect] = []
        self._selected = QRect()
        self._scale = 1.0
        self._image_origin = QPoint()
        self._drag_origin: QPoint | None = None
        self._drag_source = QPoint()
        self.setMinimumSize(420, 170)
        self.setCursor(Qt.CursorShape.OpenHandCursor)

    def set_source(self, image: QImage, rects: list[QRect], selected: QRect) -> None:
        self._image = image
        self._rects = rects
        self._selected = selected
        self.update()

    def paintEvent(self, unused: object) -> None:  # type: ignore[override]
        del unused
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#161b22"))
        if self._image.isNull():
            return
        available_width = max(1, self.width() - 24)
        available_height = max(1, self.height() - 24)
        self._scale = min(
            available_width / self._image.width(),
            available_height / self._image.height())
        draw_width = max(1, round(self._image.width() * self._scale))
        draw_height = max(1, round(self._image.height() * self._scale))
        self._image_origin = QPoint(
            (self.width() - draw_width) // 2,
            (self.height() - draw_height) // 2)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, False)
        painter.drawImage(QRect(
            self._image_origin.x(), self._image_origin.y(),
            draw_width, draw_height), self._image)

        other_pen = QPen(QColor(145, 155, 168, 150))
        other_pen.setWidth(1); other_pen.setCosmetic(True)
        painter.setPen(other_pen)
        for source in self._rects:
            painter.drawRect(self._screen_rect(source))
        selected = self._screen_rect(self._selected)
        painter.fillRect(selected, QColor(0, 190, 255, 45))
        selected_pen = QPen(QColor(0, 190, 255, 255))
        selected_pen.setWidth(2); selected_pen.setCosmetic(True)
        painter.setPen(selected_pen); painter.drawRect(selected)

    def _screen_rect(self, source: QRect) -> QRect:
        return QRect(
            self._image_origin.x() + round(source.x() * self._scale),
            self._image_origin.y() + round(source.y() * self._scale),
            max(1, round(source.width() * self._scale)),
            max(1, round(source.height() * self._scale)))

    def mousePressEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if (event.button() == Qt.MouseButton.LeftButton and
                not self._image.isNull() and
                self._screen_rect(self._selected).contains(
                    event.position().toPoint())):
            self._drag_origin = event.position().toPoint()
            self._drag_source = self._selected.topLeft()
            self.setCursor(Qt.CursorShape.ClosedHandCursor)

    def mouseMoveEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if self._drag_origin is None or self._scale <= 0:
            return
        delta = event.position().toPoint() - self._drag_origin
        self.source_dragged.emit(
            self._drag_source.x() + round(delta.x() / self._scale),
            self._drag_source.y() + round(delta.y() / self._scale))

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_origin = None
            self.setCursor(Qt.CursorShape.OpenHandCursor)


class AnimationFrameAlignmentDialog(QDialog):
    """Align frames and optionally repack them without overwriting the source image."""

    def __init__(self, workspace: ContentWorkspace, animation: ContentDefinition,
                 image: QImage, image_path: Path,
                 asset_root: Path | None,
                 translator: Translator | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.animation = animation
        self.image = image
        self.image_path = image_path
        self.asset_root = asset_root
        self.translate = translator or Translator()
        self.data = copy.deepcopy(animation.data)
        raw_frames = self.data.get("frames", [])
        self.frames = raw_frames if isinstance(raw_frames, list) else []
        for frame in self.frames:
            if not isinstance(frame, dict):
                continue
            offset = frame.get("drawOffset", {})
            frame["drawOffset"] = {
                "x": int(offset.get("x", 0)) if isinstance(offset, dict) else 0,
                "y": int(offset.get("y", 0)) if isinstance(offset, dict) else 0,
            }
            source = frame.get("source", {})
            source_width = (
                int(source.get("width", 1))
                if isinstance(source, dict) else 1)
            source_height = (
                int(source.get("height", 1))
                if isinstance(source, dict) else 1)
            anchor = frame.get("anchor", {})
            frame["anchor"] = {
                "x": (
                    int(anchor.get("x", source_width // 2))
                    if isinstance(anchor, dict)
                    else source_width // 2
                ),
                "y": (
                    int(anchor.get("y", max(0, source_height - 1)))
                    if isinstance(anchor, dict)
                    else max(0, source_height - 1)
                ),
            }
        self._original_sources = [
            copy.deepcopy(frame.get("source", {}))
            if isinstance(frame, dict) else {}
            for frame in self.frames]

        self.source_canvas = FrameSourceContextCanvas()
        self.source_canvas.source_dragged.connect(self._move_source)
        self.canvas = FrameAlignmentCanvas()
        self.canvas.offset_dragged.connect(self._set_offset)
        self.frame_index = QSpinBox(); self.frame_index.setRange(1, max(1, len(self.frames)))
        self.frame_index.valueChanged.connect(self._show_frame)
        previous_button = QPushButton("◀"); previous_button.clicked.connect(self.frame_index.stepDown)
        next_button = QPushButton("▶"); next_button.clicked.connect(self.frame_index.stepUp)
        frame_row = QHBoxLayout(); frame_row.addWidget(previous_button)
        frame_row.addWidget(self.frame_index, 1); frame_row.addWidget(next_button)

        self._reference_images: dict[str, QImage] = {}
        self.reference_animation = QComboBox()
        self.reference_animation.addItem(
            self.translate("frame_reference_none"), "")
        for candidate in sorted(
                self.workspace.definitions("animations"),
                key=lambda value: value.definition_id):
            if candidate.definition_id == self.animation.definition_id:
                continue
            self.reference_animation.addItem(
                f"{candidate.display_name} — {candidate.definition_id}",
                candidate.definition_id,
            )
        suggested_reference = self._suggest_reference_animation_id()
        suggested_index = self.reference_animation.findData(
            suggested_reference)
        if suggested_index >= 0:
            self.reference_animation.setCurrentIndex(suggested_index)
        self.reference_animation.currentIndexChanged.connect(
            self._show_frame)

        self.anchor_x = self._offset_spin(); self.anchor_y = self._offset_spin()
        self.anchor_x.valueChanged.connect(self._anchor_controls_changed)
        self.anchor_y.valueChanged.connect(self._anchor_controls_changed)
        self.offset_x = self._offset_spin(); self.offset_y = self._offset_spin()
        self.offset_x.valueChanged.connect(self._offset_controls_changed)
        self.offset_y.valueChanged.connect(self._offset_controls_changed)
        form = QFormLayout(); form.addRow(self.translate("animation_frame"), frame_row)
        form.addRow(
            self.translate("frame_reference_animation"),
            self.reference_animation)
        self.source_x = QSpinBox(); self.source_y = QSpinBox()
        self.source_width = QSpinBox(); self.source_height = QSpinBox()
        self.source_x.setRange(0, max(0, image.width() - 1))
        self.source_y.setRange(0, max(0, image.height() - 1))
        self.source_width.setRange(1, max(1, image.width()))
        self.source_height.setRange(1, max(1, image.height()))
        for control in (
                self.source_x, self.source_y,
                self.source_width, self.source_height):
            control.valueChanged.connect(self._source_controls_changed)
        form.addRow(self.translate("frame_source_x"), self.source_x)
        form.addRow(self.translate("frame_source_y"), self.source_y)
        form.addRow(self.translate("frame_source_width"), self.source_width)
        form.addRow(self.translate("frame_source_height"), self.source_height)
        frame_sizes = [
            self._source_rect(frame) for frame in self.frames
            if isinstance(frame, dict)]
        initial_canvas_width = max(
            (source.width() for source in frame_sizes), default=1)
        initial_canvas_height = max(
            (source.height() for source in frame_sizes), default=1)
        self.canvas_width = QSpinBox(); self.canvas_height = QSpinBox()
        self.canvas_width.setRange(1, 4096)
        self.canvas_height.setRange(1, 4096)
        self.canvas_width.setValue(initial_canvas_width)
        self.canvas_height.setValue(initial_canvas_height)
        self.canvas_width.valueChanged.connect(self._show_frame)
        self.canvas_height.valueChanged.connect(self._show_frame)
        form.addRow(self.translate("frame_canvas_width"), self.canvas_width)
        form.addRow(self.translate("frame_canvas_height"), self.canvas_height)
        form.addRow(self.translate("frame_anchor_x"), self.anchor_x)
        form.addRow(self.translate("frame_anchor_y"), self.anchor_y)
        form.addRow(self.translate("frame_offset_x"), self.offset_x)
        form.addRow(self.translate("frame_offset_y"), self.offset_y)
        self.move_all = QCheckBox(self.translate("move_all_frames"))
        form.addRow(self.move_all)

        arrows = QVBoxLayout()
        up = QPushButton("↑"); up.clicked.connect(lambda: self._move(0, -1))
        horizontal = QHBoxLayout()
        left = QPushButton("←"); left.clicked.connect(lambda: self._move(-1, 0))
        right = QPushButton("→"); right.clicked.connect(lambda: self._move(1, 0))
        horizontal.addWidget(left); horizontal.addWidget(right)
        down = QPushButton("↓"); down.clicked.connect(lambda: self._move(0, 1))
        arrows.addWidget(up); arrows.addLayout(horizontal); arrows.addWidget(down)

        center_frame = QPushButton(self.translate("center_current_frame"))
        center_frame.clicked.connect(self._center_current)
        center_all = QPushButton(self.translate("center_all_frames"))
        center_all.clicked.connect(self._center_all)
        reset_frame = QPushButton(self.translate("reset_current_frame"))
        reset_frame.clicked.connect(lambda: self._set_offset(0, 0))
        reset_all = QPushButton(self.translate("reset_all_frames"))
        reset_all.clicked.connect(self._reset_all)
        reset_source = QPushButton(self.translate("reset_frame_source"))
        reset_source.clicked.connect(self._reset_current_source)
        anchor_from_reference = QPushButton(
            self.translate("frame_anchor_from_reference"))
        anchor_from_reference.clicked.connect(
            self._set_anchor_from_reference)
        apply_anchor_all = QPushButton(
            self.translate("apply_anchor_all_frames"))
        apply_anchor_all.clicked.connect(
            self._apply_anchor_to_all_frames)
        canvas_help = QLabel(self.translate("frame_canvas_help"))
        canvas_help.setWordWrap(True)
        canvas_help.setStyleSheet("color: #8ecae6;")
        help_label = QLabel(self.translate("frame_alignment_help")); help_label.setWordWrap(True)
        help_label.setStyleSheet("color: #aeb8c4;")

        controls = QVBoxLayout(); controls.addLayout(form)
        controls.addWidget(canvas_help); controls.addLayout(arrows)
        controls.addWidget(center_frame); controls.addWidget(center_all)
        controls.addWidget(anchor_from_reference)
        controls.addWidget(apply_anchor_all)
        controls.addWidget(reset_frame); controls.addWidget(reset_all)
        controls.addWidget(reset_source)
        controls.addWidget(help_label); controls.addStretch(1)
        preview_column = QVBoxLayout()
        source_title = QLabel(self.translate("frame_source_context"))
        source_title.setStyleSheet("font-weight: bold;")
        result_title = QLabel(self.translate("frame_result_preview"))
        result_title.setStyleSheet("font-weight: bold;")
        preview_column.addWidget(source_title)
        preview_column.addWidget(self.source_canvas, 1)
        preview_column.addWidget(result_title)
        preview_column.addWidget(self.canvas, 2)
        body = QHBoxLayout(); body.addLayout(preview_column, 1); body.addLayout(controls)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self._save); buttons.rejected.connect(self.reject)
        layout = QVBoxLayout(self); layout.addLayout(body, 1); layout.addWidget(buttons)
        self.setWindowTitle(self.translate("edit_animation_frames"))
        self.resize(980, 760)
        self._show_frame()

    @staticmethod
    def _offset_spin() -> QSpinBox:
        value = QSpinBox(); value.setRange(-4096, 4096); return value

    def _current_frame(self) -> dict[str, object] | None:
        index = self.frame_index.value() - 1
        if 0 <= index < len(self.frames) and isinstance(self.frames[index], dict):
            return self.frames[index]
        return None

    @staticmethod
    def _frame_image_from(
            frame: dict[str, object] | None,
            source_image: QImage) -> QImage:
        source = frame.get("source") if frame else None
        if not isinstance(source, dict) or source_image.isNull():
            return QImage()
        try:
            return source_image.copy(
                int(source.get("x", 0)),
                int(source.get("y", 0)),
                int(source.get("width", 0)),
                int(source.get("height", 0)),
            )
        except (TypeError, ValueError):
            return QImage()

    def _frame_image(self, frame: dict[str, object] | None) -> QImage:
        return self._frame_image_from(frame, self.image)

    @staticmethod
    def _directional_animation_id(
            reference: object, direction: str) -> str:
        if not isinstance(reference, dict):
            return ""
        candidates: list[object] = [reference.get(direction)]
        if direction in {"left", "right"}:
            candidates.append(reference.get("side"))
        candidates.extend([
            reference.get("defaultAnimation"),
            reference.get("down"),
            reference.get("up"),
            reference.get("left"),
            reference.get("right"),
            reference.get("side"),
        ])
        for candidate in candidates:
            if isinstance(candidate, str) and candidate:
                return candidate
        return ""

    def _suggest_reference_animation_id(self) -> str:
        current_id = self.animation.definition_id
        directions = ("down", "up", "left", "right")
        for visual in self.workspace.definitions("playerVisuals"):
            data = visual.data
            idle = data.get("idle")
            if not isinstance(idle, dict):
                continue
            bindings: list[object] = [
                data.get("walk"),
                data.get("hurt"),
            ]
            actions = data.get("actions", [])
            if isinstance(actions, list):
                for action in actions:
                    if isinstance(action, dict):
                        bindings.append(action.get("clips"))
            for direction in directions:
                if not any(
                    self._directional_animation_id(binding, direction)
                    == current_id
                    for binding in bindings
                ):
                    continue
                suggested = self._directional_animation_id(
                    idle, direction)
                if (
                    suggested
                    and suggested != current_id
                    and self.workspace.find(
                        "animations", suggested) is not None
                ):
                    return suggested
        return ""

    @staticmethod
    def _frame_point(
            frame: dict[str, object], key: str,
            default: QPoint | None = None) -> QPoint:
        raw = frame.get(key, {})
        fallback = default or QPoint()
        if not isinstance(raw, dict):
            return QPoint(fallback)
        try:
            return QPoint(
                int(raw.get("x", fallback.x())),
                int(raw.get("y", fallback.y())),
            )
        except (TypeError, ValueError):
            return QPoint(fallback)

    def _reference_payload(self) -> tuple[QImage, QPoint, QPoint]:
        reference_id = self.reference_animation.currentData()
        if not isinstance(reference_id, str) or not reference_id:
            return QImage(), QPoint(), QPoint()
        animation = self.workspace.find("animations", reference_id)
        if animation is None:
            return QImage(), QPoint(), QPoint()
        raw_frames = animation.data.get("frames", [])
        if not isinstance(raw_frames, list) or not raw_frames:
            return QImage(), QPoint(), QPoint()
        reference_frame = raw_frames[0]
        if not isinstance(reference_frame, dict):
            return QImage(), QPoint(), QPoint()

        source_image = self._reference_images.get(reference_id)
        if source_image is None:
            loaded = load_definition_image(
                animation, self.workspace, self.asset_root)
            source_image = QImage(loaded) if loaded is not None else QImage()
            self._reference_images[reference_id] = source_image

        return (
            self._frame_image_from(reference_frame, source_image),
            self._frame_point(reference_frame, "anchor"),
            self._frame_point(reference_frame, "drawOffset"),
        )

    @staticmethod
    def _source_rect(frame: dict[str, object] | None) -> QRect:
        source = frame.get("source") if frame else None
        if not isinstance(source, dict):
            return QRect()
        try:
            return QRect(
                int(source.get("x", 0)), int(source.get("y", 0)),
                max(1, int(source.get("width", 1))),
                max(1, int(source.get("height", 1))))
        except (TypeError, ValueError):
            return QRect()

    def _show_frame(self, unused: object = None) -> None:
        del unused
        frame = self._current_frame()
        offset = frame.get("drawOffset", {}) if frame else {}
        x = int(offset.get("x", 0)) if isinstance(offset, dict) else 0
        y = int(offset.get("y", 0)) if isinstance(offset, dict) else 0
        anchor = frame.get("anchor", {}) if frame else {}
        source = self._source_rect(frame)
        anchor_x = (
            int(anchor.get("x", source.width() // 2))
            if isinstance(anchor, dict) else source.width() // 2)
        anchor_y = (
            int(anchor.get("y", max(0, source.height() - 1)))
            if isinstance(anchor, dict)
            else max(0, source.height() - 1))

        for control, value in (
                (self.offset_x, x), (self.offset_y, y),
                (self.anchor_x, anchor_x), (self.anchor_y, anchor_y)):
            control.blockSignals(True)
            control.setValue(value)
            control.blockSignals(False)

        for control, value in (
                (self.source_x, source.x()), (self.source_y, source.y()),
                (self.source_width, source.width()),
                (self.source_height, source.height())):
            control.blockSignals(True)
            control.setValue(value)
            control.blockSignals(False)

        all_sources = [
            self._source_rect(value) for value in self.frames
            if isinstance(value, dict)]
        self.source_canvas.set_source(
            self.image, all_sources, source)

        reference_frame, reference_anchor, reference_offset = (
            self._reference_payload())
        self.canvas.set_frame(
            self._frame_image(frame),
            x, y,
            self.canvas_width.value(),
            self.canvas_height.value(),
            anchor_x,
            anchor_y,
            reference_frame,
            reference_anchor,
            reference_offset,
        )

    def _source_controls_changed(self, unused: object = None) -> None:
        del unused
        self._set_source(
            self.source_x.value(), self.source_y.value(),
            self.source_width.value(), self.source_height.value())

    def _move_source(self, x: int, y: int) -> None:
        self._set_source(
            x, y, self.source_width.value(), self.source_height.value())

    def _set_source(self, x: int, y: int, width: int, height: int) -> None:
        frame = self._current_frame()
        if frame is None or self.image.isNull():
            return
        previous = self._source_rect(frame)
        width = max(1, min(width, self.image.width()))
        height = max(1, min(height, self.image.height()))
        x = max(0, min(x, self.image.width() - width))
        y = max(0, min(y, self.image.height() - height))
        frame["source"] = {
            "x": x, "y": y, "width": width, "height": height}
        offset = frame.get("drawOffset", {})
        old_offset_x = int(offset.get("x", 0)) if isinstance(offset, dict) else 0
        old_offset_y = int(offset.get("y", 0)) if isinstance(offset, dict) else 0
        frame["drawOffset"] = {
            "x": old_offset_x + x - previous.x(),
            "y": old_offset_y + y - previous.y()}
        self._show_frame()

    def _anchor_controls_changed(self, unused: object = None) -> None:
        del unused
        self._set_anchor(
            self.anchor_x.value(), self.anchor_y.value())

    def _set_anchor(self, x: int, y: int) -> None:
        frame = self._current_frame()
        if frame is None:
            return
        frame["anchor"] = {"x": int(x), "y": int(y)}
        self._show_frame()

    def _apply_anchor_to_all_frames(self) -> None:
        frame = self._current_frame()
        if frame is None:
            return
        anchor = self._frame_point(frame, "anchor")
        for value in self.frames:
            if isinstance(value, dict):
                value["anchor"] = {
                    "x": anchor.x(),
                    "y": anchor.y(),
                }
        self._show_frame()

    def _set_anchor_from_reference(self) -> None:
        frame = self._current_frame()
        if frame is None:
            return
        source = self._source_rect(frame)
        reference_frame, reference_anchor, unused_offset = (
            self._reference_payload())
        del unused_offset
        anchor_y = (
            reference_anchor.y()
            if not reference_frame.isNull()
            else max(0, source.height() - 1)
        )
        self._set_anchor(source.width() // 2, anchor_y)

    def _offset_controls_changed(self, unused: object = None) -> None:
        del unused
        self._set_offset(self.offset_x.value(), self.offset_y.value())

    def _set_offset(self, x: int, y: int) -> None:
        frame = self._current_frame()
        if frame is None:
            return
        current = frame.get("drawOffset", {})
        current_x = int(current.get("x", 0)) if isinstance(current, dict) else 0
        current_y = int(current.get("y", 0)) if isinstance(current, dict) else 0
        if self.move_all.isChecked():
            delta_x = x - current_x
            delta_y = y - current_y
            for value in self.frames:
                if not isinstance(value, dict):
                    continue
                offset = value.get("drawOffset", {})
                offset_x = int(offset.get("x", 0)) if isinstance(offset, dict) else 0
                offset_y = int(offset.get("y", 0)) if isinstance(offset, dict) else 0
                value["drawOffset"] = {
                    "x": offset_x + delta_x, "y": offset_y + delta_y}
            self._show_frame()
            return
        frame["drawOffset"] = {"x": x, "y": y}
        self._show_frame()

    def _move(self, x: int, y: int) -> None:
        self._set_offset(self.offset_x.value() + x, self.offset_y.value() + y)

    def _center_current(self) -> None:
        frame = self._current_frame()
        offset = self._center_offset(
            self._frame_image(frame), self.canvas_width.value(),
            self.canvas_height.value())
        self._set_offset(offset.x(), offset.y())

    def _center_all(self) -> None:
        current = self.frame_index.value()
        for index, frame in enumerate(self.frames):
            if not isinstance(frame, dict):
                continue
            offset = self._center_offset(
                self._frame_image(frame), self.canvas_width.value(),
                self.canvas_height.value())
            frame["drawOffset"] = {"x": offset.x(), "y": offset.y()}
        self.frame_index.setValue(current)
        self._show_frame()

    def _reset_all(self) -> None:
        for frame in self.frames:
            if isinstance(frame, dict):
                frame["drawOffset"] = {"x": 0, "y": 0}
        self._show_frame()

    def _reset_current_source(self) -> None:
        index = self.frame_index.value() - 1
        frame = self._current_frame()
        if frame is None or not 0 <= index < len(self._original_sources):
            return
        original = self._original_sources[index]
        if not isinstance(original, dict):
            return
        self._set_source(
            int(original.get("x", 0)), int(original.get("y", 0)),
            int(original.get("width", 1)), int(original.get("height", 1)))

    @staticmethod
    def _center_offset(frame: QImage, canvas_width: int,
                       canvas_height: int) -> QPoint:
        if frame.isNull():
            return QPoint()
        left, top, right, bottom = frame.width(), frame.height(), -1, -1
        for y in range(frame.height()):
            for x in range(frame.width()):
                if frame.pixelColor(x, y).alpha() == 0:
                    continue
                left = min(left, x); top = min(top, y)
                right = max(right, x); bottom = max(bottom, y)
        if right < left or bottom < top:
            return QPoint()
        content_width = right - left + 1
        content_height = bottom - top + 1
        base_x = (canvas_width - frame.width()) // 2
        base_y = (canvas_height - frame.height()) // 2
        return QPoint(
            (canvas_width - content_width) // 2 - base_x - left,
            (canvas_height - content_height) // 2 - base_y - top,
        )

    def _save(self) -> None:
        try:
            if self._needs_repack():
                self._save_repacked_spritesheet()
            else:
                self.workspace.replace_definition(self.animation, self.data)
        except (OSError, ValueError) as error:
            QMessageBox.warning(
                self, self.translate("edit_animation_frames"), str(error))
            return
        self.accept()

    def _needs_repack(self) -> bool:
        width = self.canvas_width.value()
        height = self.canvas_height.value()
        return any(
            isinstance(frame, dict) and
            (self._source_rect(frame).width() != width or
             self._source_rect(frame).height() != height)
            for frame in self.frames)

    def _save_repacked_spritesheet(self) -> None:
        if not self.frames:
            raise ValueError("the animation has no frames to reorganize")
        canvas_width = self.canvas_width.value()
        canvas_height = self.canvas_height.value()
        columns = self._original_column_count()
        rows = (len(self.frames) + columns - 1) // columns
        atlas_width = canvas_width * columns
        atlas_height = canvas_height * rows
        if (atlas_width > 16384 or atlas_height > 16384 or
                atlas_width * atlas_height > 64 * 1024 * 1024):
            raise ValueError(self.translate("frame_canvas_too_large"))
        atlas = QImage(
            atlas_width, atlas_height,
            QImage.Format.Format_ARGB32)
        atlas.fill(Qt.GlobalColor.transparent)
        painter = QPainter(atlas)
        rewritten: list[dict[str, object]] = []
        try:
            for index, frame in enumerate(self.frames):
                if not isinstance(frame, dict):
                    raise ValueError(f"frame {index + 1} is invalid")
                source = self._source_rect(frame)
                cropped = self._frame_image(frame)
                offset = frame.get("drawOffset", {})
                offset_x = int(offset.get("x", 0)) if isinstance(offset, dict) else 0
                offset_y = int(offset.get("y", 0)) if isinstance(offset, dict) else 0
                base_x = (canvas_width - source.width()) // 2
                base_y = (canvas_height - source.height()) // 2
                local_x = base_x + offset_x
                local_y = base_y + offset_y
                if (local_x < 0 or local_y < 0 or
                        local_x + source.width() > canvas_width or
                        local_y + source.height() > canvas_height):
                    raise ValueError(self.translate(
                        "frame_does_not_fit_canvas", frame=index + 1))
                cell_x = (index % columns) * canvas_width
                cell_y = (index // columns) * canvas_height
                painter.drawImage(cell_x + local_x, cell_y + local_y, cropped)
                rewritten_frame = copy.deepcopy(frame)
                rewritten_frame["source"] = {
                    "x": cell_x, "y": cell_y,
                    "width": canvas_width, "height": canvas_height}
                anchor = frame.get("anchor", {})
                anchor_x = int(anchor.get("x", 0)) if isinstance(anchor, dict) else 0
                anchor_y = int(anchor.get("y", 0)) if isinstance(anchor, dict) else 0
                rewritten_frame["anchor"] = {
                    "x": anchor_x + base_x, "y": anchor_y + base_y}
                rewritten_frame["drawOffset"] = {"x": 0, "y": 0}
                rewritten.append(rewritten_frame)
        finally:
            painter.end()

        image_id, output_path, image_data = self._aligned_image_target()
        output_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = output_path.with_name(
            f"{output_path.stem}.tmp{output_path.suffix}")
        if not atlas.save(str(temporary), "PNG"):
            raise OSError(f"could not write corrected spritesheet: {temporary}")
        temporary.replace(output_path)

        image_definition = self.workspace.find("visualImages", image_id)
        if image_definition is None:
            image_definition = self.workspace.create_definition(
                "visualImages", image_id, self.animation.source_path)
        self.workspace.replace_definition(image_definition, image_data)
        self.data["imageId"] = image_id
        self.data["frames"] = rewritten
        self.workspace.replace_definition(self.animation, self.data)

    def _original_column_count(self) -> int:
        rows: dict[int, int] = {}
        for source in self._original_sources:
            if not isinstance(source, dict):
                continue
            y = int(source.get("y", 0))
            rows[y] = rows.get(y, 0) + 1
        return max(rows.values(), default=max(1, len(self.frames)))

    def _aligned_image_target(self) -> tuple[str, Path, dict[str, object]]:
        current_id = str(self.data.get("imageId", ""))
        image_definition = self.workspace.find("visualImages", current_id)
        if image_definition is None:
            raise ValueError(f"visual image does not exist: {current_id}")
        root_kind = str(image_definition.data.get("root", "gameAssets"))
        root = self.asset_root if root_kind == "gameAssets" else self.workspace.root
        if root is None:
            raise ValueError("the asset root is unavailable")
        safe_animation = re.sub(
            r"[^A-Za-z0-9_.-]+", "_", self.animation.definition_id).strip("._")
        if ".aligned." in current_id:
            image_id = current_id
            output_path = self.image_path
        else:
            image_id = f"{current_id}.aligned.{safe_animation}"
            output_path = self.image_path.with_name(
                f"{self.image_path.stem}.{safe_animation}.aligned.png")
        try:
            relative = output_path.resolve().relative_to(root.resolve()).as_posix()
        except ValueError as error:
            raise ValueError("the corrected spritesheet must remain inside its asset root") from error
        return image_id, output_path, {
            "id": image_id, "root": root_kind, "relativePath": relative}
