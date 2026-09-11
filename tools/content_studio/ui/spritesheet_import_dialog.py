from __future__ import annotations

import re
import unicodedata
from math import gcd
from pathlib import Path

from PySide6.QtCore import QTimer, Qt
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QFileDialog, QFormLayout, QHBoxLayout, QLabel,
    QLineEdit, QMessageBox, QPushButton, QSpinBox, QSplitter, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.import_service import calculate_grid
from ..services.localization import Translator
from ..services.spritesheet_import_service import SpritesheetImportRequest, SpritesheetImportService
from .frame_grid_preview import FrameGridPreview


class SpritesheetImportDialog(QDialog):
    """Import one rectangular spritesheet as visualImage + animation authored data."""

    def __init__(self, workspace: ContentWorkspace, asset_root: Path | None,
                 translator: Translator | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.translate = translator or Translator()
        self.service = SpritesheetImportService()
        self._existing_animation: ContentDefinition | None = None
        self.imported_animation_id = ""
        self.source = QLineEdit()
        self.browse_button = QPushButton(self.translate("browse"))
        self.browse_button.clicked.connect(self._browse)
        source_row = QHBoxLayout(); source_row.addWidget(self.source, 1)
        source_row.addWidget(self.browse_button)
        self.image_id = QLineEdit("image.authored")
        self.animation_id = QLineEdit("animation.authored")
        self.frame_width = self._spin(16, 1)
        self.frame_height = self._spin(16, 1)
        self.duration = self._spin(8, 1)
        self.spacing = self._spin(0, 0)
        self.margin = self._spin(0, 0)
        self.preview = FrameGridPreview(self.translate("no_image"))
        self.animation_preview = QLabel(self.translate("no_image"))
        self.animation_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.animation_preview.setFixedSize(192, 192)
        self.animation_preview.setStyleSheet(
            "background: #161b22; color: #aeb8c4; border: 1px solid #4b5563;")
        self._animation_frames: list[QImage] = []
        self._animation_frame_index = 0
        self._preview_playing = True
        self._animation_timer = QTimer(self)
        self._animation_timer.timeout.connect(self._advance_animation)
        self.play_button = QPushButton(f"▶ {self.translate('play_animation')}")
        self.pause_button = QPushButton(f"⏸ {self.translate('pause_animation')}")
        self.play_button.clicked.connect(self._play_animation)
        self.pause_button.clicked.connect(self._pause_animation)
        self.details = QLabel(); self.details.setWordWrap(True)
        self.help = QLabel(self.translate("frame_preview_help")); self.help.setWordWrap(True)
        self.help.setStyleSheet("color: #aeb8c4;")
        self.frame_bounds = QLabel(); self.frame_bounds.setWordWrap(True)
        form = QFormLayout()
        form.addRow(self.translate("source_image"), source_row)
        form.addRow(self.translate("image_id"), self.image_id)
        form.addRow(self.translate("animation_id"), self.animation_id)
        form.addRow(self.translate("frame_width"), self.frame_width)
        form.addRow(self.translate("frame_height"), self.frame_height)
        form.addRow(self.translate("frame_duration"), self.duration)
        form.addRow(self.translate("spacing"), self.spacing)
        form.addRow(self.translate("margin"), self.margin)
        left = QWidget(); left_layout = QVBoxLayout(left); left_layout.addLayout(form); left_layout.addStretch(1)
        right = QWidget(); right_layout = QVBoxLayout(right)
        title = QLabel(self.translate("frames_preview")); title.setStyleSheet("font-weight: bold;")
        right_layout.addWidget(title)
        previews = QHBoxLayout(); previews.addWidget(self.preview, 1)
        animation_column = QVBoxLayout()
        animation_title = QLabel(self.translate("animation_preview"))
        animation_title.setStyleSheet("font-weight: bold;")
        animation_title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        animation_column.addWidget(animation_title)
        animation_column.addWidget(self.animation_preview)
        playback_buttons = QHBoxLayout()
        playback_buttons.addWidget(self.play_button)
        playback_buttons.addWidget(self.pause_button)
        animation_column.addLayout(playback_buttons)
        animation_column.addStretch(1)
        previews.addLayout(animation_column)
        right_layout.addLayout(previews, 1)
        right_layout.addWidget(self.details); right_layout.addWidget(self.help)
        right_layout.addWidget(self.frame_bounds)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(left); splitter.addWidget(right); splitter.setStretchFactor(1, 1)
        splitter.setSizes([340, 600])
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self._import); buttons.rejected.connect(self.reject)
        layout = QVBoxLayout(self); layout.addWidget(splitter, 1); layout.addWidget(buttons)
        for control in (self.source, self.frame_width, self.frame_height, self.duration, self.spacing, self.margin):
            if isinstance(control, QLineEdit):
                control.textChanged.connect(self._refresh_preview)
            else:
                control.valueChanged.connect(self._refresh_preview)
        self._update_playback_buttons()
        self.setWindowTitle(self.translate("spritesheet_import"))
        self.resize(1160, 650)

    @staticmethod
    def _spin(value: int, minimum: int) -> QSpinBox:
        control = QSpinBox(); control.setRange(minimum, 4096); control.setValue(value)
        return control

    @staticmethod
    def _id_stem(path: Path) -> str:
        stem = unicodedata.normalize("NFKD", path.stem).encode("ascii", "ignore").decode("ascii")
        return "_".join(part for part in re.split(r"[^A-Za-z0-9]+", stem.casefold()) if part) or "authored"

    def set_source_image(self, path: Path) -> None:
        stem = self._id_stem(path)
        self.image_id.setText(f"image.{stem}")
        self.animation_id.setText(f"animation.{stem}")
        try:
            dimensions = self.service.inspect(path)
            common_size = max(1, gcd(dimensions.width, dimensions.height))
            if dimensions.width >= dimensions.height * 2:
                self.frame_width.setValue(common_size)
                self.frame_height.setValue(dimensions.height)
            elif dimensions.height >= dimensions.width * 2:
                self.frame_width.setValue(dimensions.width)
                self.frame_height.setValue(common_size)
            else:
                self.frame_width.setValue(common_size)
                self.frame_height.setValue(common_size)
        except (OSError, ValueError):
            pass
        self.source.setText(str(path))

    def set_existing_animation(self, animation: ContentDefinition, path: Path) -> None:
        self._existing_animation = animation
        self.set_source_image(path)
        self.image_id.setText(str(animation.data.get("imageId", "")))
        self.animation_id.setText(animation.definition_id)
        frames = animation.data.get("frames", [])
        first = frames[0] if isinstance(frames, list) and frames else None
        source = first.get("source") if isinstance(first, dict) else None
        if isinstance(source, dict):
            width = int(source.get("width", 1))
            height = int(source.get("height", 1))
            self.frame_width.setValue(max(1, width))
            self.frame_height.setValue(max(1, height))
            self.margin.setValue(max(0, int(source.get("x", 0))))
            sources = [
                frame.get("source") for frame in frames
                if isinstance(frame, dict) and isinstance(frame.get("source"), dict)
            ]
            xs = sorted({int(value.get("x", 0)) for value in sources})
            ys = sorted({int(value.get("y", 0)) for value in sources})
            if len(xs) > 1:
                self.spacing.setValue(max(0, xs[1] - xs[0] - width))
            elif len(ys) > 1:
                self.spacing.setValue(max(0, ys[1] - ys[0] - height))
            self.duration.setValue(max(1, int(first.get("durationTicks", 1))))
        self.source.setReadOnly(True)
        self.image_id.setReadOnly(True)
        self.animation_id.setReadOnly(True)
        self.browse_button.setEnabled(False)
        self.setWindowTitle(self.translate("edit_spritesheet_import"))
        self._refresh_preview()

    def _browse(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, self.translate("source_image"), "",
            "Images (*.png *.jpg *.jpeg *.bmp *.gif)",
        )
        if path:
            self.set_source_image(Path(path))

    def _request(self) -> SpritesheetImportRequest:
        return SpritesheetImportRequest(
            source_image=Path(self.source.text()),
            image_id=self.image_id.text().strip(),
            animation_id=self.animation_id.text().strip(),
            frame_width=self.frame_width.value(),
            frame_height=self.frame_height.value(),
            duration_ticks=self.duration.value(),
            spacing=self.spacing.value(),
            margin=self.margin.value(),
            asset_root=self.asset_root,
        )

    def _refresh_preview(self) -> None:
        if not self.source.text().strip():
            self.preview.clear_image(self.translate("no_image")); self._set_animation_frames([])
            self.details.clear(); self.frame_bounds.clear(); return
        try:
            request = self._request()
            dimensions = self.service.inspect(request.source_image)
            columns, rows = calculate_grid(
                dimensions, request.frame_width, request.frame_height,
                request.spacing, request.margin,
            )
            image = QImage(str(request.source_image))
            if image.isNull():
                raise ValueError(self.translate("image_unavailable"))
            self.preview.show_image(image, (
                request.margin, request.margin, request.frame_width, request.frame_height,
                request.spacing, columns, rows,
            ))
            frames = [
                image.copy(
                    request.margin + column * (request.frame_width + request.spacing),
                    request.margin + row * (request.frame_height + request.spacing),
                    request.frame_width, request.frame_height,
                )
                for row in range(rows)
                for column in range(columns)
            ]
            self._set_animation_frames(frames)
            details = self.translate(
                "grid_summary", width=dimensions.width, height=dimensions.height,
                columns=columns, rows=rows, frames=columns * rows)
            covered_width = (
                request.margin * 2
                + columns * request.frame_width
                + max(0, columns - 1) * request.spacing
            )
            covered_height = (
                request.margin * 2
                + rows * request.frame_height
                + max(0, rows - 1) * request.spacing
            )
            unused_width = max(0, dimensions.width - covered_width)
            unused_height = max(0, dimensions.height - covered_height)
            if unused_width or unused_height:
                details += "\n" + self.translate(
                    "grid_unused_edge", width=unused_width, height=unused_height)
            self.details.setText(details)
            self.frame_bounds.setText(self.translate(
                "frame_bounds", left=request.margin,
                right=request.margin + request.frame_width - 1,
                top=request.margin, bottom=request.margin + request.frame_height - 1,
                width=request.frame_width, height=request.frame_height))
        except (OSError, ValueError) as error:
            self.preview.clear_image(self.translate("invalid_image")); self._set_animation_frames([])
            self.details.setText(str(error)); self.frame_bounds.clear()

    def _set_animation_frames(self, frames: list[QImage]) -> None:
        self._animation_frames = frames
        self._animation_frame_index = 0
        if not frames:
            self._animation_timer.stop()
            self.animation_preview.setPixmap(QPixmap())
            self.animation_preview.setText(self.translate("no_image"))
            self._update_playback_buttons()
            return
        self.animation_preview.setText("")
        self._show_animation_frame()
        self._restart_animation()

    def _restart_animation(self, unused: object = None) -> None:
        del unused
        self._animation_timer.stop()
        if self._preview_playing and len(self._animation_frames) > 1:
            self._animation_timer.start(max(16, round(self.duration.value() * 1000 / 60)))
        self._update_playback_buttons()

    def _play_animation(self) -> None:
        self._preview_playing = True
        self._restart_animation()

    def _pause_animation(self) -> None:
        self._preview_playing = False
        self._animation_timer.stop()
        self._update_playback_buttons()

    def _update_playback_buttons(self) -> None:
        has_frames = bool(self._animation_frames)
        self.play_button.setEnabled(has_frames and not self._preview_playing)
        self.pause_button.setEnabled(has_frames and self._preview_playing)

    def _advance_animation(self) -> None:
        if not self._animation_frames:
            return
        if self._animation_frame_index + 1 >= len(self._animation_frames):
            self._animation_frame_index = 0
        else:
            self._animation_frame_index += 1
        self._show_animation_frame()

    def _show_animation_frame(self) -> None:
        frame = self._animation_frames[self._animation_frame_index]
        self.animation_preview.setPixmap(QPixmap.fromImage(frame).scaled(
            184, 184,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation))

    def _import(self) -> None:
        result = self.service.import_spritesheet(
            self.workspace, self._request(), self._existing_animation)
        if not result.ok:
            title = (self.translate("edit_spritesheet_import")
                     if self._existing_animation else self.translate("spritesheet_import"))
            QMessageBox.warning(self, title, "\n".join(
                issue.message for issue in result.diagnostics or []))
            return
        self.imported_animation_id = result.animation_id
        self.accept()
