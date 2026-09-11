from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPen, QPixmap
from PySide6.QtWidgets import QLabel, QWidget


class FrameGridPreview(QLabel):
    """Fitted image preview with one-pixel internal frame cut guides."""

    def __init__(self, placeholder: str, parent: QWidget | None = None) -> None:
        super().__init__(placeholder, parent)
        self._image = QImage()
        self._grid: tuple[int, int, int, int, int, int, int] | None = None
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setMinimumSize(420, 360)
        self.setStyleSheet("background: #222831; color: #aeb8c4;")

    def show_image(self, image: QImage,
                   grid: tuple[int, int, int, int, int, int, int]) -> None:
        self._image = image
        self._grid = grid
        self.setText("")
        self._fit_image()

    def clear_image(self, placeholder: str) -> None:
        self._image = QImage()
        self._grid = None
        self.setPixmap(QPixmap())
        self.setText(placeholder)

    def resizeEvent(self, event: object) -> None:  # type: ignore[override]
        super().resizeEvent(event)  # type: ignore[arg-type]
        self._fit_image()

    def _fit_image(self) -> None:
        if self._image.isNull():
            return
        pixmap = QPixmap.fromImage(self._image).scaled(
            self.size(), Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation)
        if self._grid is not None:
            left, top, frame_width, frame_height, spacing, columns, rows = self._grid
            scale_x = pixmap.width() / self._image.width()
            scale_y = pixmap.height() / self._image.height()
            grid_left = round(left * scale_x)
            grid_top = round(top * scale_y)
            grid_right = min(
                pixmap.width() - 1,
                round((left + columns * frame_width + max(0, columns - 1) * spacing) * scale_x) - 1,
            )
            grid_bottom = min(
                pixmap.height() - 1,
                round((top + rows * frame_height + max(0, rows - 1) * spacing) * scale_y) - 1,
            )
            painter = QPainter(pixmap)
            pen = QPen(QColor(0, 190, 255, 220))
            pen.setWidth(1)
            pen.setCosmetic(True)
            painter.setPen(pen)
            for column in range(1, columns):
                x = round((left + column * (frame_width + spacing)) * scale_x)
                painter.drawLine(x, grid_top, x, grid_bottom)
            for row in range(1, rows):
                y = round((top + row * (frame_height + spacing)) * scale_y)
                painter.drawLine(grid_left, y, grid_right, y)
            painter.end()
        self.setPixmap(pixmap)
