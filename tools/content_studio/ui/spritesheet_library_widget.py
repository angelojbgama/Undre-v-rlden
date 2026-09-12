from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QPoint, QRect, QTimer, Qt, Signal
from PySide6.QtGui import QImage, QPainter, QPixmap
from PySide6.QtWidgets import (
    QFileDialog, QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem,
    QMenu, QPushButton, QSplitter, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..services.localization import Translator
from .animation_frame_alignment_dialog import AnimationFrameAlignmentDialog
from .frame_grid_preview import FrameGridPreview
from .spritesheet_import_dialog import SpritesheetImportDialog


class SpritesheetLibraryWidget(QWidget):
    """Animation library backed by authored visualImages and animations."""

    changed = Signal()
    status_changed = Signal(str)

    def __init__(self, workspace: ContentWorkspace | None, asset_root: Path | None,
                 translator: Translator | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.translate = translator or Translator()
        self.search = QLineEdit(); self.search.textChanged.connect(self.refresh)
        self.animations = QListWidget(); self.animations.currentItemChanged.connect(self._selection_changed)
        self.animations.itemDoubleClicked.connect(lambda unused: self.edit_frames())
        self.animations.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.animations.customContextMenuRequested.connect(self._show_context_menu)
        self.import_button = QPushButton(); self.import_button.clicked.connect(self.import_spritesheet)
        self.edit_button = QPushButton(); self.edit_button.clicked.connect(self.edit_frames)
        self.preview = FrameGridPreview(self.translate("no_image"))
        self.animation_preview = QLabel(self.translate("no_image"))
        self.animation_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.animation_preview.setFixedSize(192, 192)
        self.animation_preview.setStyleSheet(
            "background: #161b22; color: #aeb8c4; border: 1px solid #4b5563;")
        self.animation_title = QLabel()
        self.animation_title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.animation_title.setStyleSheet("font-weight: bold;")
        self.play_button = QPushButton(); self.play_button.clicked.connect(self._play_animation)
        self.pause_button = QPushButton(); self.pause_button.clicked.connect(self._pause_animation)
        self._animation_image_source = QImage()
        self._animation_frames: list[dict[str, object]] = []
        self._animation_frame_index = 0
        self._preview_playing = True
        self._animation_timer = QTimer(self)
        self._animation_timer.setSingleShot(True)
        self._animation_timer.timeout.connect(self._advance_animation)
        self.details = QLabel(); self.details.setWordWrap(True)
        left = QWidget(); left_layout = QVBoxLayout(left)
        library_buttons = QHBoxLayout(); library_buttons.addWidget(self.import_button)
        library_buttons.addWidget(self.edit_button)
        left_layout.addWidget(self.search); left_layout.addWidget(self.animations, 1)
        left_layout.addLayout(library_buttons)
        right = QWidget(); right_layout = QVBoxLayout(right)
        previews = QHBoxLayout(); previews.addWidget(self.preview, 1)
        animation_column = QVBoxLayout(); animation_column.addWidget(self.animation_title)
        animation_column.addWidget(self.animation_preview)
        playback = QHBoxLayout(); playback.addWidget(self.play_button)
        playback.addWidget(self.pause_button)
        animation_column.addLayout(playback); animation_column.addStretch(1)
        previews.addLayout(animation_column)
        right_layout.addLayout(previews, 1); right_layout.addWidget(self.details)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(left); splitter.addWidget(right); splitter.setStretchFactor(1, 1)
        splitter.setSizes([260, 620])
        layout = QVBoxLayout(self); layout.addWidget(splitter)
        self.retranslate(self.translate)

    def set_context(self, workspace: ContentWorkspace | None, asset_root: Path | None) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.search.setPlaceholderText(self.translate("search"))
        self.import_button.setText(self.translate("spritesheet_import"))
        self.edit_button.setText(self.translate("edit_animation_frames"))
        self.animation_title.setText(self.translate("animation_preview"))
        self.play_button.setText(f"▶ {self.translate('play_animation')}")
        self.pause_button.setText(f"⏸ {self.translate('pause_animation')}")
        self._update_playback_buttons()
        self.refresh()

    def refresh(self) -> None:
        current = self.animations.currentItem().data(Qt.ItemDataRole.UserRole) if self.animations.currentItem() else ""
        self.animations.blockSignals(True)
        self.animations.clear()
        definitions = self.workspace.definitions("animations", self.search.text()) if self.workspace else []
        for definition in definitions:
            item = QListWidgetItem(definition.display_name)
            item.setToolTip(definition.definition_id)
            item.setData(Qt.ItemDataRole.UserRole, definition.definition_id)
            self.animations.addItem(item)
            if definition.definition_id == current:
                self.animations.setCurrentItem(item)
        if self.animations.currentItem() is None and self.animations.count():
            self.animations.setCurrentRow(0)
        self.animations.blockSignals(False)
        self._selection_changed(self.animations.currentItem(), None)

    def _current_animation(self):
        item = self.animations.currentItem()
        animation_id = str(item.data(Qt.ItemDataRole.UserRole)) if item else ""
        return self.workspace.find("animations", animation_id) if self.workspace and animation_id else None

    def _animation_image(self, animation) -> QImage:
        path = self._animation_image_path(animation)
        return QImage(str(path)) if path is not None else QImage()

    def _animation_image_path(self, animation) -> Path | None:
        if animation is None or self.workspace is None:
            return None
        image_id = str(animation.data.get("imageId", ""))
        image_definition = self.workspace.find("visualImages", image_id)
        root = (self.asset_root if image_definition
                and image_definition.data.get("root") == "gameAssets" else self.workspace.root)
        relative = image_definition.data.get("relativePath") if image_definition else None
        return root / relative if root and isinstance(relative, str) else None

    def _show_context_menu(self, position) -> None:
        item = self.animations.itemAt(position)
        if item is None:
            return
        self.animations.setCurrentItem(item)
        menu = QMenu(self)
        edit_frames_action = menu.addAction(self.translate("edit_animation_frames"))
        edit_import_action = menu.addAction(self.translate("edit_spritesheet_import"))
        selected_action = menu.exec(self.animations.viewport().mapToGlobal(position))
        if selected_action == edit_frames_action:
            self.edit_frames()
        elif selected_action == edit_import_action:
            self.edit_import()

    def edit_frames(self) -> None:
        animation = self._current_animation()
        image_path = self._animation_image_path(animation)
        image = QImage(str(image_path)) if image_path is not None else QImage()
        if (animation is None or image_path is None or image.isNull() or
                self.workspace is None):
            return
        dialog = AnimationFrameAlignmentDialog(
            self.workspace, animation, image, image_path, self.asset_root,
            self.translate, self)
        if dialog.exec():
            self.refresh()
            self.changed.emit()
            self.status_changed.emit(self.translate("animation_frames_updated"))

    def edit_import(self) -> None:
        animation = self._current_animation()
        image_path = self._animation_image_path(animation)
        if animation is None or image_path is None or self.workspace is None:
            return
        dialog = SpritesheetImportDialog(
            self.workspace, self.asset_root, self.translate, self)
        dialog.set_existing_animation(animation, image_path)
        if dialog.exec():
            self.refresh()
            self.changed.emit()
            self.status_changed.emit(self.translate("spritesheet_import_updated"))

    def import_spritesheet(self) -> None:
        if self.workspace is None:
            return
        path, _ = QFileDialog.getOpenFileName(
            self, self.translate("spritesheet_import"), "",
            "Images (*.png *.jpg *.jpeg *.bmp *.gif)",
        )
        if not path:
            return
        dialog = SpritesheetImportDialog(self.workspace, self.asset_root, self.translate, self)
        dialog.set_source_image(Path(path))
        if not dialog.exec():
            return
        self.refresh()
        for index in range(self.animations.count()):
            item = self.animations.item(index)
            if item.data(Qt.ItemDataRole.UserRole) == dialog.imported_animation_id:
                self.animations.setCurrentItem(item)
                break
        self.changed.emit()
        self.status_changed.emit(self.translate("animation_imported"))

    def _selection_changed(self, current: QListWidgetItem | None,
                           unused: QListWidgetItem | None) -> None:
        del unused
        animation_id = str(current.data(Qt.ItemDataRole.UserRole)) if current else ""
        animation = self.workspace.find("animations", animation_id) if self.workspace and animation_id else None
        self.edit_button.setEnabled(animation is not None)
        if animation is None or self.workspace is None:
            self.preview.clear_image(self.translate("no_image"))
            self._set_animation_preview(QImage(), [])
            self.details.setText(self.translate("no_animations"))
            return
        image_id = str(animation.data.get("imageId", ""))
        image = self._animation_image(animation)
        frames = animation.data.get("frames", [])
        sources = [frame.get("source") for frame in frames if isinstance(frame, dict) and isinstance(frame.get("source"), dict)] if isinstance(frames, list) else []
        if image.isNull() or not sources:
            self.preview.clear_image(self.translate("image_unavailable"))
            self._set_animation_preview(QImage(), [])
            self.details.setText(f"{animation_id}\n{image_id}")
            return
        first = sources[0]
        frame_width = int(first.get("width", 1)); frame_height = int(first.get("height", 1))
        xs = sorted({int(source.get("x", 0)) for source in sources})
        ys = sorted({int(source.get("y", 0)) for source in sources})
        spacing = max(0, xs[1] - xs[0] - frame_width) if len(xs) > 1 else 0
        self.preview.show_image(image, (
            xs[0], ys[0], frame_width, frame_height, spacing, len(xs), len(ys),
        ))
        animation_frames = [frame for frame in frames if isinstance(frame, dict)]
        self._set_animation_preview(image, animation_frames)
        self.details.setText(
            f"{animation_id}\n{frame_width} × {frame_height} px — {len(sources)} frames")

    def _set_animation_preview(self, image: QImage,
                               frames: list[dict[str, object]]) -> None:
        self._animation_timer.stop()
        self._animation_image_source = image
        self._animation_frames = frames
        self._animation_frame_index = 0
        if image.isNull() or not frames:
            self.animation_preview.setPixmap(QPixmap())
            self.animation_preview.setText(self.translate("no_image"))
            self._update_playback_buttons()
            return
        self.animation_preview.setText("")
        self._show_animation_frame()
        self._schedule_animation_frame()

    def _play_animation(self) -> None:
        self._preview_playing = True
        self._schedule_animation_frame()

    def _pause_animation(self) -> None:
        self._preview_playing = False
        self._animation_timer.stop()
        self._update_playback_buttons()

    def _schedule_animation_frame(self) -> None:
        self._animation_timer.stop()
        if self._preview_playing and len(self._animation_frames) > 1:
            frame = self._animation_frames[self._animation_frame_index]
            duration = max(1, int(frame.get("durationTicks", 1)))
            self._animation_timer.start(max(16, round(duration * 1000 / 60)))
        self._update_playback_buttons()

    def _advance_animation(self) -> None:
        if not self._animation_frames:
            return
        self._animation_frame_index = (
            self._animation_frame_index + 1) % len(self._animation_frames)
        self._show_animation_frame()
        self._schedule_animation_frame()

    def _update_playback_buttons(self) -> None:
        can_animate = len(self._animation_frames) > 1
        self.play_button.setEnabled(can_animate and not self._preview_playing)
        self.pause_button.setEnabled(can_animate and self._preview_playing)

    def _show_animation_frame(self) -> None:
        if self._animation_image_source.isNull() or not self._animation_frames:
            return
        valid_frames = [
            frame for frame in self._animation_frames
            if isinstance(frame.get("source"), dict)
        ]
        if not valid_frames:
            return
        maximum_width = max(int(frame["source"].get("width", 1)) for frame in valid_frames)  # type: ignore[union-attr]
        maximum_height = max(int(frame["source"].get("height", 1)) for frame in valid_frames)  # type: ignore[union-attr]
        padding = max(2, max(maximum_width, maximum_height) // 8)
        canvas = QImage(
            maximum_width + padding * 2, maximum_height + padding * 2,
            QImage.Format.Format_ARGB32,
        )
        canvas.fill(Qt.GlobalColor.transparent)
        frame = self._animation_frames[self._animation_frame_index]
        source = frame.get("source", {})
        anchor = frame.get("anchor", {})
        offset = frame.get("drawOffset", {})
        if not isinstance(source, dict):
            return
        anchor = anchor if isinstance(anchor, dict) else {}
        offset = offset if isinstance(offset, dict) else {}
        source_rect = QRect(
            int(source.get("x", 0)), int(source.get("y", 0)),
            int(source.get("width", 1)), int(source.get("height", 1)),
        )
        logical_position = QPoint(canvas.width() // 2, canvas.height() - padding - 1)
        destination = QPoint(
            logical_position.x() - int(anchor.get("x", 0)) + int(offset.get("x", 0)),
            logical_position.y() - int(anchor.get("y", 0)) + int(offset.get("y", 0)),
        )
        painter = QPainter(canvas)
        painter.drawImage(destination, self._animation_image_source, source_rect)
        painter.end()
        self.animation_preview.setPixmap(QPixmap.fromImage(canvas).scaled(
            184, 184, Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation,
        ))
