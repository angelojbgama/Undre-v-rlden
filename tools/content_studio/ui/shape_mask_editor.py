from __future__ import annotations

from PySide6.QtCore import QPoint, QRect, Qt, Signal
from PySide6.QtGui import QColor, QImage, QMouseEvent, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QHBoxLayout, QLabel,
    QPushButton, QScrollArea, QSpinBox, QVBoxLayout, QWidget,
)

from ..services.localization import Translator


class ShapeMaskCanvas(QWidget):
    changed = Signal()

    def __init__(self, image: QImage, width: int, height: int,
                 cells: list[int], origin_x: int, origin_y: int,
                 parent: QWidget | None = None,
                 depth_anchor: dict[str, int] | None = None) -> None:
        super().__init__(parent)
        self.image = image
        self.mask_width = max(1, width)
        self.mask_height = max(1, height)
        expected = self.mask_width * self.mask_height
        self.cells = list(cells[:expected]) + [0] * max(0, expected - len(cells))
        self.cells = [1 if value else 0 for value in self.cells]
        self.origin_x = origin_x
        self.origin_y = origin_y
        self.depth_anchor_x = (
            int(depth_anchor.get("x", 0)) if isinstance(depth_anchor, dict) else None)
        self.depth_anchor_y = (
            int(depth_anchor.get("y", 0)) if isinstance(depth_anchor, dict) else None)
        self.zoom = 12
        self.tool = "brush"
        self.show_sprite = True
        self.show_mask = True
        self.show_anchor = True
        self._drawing = False
        self._rectangle_start: tuple[int, int] | None = None
        self._rectangle_end: tuple[int, int] | None = None
        self.setMouseTracking(True)
        self._resize_canvas()

    def _resize_canvas(self) -> None:
        self.setFixedSize(self.mask_width * self.zoom, self.mask_height * self.zoom)
        self.update()

    def set_zoom(self, value: int) -> None:
        self.zoom = max(2, value)
        self._resize_canvas()

    def set_tool(self, value: str) -> None:
        self.tool = value
        self._rectangle_start = None
        self._rectangle_end = None
        self.update()

    def _cell_at(self, point: QPoint) -> tuple[int, int] | None:
        x = point.x() // self.zoom
        y = point.y() // self.zoom
        if 0 <= x < self.mask_width and 0 <= y < self.mask_height:
            return x, y
        return None

    def _set_cell(self, x: int, y: int, value: int) -> None:
        index = y * self.mask_width + x
        value = 1 if value else 0
        if self.cells[index] != value:
            self.cells[index] = value
            self.changed.emit()
            self.update()

    def _apply_point(self, point: QPoint) -> None:
        cell = self._cell_at(point)
        if cell is None:
            return
        x, y = cell
        if self.tool == "depth":
            self.depth_anchor_x = self.origin_x + x
            self.depth_anchor_y = self.origin_y + y
            self.changed.emit()
            self.update()
            return
        self._set_cell(x, y, 0 if self.tool == "erase" else 1)

    def mousePressEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() != Qt.MouseButton.LeftButton:
            return
        cell = self._cell_at(event.position().toPoint())
        if cell is None:
            return
        self._drawing = True
        if self.tool == "rectangle":
            self._rectangle_start = cell
            self._rectangle_end = cell
            self.update()
        else:
            self._apply_point(event.position().toPoint())

    def mouseMoveEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if not self._drawing:
            return
        cell = self._cell_at(event.position().toPoint())
        if cell is None:
            return
        if self.tool == "rectangle":
            self._rectangle_end = cell
            self.update()
        else:
            self._apply_point(event.position().toPoint())

    def mouseReleaseEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        if event.button() != Qt.MouseButton.LeftButton or not self._drawing:
            return
        self._drawing = False
        if self.tool != "rectangle" or self._rectangle_start is None:
            return
        end = self._cell_at(event.position().toPoint()) or self._rectangle_end
        if end is None:
            return
        x0, y0 = self._rectangle_start
        x1, y1 = end
        left, right = sorted((x0, x1))
        top, bottom = sorted((y0, y1))
        changed = False
        for y in range(top, bottom + 1):
            for x in range(left, right + 1):
                index = y * self.mask_width + x
                if self.cells[index] != 1:
                    self.cells[index] = 1
                    changed = True
        self._rectangle_start = None
        self._rectangle_end = None
        if changed:
            self.changed.emit()
        self.update()

    def paintEvent(self, event: object) -> None:  # type: ignore[override]
        del event
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor(24, 29, 35))
        destination = QRect(0, 0, self.mask_width * self.zoom,
                            self.mask_height * self.zoom)
        if self.show_sprite and not self.image.isNull():
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, False)
            painter.drawImage(destination, self.image)
        if self.show_mask:
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor(255, 55, 55, 115))
            for y in range(self.mask_height):
                row = y * self.mask_width
                for x in range(self.mask_width):
                    if self.cells[row + x]:
                        painter.drawRect(x * self.zoom, y * self.zoom,
                                         self.zoom, self.zoom)
        if self._rectangle_start is not None and self._rectangle_end is not None:
            x0, y0 = self._rectangle_start
            x1, y1 = self._rectangle_end
            left, right = sorted((x0, x1))
            top, bottom = sorted((y0, y1))
            painter.setBrush(QColor(255, 90, 90, 55))
            painter.setPen(QPen(QColor(255, 170, 170), 1))
            painter.drawRect(left * self.zoom, top * self.zoom,
                             (right - left + 1) * self.zoom,
                             (bottom - top + 1) * self.zoom)
        if self.zoom >= 6 and self.mask_width <= 192 and self.mask_height <= 192:
            painter.setPen(QPen(QColor(255, 255, 255, 35), 1))
            for x in range(self.mask_width + 1):
                painter.drawLine(x * self.zoom, 0, x * self.zoom, self.height())
            for y in range(self.mask_height + 1):
                painter.drawLine(0, y * self.zoom, self.width(), y * self.zoom)
        if self.show_anchor:
            anchor_x = -self.origin_x
            anchor_y = -self.origin_y
            if 0 <= anchor_x <= self.mask_width and 0 <= anchor_y <= self.mask_height:
                px = anchor_x * self.zoom
                py = anchor_y * self.zoom
                painter.setPen(QPen(QColor(80, 210, 255), 2))
                painter.drawLine(max(0, px - 12), py, min(self.width(), px + 12), py)
                painter.drawLine(px, max(0, py - 12), px, min(self.height(), py + 12))
            if self.depth_anchor_x is not None and self.depth_anchor_y is not None:
                local_x = self.depth_anchor_x - self.origin_x
                local_y = self.depth_anchor_y - self.origin_y
                if 0 <= local_x < self.mask_width and 0 <= local_y < self.mask_height:
                    px = local_x * self.zoom + self.zoom // 2
                    py = local_y * self.zoom + self.zoom // 2
                    painter.setPen(QPen(QColor(255, 220, 70), 2))
                    painter.drawLine(max(0, px - 14), py, min(self.width(), px + 14), py)
                    painter.drawLine(px, max(0, py - 14), px, min(self.height(), py + 14))
        painter.end()

    def generate_from_alpha(self) -> None:
        if self.image.isNull():
            return
        image = self.image.convertToFormat(QImage.Format.Format_ARGB32)
        for y in range(self.mask_height):
            for x in range(self.mask_width):
                self.cells[y * self.mask_width + x] = (
                    1 if image.pixelColor(x, y).alpha() > 16 else 0)
        self.changed.emit()
        self.update()

    def fill_all(self) -> None:
        self.cells = [1] * (self.mask_width * self.mask_height)
        self.changed.emit()
        self.update()

    def clear_all(self) -> None:
        self.cells = [0] * (self.mask_width * self.mask_height)
        self.changed.emit()
        self.update()


class ShapeMaskEditorDialog(QDialog):
    """Reusable pixel-mask authoring dialog; runtime consumes compact AABBs."""

    def __init__(self, image: QImage, mask: dict[str, object],
                 translator: Translator, parent: QWidget | None = None, *,
                 depth_anchor: dict[str, int] | None = None,
                 allow_depth_tool: bool = False,
                 title_key: str = "mask_editor_title") -> None:
        super().__init__(parent)
        self.translate = translator
        width = max(1, int(mask.get("width", image.width() or 1)))
        height = max(1, int(mask.get("height", image.height() or 1)))
        origin = mask.get("origin", {})
        origin = origin if isinstance(origin, dict) else {}
        cells = mask.get("cells", [])
        cells = [int(value) for value in cells] if isinstance(cells, (list, tuple)) else []
        self.canvas = ShapeMaskCanvas(
            image, width, height, cells,
            int(origin.get("x", 0)), int(origin.get("y", 0)), self,
            depth_anchor=depth_anchor)

        self.tool = QComboBox()
        self.tool.addItem(self.translate("mask_tool_brush"), "brush")
        self.tool.addItem(self.translate("mask_tool_erase"), "erase")
        self.tool.addItem(self.translate("mask_tool_rectangle"), "rectangle")
        if allow_depth_tool:
            self.tool.addItem(self.translate("mask_tool_depth"), "depth")
        self.tool.currentIndexChanged.connect(
            lambda: self.canvas.set_tool(str(self.tool.currentData())))
        self.zoom = QSpinBox(); self.zoom.setRange(2, 32); self.zoom.setValue(12)
        self.zoom.setSuffix("×")
        self.zoom.valueChanged.connect(self.canvas.set_zoom)
        self.show_sprite = QCheckBox(self.translate("mask_show_sprite")); self.show_sprite.setChecked(True)
        self.show_mask = QCheckBox(self.translate("mask_show_mask")); self.show_mask.setChecked(True)
        self.show_anchor = QCheckBox(self.translate("mask_show_anchor")); self.show_anchor.setChecked(True)
        self.show_sprite.toggled.connect(self._view_changed)
        self.show_mask.toggled.connect(self._view_changed)
        self.show_anchor.toggled.connect(self._view_changed)

        alpha = QPushButton(self.translate("mask_generate_alpha")); alpha.clicked.connect(self.canvas.generate_from_alpha)
        fill = QPushButton(self.translate("mask_fill_all")); fill.clicked.connect(self.canvas.fill_all)
        clear = QPushButton(self.translate("mask_clear_all")); clear.clicked.connect(self.canvas.clear_all)
        toolbar = QHBoxLayout()
        toolbar.addWidget(QLabel(self.translate("mask_tool"))); toolbar.addWidget(self.tool)
        toolbar.addWidget(alpha); toolbar.addWidget(fill); toolbar.addWidget(clear)
        toolbar.addStretch(1)
        toolbar.addWidget(QLabel(self.translate("mask_zoom"))); toolbar.addWidget(self.zoom)

        viewbar = QHBoxLayout()
        viewbar.addWidget(self.show_sprite); viewbar.addWidget(self.show_mask)
        viewbar.addWidget(self.show_anchor); viewbar.addStretch(1)
        viewbar.addWidget(QLabel(self.translate(
            "mask_origin", x=self.canvas.origin_x, y=self.canvas.origin_y)))

        scroll = QScrollArea(); scroll.setWidget(self.canvas); scroll.setWidgetResizable(False)
        scroll.setAlignment(Qt.AlignmentFlag.AlignCenter)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self.accept); buttons.rejected.connect(self.reject)
        layout = QVBoxLayout(self)
        layout.addLayout(toolbar); layout.addLayout(viewbar); layout.addWidget(scroll, 1)
        layout.addWidget(buttons)
        self.setWindowTitle(self.translate(title_key))
        self.resize(980, 820)

    def _view_changed(self) -> None:
        self.canvas.show_sprite = self.show_sprite.isChecked()
        self.canvas.show_mask = self.show_mask.isChecked()
        self.canvas.show_anchor = self.show_anchor.isChecked()
        self.canvas.update()

    def result_mask(self) -> dict[str, object]:
        return {
            "width": self.canvas.mask_width,
            "height": self.canvas.mask_height,
            "origin": {"x": self.canvas.origin_x, "y": self.canvas.origin_y},
            "cells": list(self.canvas.cells),
        }

    def result_depth_anchor(self) -> dict[str, int]:
        return {
            "x": int(self.canvas.depth_anchor_x or 0),
            "y": int(self.canvas.depth_anchor_y or 0),
        }
