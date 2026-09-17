from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PySide6.QtCore import QPoint, QPointF, QRect, Qt, QTimer, Signal
from PySide6.QtGui import (
    QColor,
    QImage,
    QPainter,
    QPen,
)
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QSpinBox,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..services.attack_authoring_service import (
    AttackAuthoringService,
    DIRECTIONS,
    TIMELINE_KINDS,
)
from ..services.localization import Translator
from .studio_visual_resolver import StudioVisualResolver

STATUS_KEYS = {
    "builtin": "attack_status_builtin",
    "override": "attack_status_override",
    "authored": "attack_status_authored",
}

EVENT_COLORS = {
    "activateHitbox": QColor(90, 200, 110),
    "deactivateHitbox": QColor(210, 120, 90),
    "spawnProjectile": QColor(110, 160, 230),
}

DIRECTION_VECTORS = {
    "down": (0, 1),
    "up": (0, -1),
    "left": (-1, 0),
    "right": (1, 0),
}


@dataclass(frozen=True, slots=True)
class AttackFrameInfo:
    """One authored animation frame mapped onto the attack timeline."""

    image: QImage
    anchor_x: int
    anchor_y: int
    start_tick: int
    end_tick: int
    frame_index: int


class AttackVisualResolver:
    """Resolve attack animation frames without coupling the runtime to Python."""

    def __init__(
        self,
        workspace: ContentWorkspace,
        asset_root: Path | None,
    ) -> None:
        self.workspace = workspace
        self.visuals = StudioVisualResolver()
        self.visuals.set_context(workspace, asset_root)

    def frames(
        self,
        definition_id: str,
        data: dict[str, object],
        facing: str,
    ) -> tuple[str, list[AttackFrameInfo]]:
        action_key = str(data.get("visualActionId") or "")
        animation_id = self._animation_id(
            definition_id,
            data,
            facing,
        )

        if not animation_id:
            return action_key, []

        animation = self.workspace.find(
            "animations",
            animation_id,
        )

        if animation is None:
            return animation_id, []

        result: list[AttackFrameInfo] = []
        cursor = 0

        for frame_index, frame in enumerate(
            animation.data.get("frames", [])
            if isinstance(animation.data.get("frames"), list)
            else []
        ):
            if not isinstance(frame, dict):
                continue

            visual = self.visuals.resolve_animation_frame(
                animation_id,
                frame_index,
            )

            if visual is None:
                continue

            try:
                duration = max(1, int(frame.get("durationTicks", 1)))
            except (TypeError, ValueError):
                duration = 1

            result.append(
                AttackFrameInfo(
                    image=visual.image,
                    anchor_x=visual.anchor_x,
                    anchor_y=visual.anchor_y,
                    start_tick=cursor,
                    end_tick=cursor + duration,
                    frame_index=frame_index,
                )
            )
            cursor += duration

        return animation_id, result

    def _animation_id(
        self,
        definition_id: str,
        data: dict[str, object],
        facing: str,
    ) -> str:
        visual_action = str(data.get("visualActionId") or "")

        for enemy in self.workspace.definitions("enemies"):
            attack_ids = enemy.data.get("attackIds")

            if (
                not isinstance(attack_ids, list)
                or definition_id not in attack_ids
            ):
                continue

            visual_set_id = enemy.data.get("visualSetId")

            if not isinstance(visual_set_id, str):
                continue

            animation_id = self._visual_action_animation(
                visual_set_id,
                "attacks",
                visual_action,
                facing,
            )

            if animation_id:
                return animation_id

        action_name = self._player_action_name(
            definition_id,
            data,
        )

        for player in self.workspace.definitions("players"):
            visual_set_id = player.data.get("visualSetId")

            if not isinstance(visual_set_id, str):
                continue

            animation_id = self._visual_action_animation(
                visual_set_id,
                "actions",
                action_name,
                facing,
            )

            if animation_id:
                return animation_id

        return ""

    @staticmethod
    def _player_action_name(
        definition_id: str,
        data: dict[str, object],
    ) -> str:
        visual_action = str(data.get("visualActionId") or "")
        action_name = visual_action.rsplit(".", 1)[-1]

        if action_name in {"sword", "bow"}:
            return action_name

        if str(data.get("kind")) == "projectile":
            return "bow"

        return definition_id.rsplit(".", 1)[-1]

    def _visual_action_animation(
        self,
        visual_set_id: str,
        field_name: str,
        action_name: str,
        facing: str,
    ) -> str:
        visual_set = self.workspace.find(
            "playerVisuals" if field_name == "actions" else "enemyVisuals",
            visual_set_id,
        )

        if visual_set is None:
            return ""

        actions = visual_set.data.get(field_name)

        if not isinstance(actions, list):
            return ""

        for action in actions:
            if not isinstance(action, dict):
                continue

            action_id = action.get(
                "actionId" if field_name == "actions" else "visualActionId"
            )

            if action_id != action_name:
                continue

            clips = action.get("clips")

            if not isinstance(clips, dict):
                continue

            animation_id = clips.get(facing)

            if not animation_id and facing == "right":
                animation_id = clips.get("left")

            return animation_id if isinstance(animation_id, str) else ""

        return ""


class AttackPreviewCanvas(QWidget):
    """Draws one animation frame with the attack values overlaid."""

    def __init__(
        self,
        translator: Translator,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)

        self.translate = translator

        self.setMinimumSize(264, 264)

        self._frame: QImage | None = None

        self._anchor = (24, 47)

        self._facing = "down"

        self._hitbox: dict[str, int] | None = None

        self._minimum_range = 0

        self._maximum_range = 0

        self._knockback = 0

        self._damage_amount = 0

    def set_frame(
        self,
        frame: QImage | None,
        anchor: tuple[int, int],
    ) -> None:
        self._frame = frame if (
            frame is not None and not frame.isNull()
        ) else None

        self._anchor = anchor

        self.update()

    def set_overlay(
        self,
        facing: str,
        hitbox: dict[str, int] | None,
        minimum_range: int,
        maximum_range: int,
        knockback: int,
        damage_amount: int = 0,
    ) -> None:
        self._facing = facing

        self._hitbox = hitbox

        self._minimum_range = minimum_range

        self._maximum_range = maximum_range

        self._knockback = knockback

        self._damage_amount = damage_amount

        self.update()

    def paintEvent(self, event) -> None:  # noqa: N802
        del event

        painter = QPainter(self)

        painter.fillRect(
            self.rect(),
            QColor(22, 27, 34),
        )

        frame = self._frame

        frame_width = frame.width() if frame is not None else 48

        frame_height = frame.height() if frame is not None else 48

        scale = max(
            1,
            min(
                (self.width() - 56) // max(1, frame_width),
                (self.height() - 56) // max(1, frame_height),
            ),
        )

        anchor_x = (
            (self.width() - frame_width * scale) // 2
            + self._anchor[0] * scale
        )

        anchor_y = (
            (self.height() - frame_height * scale) // 2
            + self._anchor[1] * scale
        )

        if frame is not None:
            painter.setRenderHint(
                QPainter.RenderHint.SmoothPixmapTransform,
                False,
            )

            painter.drawImage(
                QRect(
                    anchor_x - self._anchor[0] * scale,
                    anchor_y - self._anchor[1] * scale,
                    frame_width * scale,
                    frame_height * scale,
                ),
                frame,
            )
        else:
            painter.setPen(
                QPen(QColor(120, 130, 145), 1)
            )

            painter.drawText(
                8,
                16,
                self.translate(
                    "attack_preview_placeholder"
                ),
            )

            painter.setBrush(
                QColor(60, 70, 85)
            )

            painter.drawEllipse(
                QPoint(
                    anchor_x,
                    anchor_y - 28 * scale,
                ),
                6 * scale,
                6 * scale,
            )

            painter.drawRect(
                anchor_x - 5 * scale,
                anchor_y - 21 * scale,
                10 * scale,
                18 * scale,
            )

        painter.setRenderHint(
            QPainter.RenderHint.Antialiasing,
            True,
        )

        summary = (
            f"{self.translate('attack_damage')}: {self._damage_amount}  "
            f"{self.translate('attack_knockback')}: {self._knockback}px  "
            f"{self.translate('attack_min_range')}: {self._minimum_range}px  "
            f"{self.translate('attack_max_range')}: {self._maximum_range}px"
        )

        painter.setPen(
            QPen(QColor(210, 220, 232), 1)
        )

        painter.drawText(
            QRect(8, 8, self.width() - 16, 34),
            Qt.TextFlag.TextWordWrap,
            summary,
        )

        feet = QPointF(anchor_x, anchor_y)

        direction_x, direction_y = DIRECTION_VECTORS.get(
            self._facing,
            (0, 1),
        )

        # Maximum range as a dashed line from the feet.
        if self._maximum_range > 0:
            painter.setPen(
                QPen(
                    QColor(110, 160, 230),
                    2,
                    Qt.PenStyle.DashLine,
                )
            )

            painter.drawLine(
                feet,
                feet
                + QPointF(
                    direction_x
                    * self._maximum_range
                    * scale,
                    direction_y
                    * self._maximum_range
                    * scale,
                ),
            )

        # Minimum range as a short perpendicular marker.
        if self._minimum_range > 0:
            painter.setPen(
                QPen(QColor(110, 160, 230), 3)
            )

            painter.drawLine(
                feet
                + QPointF(
                    direction_x
                    * self._minimum_range
                    * scale,
                    direction_y
                    * self._minimum_range
                    * scale,
                )
                - QPointF(direction_y, direction_x) * 6,
                feet
                + QPointF(
                    direction_x
                    * self._minimum_range
                    * scale,
                    direction_y
                    * self._minimum_range
                    * scale,
                )
                + QPointF(direction_y, direction_x) * 6,
            )

        # Melee hitbox: translucent red rectangle relative to the feet.
        if self._hitbox is not None:
            rect_x = (
                anchor_x
                + self._hitbox.get("offsetX", 0) * scale
            )

            rect_y = (
                anchor_y
                + self._hitbox.get("offsetY", 0) * scale
            )

            rect_w = max(
                1,
                self._hitbox.get("width", 0) * scale,
            )

            rect_h = max(
                1,
                self._hitbox.get("height", 0) * scale,
            )

            painter.setPen(
                QPen(QColor(230, 90, 90), 2)
            )

            painter.setBrush(
                QColor(230, 90, 90, 60)
            )

            painter.drawRect(
                int(rect_x),
                int(rect_y),
                int(rect_w),
                int(rect_h),
            )

        # Knockback arrow in the facing direction.
        if self._knockback != 0:
            end = feet + QPointF(
                direction_x * self._knockback * scale,
                direction_y * self._knockback * scale,
            )

            painter.setPen(
                QPen(QColor(240, 200, 90), 3)
            )

            painter.drawLine(feet, end)

            painter.setBrush(
                QColor(240, 200, 90)
            )

            painter.setPen(Qt.PenStyle.NoPen)

            offset = QPointF(direction_y, direction_x) * 5

            tip = end + QPointF(
                direction_x * 7,
                direction_y * 7,
            )

            painter.drawPolygon(
                [
                    QPointF(tip),
                    end - offset,
                    end + offset,
                ]
            )

        painter.end()


class TimelineBar(QWidget):
    """Visual attack timeline: frame spans, ruler ticks and gameplay events."""

    event_selected = Signal(int)
    tick_selected = Signal(int)

    def __init__(
        self,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)

        self.setMinimumHeight(160)

        self.total_ticks = 1

        self.events: list[
            tuple[int, str]
        ] = []

        self.selected_row = -1

        self.visual_animation_id = ""

        self.visual_frames: list[AttackFrameInfo] = []

        self.current_tick = 0

    def set_data(
        self,
        total_ticks: int,
        events: list[tuple[int, str]],
        selected_row: int,
        visual_frames: list[AttackFrameInfo] | None = None,
        visual_animation_id: str = "",
        current_tick: int = 0,
    ) -> None:
        self.total_ticks = max(1, total_ticks)

        self.events = list(events)

        self.selected_row = selected_row

        self.visual_frames = list(visual_frames or [])

        self.visual_animation_id = visual_animation_id

        self.current_tick = max(0, min(int(current_tick), self.total_ticks - 1))

        self.update()

    def _event_positions(
        self,
    ) -> list[tuple[int, int, str]]:
        if not self.events:
            return []

        left = 16

        right = max(
            left + 1,
            self.width() - 16,
        )

        span = max(1, self.total_ticks - 1)

        positions: list[tuple[int, int, str]] = []

        for row, (tick, kind) in enumerate(self.events):
            ratio = (
                tick / span
                if self.total_ticks > 1
                else 0
            )

            x = int(left + ratio * (right - left))

            positions.append((row, x, kind))

        return positions

    def paintEvent(self, event) -> None:  # noqa: N802
        del event

        painter = QPainter(self)

        painter.setRenderHint(
            QPainter.RenderHint.Antialiasing,
            True,
        )

        painter.fillRect(
            self.rect(),
            QColor(24, 29, 38),
        )

        left = 16

        right = max(
            left + 1,
            self.width() - 16,
        )

        ruler_y = self.height() - 10

        frame_height = max(
            24,
            min(
                48,
                (self.height() - 70) // 2,
            ),
        )

        event_height = max(
            16,
            min(
                26,
                (self.height() - 70) // 2,
            ),
        )

        frame_y = 18

        event_y = frame_y + frame_height + 12

        self._visualize_animation_at_tick(
            painter,
            left,
            right,
            frame_y,
            frame_height,
        )

        self._visualize_events_at_tick(
            painter,
            left,
            right,
            event_y,
            event_height,
        )

        painter.setPen(
            QPen(QColor(90, 100, 115), 2)
        )

        painter.drawLine(
            left,
            ruler_y,
            right,
            ruler_y,
        )

        painter.setPen(
            QPen(QColor(150, 160, 175), 1)
        )

        for tick, x in self._tick_positions():
            painter.drawLine(
                x,
                ruler_y - 4,
                x,
                ruler_y + 4,
            )

            painter.drawText(
                x - (18 if tick else 2),
                ruler_y + 18,
                str(tick),
            )

        painter.end()

    def mousePressEvent(self, event) -> None:  # noqa: N802
        click = event.position().toPoint()

        best_row = -1

        best_distance = 13

        for row, x, unused in self._event_positions():
            del unused

            distance = abs(click.x() - x)

            if distance <= best_distance:
                best_distance = distance

                best_row = row

        if best_row >= 0:
            self.event_selected.emit(best_row)

        tick = self._tick_at_x(click.x())

        if tick != self.current_tick:
            self.current_tick = tick

            self.update()

            self.tick_selected.emit(tick)

    def _tick_positions(
        self,
    ) -> list[tuple[int, int]]:
        left = 16

        right = max(left + 1, self.width() - 16)

        span = max(1, self.total_ticks - 1)

        values = [
            0,
            self.total_ticks // 4,
            self.total_ticks // 2,
            (self.total_ticks * 3) // 4,
            self.total_ticks - 1,
        ]

        return [
            (
                value,
                int(left + (value / span) * (right - left)),
            )
            for value in sorted(set(values))
            if 0 <= value < self.total_ticks
        ]

    def _frame_positions(
        self,
    ) -> list[tuple[AttackFrameInfo, int, int]]:
        if not self.visual_frames:
            return []

        left = 16

        right = max(left + 1, self.width() - 16)

        scale = (right - left) / max(1, self.total_ticks)

        positions: list[tuple[AttackFrameInfo, int, int]] = []

        for frame in self.visual_frames:
            x1 = int(left + frame.start_tick * scale)

            x2 = int(left + frame.end_tick * scale)

            positions.append(
                (
                    frame,
                    x1,
                    max(x1 + 2, x2),
                )
            )

        return positions

    def _frame_at_tick(
        self,
        tick: int,
    ) -> AttackFrameInfo | None:
        matches = [
            frame
            for frame in self.visual_frames
            if frame.start_tick <= tick < frame.end_tick
        ]

        if matches:
            return matches[-1]

        return self.visual_frames[-1] if self.visual_frames else None

    def _tick_at_x(
        self,
        x: int,
    ) -> int:
        left = 16

        right = max(left + 1, self.width() - 16)

        ratio = min(1.0, max(0.0, (x - left) / (right - left)))

        return int(round(ratio * max(0, self.total_ticks - 1)))

    def _visualize_animation_at_tick(
        self,
        painter: QPainter,
        left: int,
        right: int,
        track_y: int,
        track_height: int,
    ) -> None:
        positions = self._frame_positions()

        if not positions:
            return

        painter.setPen(
            QPen(QColor(70, 78, 92), 1)
        )

        for index, (frame, x, x2) in enumerate(positions):
            painter.setBrush(
                QColor(36, 44, 58)
                if index % 2 == 0
                else QColor(44, 52, 68)
            )

            painter.drawRect(x, track_y, x2 - x, track_height)

            thumbnail = frame.image.scaled(
                track_height - 4,
                track_height - 4,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.FastTransformation,
            )

            thumbnail_x = x + max(0, ((x2 - x) - thumbnail.width()) // 2)

            thumbnail_y = track_y + (track_height - thumbnail.height()) // 2

            painter.drawImage(QPoint(thumbnail_x, thumbnail_y), thumbnail)

        painter.setPen(
            QPen(QColor(150, 160, 175), 1)
        )

        label = self.visual_animation_id or "animation"

        painter.drawText(
            left,
            track_y - 4,
            label,
        )

        current_x = int(
            left
            + self.current_tick
            * (right - left)
            / max(1, self.total_ticks - 1)
        )

        playhead_color = QColor(240, 248, 255)

        painter.setPen(
            QPen(playhead_color, 2)
        )

        painter.drawLine(
            current_x,
            track_y - 1,
            current_x,
            track_y + track_height + 1,
        )

    def _visualize_events_at_tick(
        self,
        painter: QPainter,
        left: int,
        right: int,
        track_y: int,
        track_height: int,
    ) -> None:
        painter.setPen(
            QPen(QColor(70, 78, 92), 1)
        )

        painter.setBrush(
            QColor(32, 38, 50)
        )

        painter.drawRect(
            left,
            track_y,
            right - left,
            track_height,
        )

        span = max(1, self.total_ticks - 1)

        for row, (tick, kind) in enumerate(self.events):
            x = int(left + tick / span * (right - left))

            color = EVENT_COLORS.get(kind, QColor(160, 160, 160))

            selected = row == self.selected_row

            painter.setPen(
                QPen(color, 4 if selected else 2)
            )

            painter.setBrush(color)

            painter.drawEllipse(
                QPoint(x, track_y + track_height // 2),
                8 if selected else 6,
                8 if selected else 6,
            )

            if selected:
                painter.drawLine(
                    x,
                    track_y - 4,
                    x,
                    track_y + track_height + 4,
        )


class InfoButton(QToolButton):
    """Small visual explanation marker for editor fields."""

    def __init__(
        self,
        explanation: str,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)

        self.setText("i")

        self.setToolTip(explanation)

        self.setStatusTip(explanation)

        self.setAccessibleName("information")

        self.setAccessibleDescription(explanation)

        self.setFocusPolicy(
            Qt.FocusPolicy.NoFocus
        )

        self.setCursor(
            Qt.CursorShape.PointingHandCursor
        )

        self.setFixedSize(20, 20)

        self.setStyleSheet(
            """
            QToolButton {
                color: #dbe7f4;
                background: #324459;
                border: 1px solid #586d84;
                border-radius: 10px;
                font-weight: 600;
                font-size: 11px;
            }

            QToolButton:hover {
                background: #436179;
                border-color: #8ea9c4;
            }
            """
        )

class AttackDefinitionDialog(QDialog):
    """Structured editor for one attack definition."""

    def __init__(
        self,
        workspace: ContentWorkspace,
        asset_root: Path | None,
        definition_id: str,
        translator: Translator,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)

        self.translate = translator
        self.workspace = workspace
        self.definition_id = definition_id

        self.data = AttackAuthoringService(
            workspace
        ).configuration(
            definition_id
        )

        self.asset_root = asset_root

        self.preview = AttackPreviewCanvas(translator)

        self._visual_resolver = AttackVisualResolver(
            workspace,
            asset_root,
        )

        self._visual_animation_id = ""

        self._visual_frames: list[AttackFrameInfo] = []

        self._selected_tick = 0

        self._selected_event = -1

        self._events: list[tuple[int, str]] = []

        self._visual_loaded = False

        self.setWindowTitle(
            definition_id
        )

        self.resize(920, 700)

        self.kind = QComboBox()

        for kind in (
            "meleeHitbox",
            "projectile",
        ):
            self.kind.addItem(
                kind,
                kind,
            )

        self.damage_amount = QSpinBox()
        self.damage_amount.setRange(
            -999,
            999,
        )

        self.knockback = QSpinBox()
        self.knockback.setRange(
            -999,
            999,
        )

        self.total_ticks = QSpinBox()
        self.total_ticks.setRange(
            1,
            9999,
        )

        self.cooldown_ticks = QSpinBox()
        self.cooldown_ticks.setRange(
            0,
            9999,
        )

        self.minimum_range = QSpinBox()
        self.minimum_range.setRange(
            -999,
            9999,
        )

        self.maximum_range = QSpinBox()
        self.maximum_range.setRange(
            -999,
            9999,
        )

        self.visual_action = QLineEdit()

        self.visual_action.textChanged.connect(
            self._refresh_visual
        )

        self.facing = QComboBox()

        for direction in DIRECTIONS:
            self.facing.addItem(
                direction.capitalize(),
                direction,
            )

        form = QFormLayout()

        id_label = QLabel(
            definition_id
        )

        form.addRow(
            self.translate("attack_id"),
            id_label,
        )

        form.addRow(
            self.translate("attack_kind"),
            self._with_info(
                self.kind,
                "attack_kind_info",
            ),
        )

        form.addRow(
            self.translate("attack_damage"),
            self._with_info(
                self.damage_amount,
                "attack_damage_info",
            ),
        )

        form.addRow(
            self.translate("attack_knockback"),
            self._with_info(
                self.knockback,
                "attack_knockback_info",
            ),
        )

        form.addRow(
            self.translate("attack_total_ticks"),
            self._with_info(
                self.total_ticks,
                "attack_total_ticks_info",
            ),
        )

        form.addRow(
            self.translate("attack_cooldown"),
            self._with_info(
                self.cooldown_ticks,
                "attack_cooldown_info",
            ),
        )

        form.addRow(
            self.translate("attack_min_range"),
            self._with_info(
                self.minimum_range,
                "attack_min_range_info",
            ),
        )

        form.addRow(
            self.translate("attack_max_range"),
            self._with_info(
                self.maximum_range,
                "attack_max_range_info",
            ),
        )

        form.addRow(
            self.translate("attack_visual_action"),
            self._with_info(
                self.visual_action,
                "attack_visual_action_info",
            ),
        )

        form.addRow(
            self.translate("attack_facing"),
            self._with_info(
                self.facing,
                "attack_facing_info",
            ),
        )

        self._direction_boxes: dict[
            str,
            dict[str, QSpinBox],
        ] = {}

        melee_group = QGroupBox(
            self.translate("attack_melee_hitboxes")
        )

        melee_layout = QVBoxLayout(
            melee_group
        )

        melee_info_row = QHBoxLayout()

        melee_info_row.addWidget(
            InfoButton(
                self.translate("attack_melee_hitboxes_info")
            )
        )

        melee_info_row.addStretch(1)

        melee_layout.addLayout(
            melee_info_row
        )

        for direction in DIRECTIONS:
            row = QHBoxLayout()

            boxes: dict[str, QSpinBox] = {}

            for field in (
                "offsetX",
                "offsetY",
                "width",
                "height",
            ):
                spin = QSpinBox()

                spin.setRange(
                    -999,
                    999,
                )

                boxes[field] = spin

                row.addWidget(
                    QLabel(
                        self.translate(
                            f"attack_{field}"
                        )
                    )
                )

                row.addWidget(
                    spin
                )

            self._direction_boxes[direction] = boxes

            melee_layout.addLayout(
                row
            )

        self.projectile = QComboBox()

        for definition in workspace.definitions(
            "projectiles"
        ):
            self.projectile.addItem(
                definition.definition_id,
                definition.definition_id,
            )

        form.addRow(
            self.translate("attack_projectile"),
            self._with_info(
                self.projectile,
                "attack_projectile_info",
            ),
        )

        timeline_group = QGroupBox(
            self.translate("attack_timeline")
        )

        timeline_layout = QVBoxLayout(
            timeline_group
        )

        timeline_info_row = QHBoxLayout()

        timeline_info_row.addWidget(
            InfoButton(
                self.translate("attack_timeline_info")
            )
        )

        timeline_info_row.addStretch(1)

        timeline_layout.addLayout(
            timeline_info_row
        )

        self.timeline = TimelineBar()

        timeline_layout.addWidget(
            self.timeline
        )

        event_row = QHBoxLayout()

        self.event_tick = QSpinBox()
        self.event_tick.setRange(
            0,
            9999,
        )

        self.event_kind = QComboBox()

        for kind in TIMELINE_KINDS:
            self.event_kind.addItem(
                kind,
                kind,
            )

        self.event_add = QPushButton(
            self.translate("attack_event_add")
        )

        self.event_remove = QPushButton(
            self.translate("attack_event_remove")
        )

        self.play_button = QPushButton(
            self.translate("attack_timeline_play")
        )

        event_row.addWidget(
            QLabel(
                self.translate("attack_event_tick")
            )
        )

        event_row.addWidget(
            self.event_tick
        )

        event_row.addWidget(
            self.event_kind
        )

        event_row.addWidget(
            self.event_add
        )

        event_row.addWidget(
            self.event_remove
        )

        event_row.addStretch(1)

        event_row.addWidget(
            self.play_button
        )

        timeline_layout.addLayout(
            event_row
        )

        timeline_hint = QLabel(
            self.translate("attack_timeline_hint")
        )

        timeline_hint.setWordWrap(True)

        timeline_layout.addWidget(timeline_hint)

        buttons = QHBoxLayout()

        self.ok_button = QPushButton(
            self.translate("attack_save")
        )

        self.ok_button.setDefault(
            True
        )

        self.cancel_button = QPushButton(
            self.translate("attack_cancel")
        )

        buttons.addStretch(
            1
        )

        buttons.addWidget(
            self.cancel_button
        )

        buttons.addWidget(
            self.ok_button
        )

        editor_layout = QHBoxLayout()

        form_column = QVBoxLayout()

        form_column.addLayout(form)

        form_column.addWidget(melee_group)

        preview_column = QVBoxLayout()

        preview_column.addWidget(
            self.preview,
            1,
        )

        editor_layout.addLayout(
            form_column,
            3,
        )

        editor_layout.addLayout(
            preview_column,
            2,
        )

        layout = QVBoxLayout(self)

        layout.addLayout(editor_layout)

        layout.addWidget(timeline_group)

        layout.addLayout(buttons)

        self.ok_button.clicked.connect(
            self._save
        )

        self.cancel_button.clicked.connect(
            self.reject
        )

        self.event_add.clicked.connect(
            self._add_event
        )

        self.event_remove.clicked.connect(
            self._remove_event
        )

        self.timeline.event_selected.connect(
            self._select_event
        )

        self.timeline.tick_selected.connect(
            self._select_tick
        )

        self.damage_amount.valueChanged.connect(
            self._update_preview
        )

        self.knockback.valueChanged.connect(
            self._update_preview
        )

        self.minimum_range.valueChanged.connect(
            self._update_preview
        )

        self.maximum_range.valueChanged.connect(
            self._update_preview
        )

        self.total_ticks.valueChanged.connect(
            self._sync_timeline
        )

        self.facing.currentIndexChanged.connect(
            self._update_preview
        )

        self.play_timer = QTimer(self)

        self.play_timer.setInterval(66)

        self.play_timer.timeout.connect(
            self._advance_playback
        )

        self.play_button.clicked.connect(
            self._toggle_play
        )

        self.kind.currentIndexChanged.connect(
            self._update_preview
        )

        for boxes in self._direction_boxes.values():
            for spin in boxes.values():
                spin.valueChanged.connect(
                    self._update_preview
                )

        self.kind.currentIndexChanged.connect(
            self._sync_enabled
        )

        self._load()

    def _with_info(
        self,
        widget: QWidget,
        translation_key: str,
    ) -> QWidget:
        wrapper = QWidget(self)

        row = QHBoxLayout(wrapper)

        row.setContentsMargins(0, 0, 0, 0)

        row.setSpacing(6)

        row.addWidget(
            widget,
            1,
        )

        row.addWidget(
            InfoButton(
                self.translate(translation_key),
                wrapper,
            )
        )

        return wrapper

    def _load(self) -> None:
        data = self.data

        self.kind.setCurrentIndex(
            max(
                0,
                self.kind.findData(
                    data.get("kind")
                ),
            )
        )

        damage = (
            data.get("damage")
            if isinstance(data.get("damage"), dict)
            else {}
        )

        self.damage_amount.setValue(
            int(damage.get("amount", 0) or 0)
        )

        self.knockback.setValue(
            int(
                damage.get("knockbackPixels", 0)
                or 0
            )
        )

        self.total_ticks.setValue(
            int(data.get("totalTicks", 1) or 1)
        )

        self.cooldown_ticks.setValue(
            int(
                data.get("cooldownTicks", 0)
                or 0
            )
        )

        self.minimum_range.setValue(
            int(
                data.get("minimumRangePixels", 0)
                or 0
            )
        )

        self.maximum_range.setValue(
            int(
                data.get("maximumRangePixels", 0)
                or 0
            )
        )

        self.visual_action.setText(
            str(data.get("visualActionId", ""))
        )

        melee = data.get("meleeHitboxes")

        for direction in DIRECTIONS:
            box = (
                melee.get(direction)
                if isinstance(melee, dict)
                else None
            )

            box = box if isinstance(box, dict) else {}

            for field, spin in self._direction_boxes[
                direction
            ].items():
                spin.setValue(
                    int(box.get(field, 0) or 0)
                )

        projectile = data.get(
            "projectileDefinitionId"
        )

        index = self.projectile.findData(
            projectile
        )

        self.projectile.setCurrentIndex(
            index
        )

        for event in (
            data.get("timeline")
            if isinstance(data.get("timeline"), list)
            else []
        ):
            if not isinstance(event, dict):
                continue

            self._append_event(
                int(event.get("tick", 0) or 0),
                str(event.get("kind", "")),
            )

        self._sync_enabled()

        self._refresh_visual()

    def _append_event(
        self,
        tick: int,
        kind: str,
    ) -> None:
        self._events.append((int(tick), str(kind)))

        self._events.sort(key=lambda event: event[0])

        self._sync_timeline()

    def _add_event(self) -> None:
        tick = self.event_tick.value()

        kind = str(self.event_kind.currentData())

        self._append_event(
            tick,
            kind,
        )

        self._selected_event = self._events.index((tick, kind))

        self._sync_timeline()

    def _remove_event(self) -> None:
        if 0 <= self._selected_event < len(self._events):
            del self._events[self._selected_event]

            self._selected_event = -1

            self._sync_timeline()

            self._update_preview()

    def _select_event(
        self,
        row: int,
    ) -> None:
        if not 0 <= row < len(self._events):
            return

        self._selected_event = row

        self._sync_timeline()

        tick = self._events[row][0]

        self.event_tick.setValue(tick)

        self._select_tick(tick)

    def _select_tick(
        self,
        tick: int,
    ) -> None:
        self._selected_tick = max(
            0,
            min(tick, self.total_ticks.value() - 1),
        )

        self._update_preview()

    def _toggle_play(self) -> None:
        if self.play_timer.isActive():
            self.play_timer.stop()

            self.play_button.setText(
                self.translate("attack_timeline_play")
            )

            return

        self.play_timer.start()

        self.play_button.setText(
            self.translate("attack_timeline_pause")
        )

        self._advance_playback()

    def _advance_playback(self) -> None:
        next_tick = (self._selected_tick + 1) % self.total_ticks.value()

        self._select_tick(next_tick)

    def _active_hitbox(
        self,
    ) -> dict[str, int] | None:
        if str(self.kind.currentData()) != "meleeHitbox":
            return None

        active = False

        for tick, kind in sorted(self._events):
            if tick > self._selected_tick:
                break

            if kind == "activateHitbox":
                active = True
            elif kind == "deactivateHitbox":
                active = False

        if not active:
            return None

        return {
            field: spin.value()
            for field, spin in self._direction_boxes[
                str(self.facing.currentData())
            ].items()
        }

    def _refresh_visual(self) -> None:
        (
            animation_id,
            frames,
        ) = self._visual_resolver.frames(
            self.definition_id,
            {
                **self.data,
                "visualActionId": self.visual_action.text().strip(),
                "kind": self.kind.currentData(),
            },
            str(self.facing.currentData() or "down"),
        )

        self._visual_animation_id = animation_id

        self._visual_frames = frames

        self._visual_loaded = True

        self._sync_timeline()

        self._update_preview()

    def _update_preview(self) -> None:
        if not self._visual_loaded:
            self._refresh_visual()

            return

        matches = [
            frame
            for frame in self._visual_frames
            if frame.start_tick <= self._selected_tick < frame.end_tick
        ]

        frame = matches[-1] if matches else self._visual_frames[-1]

        self.preview.set_frame(
            frame.image,
            (frame.anchor_x, frame.anchor_y),
        )

        self.preview.set_overlay(
            str(self.facing.currentData() or "down"),
            self._active_hitbox(),
            self.minimum_range.value(),
            self.maximum_range.value(),
            self.knockback.value(),
            self.damage_amount.value(),
        )

    def _sync_timeline(self) -> None:
        total = self.total_ticks.value()

        self.event_tick.setRange(0, total - 1)

        self._selected_tick = max(
            0,
            min(self._selected_tick, total - 1),
        )

        self.timeline.set_data(
            total,
            self._events,
            self._selected_event,
            self._visual_frames,
            self._visual_animation_id,
            self._selected_tick,
        )

        self._update_preview()

    def _sync_enabled(self) -> None:
        melee = (
            self.kind.currentData()
            == "meleeHitbox"
        )

        for boxes in self._direction_boxes.values():
            for spin in boxes.values():
                spin.setEnabled(melee)

        self.projectile.setEnabled(
            not melee
        )

        self._update_preview()

    def _collect_timeline(
        self,
    ) -> list[dict[str, object]]:
        events: list[dict[str, object]] = []

        for tick, kind in self._events:

            events.append(
                {
                    "tick": tick,
                    "kind": kind,
                }
            )

        events.sort(
            key=lambda event: event["tick"]
        )

        return events

    def result_data(self) -> dict[str, object]:
        kind = str(self.kind.currentData())

        data: dict[str, object] = {
            "id": self.definition_id,
            "kind": kind,
            "damage": {
                "amount": self.damage_amount.value(),
                "knockbackPixels": self.knockback.value(),
            },
            "totalTicks": self.total_ticks.value(),
            "cooldownTicks": self.cooldown_ticks.value(),
            "minimumRangePixels": self.minimum_range.value(),
            "maximumRangePixels": self.maximum_range.value(),
            "visualActionId": self.visual_action.text().strip(),
            "timeline": self._collect_timeline(),
        }

        if kind == "meleeHitbox":
            data["meleeHitboxes"] = {
                direction: {
                    field: spin.value()
                    for field, spin in self._direction_boxes[
                        direction
                    ].items()
                }
                for direction in DIRECTIONS
            }

            data["projectileDefinitionId"] = None
        else:
            data["meleeHitboxes"] = None
            data["projectileDefinitionId"] = (
                self.projectile.currentData()
            )

        return data

    def _save(self) -> None:
        service = AttackAuthoringService(
            self.workspace
        )

        try:
            service.configure(
                self.definition_id,
                self.result_data(),
            )
        except ValueError as error:
            self.statusMessage = str(error)

            self.statusMessageReady = True

            self.reject()

            return

        self.accept()


class AttackManagerDialog(QDialog):
    """Embedded attack library for management from other widgets.

    Data-driven by design: lists every builtin and authored attack the
    workspace can see, so newly created attacks appear without code
    changes. Consumers forward `changed`/`status_changed`.
    """

    changed = Signal()

    status_changed = Signal(str)

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        translator: Translator,
        parent: QWidget | None = None,
        asset_root: Path | None = None,
    ) -> None:
        super().__init__(parent)

        self.translate = translator

        self.setWindowTitle(
            self.translate("player_attacks_manage")
        )

        self.library = AttackLibraryWidget(
            workspace,
            translator,
            self,
            asset_root,
        )

        self.library.changed.connect(
            self.changed
        )

        self.library.status_changed.connect(
            self.status_changed
        )

        self.close_button = QPushButton(
            self.translate("attack_cancel")
        )

        self.close_button.clicked.connect(
            self.reject
        )

        layout = QVBoxLayout(self)

        layout.addWidget(
            self.library,
            1,
        )

        layout.addWidget(
            self.close_button
        )


class AttackLibraryWidget(QWidget):
    """Authoring view for attack definitions."""

    changed = Signal()

    status_changed = Signal(str)

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        translator: Translator | None = None,
        parent: QWidget | None = None,
        asset_root: Path | None = None,
    ) -> None:
        super().__init__(parent)

        self.translate = (
            translator or Translator()
        )

        self.workspace = workspace

        self.asset_root = asset_root

        self.search = QLineEdit()
        self.search.textChanged.connect(self.refresh)

        self.attacks = QListWidget()
        self.attacks.itemDoubleClicked.connect(
            self._edit_selected
        )

        self.add_button = QPushButton()
        self.add_button.clicked.connect(self._add)

        self.edit_button = QPushButton()
        self.edit_button.clicked.connect(
            self._edit_selected
        )

        self.delete_button = QPushButton()
        self.delete_button.clicked.connect(
            self._delete_selected
        )

        self.help = QLabel()
        self.help.setWordWrap(True)
        self.help.setStyleSheet("color: #aeb8c4;")

        buttons = QHBoxLayout()
        buttons.addWidget(self.add_button)
        buttons.addWidget(self.edit_button)
        buttons.addWidget(self.delete_button)
        buttons.addStretch(1)

        layout = QVBoxLayout(self)
        layout.addWidget(self.search)
        layout.addWidget(self.attacks, 1)
        layout.addLayout(buttons)
        layout.addWidget(self.help)

        self.retranslate(self.translate)

    def set_context(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None = None,
    ) -> None:
        self.workspace = workspace

        self.asset_root = asset_root

        self.refresh()

    def retranslate(
        self,
        translator: Translator,
    ) -> None:
        self.translate = translator

        self.search.setPlaceholderText(
            self.translate("attack_search")
        )

        self.add_button.setText(
            self.translate("attack_new")
        )

        self.edit_button.setText(
            self.translate("attack_edit")
        )

        self.delete_button.setText(
            self.translate("attack_delete")
        )

        self.help.setText(
            self.translate("attack_library_help")
        )

        self.refresh()

    def refresh(self) -> None:
        self.attacks.blockSignals(True)
        self.attacks.clear()

        if self.workspace is None:
            self.attacks.blockSignals(False)
            return

        query = (
            self.search.text().strip().casefold()
        )

        for entry in AttackAuthoringService(
            self.workspace
        ).entries():
            label = (
                f"{entry.definition_id}  "
                f"[{self.translate(STATUS_KEYS[entry.status])}]"
            )

            if query and query not in label.casefold():
                continue

            item = QListWidgetItem(label)
            item.setData(
                Qt.ItemDataRole.UserRole,
                entry.definition_id,
            )

            if entry.status == "builtin":
                font = item.font()
                font.setItalic(True)
                item.setFont(font)

            self.attacks.addItem(item)

        self.attacks.blockSignals(False)

    def _current_id(self) -> str:
        item = self.attacks.currentItem()
        if item is None:
            return ""
        return str(
            item.data(Qt.ItemDataRole.UserRole) or ""
        )

    def _add(self) -> None:
        if self.workspace is None:
            return

        definition_id, ok = self._ask_id(
            "attack_new_title"
        )

        if not ok or not definition_id:
            return

        try:
            self.workspace.create_definition(
                "attacks",
                definition_id,
            )
        except ValueError as error:
            self.status_changed.emit(str(error))
            return

        self.changed.emit()

        self._open_editor(definition_id)

    def _edit_selected(self, *unused) -> None:
        del unused

        definition_id = self._current_id()

        if definition_id:
            self._open_editor(definition_id)

    def _delete_selected(self) -> None:
        if self.workspace is None:
            return

        definition_id = self._current_id()

        if not definition_id:
            return

        try:
            AttackAuthoringService(
                self.workspace
            ).delete(definition_id)
        except ValueError as error:
            self.status_changed.emit(str(error))
            return

        self.status_changed.emit(
            self.translate("attack_deleted").format(
                definition_id=definition_id,
            )
        )

        self.changed.emit()

    def _build_editor(
        self,
        definition_id: str,
    ) -> AttackDefinitionDialog:
        return AttackDefinitionDialog(
            self.workspace,
            self.asset_root,
            definition_id,
            self.translate,
            self,
        )

    def _open_editor(
        self,
        definition_id: str,
    ) -> None:
        dialog = self._build_editor(
            definition_id
        )

        result = dialog.exec()

        if (
            hasattr(dialog, "statusMessage")
            and getattr(dialog, "statusMessage", None)
        ):
            self.status_changed.emit(
                str(dialog.statusMessage)
            )

        if result:
            self.status_changed.emit(
                self.translate("attack_saved").format(
                    definition_id=definition_id,
                )
            )

            self.changed.emit()

    def _ask_id(self, title_key: str):
        from PySide6.QtWidgets import QInputDialog

        definition_id, ok = QInputDialog.getText(
            self,
            self.translate(title_key),
            self.translate("attack_id"),
        )

        return definition_id.strip(), ok
