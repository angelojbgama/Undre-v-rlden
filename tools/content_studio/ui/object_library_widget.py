from __future__ import annotations

import re
import unicodedata
from pathlib import Path

from PySide6.QtCore import QPoint, QRect, QTimer, Qt, Signal
from PySide6.QtGui import QIcon, QImage, QMouseEvent, QPainter, QPixmap
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QGroupBox,
    QHBoxLayout, QLabel, QLineEdit, QListWidgetItem, QMenu, QMessageBox,
    QPushButton, QSpinBox, QSplitter, QVBoxLayout, QWidget,
)

from ..interaction.drag_payload import StudioDragPayload
from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.localization import Translator
from ..services.object_authoring_service import ObjectAuthoringRequest, ObjectAuthoringService
from .shape_mask_editor import ShapeMaskEditorDialog
from .widgets import PayloadListWidget


def _frame_pixmap(image: QImage, frame: dict[str, object], size: int = 320) -> QPixmap:
    source = frame.get("source", {})
    anchor = frame.get("anchor", {})
    offset = frame.get("drawOffset", {})
    if image.isNull() or not isinstance(source, dict):
        return QPixmap()
    anchor = anchor if isinstance(anchor, dict) else {}
    offset = offset if isinstance(offset, dict) else {}
    width = max(1, int(source.get("width", 1)))
    height = max(1, int(source.get("height", 1)))
    padding = max(2, max(width, height) // 8)
    canvas = QImage(width + padding * 2, height + padding * 2,
                    QImage.Format.Format_ARGB32)
    canvas.fill(Qt.GlobalColor.transparent)
    logical = QPoint(canvas.width() // 2, canvas.height() - padding - 1)
    destination = QPoint(
        logical.x() - int(anchor.get("x", 0)) + int(offset.get("x", 0)),
        logical.y() - int(anchor.get("y", 0)) + int(offset.get("y", 0)))
    painter = QPainter(canvas)
    painter.drawImage(destination, image, QRect(
        int(source.get("x", 0)), int(source.get("y", 0)), width, height))
    painter.end()
    return QPixmap.fromImage(canvas).scaled(
        size, size, Qt.AspectRatioMode.KeepAspectRatio,
        Qt.TransformationMode.FastTransformation)


class ClickableSpritePreview(QLabel):
    clicked = Signal()

    def mousePressEvent(self, event: QMouseEvent) -> None:  # type: ignore[override]
        self.clicked.emit()
        super().mousePressEvent(event)


class ObjectDefinitionDialog(QDialog):
    """Create or configure one placeable object and its visual states."""

    def __init__(self, workspace: ContentWorkspace, translator: Translator,
                 definition: ContentDefinition | None = None,
                 parent: QWidget | None = None,
                 asset_root: Path | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.translate = translator
        self.definition = definition
        self.asset_root = asset_root
        self.service = ObjectAuthoringService()
        self.created_object_id = ""
        self.name = QLineEdit()
        self.object_id = QLineEdit("object.scenery")
        self.animation = QComboBox()
        available_animations = [
            value for value in workspace.definitions("animations")
            if not (value.definition_id.startswith("animation.object.") and
                    value.definition_id.endswith((
                        ".idle", ".closed", ".opening", ".damaged",
                        ".destroying", ".destroyed")))
        ]
        for animation_definition in available_animations:
            self.animation.addItem(
                f"{animation_definition.display_name}  [{animation_definition.definition_id}]",
                animation_definition.definition_id,
            )
        self.preset = QComboBox()
        for key in ("scenery", "interactable", "destructible", "container", "door"):
            self.preset.addItem(self.translate(f"object_preset_{key}"), key)
        self.capacity = QSpinBox(); self.capacity.setRange(1, 999); self.capacity.setValue(5)
        self.scenery_loop = QCheckBox(self.translate("scenery_loop"))
        self.maximum_health = QSpinBox(); self.maximum_health.setRange(1, 9999)
        self.maximum_health.setValue(1)
        self.reward_profile = QComboBox()
        self.reward_profile.addItem(self.translate("destructible_reward_none"), "")
        for reward in workspace.definitions("rewardProfiles"):
            self.reward_profile.addItem(
                f"{reward.display_name}  [{reward.definition_id}]", reward.definition_id)
        self.leave_destroyed_residue = QCheckBox(
            self.translate("leave_destroyed_residue"))
        self.leave_destroyed_residue.setChecked(False)
        self.damage_frame = QSpinBox()
        self.damage_duration = QSpinBox(); self.damage_duration.setRange(1, 3600)
        self.damage_duration.setValue(8)
        self.destruction_frame_duration = QSpinBox()
        self.destruction_frame_duration.setRange(1, 3600)
        self.destruction_frame_duration.setValue(8)
        self.destructible_idle_frame = QSpinBox()
        self.destruction_start = QSpinBox()
        self.destruction_end = QSpinBox()
        self.closed_frame = QSpinBox()
        self.opening_start = QSpinBox()
        self.opening_end = QSpinBox()
        self.example = ClickableSpritePreview(self.translate("no_image"))
        self.example.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.example.setMinimumSize(320, 320)
        self.example.setCursor(Qt.CursorShape.PointingHandCursor)
        self.example.setStyleSheet(
            "background: #161b22; color: #aeb8c4; border: 1px solid #34404d;")
        self.example.clicked.connect(self._play_example)
        self.example_caption = QLabel(self.translate("click_object_preview"))
        self.example_caption.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.example_caption.setWordWrap(True)
        self.example_caption.setStyleSheet("color: #aeb8c4;")
        self._example_image = QImage()
        self._example_frames: list[dict[str, object]] = []
        self._example_index = 0
        self._example_loops = False
        self._example_completion = ""
        self._preview_health = 1
        self._example_timer = QTimer(self); self._example_timer.setSingleShot(True)
        self._example_timer.timeout.connect(self._advance_example)
        form = QFormLayout()
        form.addRow(self.translate("object_name"), self.name)
        form.addRow(self.translate("object_id"), self.object_id)
        form.addRow(self.translate("object_animation"), self.animation)
        form.addRow(self.translate("object_type"), self.preset)
        self.scenery_group = QGroupBox(self.translate("scenery_configuration"))
        scenery_layout = QVBoxLayout(self.scenery_group)
        scenery_layout.addWidget(self.scenery_loop)
        scenery_hint = QLabel(self.translate("scenery_loop_help"))
        scenery_hint.setWordWrap(True); scenery_hint.setStyleSheet("color: #aeb8c4;")
        scenery_layout.addWidget(scenery_hint)
        self.destructible_group = QGroupBox(self.translate("destructible_configuration"))
        destructible_form = QFormLayout(self.destructible_group)
        destructible_form.addRow(self.translate("maximum_health"), self.maximum_health)
        destructible_form.addRow(
            self.translate("destructible_reward_profile"), self.reward_profile)
        destructible_form.addRow(self.leave_destroyed_residue)
        reward_hint = QLabel(self.translate("destructible_reward_help"))
        reward_hint.setWordWrap(True); reward_hint.setStyleSheet("color: #aeb8c4;")
        destructible_form.addRow(reward_hint)
        destructible_form.addRow(
            self.translate("destructible_idle_frame"), self.destructible_idle_frame)
        destructible_form.addRow(
            self.translate("damage_frame"), self.damage_frame)
        destructible_form.addRow(
            self.translate("damage_frame_ticks"), self.damage_duration)
        destructible_form.addRow(
            self.translate("destruction_start_frame"), self.destruction_start)
        destructible_form.addRow(
            self.translate("destruction_end_frame"), self.destruction_end)
        destructible_form.addRow(
            self.translate("destruction_frame_ticks"),
            self.destruction_frame_duration)
        destructible_hint = QLabel(self.translate("destructible_frames_help"))
        destructible_hint.setWordWrap(True)
        destructible_hint.setStyleSheet("color: #aeb8c4;")
        destructible_form.addRow(destructible_hint)
        self.chest_group = QGroupBox(self.translate("chest_configuration"))
        chest_form = QFormLayout(self.chest_group)
        chest_form.addRow(self.translate("container_capacity"), self.capacity)
        chest_form.addRow(self.translate("closed_frame"), self.closed_frame)
        chest_form.addRow(self.translate("opening_start_frame"), self.opening_start)
        chest_form.addRow(self.translate("opening_end_frame"), self.opening_end)
        chest_hint = QLabel(self.translate("chest_animation_help"))
        chest_hint.setWordWrap(True); chest_hint.setStyleSheet("color: #aeb8c4;")
        chest_form.addRow(chest_hint)
        self.collision_enabled = QCheckBox(self.translate("object_collision_enabled"))
        self.edit_collision = QPushButton(self.translate("edit_collision_mask"))
        self._collision_mask: dict[str, object] | None = None
        self.collision_group = QGroupBox(self.translate("object_collision_group"))
        collision_layout = QVBoxLayout(self.collision_group)
        collision_layout.addWidget(self.collision_enabled)
        collision_layout.addWidget(self.edit_collision)
        collision_hint = QLabel(self.translate("object_collision_help"))
        collision_hint.setWordWrap(True); collision_hint.setStyleSheet("color: #aeb8c4;")
        collision_layout.addWidget(collision_hint)
        self.occlusion_enabled = QCheckBox(self.translate("object_occlusion_enabled"))
        self.edit_depth_occlusion = QPushButton(self.translate("edit_depth_occlusion"))
        self._occlusion_mask: dict[str, object] | None = None
        self._depth_anchor: dict[str, int] = {"x": 0, "y": 0}
        self.depth_group = QGroupBox(self.translate("object_depth_group"))
        depth_layout = QVBoxLayout(self.depth_group)
        depth_layout.addWidget(self.occlusion_enabled)
        depth_layout.addWidget(self.edit_depth_occlusion)
        depth_hint = QLabel(self.translate("object_depth_help"))
        depth_hint.setWordWrap(True); depth_hint.setStyleSheet("color: #aeb8c4;")
        depth_layout.addWidget(depth_hint)
        hint = QLabel(self.translate("object_creation_help")); hint.setWordWrap(True)
        hint.setStyleSheet("color: #aeb8c4;")
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self._create); buttons.rejected.connect(self.reject)
        left = QWidget(); left_layout = QVBoxLayout(left)
        left_layout.setContentsMargins(0, 0, 0, 0)
        left_layout.addLayout(form); left_layout.addWidget(self.scenery_group)
        left_layout.addWidget(self.destructible_group)
        left_layout.addWidget(self.chest_group)
        left_layout.addWidget(self.collision_group)
        left_layout.addWidget(self.depth_group)
        left_layout.addWidget(hint); left_layout.addStretch(1)
        right = QWidget(); right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.addWidget(self.example, 1); right_layout.addWidget(self.example_caption)
        body = QHBoxLayout(); body.addWidget(left, 1); body.addWidget(right, 1)
        layout = QVBoxLayout(self); layout.addLayout(body, 1)
        layout.addWidget(buttons)
        self.setWindowTitle(
            self.translate("configure_object") if self.definition
            else self.translate("create_object"))
        self.resize(980, 760)
        if self.definition is None:
            self.animation.currentIndexChanged.connect(self._suggest_identity)
            self._suggest_identity()
        else:
            self._load_definition(self.definition)
            self.object_id.setReadOnly(True)
        self.animation.currentIndexChanged.connect(self._animation_changed)
        self.preset.currentIndexChanged.connect(self._type_changed)
        self.scenery_loop.toggled.connect(self._reset_example)
        self.maximum_health.valueChanged.connect(self._reset_example)
        self.destructible_idle_frame.valueChanged.connect(
            self._destruction_range_changed)
        self.damage_frame.valueChanged.connect(self._destruction_range_changed)
        self.damage_duration.valueChanged.connect(self._reset_example)
        self.destruction_start.valueChanged.connect(
            self._destruction_range_changed)
        self.destruction_end.valueChanged.connect(
            self._destruction_range_changed)
        self.destruction_frame_duration.valueChanged.connect(self._reset_example)
        self.closed_frame.valueChanged.connect(self._reset_example)
        self.opening_start.valueChanged.connect(self._reset_example)
        self.opening_end.valueChanged.connect(self._reset_example)
        self.collision_enabled.toggled.connect(self._collision_toggled)
        self.edit_collision.clicked.connect(self._edit_collision)
        self.occlusion_enabled.toggled.connect(self._occlusion_toggled)
        self.edit_depth_occlusion.clicked.connect(self._edit_depth_occlusion)
        self._animation_changed(); self._type_changed()
        self._collision_toggled(self.collision_enabled.isChecked())
        self._occlusion_toggled(self.occlusion_enabled.isChecked())

    @staticmethod
    def _slug(value: str) -> str:
        normalized = unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode("ascii")
        return "_".join(part for part in re.split(r"[^A-Za-z0-9]+", normalized.casefold()) if part)

    def _suggest_identity(self, unused: object = None) -> None:
        del unused
        animation_id = str(self.animation.currentData() or "")
        stem = animation_id.removeprefix("animation.").removeprefix("anim.")
        stem = self._slug(stem) or "scenery"
        self.name.setText(stem.replace("_", " ").title())
        self.object_id.setText(f"object.{stem}")

    def _load_definition(self, definition: ContentDefinition) -> None:
        request = self.service.request_for(self.workspace, definition)
        self.name.setText(request.display_name)
        self.object_id.setText(request.object_id)
        animation_index = self.animation.findData(request.animation_id)
        if animation_index >= 0:
            self.animation.setCurrentIndex(animation_index)
        preset_index = self.preset.findData(request.preset)
        if preset_index >= 0:
            self.preset.setCurrentIndex(preset_index)
        self.capacity.setValue(request.container_capacity)
        self.scenery_loop.setChecked(request.scenery_loop)
        self.maximum_health.setValue(request.maximum_health)
        reward_index = self.reward_profile.findData(request.reward_profile_id)
        self.reward_profile.setCurrentIndex(max(0, reward_index))
        self.leave_destroyed_residue.setChecked(request.leave_destroyed_residue)
        self.damage_frame.setValue(request.damage_frame)
        self.damage_duration.setValue(request.damage_duration_ticks)
        self.destruction_frame_duration.setValue(request.destruction_frame_ticks)
        self.destructible_idle_frame.setValue(request.destructible_idle_frame)
        self.destruction_start.setValue(request.destruction_start_frame)
        self.destruction_end.setValue(request.destruction_end_frame)
        self.closed_frame.setValue(request.closed_frame)
        self.opening_start.setValue(request.opening_start_frame)
        self.opening_end.setValue(request.opening_end_frame)
        if request.collision_enabled:
            self._collision_mask = {
                "width": request.collision_width,
                "height": request.collision_height,
                "origin": {
                    "x": request.collision_origin_x,
                    "y": request.collision_origin_y,
                },
                "cells": list(request.collision_cells),
            }
            self.collision_enabled.setChecked(True)
        else:
            self._collision_mask = None
            self.collision_enabled.setChecked(False)
        self._depth_anchor = {
            "x": request.depth_anchor_x,
            "y": request.depth_anchor_y,
        }
        if request.occlusion_enabled:
            self._occlusion_mask = {
                "width": request.occlusion_width,
                "height": request.occlusion_height,
                "origin": {
                    "x": request.occlusion_origin_x,
                    "y": request.occlusion_origin_y,
                },
                "cells": list(request.occlusion_cells),
            }
            self.occlusion_enabled.setChecked(True)
        else:
            self._occlusion_mask = None
            self.occlusion_enabled.setChecked(False)

    def _animation_changed(self, unused: object = None) -> None:
        user_change = unused is not None
        animation = self.workspace.find("animations", str(self.animation.currentData() or ""))
        frames = animation.data.get("frames", []) if animation else []
        maximum = max(0, len(frames) - 1) if isinstance(frames, list) else 0
        for control in (
                self.destructible_idle_frame, self.damage_frame,
                self.destruction_start, self.destruction_end,
                self.closed_frame, self.opening_start, self.opening_end):
            control.setRange(0, maximum)
        if self.definition is None or user_change:
            self.destructible_idle_frame.setValue(0)
            self.damage_frame.setValue(min(1, maximum))
            self.destruction_start.setValue(min(2, maximum))
            self.destruction_end.setValue(maximum)
            if isinstance(frames, list) and frames:
                damage = frames[min(1, maximum)]
                breaking = frames[min(2, maximum)]
                if isinstance(damage, dict):
                    self.damage_duration.setValue(max(
                        1, int(damage.get("durationTicks", 8))))
                if isinstance(breaking, dict):
                    self.destruction_frame_duration.setValue(max(
                        1, int(breaking.get("durationTicks", 8))))
            self.closed_frame.setValue(0)
            self.opening_start.setValue(min(1, maximum))
            self.opening_end.setValue(maximum)
        if user_change and self.collision_enabled.isChecked():
            self._collision_mask = self._default_collision_mask()
        if user_change:
            self._depth_anchor = {"x": 0, "y": 0}
            if self.occlusion_enabled.isChecked():
                self._occlusion_mask = self._default_occlusion_mask()
        self._reset_example()

    def _type_changed(self, unused: object = None) -> None:
        del unused
        preset = str(self.preset.currentData())
        self.scenery_group.setVisible(preset == "scenery")
        self.destructible_group.setVisible(preset == "destructible")
        self.chest_group.setVisible(preset == "container")
        # Destructibles are solid by default, but collision remains the generic
        # optional component and can still be edited with the same mask tool.
        if preset == "destructible" and not self.collision_enabled.isChecked():
            default_collision = self._default_collision_mask()
            if default_collision is not None:
                self._collision_mask = default_collision
                self.collision_enabled.setChecked(True)
        self._reset_example()

    def _collision_frame(self) -> tuple[ContentDefinition, dict[str, object]] | None:
        animation = self._source_animation()
        frames = animation.data.get("frames", []) if animation else []
        available = [frame for frame in frames if isinstance(frame, dict)] if isinstance(frames, list) else []
        if animation is None or not available:
            return None
        preset = str(self.preset.currentData() or "scenery")
        index = (self.closed_frame.value() if preset == "container" else
                 self.destructible_idle_frame.value() if preset == "destructible" else 0)
        return animation, available[min(index, len(available) - 1)]

    def _default_collision_mask(self) -> dict[str, object] | None:
        selected = self._collision_frame()
        if selected is None:
            return None
        unused_animation, frame = selected
        del unused_animation
        source = frame.get("source", {})
        anchor = frame.get("anchor", {})
        offset = frame.get("drawOffset", {})
        source = source if isinstance(source, dict) else {}
        anchor = anchor if isinstance(anchor, dict) else {}
        offset = offset if isinstance(offset, dict) else {}
        width = max(1, int(source.get("width", 1)))
        height = max(1, int(source.get("height", 1)))
        return {
            "width": width,
            "height": height,
            "origin": {
                "x": -int(anchor.get("x", 0)) + int(offset.get("x", 0)),
                "y": -int(anchor.get("y", 0)) + int(offset.get("y", 0)),
            },
            "cells": [1] * (width * height),
        }

    def _collision_toggled(self, checked: bool) -> None:
        if checked and self._collision_mask is None:
            self._collision_mask = self._default_collision_mask()
        self.edit_collision.setEnabled(checked and self._collision_mask is not None)

    def _edit_collision(self) -> None:
        selected = self._collision_frame()
        if selected is None:
            return
        animation, frame = selected
        source = frame.get("source", {})
        if not isinstance(source, dict):
            return
        source_image = self._source_image(animation)
        if source_image.isNull():
            QMessageBox.warning(self, self.windowTitle(), self.translate("image_unavailable"))
            return
        width = max(1, int(source.get("width", 1)))
        height = max(1, int(source.get("height", 1)))
        current = self._collision_mask or self._default_collision_mask()
        if current is None:
            return
        if int(current.get("width", 0)) != width or int(current.get("height", 0)) != height:
            current = self._default_collision_mask()
            if current is None:
                return
        sprite = source_image.copy(QRect(
            int(source.get("x", 0)), int(source.get("y", 0)), width, height))
        dialog = ShapeMaskEditorDialog(sprite, current, self.translate, self)
        if dialog.exec():
            self._collision_mask = dialog.result_mask()
            self.collision_enabled.setChecked(True)
            self.edit_collision.setEnabled(True)

    def _default_occlusion_mask(self) -> dict[str, object] | None:
        selected = self._collision_frame()
        if selected is None:
            return None
        unused_animation, frame = selected
        del unused_animation
        source = frame.get("source", {})
        anchor = frame.get("anchor", {})
        offset = frame.get("drawOffset", {})
        source = source if isinstance(source, dict) else {}
        anchor = anchor if isinstance(anchor, dict) else {}
        offset = offset if isinstance(offset, dict) else {}
        width = max(1, int(source.get("width", 1)))
        height = max(1, int(source.get("height", 1)))
        return {
            "width": width,
            "height": height,
            "origin": {
                "x": -int(anchor.get("x", 0)) + int(offset.get("x", 0)),
                "y": -int(anchor.get("y", 0)) + int(offset.get("y", 0)),
            },
            # Occlusion is opt-in. Alpha generation remains an explicit button
            # instead of becoming gameplay semantics automatically.
            "cells": [0] * (width * height),
        }

    def _occlusion_toggled(self, checked: bool) -> None:
        if checked and self._occlusion_mask is None:
            self._occlusion_mask = self._default_occlusion_mask()

    def _edit_depth_occlusion(self) -> None:
        selected = self._collision_frame()
        if selected is None:
            return
        animation, frame = selected
        source = frame.get("source", {})
        if not isinstance(source, dict):
            return
        source_image = self._source_image(animation)
        if source_image.isNull():
            QMessageBox.warning(self, self.windowTitle(),
                                self.translate("image_unavailable"))
            return
        width = max(1, int(source.get("width", 1)))
        height = max(1, int(source.get("height", 1)))
        current = self._occlusion_mask or self._default_occlusion_mask()
        if current is None:
            return
        if (int(current.get("width", 0)) != width or
                int(current.get("height", 0)) != height):
            current = self._default_occlusion_mask()
            if current is None:
                return
        sprite = source_image.copy(QRect(
            int(source.get("x", 0)), int(source.get("y", 0)), width, height))
        dialog = ShapeMaskEditorDialog(
            sprite, current, self.translate, self,
            depth_anchor=self._depth_anchor,
            allow_depth_tool=True,
            title_key="occlusion_editor_title")
        if dialog.exec():
            self._occlusion_mask = dialog.result_mask()
            self._depth_anchor = dialog.result_depth_anchor()
            cells = self._occlusion_mask.get("cells", [])
            has_occlusion = (
                isinstance(cells, list) and any(int(value) != 0 for value in cells)
            )
            # Depth Anchor is independent. Occlusion is persisted only when the
            # author explicitly painted at least one pixel.
            self.occlusion_enabled.setChecked(has_occlusion)

    def _source_animation(self) -> ContentDefinition | None:
        return self.workspace.find(
            "animations", str(self.animation.currentData() or ""))

    def _destruction_range_changed(self, unused: object = None) -> None:
        del unused
        self._reset_example()

    def _source_image(self, animation: ContentDefinition) -> QImage:
        image = self.workspace.find(
            "visualImages", str(animation.data.get("imageId", "")))
        if image is None:
            return QImage()
        root = self.asset_root if image.data.get("root") == "gameAssets" else self.workspace.root
        relative = image.data.get("relativePath")
        return QImage(str(root / relative)) if root and isinstance(relative, str) else QImage()

    def _reset_example(self, unused: object = None) -> None:
        del unused
        self._example_timer.stop()
        self._example_loops = False
        self._example_completion = ""
        self._preview_health = self.maximum_health.value()
        animation = self._source_animation()
        frames = animation.data.get("frames", []) if animation else []
        available = [frame for frame in frames if isinstance(frame, dict)] if isinstance(frames, list) else []
        self._example_image = self._source_image(animation) if animation else QImage()
        if self._example_image.isNull() or not available:
            self._example_frames = []
            self.example.setPixmap(QPixmap()); self.example.setText(self.translate("image_unavailable"))
            return
        preset = str(self.preset.currentData())
        self.example_caption.setText(
            self.translate("destructible_preview_attack", current=self._preview_health,
                           maximum=self.maximum_health.value())
            if preset == "destructible" else self.translate("click_object_preview"))
        index = (self.closed_frame.value() if preset == "container" else
                 self.destructible_idle_frame.value()
                 if preset == "destructible" else 0)
        self._example_frames = [available[min(index, len(available) - 1)]]
        self._example_index = 0
        self._show_example_frame()
        if preset == "scenery" and self.scenery_loop.isChecked():
            self._example_frames = available
            self._example_loops = True
            self.example_caption.setText(self.translate("object_preview_looping"))
            self._schedule_example()

    def _play_example(self) -> None:
        animation = self._source_animation()
        frames = animation.data.get("frames", []) if animation else []
        available = [frame for frame in frames if isinstance(frame, dict)] if isinstance(frames, list) else []
        source_image = self._source_image(animation) if animation else QImage()
        if source_image.isNull() or not available:
            return
        preset = str(self.preset.currentData())
        if preset == "destructible":
            if self._preview_health <= 0:
                self._reset_example()
                return
            self._example_timer.stop()
            self._preview_health -= 1
            damaged = self._preview_health > 0
            if damaged:
                index = min(self.damage_frame.value(), len(available) - 1)
                available = [self._preview_frame(
                    available[index], self.damage_duration.value())]
            else:
                start = min(self.destruction_start.value(), len(available) - 1)
                end = min(self.destruction_end.value(), len(available) - 1)
                available = ([self._preview_frame(
                    frame, self.destruction_frame_duration.value())
                    for frame in available[start:end + 1]]
                    if start <= end else [])
            self._example_image = source_image
            self._example_frames = available
            self._example_completion = "idle" if self._preview_health > 0 else "destroyed"
            self.example_caption.setText(self.translate(
                "destructible_preview_damaged" if self._preview_health > 0
                else "destructible_preview_breaking",
                current=self._preview_health, maximum=self.maximum_health.value()))
        elif preset == "container":
            start = min(self.opening_start.value(), len(available) - 1)
            end = min(self.opening_end.value(), len(available) - 1)
            self._example_frames = available[start:end + 1] if start <= end else []
        else:
            self._example_frames = available
        if not self._example_frames:
            return
        self._example_timer.stop(); self._example_index = 0
        self._example_loops = preset == "scenery" and self.scenery_loop.isChecked()
        if preset == "destructible":
            pass
        elif self._example_loops:
            self.example_caption.setText(self.translate("object_preview_looping"))
        else:
            self.example_caption.setText(
                self.translate("object_preview_opening") if len(self._example_frames) > 1
                else self.translate("object_preview_opened"))
        self._show_example_frame(); self._schedule_example()

    def _show_example_frame(self) -> None:
        if not self._example_frames:
            return
        self.example.setText("")
        self.example.setPixmap(_frame_pixmap(
            self._example_image, self._example_frames[self._example_index], 300))

    def _schedule_example(self) -> None:
        if (self._example_index + 1 >= len(self._example_frames) and
                not self._example_loops and not self._example_completion):
            return
        duration = max(1, int(
            self._example_frames[self._example_index].get("durationTicks", 1)))
        self._example_timer.start(max(16, round(duration * 1000 / 60)))

    def _advance_example(self) -> None:
        if self._example_index + 1 >= len(self._example_frames):
            if self._example_loops and self._example_frames:
                self._example_index = 0
                self._show_example_frame(); self._schedule_example()
            elif self._example_completion:
                completion = self._example_completion
                self._example_completion = ""
                if completion == "idle":
                    self._show_destructible_idle()
                else:
                    self._show_destructible_destroyed()
            return
        self._example_index += 1
        self._show_example_frame(); self._schedule_example()
        if self._example_index + 1 >= len(self._example_frames) and not self._example_loops:
            if not self._example_completion:
                self.example_caption.setText(self.translate("object_preview_opened"))

    def _show_destructible_idle(self) -> None:
        animation = self._source_animation()
        frames = animation.data.get("frames", []) if animation else []
        available = [frame for frame in frames if isinstance(frame, dict)] if isinstance(frames, list) else []
        if animation is not None and available:
            self._example_image = self._source_image(animation)
            index = min(self.destructible_idle_frame.value(), len(available) - 1)
            self._example_frames = [available[index]]
            self._example_index = 0
            self._show_example_frame()
        self.example_caption.setText(self.translate(
            "destructible_preview_attack", current=self._preview_health,
            maximum=self.maximum_health.value()))

    def _show_destructible_destroyed(self) -> None:
        animation = self._source_animation()
        frames = animation.data.get("frames", []) if animation else []
        available = [frame for frame in frames if isinstance(frame, dict)] if isinstance(frames, list) else []
        if animation is not None and available:
            self._example_image = self._source_image(animation)
            index = min(self.destruction_end.value(), len(available) - 1)
            self._example_frames = [available[index]]
            self._example_index = 0
            self._show_example_frame()
        self.example_caption.setText(self.translate("destructible_preview_destroyed"))

    @staticmethod
    def _preview_frame(frame: dict[str, object],
                       duration_ticks: int) -> dict[str, object]:
        result = dict(frame)
        result["durationTicks"] = max(1, duration_ticks)
        return result

    def _create(self) -> None:
        try:
            collision = self._collision_mask if self.collision_enabled.isChecked() else None
            if self.collision_enabled.isChecked() and collision is None:
                collision = self._default_collision_mask()
            origin = collision.get("origin", {}) if isinstance(collision, dict) else {}
            cells = collision.get("cells", []) if isinstance(collision, dict) else []
            occlusion = self._occlusion_mask if self.occlusion_enabled.isChecked() else None
            if self.occlusion_enabled.isChecked() and occlusion is None:
                occlusion = self._default_occlusion_mask()
            occlusion_origin = (
                occlusion.get("origin", {}) if isinstance(occlusion, dict) else {})
            occlusion_cells = (
                occlusion.get("cells", []) if isinstance(occlusion, dict) else [])
            request = ObjectAuthoringRequest(
                object_id=self.object_id.text(),
                display_name=self.name.text(),
                animation_id=str(self.animation.currentData() or ""),
                preset=str(self.preset.currentData() or "scenery"),
                scenery_loop=self.scenery_loop.isChecked(),
                container_capacity=self.capacity.value(),
                maximum_health=self.maximum_health.value(),
                reward_profile_id=str(self.reward_profile.currentData() or ""),
                leave_destroyed_residue=self.leave_destroyed_residue.isChecked(),
                damage_frame=self.damage_frame.value(),
                damage_duration_ticks=self.damage_duration.value(),
                destruction_frame_ticks=self.destruction_frame_duration.value(),
                destructible_idle_frame=self.destructible_idle_frame.value(),
                destruction_start_frame=self.destruction_start.value(),
                destruction_end_frame=self.destruction_end.value(),
                closed_frame=self.closed_frame.value(),
                opening_start_frame=self.opening_start.value(),
                opening_end_frame=self.opening_end.value(),
                collision_enabled=isinstance(collision, dict),
                collision_width=int(collision.get("width", 0)) if isinstance(collision, dict) else 0,
                collision_height=int(collision.get("height", 0)) if isinstance(collision, dict) else 0,
                collision_origin_x=int(origin.get("x", 0)) if isinstance(origin, dict) else 0,
                collision_origin_y=int(origin.get("y", 0)) if isinstance(origin, dict) else 0,
                collision_cells=tuple(int(value) for value in cells) if isinstance(cells, list) else (),
                depth_anchor_x=int(self._depth_anchor.get("x", 0)),
                depth_anchor_y=int(self._depth_anchor.get("y", 0)),
                occlusion_enabled=isinstance(occlusion, dict),
                occlusion_width=int(occlusion.get("width", 0))
                    if isinstance(occlusion, dict) else 0,
                occlusion_height=int(occlusion.get("height", 0))
                    if isinstance(occlusion, dict) else 0,
                occlusion_origin_x=int(occlusion_origin.get("x", 0))
                    if isinstance(occlusion_origin, dict) else 0,
                occlusion_origin_y=int(occlusion_origin.get("y", 0))
                    if isinstance(occlusion_origin, dict) else 0,
                occlusion_cells=tuple(int(value) for value in occlusion_cells)
                    if isinstance(occlusion_cells, list) else (),
            )
            created = (self.service.update(self.workspace, request) if self.definition
                       else self.service.create(self.workspace, request))
        except ValueError as error:
            QMessageBox.warning(self, self.windowTitle(), str(error))
            return
        self.created_object_id = created.definition_id
        self.accept()

class ObjectLibraryWidget(QWidget):
    """Create and place authored world objects backed by imported animations."""

    selected = Signal(object)
    place_requested = Signal(str, str)
    changed = Signal()
    status_changed = Signal(str)

    def __init__(self, workspace: ContentWorkspace | None, asset_root: Path | None,
                 translator: Translator | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.translate = translator or Translator()
        self.search = QLineEdit(); self.search.textChanged.connect(self.refresh)
        self.objects = PayloadListWidget(); self.objects.payload_factory = self._drag_payload
        self.objects.currentItemChanged.connect(self._selection_changed)
        self.objects.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.objects.customContextMenuRequested.connect(self._context_menu)
        self.create_button = QPushButton(); self.create_button.clicked.connect(self.create_object)
        self.configure_button = QPushButton(); self.configure_button.clicked.connect(self.configure_current)
        self.place_button = QPushButton(); self.place_button.clicked.connect(self.place_current)
        self.preview = QLabel(self.translate("no_image"))
        self.preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview.setMinimumSize(360, 360)
        self.preview.setStyleSheet("background: #161b22; color: #aeb8c4;")
        self.details = QLabel(); self.details.setWordWrap(True)
        self.interaction_preview = QPushButton()
        self.interaction_preview.clicked.connect(self.preview_interaction)
        self._image = QImage()
        self._frames: list[dict[str, object]] = []
        self._frame_index = 0
        self._preview_loops = False
        self._timer = QTimer(self); self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._advance)

        buttons = QHBoxLayout(); buttons.addWidget(self.create_button)
        buttons.addWidget(self.configure_button)
        buttons.addWidget(self.place_button)
        left = QWidget(); left_layout = QVBoxLayout(left)
        left_layout.addWidget(self.search); left_layout.addWidget(self.objects, 1)
        left_layout.addLayout(buttons)
        right = QWidget(); right_layout = QVBoxLayout(right)
        right_layout.addWidget(self.preview, 1)
        right_layout.addWidget(self.interaction_preview)
        right_layout.addWidget(self.details)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(left); splitter.addWidget(right); splitter.setStretchFactor(1, 1)
        splitter.setSizes([280, 620])
        layout = QVBoxLayout(self); layout.addWidget(splitter)
        self.retranslate(self.translate)

    def set_context(self, workspace: ContentWorkspace | None, asset_root: Path | None) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.search.setPlaceholderText(self.translate("search_objects"))
        self.create_button.setText(self.translate("create_object"))
        self.configure_button.setText(self.translate("configure_object"))
        self.place_button.setText(self.translate("place_object"))
        self.interaction_preview.setText(self.translate("preview_interaction"))
        self.refresh()

    def refresh(self) -> None:
        current = self._current_id()
        self.objects.blockSignals(True); self.objects.clear()
        definitions = self.workspace.definitions("objects", self.search.text()) if self.workspace else []
        for definition in definitions:
            item = QListWidgetItem(
                f"{definition.display_name}  [{definition.definition_id}]")
            item.setData(Qt.ItemDataRole.UserRole, definition.definition_id)
            icon = self._object_icon(definition)
            if not icon.isNull():
                item.setIcon(QIcon(icon))
            self.objects.addItem(item)
            if definition.definition_id == current:
                self.objects.setCurrentItem(item)
        if self.objects.currentItem() is None and self.objects.count():
            self.objects.setCurrentRow(0)
        self.objects.blockSignals(False)
        self._selection_changed(self.objects.currentItem(), None)

    def create_object(self) -> None:
        if self.workspace is None:
            return
        if not self.workspace.definitions("animations"):
            QMessageBox.information(
                self, self.translate("create_object"), self.translate("object_needs_animation"))
            return
        dialog = ObjectDefinitionDialog(
            self.workspace, self.translate, parent=self, asset_root=self.asset_root)
        if not dialog.exec():
            return
        self.refresh(); self._select(dialog.created_object_id)
        self.changed.emit(); self.status_changed.emit(self.translate("object_created"))

    def configure_current(self) -> None:
        if self.workspace is None:
            return
        definition = self.workspace.find("objects", self._current_id())
        if definition is None:
            return
        dialog = ObjectDefinitionDialog(
            self.workspace, self.translate, definition=definition,
            parent=self, asset_root=self.asset_root)
        if not dialog.exec():
            return
        object_id = dialog.created_object_id
        self.refresh(); self._select(object_id)
        self.changed.emit(); self.status_changed.emit(self.translate("object_configured"))

    def place_current(self) -> None:
        object_id = self._current_id()
        if object_id:
            self.place_requested.emit("objects", object_id)

    def _current_id(self) -> str:
        item = self.objects.currentItem()
        return str(item.data(Qt.ItemDataRole.UserRole)) if item else ""

    def _select(self, object_id: str) -> None:
        for index in range(self.objects.count()):
            item = self.objects.item(index)
            if item.data(Qt.ItemDataRole.UserRole) == object_id:
                self.objects.setCurrentItem(item); return

    def _selection_changed(self, current: QListWidgetItem | None,
                           unused: QListWidgetItem | None) -> None:
        del unused
        object_id = str(current.data(Qt.ItemDataRole.UserRole)) if current else ""
        definition = self.workspace.find("objects", object_id) if self.workspace and object_id else None
        self.place_button.setEnabled(definition is not None)
        self.configure_button.setEnabled(definition is not None and definition.origin == "project")
        self.selected.emit(definition)
        self._show_object(definition)

    def _show_object(self, definition: ContentDefinition | None) -> None:
        self._timer.stop(); self._image = QImage(); self._frames = []; self._frame_index = 0
        animation = self._object_animation(definition)
        self.interaction_preview.setEnabled(self._opened_animation(definition) is not None)
        if animation is None:
            self.preview.setPixmap(QPixmap()); self.preview.setText(self.translate("no_image"))
            self.details.setText(self.translate("no_objects")); return
        self._image = self._animation_image(animation)
        frames = animation.data.get("frames", [])
        self._frames = [frame for frame in frames if isinstance(frame, dict)] if isinstance(frames, list) else []
        if self._image.isNull() or not self._frames:
            self.preview.setPixmap(QPixmap()); self.preview.setText(self.translate("image_unavailable"))
            return
        self._preview_loops = bool(animation.data.get("loop", False))
        self.preview.setText(""); self._show_frame(); self._schedule()
        preset = self._preset_name(definition)
        lines = [definition.display_name, definition.definition_id,
                 self.translate(f"object_preset_{preset}")]
        if preset == "container":
            lines.append(self.translate("chest_contents_help"))
        self.details.setText("\n".join(lines))

    def _object_animation(self, definition: ContentDefinition | None) -> ContentDefinition | None:
        if definition is None or self.workspace is None:
            return None
        visual = self.workspace.find("objectVisuals", str(definition.data.get("visualSetId", "")))
        animation_id = str(visual.data.get("idleAnimationId", "")) if visual else ""
        return self.workspace.find("animations", animation_id) if animation_id else None

    def _opened_animation(self, definition: ContentDefinition | None) -> ContentDefinition | None:
        if definition is None or self.workspace is None:
            return None
        visual = self.workspace.find("objectVisuals", str(definition.data.get("visualSetId", "")))
        animation_id = str(visual.data.get("openedAnimationId", "")) if visual else ""
        return self.workspace.find("animations", animation_id) if animation_id else None

    def preview_interaction(self) -> None:
        if self.workspace is None:
            return
        definition = self.workspace.find("objects", self._current_id())
        animation = self._opened_animation(definition)
        if animation is None:
            return
        self._timer.stop()
        self._image = self._animation_image(animation)
        frames = animation.data.get("frames", [])
        self._frames = [frame for frame in frames if isinstance(frame, dict)] if isinstance(frames, list) else []
        self._frame_index = 0
        self._preview_loops = False
        if not self._image.isNull() and self._frames:
            self.preview.setText(""); self._show_frame(); self._schedule()

    def _animation_image(self, animation: ContentDefinition) -> QImage:
        if self.workspace is None:
            return QImage()
        image = self.workspace.find("visualImages", str(animation.data.get("imageId", "")))
        if image is None:
            return QImage()
        root = self.asset_root if image.data.get("root") == "gameAssets" else self.workspace.root
        relative = image.data.get("relativePath")
        return QImage(str(root / relative)) if root and isinstance(relative, str) else QImage()

    def _object_icon(self, definition: ContentDefinition) -> QPixmap:
        animation = self._object_animation(definition)
        if animation is None:
            return QPixmap()
        image = self._animation_image(animation)
        frames = animation.data.get("frames", [])
        first = frames[0] if isinstance(frames, list) and frames else None
        source = first.get("source") if isinstance(first, dict) else None
        if image.isNull() or not isinstance(source, dict):
            return QPixmap()
        crop = image.copy(
            int(source.get("x", 0)), int(source.get("y", 0)),
            int(source.get("width", 1)), int(source.get("height", 1)))
        return QPixmap.fromImage(crop).scaled(
            48, 48, Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation)

    def _show_frame(self) -> None:
        self.preview.setPixmap(_frame_pixmap(
            self._image, self._frames[self._frame_index]))

    def _schedule(self) -> None:
        if len(self._frames) > 1:
            duration = max(1, int(self._frames[self._frame_index].get("durationTicks", 1)))
            self._timer.start(max(16, round(duration * 1000 / 60)))

    def _advance(self) -> None:
        if not self._frames:
            return
        if self._frame_index + 1 < len(self._frames):
            self._frame_index += 1
            self._show_frame(); self._schedule()
        elif self._preview_loops:
            self._frame_index = 0
            self._show_frame(); self._schedule()

    def _context_menu(self, position: QPoint) -> None:
        item = self.objects.itemAt(position)
        if item is None:
            return
        self.objects.setCurrentItem(item)
        menu = QMenu(self)
        configure = menu.addAction(self.translate("configure_object"))
        configure.setEnabled(self.configure_button.isEnabled())
        place = menu.addAction(self.translate("place_object"))
        chosen = menu.exec(self.objects.viewport().mapToGlobal(position))
        if chosen == configure:
            self.configure_current()
        elif chosen == place:
            self.place_current()

    @staticmethod
    def _preset_name(definition: ContentDefinition) -> str:
        data = definition.data
        for key in ("door", "container", "destructible", "interactable"):
            if data.get(key) is not None:
                return key
        return "scenery"

    def _drag_payload(self, items: list[QListWidgetItem]) -> StudioDragPayload | None:
        if not items:
            return None
        return StudioDragPayload.content(
            "objects", str(items[0].data(Qt.ItemDataRole.UserRole)))
