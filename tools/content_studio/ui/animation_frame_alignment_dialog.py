from __future__ import annotations

import copy

from PySide6.QtCore import QPoint, QRect, Qt, Signal
from PySide6.QtGui import QColor, QImage, QMouseEvent, QPainter, QPen
from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QFormLayout, QHBoxLayout, QLabel, QPushButton,
    QSpinBox, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.localization import Translator


class FrameAlignmentCanvas(QWidget):
    """Pixel-art frame preview whose contents can be dragged by whole pixels."""

    offset_dragged = Signal(int, int)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._frame = QImage()
        self._offset = QPoint()
        self._scale = 1.0
        self._drag_origin: QPoint | None = None
        self._drag_offset = QPoint()
        self.setMinimumSize(420, 420)
        self.setCursor(Qt.CursorShape.OpenHandCursor)

    def set_frame(self, frame: QImage, offset_x: int, offset_y: int) -> None:
        self._frame = frame
        self._offset = QPoint(offset_x, offset_y)
        self.update()

    def paintEvent(self, unused: object) -> None:  # type: ignore[override]
        del unused
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#161b22"))
        if self._frame.isNull():
            painter.setPen(QColor("#aeb8c4"))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "No frame")
            return

        logical_padding = max(4, max(self._frame.width(), self._frame.height()) // 3)
        logical_width = self._frame.width() + logical_padding * 2
        logical_height = self._frame.height() + logical_padding * 2
        self._scale = max(1.0, min(
            (self.width() - 24) / logical_width,
            (self.height() - 24) / logical_height,
        ))
        frame_width = round(self._frame.width() * self._scale)
        frame_height = round(self._frame.height() * self._scale)
        frame_left = (self.width() - frame_width) // 2
        frame_top = (self.height() - frame_height) // 2
        destination = QRect(
            frame_left + round(self._offset.x() * self._scale),
            frame_top + round(self._offset.y() * self._scale),
            frame_width,
            frame_height,
        )

        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, False)
        painter.drawImage(destination, self._frame)
        guide = QPen(QColor(0, 190, 255, 220)); guide.setWidth(1); guide.setCosmetic(True)
        painter.setPen(guide)
        painter.drawRect(QRect(frame_left, frame_top, frame_width - 1, frame_height - 1))
        center = QPen(QColor(255, 210, 75, 170)); center.setWidth(1); center.setCosmetic(True)
        center.setStyle(Qt.PenStyle.DashLine); painter.setPen(center)
        painter.drawLine(frame_left + frame_width // 2, frame_top,
                         frame_left + frame_width // 2, frame_top + frame_height)
        painter.drawLine(frame_left, frame_top + frame_height // 2,
                         frame_left + frame_width, frame_top + frame_height // 2)

    def mousePressEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() == Qt.MouseButton.LeftButton and not self._frame.isNull():
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


class AnimationFrameAlignmentDialog(QDialog):
    """Edit per-frame draw offsets without modifying the source image."""

    def __init__(self, workspace: ContentWorkspace, animation: ContentDefinition,
                 image: QImage, translator: Translator | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.animation = animation
        self.image = image
        self.translate = translator or Translator()
        self.data = copy.deepcopy(animation.data)
        raw_frames = self.data.get("frames", [])
        self.frames = raw_frames if isinstance(raw_frames, list) else []

        self.canvas = FrameAlignmentCanvas()
        self.canvas.offset_dragged.connect(self._set_offset)
        self.frame_index = QSpinBox(); self.frame_index.setRange(1, max(1, len(self.frames)))
        self.frame_index.valueChanged.connect(self._show_frame)
        previous_button = QPushButton("◀"); previous_button.clicked.connect(self.frame_index.stepDown)
        next_button = QPushButton("▶"); next_button.clicked.connect(self.frame_index.stepUp)
        frame_row = QHBoxLayout(); frame_row.addWidget(previous_button)
        frame_row.addWidget(self.frame_index, 1); frame_row.addWidget(next_button)

        self.offset_x = self._offset_spin(); self.offset_y = self._offset_spin()
        self.offset_x.valueChanged.connect(self._offset_controls_changed)
        self.offset_y.valueChanged.connect(self._offset_controls_changed)
        form = QFormLayout(); form.addRow(self.translate("animation_frame"), frame_row)
        form.addRow(self.translate("frame_offset_x"), self.offset_x)
        form.addRow(self.translate("frame_offset_y"), self.offset_y)

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
        help_label = QLabel(self.translate("frame_alignment_help")); help_label.setWordWrap(True)
        help_label.setStyleSheet("color: #aeb8c4;")

        controls = QVBoxLayout(); controls.addLayout(form); controls.addLayout(arrows)
        controls.addWidget(center_frame); controls.addWidget(center_all)
        controls.addWidget(reset_frame); controls.addWidget(reset_all)
        controls.addWidget(help_label); controls.addStretch(1)
        body = QHBoxLayout(); body.addWidget(self.canvas, 1); body.addLayout(controls)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self._save); buttons.rejected.connect(self.reject)
        layout = QVBoxLayout(self); layout.addLayout(body, 1); layout.addWidget(buttons)
        self.setWindowTitle(self.translate("edit_animation_frames"))
        self.resize(820, 560)
        self._show_frame()

    @staticmethod
    def _offset_spin() -> QSpinBox:
        value = QSpinBox(); value.setRange(-4096, 4096); return value

    def _current_frame(self) -> dict[str, object] | None:
        index = self.frame_index.value() - 1
        if 0 <= index < len(self.frames) and isinstance(self.frames[index], dict):
            return self.frames[index]
        return None

    def _frame_image(self, frame: dict[str, object] | None) -> QImage:
        source = frame.get("source") if frame else None
        if not isinstance(source, dict):
            return QImage()
        try:
            return self.image.copy(
                int(source.get("x", 0)), int(source.get("y", 0)),
                int(source.get("width", 0)), int(source.get("height", 0)),
            )
        except (TypeError, ValueError):
            return QImage()

    def _show_frame(self, unused: object = None) -> None:
        del unused
        frame = self._current_frame()
        offset = frame.get("drawOffset", {}) if frame else {}
        x = int(offset.get("x", 0)) if isinstance(offset, dict) else 0
        y = int(offset.get("y", 0)) if isinstance(offset, dict) else 0
        self.offset_x.blockSignals(True); self.offset_y.blockSignals(True)
        self.offset_x.setValue(x); self.offset_y.setValue(y)
        self.offset_x.blockSignals(False); self.offset_y.blockSignals(False)
        self.canvas.set_frame(self._frame_image(frame), x, y)

    def _offset_controls_changed(self, unused: object = None) -> None:
        del unused
        self._set_offset(self.offset_x.value(), self.offset_y.value())

    def _set_offset(self, x: int, y: int) -> None:
        frame = self._current_frame()
        if frame is None:
            return
        frame["drawOffset"] = {"x": x, "y": y}
        self._show_frame()

    def _move(self, x: int, y: int) -> None:
        self._set_offset(self.offset_x.value() + x, self.offset_y.value() + y)

    def _center_current(self) -> None:
        frame = self._current_frame()
        offset = self._center_offset(self._frame_image(frame))
        self._set_offset(offset.x(), offset.y())

    def _center_all(self) -> None:
        current = self.frame_index.value()
        for index, frame in enumerate(self.frames):
            if not isinstance(frame, dict):
                continue
            offset = self._center_offset(self._frame_image(frame))
            frame["drawOffset"] = {"x": offset.x(), "y": offset.y()}
        self.frame_index.setValue(current)
        self._show_frame()

    def _reset_all(self) -> None:
        for frame in self.frames:
            if isinstance(frame, dict):
                frame["drawOffset"] = {"x": 0, "y": 0}
        self._show_frame()

    @staticmethod
    def _center_offset(frame: QImage) -> QPoint:
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
        return QPoint(
            (frame.width() - content_width) // 2 - left,
            (frame.height() - content_height) // 2 - top,
        )

    def _save(self) -> None:
        self.workspace.replace_definition(self.animation, self.data)
        self.accept()
