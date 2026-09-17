from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PySide6.QtCore import QPoint, QPointF, QRect, QRectF, QSize, QSizeF, Qt, QTimer, Signal
from PySide6.QtGui import (
    QColor,
    QIcon,
    QImage,
    QPainter,
    QPen,
    QPixmap,
    QTransform,
)
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QMessageBox,
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
from ..services.projectile_authoring_service import (
    ProjectileAuthoringService,
)
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
    "playEffect": QColor(190, 130, 230),
}

DIRECTION_VECTORS = {
    "down": (0, 1),
    "up": (0, -1),
    "left": (-1, 0),
    "right": (1, 0),
}

# Mirrors gameplay::clockwiseQuarterTurns in
# src/game/gameplay/attack_definitions.cpp: up=0, right=1, down=2, left=3.
FACING_QUARTERS = {
    "up": 0,
    "right": 1,
    "down": 2,
    "left": 3,
}


def quarter_turns(canonical: str, facing: str) -> int:
    return (
        FACING_QUARTERS.get(facing, 0)
        - FACING_QUARTERS.get(canonical, 0)
        + 4
    ) % 4


def rotate_image_quarter_turns(
    image: QImage,
    turns: int,
) -> QImage:
    turns %= 4

    if turns == 0:
        return image

    return image.transformed(
        QTransform().rotate(90 * turns)
    )


def rotated_anchor(
    width: int,
    height: int,
    anchor: tuple[int, int],
    turns: int,
) -> tuple[int, int]:
    # Mirrors rotatedAnchor in src/game/game_presentation.cpp.
    if turns % 4 == 1:
        return (height - anchor[1], anchor[0])

    if turns % 4 == 2:
        return (
            width - anchor[0],
            height - anchor[1],
        )

    if turns % 4 == 3:
        return (anchor[1], width - anchor[0])

    return anchor


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

        self._projectile: tuple[QImage, int, int, int, int, bool] | None = (
            None
        )

    def set_projectile(
        self,
        image: QImage | None,
        anchor: tuple[int, int] | None,
        offset: tuple[int, int] | None,
        behind: bool = False,
    ) -> None:
        self._projectile = (
            (
                image,
                anchor[0],
                anchor[1],
                offset[0],
                offset[1],
                behind,
            )
            if (
                image is not None
                and not image.isNull()
                and anchor is not None
                and offset is not None
            )
            else None
        )

        self.update()

    def _draw_projectile(
        self,
        painter: QPainter,
        scale: int,
        anchor_x: int,
        anchor_y: int,
    ) -> None:
        projectile = self._projectile

        if projectile is None:
            return

        image, anchor_x_p, anchor_y_p, offset_x, offset_y, _behind = (
            projectile
        )

        top_left = QPointF(
            anchor_x + offset_x * scale - anchor_x_p * scale,
            anchor_y + offset_y * scale - anchor_y_p * scale,
        )

        painter.setRenderHint(
            QPainter.RenderHint.SmoothPixmapTransform,
            False,
        )

        painter.drawImage(
            QRectF(
                top_left,
                QSizeF(
                    image.width() * scale,
                    image.height() * scale,
                ),
            ),
            image,
        )

        painter.setRenderHint(
            QPainter.RenderHint.Antialiasing,
            True,
        )

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

        if (
            self._projectile is not None
            and self._projectile[5]
        ):
            self._draw_projectile(
                painter,
                scale,
                anchor_x,
                anchor_y,
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

        if (
            self._projectile is not None
            and not self._projectile[5]
        ):
            self._draw_projectile(
                painter,
                scale,
                anchor_x,
                anchor_y,
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
    event_moved = Signal(int, int)
    drag_finished = Signal()
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

        self._drag_event_row = -1

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

            self._drag_event_row = best_row

            self.setCursor(
                Qt.CursorShape.ClosedHandCursor
            )

            return

        tick = self._tick_at_x(click.x())

        if tick != self.current_tick:
            self.current_tick = tick

            self.update()

            self.tick_selected.emit(tick)

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        if not 0 <= self._drag_event_row < len(
            self.events
        ):
            return

        tick = self._tick_at_x(
            int(event.position().x())
        )

        row = self._drag_event_row

        current_tick, kind = self.events[row]

        if tick == current_tick:
            return

        self.events[row] = (tick, kind)

        self.current_tick = tick

        self.update()

        self.event_moved.emit(row, tick)

        self.tick_selected.emit(tick)

    def mouseReleaseEvent(self, event) -> None:  # noqa: N802
        if self._drag_event_row >= 0:
            self._drag_event_row = -1

            self.unsetCursor()

            self.drag_finished.emit()

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

class ProjectileSpawnCanvas(QWidget):
    """Draws one player frame with the draggable projectile spawn point."""

    offset_changed = Signal(int, int)

    def __init__(
        self,
        translator: Translator,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)

        self.translate = translator

        self.setMinimumSize(320, 260)

        self._frame: QImage | None = None

        self._frame_anchor = (24, 47)

        self._projectile: QImage | None = None

        self._projectile_anchor = (8, 8)

        self._projectile_behind = False

        self._offset = (0, 0)

        self._dragging = False

        self._grab_delta = QPointF()

        self.setCursor(
            Qt.CursorShape.PointingHandCursor
        )

    def set_frame(
        self,
        image: QImage | None,
        anchor: tuple[int, int],
    ) -> None:
        self._frame = (
            image
            if image is not None and not image.isNull()
            else None
        )

        self._frame_anchor = anchor

        self.update()

    def set_projectile(
        self,
        image: QImage | None,
        anchor: tuple[int, int],
        behind: bool = False,
    ) -> None:
        self._projectile = (
            image
            if image is not None and not image.isNull()
            else None
        )

        self._projectile_anchor = anchor

        self._projectile_behind = behind

        self.update()

    def set_offset(
        self,
        offset_x: int,
        offset_y: int,
    ) -> None:
        self._offset = (offset_x, offset_y)

        self.update()

    def _metrics(self) -> tuple[int, float, float]:
        frame = self._frame

        frame_width = (
            frame.width() if frame is not None else 48
        )

        frame_height = (
            frame.height() if frame is not None else 48
        )

        scale = max(
            1,
            min(
                (self.width() - 56) // max(1, frame_width),
                (self.height() - 56) // max(1, frame_height),
            ),
        )

        anchor_x = (
            (self.width() - frame_width * scale) // 2
            + self._frame_anchor[0] * scale
        )

        anchor_y = (
            (self.height() - frame_height * scale) // 2
            + self._frame_anchor[1] * scale
        )

        return scale, float(anchor_x), float(anchor_y)

    def _projectile_rect(
        self,
        scale: int,
        feet: QPointF,
    ) -> QRectF | None:
        projectile = self._projectile

        if projectile is None:
            return None

        top_left = QPointF(
            feet.x()
            + self._offset[0] * scale
            - self._projectile_anchor[0] * scale,
            feet.y()
            + self._offset[1] * scale
            - self._projectile_anchor[1] * scale,
        )

        return QRectF(
            top_left,
            QSizeF(
                projectile.width() * scale,
                projectile.height() * scale,
            ),
        )

    def paintEvent(self, event) -> None:  # noqa: N802
        del event

        painter = QPainter(self)

        painter.fillRect(
            self.rect(),
            QColor(22, 27, 34),
        )

        scale, anchor_x, anchor_y = self._metrics()

        feet = QPointF(anchor_x, anchor_y)

        if self._projectile_behind:
            self._draw_projectile_visual(
                painter,
                scale,
                feet,
            )

        frame = self._frame

        if frame is not None:
            painter.setRenderHint(
                QPainter.RenderHint.SmoothPixmapTransform,
                False,
            )

            painter.drawImage(
                QRect(
                    int(
                        anchor_x
                        - self._frame_anchor[0] * scale
                    ),
                    int(
                        anchor_y
                        - self._frame_anchor[1] * scale
                    ),
                    frame.width() * scale,
                    frame.height() * scale,
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
                self.translate("attack_preview_placeholder"),
            )

        # Feet crosshair.
        painter.setPen(
            QPen(QColor(120, 200, 140), 1)
        )

        painter.drawLine(
            feet.x() - 5 * scale,
            feet.y(),
            feet.x() + 5 * scale,
            feet.y(),
        )

        painter.drawLine(
            feet.x(),
            feet.y() - 5 * scale,
            feet.x(),
            feet.y() + 5 * scale,
        )

        if not self._projectile_behind:
            self._draw_projectile_visual(
                painter,
                scale,
                feet,
            )

    def _draw_projectile_visual(
        self,
        painter: QPainter,
        scale: int,
        feet: QPointF,
    ) -> None:
        rect = self._projectile_rect(scale, feet)

        if rect is None:
            return

        projectile = self._projectile

        assert projectile is not None

        painter.setRenderHint(
            QPainter.RenderHint.SmoothPixmapTransform,
            False,
        )

        painter.drawImage(rect, projectile)

        painter.setRenderHint(
            QPainter.RenderHint.Antialiasing,
            True,
        )

        painter.setPen(
            QPen(
                QColor(230, 180, 90),
                1,
                Qt.PenStyle.DashLine,
            )
        )

        painter.drawRect(rect.adjusted(-1, -1, 1, 1))

    def mousePressEvent(self, event) -> None:  # noqa: N802
        scale, anchor_x, anchor_y = self._metrics()

        rect = self._projectile_rect(
            scale,
            QPointF(anchor_x, anchor_y),
        )

        if (
            rect is not None
            and event.button()
            == Qt.MouseButton.LeftButton
            and rect.adjusted(-6, -6, 6, 6).contains(
                event.position()
            )
        ):
            self._dragging = True

            self._grab_delta = (
                event.position() - rect.topLeft()
            )

            event.accept()

    def mouseMoveEvent(self, event) -> None:  # noqa: N802
        if not self._dragging:
            return

        scale, anchor_x, anchor_y = self._metrics()

        top_left = event.position() - self._grab_delta

        offset_x = int(
            round(
                (
                    top_left.x()
                    - anchor_x
                    + self._projectile_anchor[0] * scale
                )
                / scale
            )
        )

        offset_y = int(
            round(
                (
                    top_left.y()
                    - anchor_y
                    + self._projectile_anchor[1] * scale
                )
                / scale
            )
        )

        offset_x = max(-999, min(999, offset_x))
        offset_y = max(-999, min(999, offset_y))

        if (offset_x, offset_y) != self._offset:
            self._offset = (offset_x, offset_y)

            self.offset_changed.emit(offset_x, offset_y)

            self.update()

        event.accept()

    def mouseReleaseEvent(self, event) -> None:  # noqa: N802
        if self._dragging and (
            event.button() == Qt.MouseButton.LeftButton
        ):
            self._dragging = False

            event.accept()


class ProjectileSpawnEditorDialog(QDialog):
    """Author projectile spawn offsets against the player's sheet poses."""

    def __init__(
        self,
        workspace: ContentWorkspace,
        translator: Translator,
        projectile_id: str,
        visual_resolver: AttackVisualResolver,
        attack_definition_id: str,
        attack_data: dict[str, object],
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)

        self.translate = translator

        self.workspace = workspace

        self.projectile_id = projectile_id

        self._resolver = visual_resolver

        self._attack_definition_id = attack_definition_id

        self._attack_data = dict(attack_data)

        service = ProjectileAuthoringService(workspace)

        data = service.definition_data(projectile_id)

        if data is None:
            raise ValueError(
                f"unknown projectile: {projectile_id}"
            )

        raw_offsets = data.get("spawnOffsets")

        raw_offsets = (
            raw_offsets
            if isinstance(raw_offsets, dict)
            else {}
        )

        self._offsets: dict[str, dict[str, int]] = {}

        for direction in DIRECTIONS:
            values = raw_offsets.get(direction)

            values = (
                values if isinstance(values, dict) else {}
            )

            self._offsets[direction] = {
                "x": int(values.get("x", 0) or 0),
                "y": int(values.get("y", 0) or 0),
            }

        visual = None

        for entry in service.entries():
            if (
                entry.definition_id == projectile_id
                and entry.visual_id
            ):
                visual = (
                    self._resolver.visuals
                    .resolve_static_sprite(entry.visual_id)
                )

                break

        self._projectile_visual = visual

        canonical = data.get("canonicalFacing")

        self._canonical_facing = (
            canonical
            if canonical in FACING_QUARTERS
            else "up"
        )

        self._render_layer = (
            data.get("renderLayer")
            if data.get("renderLayer") in ("actor", "world")
            else "actor"
        )

        self.setWindowTitle(
            self.translate("projectile_spawn_editor_title")
        )

        self.resize(540, 600)

        layout = QVBoxLayout(self)

        hint = QLabel(
            self.translate("projectile_spawn_offsets_info")
        )

        hint.setWordWrap(True)

        layout.addWidget(hint)

        directions_row = QHBoxLayout()

        self._direction_buttons: dict[str, QPushButton] = {}

        for direction in DIRECTIONS:
            button = QPushButton(direction.capitalize())

            button.setCheckable(True)

            button.clicked.connect(
                lambda _checked=False,
                target=direction: (
                    self._select_direction(target)
                )
            )

            self._direction_buttons[direction] = button

            directions_row.addWidget(button)

        layout.addLayout(directions_row)

        facing_row = QHBoxLayout()

        facing_row.addWidget(
            QLabel(
                self.translate("projectile_canonical_facing")
            )
        )

        self.canonical_facing = QComboBox()

        for facing in DIRECTIONS:
            self.canonical_facing.addItem(
                facing.capitalize(),
                facing,
            )

        self.canonical_facing.setCurrentIndex(
            self.canonical_facing.findData(
                self._canonical_facing
            )
        )

        self.canonical_facing.currentIndexChanged.connect(
            self._canonical_facing_changed
        )

        facing_row.addWidget(self.canonical_facing)

        facing_row.addStretch(1)

        layout.addLayout(facing_row)

        layer_row = QHBoxLayout()

        layer_row.addWidget(
            QLabel(
                self.translate("projectile_render_layer")
            )
        )

        self.render_layer = QComboBox()

        self.render_layer.addItem(
            self.translate("projectile_render_layer_actor"),
            "actor",
        )

        self.render_layer.addItem(
            self.translate("projectile_render_layer_world"),
            "world",
        )

        self.render_layer.setCurrentIndex(
            self.render_layer.findData(self._render_layer)
        )

        self.render_layer.currentIndexChanged.connect(
            self._render_layer_changed
        )

        layer_row.addWidget(self.render_layer)

        layer_row.addStretch(1)

        layout.addLayout(layer_row)

        self.canvas = ProjectileSpawnCanvas(translator)

        self.canvas.offset_changed.connect(
            self._offset_from_canvas
        )

        layout.addWidget(self.canvas, 1)

        offset_row = QHBoxLayout()

        offset_row.addWidget(
            QLabel("X")
        )

        self.offset_x = QSpinBox()

        self.offset_x.setRange(-999, 999)

        offset_row.addWidget(self.offset_x)

        offset_row.addWidget(
            QLabel("Y")
        )

        self.offset_y = QSpinBox()

        self.offset_y.setRange(-999, 999)

        offset_row.addWidget(self.offset_y)

        offset_row.addStretch(1)

        layout.addLayout(offset_row)

        self.offset_x.valueChanged.connect(
            self._offset_from_spins
        )

        self.offset_y.valueChanged.connect(
            self._offset_from_spins
        )

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel
        )

        buttons.accepted.connect(self._save)

        buttons.rejected.connect(self.reject)

        layout.addWidget(buttons)

        self._current_direction = ""

        self._select_direction("down")

    def _select_direction(self, direction: str) -> None:
        if direction == self._current_direction:
            return

        self._current_direction = direction

        for candidate, button in (
            self._direction_buttons.items()
        ):
            button.setChecked(candidate == direction)

        self._apply_canvas_projectile()

        _, frames = self._resolver.frames(
            self._attack_definition_id,
            {
                **self._attack_data,
                "visualActionId": str(
                    self._attack_data.get("visualActionId")
                    or ""
                ),
                "kind": "projectile",
            },
            direction,
        )

        frame = frames[0] if frames else None

        self.canvas.set_frame(
            frame.image if frame is not None else None,
            (
                (frame.anchor_x, frame.anchor_y)
                if frame is not None
                else (24, 47)
            ),
        )

        offsets = self._offsets[direction]

        self.offset_x.blockSignals(True)
        self.offset_y.blockSignals(True)

        self.offset_x.setValue(offsets["x"])
        self.offset_y.setValue(offsets["y"])

        self.offset_x.blockSignals(False)
        self.offset_y.blockSignals(False)

        self.canvas.set_offset(
            offsets["x"],
            offsets["y"],
        )

    def _render_layer_changed(self) -> None:
        layer = self.render_layer.currentData()

        if isinstance(layer, str) and layer:
            self._render_layer = layer

            self._apply_canvas_projectile()

    def _canonical_facing_changed(self) -> None:
        facing = self.canonical_facing.currentData()

        if isinstance(facing, str) and facing:
            self._canonical_facing = facing

            self._apply_canvas_projectile()

    def _apply_canvas_projectile(self) -> None:
        visual = self._projectile_visual

        if visual is None:
            self.canvas.set_projectile(None, (8, 8))

            return

        direction = self._current_direction or "down"

        turns = quarter_turns(
            self._canonical_facing,
            direction,
        )

        anchor = rotated_anchor(
            visual.image.width(),
            visual.image.height(),
            (visual.anchor_x, visual.anchor_y),
            turns,
        )

        self.canvas.set_projectile(
            rotate_image_quarter_turns(visual.image, turns),
            anchor,
            self._render_layer == "world",
        )

    def _offset_from_spins(self) -> None:
        direction = self._current_direction

        if not direction:
            return

        self._offsets[direction] = {
            "x": self.offset_x.value(),
            "y": self.offset_y.value(),
        }

        self.canvas.set_offset(
            self.offset_x.value(),
            self.offset_y.value(),
        )

    def _offset_from_canvas(
        self,
        offset_x: int,
        offset_y: int,
    ) -> None:
        direction = self._current_direction

        if not direction:
            return

        self._offsets[direction] = {
            "x": offset_x,
            "y": offset_y,
        }

        self.offset_x.blockSignals(True)
        self.offset_y.blockSignals(True)

        self.offset_x.setValue(offset_x)
        self.offset_y.setValue(offset_y)

        self.offset_x.blockSignals(False)
        self.offset_y.blockSignals(False)

    def _save(self) -> None:
        try:
            ProjectileAuthoringService(
                self.workspace
            ).update_spawn_offsets(
                self.projectile_id,
                self._offsets,
                self._canonical_facing,
                self._render_layer,
            )
        except ValueError as error:
            QMessageBox.critical(
                self,
                self.translate("projectile_new_title"),
                str(error),
            )

            return

        self.accept()


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

        self.projectile_new = QPushButton(
            self.translate("projectile_new_from_animation")
        )

        self.projectile_edit = QPushButton(
            self.translate("projectile_edit_positions")
        )

        projectile_editor = QWidget(self)

        projectile_layout = QHBoxLayout(projectile_editor)

        projectile_layout.setContentsMargins(0, 0, 0, 0)

        projectile_layout.setSpacing(6)

        projectile_layout.addWidget(
            self.projectile,
            1,
        )

        projectile_layout.addWidget(
            self.projectile_new
        )

        projectile_layout.addWidget(
            self.projectile_edit
        )

        self.projectile_preview = QLabel()

        self.projectile_preview.setMinimumSize(
            64,
            64,
        )

        self.projectile_preview.setAlignment(
            Qt.AlignmentFlag.AlignCenter
        )

        projectile_layout.addWidget(
            self.projectile_preview
        )

        form.addRow(
            self.translate("attack_projectile"),
            self._with_info(
                projectile_editor,
                "attack_projectile_info",
            ),
        )

        self._refresh_projectiles()

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

        self.event_animation = QComboBox()

        self.event_animation.setEnabled(False)

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
            QLabel(
                self.translate("attack_event_animation")
            )
        )

        event_row.addWidget(
            self.event_animation
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

        self.projectile_new.clicked.connect(
            self._create_projectile
        )

        self.projectile.currentIndexChanged.connect(
            self._update_projectile_preview
        )

        self.projectile_edit.clicked.connect(
            self._edit_projectile
        )

        self.event_add.clicked.connect(
            self._add_event
        )

        self.event_animation.currentIndexChanged.connect(
            self._event_animation_changed
        )

        self.event_kind.currentIndexChanged.connect(
            self._event_kind_changed
        )

        self.event_remove.clicked.connect(
            self._remove_event
        )

        self.timeline.event_selected.connect(
            self._select_event
        )

        self.timeline.event_moved.connect(
            self._move_event
        )

        self.timeline.drag_finished.connect(
            self._finish_event_drag
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

    def _refresh_projectiles(
        self,
        selected_id: str | None = None,
    ) -> None:
        entries = ProjectileAuthoringService(
            self.workspace
        ).entries()

        if selected_id is None:
            selected_id = str(
                self.data.get("projectileDefinitionId") or ""
            )

        self.projectile.blockSignals(True)
        self.projectile.clear()

        for entry in entries:
            self.projectile.addItem(
                entry.definition_id,
                entry.definition_id,
            )

            icon = self._projectile_icon(entry.visual_id)

            if icon is not None:
                row_index = self.projectile.count() - 1

                self.projectile.setItemData(
                    row_index,
                    icon,
                    Qt.ItemDataRole.DecorationRole,
                )

        self.projectile.blockSignals(False)

        index = self.projectile.findData(selected_id)

        self.projectile.setCurrentIndex(index)

        self._projectile_data: dict[str, object] | None = None

        self._update_projectile_preview()

    def _projectile_icon(
        self,
        visual_id: str,
    ) -> QPixmap | None:
        if not visual_id:
            return None

        visual = (
            self._visual_resolver.visuals
            .resolve_static_sprite(visual_id)
        )

        if visual is None:
            return None

        return QPixmap.fromImage(
            visual.image
        ).scaled(
            24,
            24,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation,
        )

    def _update_projectile_preview(self) -> None:
        service = ProjectileAuthoringService(
            self.workspace
        )

        definition_id = self.projectile.currentData()
        visual_id = ""

        if isinstance(definition_id, str) and definition_id:
            for entry in service.entries():
                if entry.definition_id == definition_id:
                    visual_id = entry.visual_id

                    break

        data = (
            service.definition_data(definition_id)
            if isinstance(definition_id, str) and definition_id
            else None
        )

        self._projectile_data = data

        visual = (
            self._visual_resolver.visuals
            .resolve_static_sprite(visual_id)
            if visual_id
            else None
        )

        if visual is None:
            self.projectile_preview.setPixmap(
                QPixmap()
            )

            return

        pixmap = QPixmap.fromImage(visual.image)

        self.projectile_preview.setPixmap(
            pixmap.scaled(
                64,
                64,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.FastTransformation,
            )
        )

    def _create_projectile(self) -> None:
        service = ProjectileAuthoringService(self.workspace)

        animations = service.animation_ids()

        if not animations:
            QMessageBox.warning(
                self,
                self.translate("projectile_new_title"),
                self.translate("projectile_no_animations"),
            )

            return

        suggested_id = "projectile.arrow"

        definition_id, confirmed_id = QInputDialog.getText(
            self,
            self.translate("projectile_new_title"),
            self.translate("projectile_new_id"),
            text=suggested_id,
        )

        if not confirmed_id:
            return

        default_index = next(
            (
                index
                for index, animation_id in enumerate(animations)
                if "arrow" in animation_id.casefold()
            ),
            0,
        )

        animation_id = self._choose_animation(
            animations,
            default_index,
        )

        if animation_id is None:
            return

        try:
            projectile_id = service.create_from_animation(
                definition_id,
                str(animation_id),
            )
        except ValueError as error:
            QMessageBox.critical(
                self,
                self.translate("projectile_new_title"),
                str(error),
            )

            return

        self._refresh_projectiles(projectile_id)

    def _edit_projectile(self) -> None:
        projectile_id = self.projectile.currentData()

        if not isinstance(projectile_id, str) or not projectile_id:
            return

        dialog = ProjectileSpawnEditorDialog(
            self.workspace,
            self.translate,
            projectile_id,
            self._visual_resolver,
            self.definition_id,
            self.result_data(),
            self,
        )

        if dialog.exec() == QDialog.DialogCode.Accepted:
            self._update_projectile_preview()

            self._update_preview()

    def _choose_animation(
        self,
        animations: list[str],
        default_index: int,
    ) -> str | None:
        dialog = QDialog(self)

        dialog.setWindowTitle(
            self.translate("projectile_new_title")
        )

        layout = QVBoxLayout(dialog)

        label = QLabel(
            self.translate("projectile_new_animation")
        )

        layout.addWidget(label)

        listing = QListWidget(dialog)

        listing.setIconSize(
            QSize(32, 32)
        )

        for animation_id in animations:
            item = QListWidgetItem(animation_id)

            visual = (
                self._visual_resolver.visuals
                .resolve_animation_frame(animation_id, 0)
            )

            if visual is not None:
                item.setIcon(
                    QIcon(
                        QPixmap.fromImage(
                            visual.image
                        ).scaled(
                            32,
                            32,
                            Qt.AspectRatioMode.KeepAspectRatio,
                            Qt.TransformationMode.FastTransformation,
                        )
                    )
                )

            listing.addItem(item)

        listing.setCurrentRow(default_index)

        listing.itemDoubleClicked.connect(
            lambda _item: dialog.accept()
        )

        layout.addWidget(listing)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel
        )

        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)

        layout.addWidget(buttons)

        if dialog.exec() != QDialog.DialogCode.Accepted:
            return None

        current = listing.currentItem()

        return current.text() if current is not None else None

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
                str(event.get("animationId", "") or ""),
                int(event.get("offsetX", 0) or 0),
                int(event.get("offsetY", 0) or 0),
            )

        self._sync_enabled()

        self._refresh_visual()

    def _append_event(
        self,
        tick: int,
        kind: str,
        animation_id: str = "",
        offset_x: int = 0,
        offset_y: int = 0,
    ) -> None:
        self._events.append(
            (int(tick), str(kind), str(animation_id), int(offset_x), int(offset_y))
        )

        self._events.sort(key=lambda event: event[0])

        self._sync_timeline()

    def _add_event(self) -> None:
        tick = self.event_tick.value()

        kind = str(self.event_kind.currentData())

        animation_id = (
            str(self.event_animation.currentData() or "")
            if kind == "playEffect"
            else ""
        )

        self._append_event(
            tick,
            kind,
            animation_id,
        )

        self._selected_event = self._events.index(
            (tick, kind, animation_id, 0, 0)
        )

        self._sync_timeline()

    def _remove_event(self) -> None:
        if 0 <= self._selected_event < len(self._events):
            del self._events[self._selected_event]

            self._selected_event = -1

            self._sync_timeline()

            self._update_preview()

    def _event_kind_changed(self) -> None:
        if 0 <= self._selected_event < len(self._events):
            self._events[self._selected_event] = (
                self._events[self._selected_event][0],
                str(self.event_kind.currentData() or ""),
                self._events[self._selected_event][2],
                self._events[self._selected_event][3],
                self._events[self._selected_event][4],
            )

        self._sync_event_animation(
            str(self.event_kind.currentData() or ""),
            str(self.event_animation.currentData() or ""),
        )

        self._sync_timeline()

    def _move_event(
        self,
        row: int,
        tick: int,
    ) -> None:
        if not 0 <= row < len(self._events):
            return

        current_tick, kind, animation_id, offset_x, offset_y = (
            self._events[row]
        )

        self._events[row] = (
            int(tick), kind, animation_id, offset_x, offset_y,
        )

        self._selected_event = row

        self.event_tick.setValue(int(tick))

    def _finish_event_drag(self) -> None:
        if not 0 <= self._selected_event < len(
            self._events
        ):
            return

        moved = self._events[self._selected_event]

        self._events.sort(key=lambda event: event[0])

        self._selected_event = self._events.index(
            moved
        )

        self._sync_timeline()

    def _refresh_event_animations(self) -> None:
        self.event_animation.blockSignals(True)

        self.event_animation.clear()

        if self.workspace is not None:
            for definition in sorted(
                self.workspace.definitions("animations"),
                key=lambda item: item.definition_id,
            ):
                self.event_animation.addItem(
                    definition.definition_id,
                    definition.definition_id,
                )

        self.event_animation.blockSignals(False)

    def _sync_event_animation(
        self,
        kind: str,
        animation_id: str,
    ) -> None:
        is_effect = kind == "playEffect"

        self.event_animation.setEnabled(is_effect)

        if not is_effect:
            return

        index = self.event_animation.findData(animation_id)

        self.event_animation.blockSignals(True)

        self.event_animation.setCurrentIndex(
            max(0, index)
        )

        self.event_animation.blockSignals(False)

    def _event_animation_changed(self) -> None:
        if not 0 <= self._selected_event < len(
            self._events
        ):
            return

        tick, kind, _old, offset_x, offset_y = self._events[
            self._selected_event
        ]

        if kind != "playEffect":
            return

        animation_id = str(
            self.event_animation.currentData() or ""
        )

        self._events[self._selected_event] = (
            tick, kind, animation_id, offset_x, offset_y,
        )

    def _select_event(
        self,
        row: int,
    ) -> None:
        if not 0 <= row < len(self._events):
            return

        self._selected_event = row

        self._sync_timeline()

        tick, kind, animation_id, _offset_x, _offset_y = self._events[row]

        self.event_tick.setValue(tick)

        self._sync_event_animation(kind, animation_id)

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

        if matches:
            frame = matches[-1]

            self.preview.set_frame(
                frame.image,
                (frame.anchor_x, frame.anchor_y),
            )
        elif self._visual_frames:
            frame = self._visual_frames[-1]

            self.preview.set_frame(
                frame.image,
                (frame.anchor_x, frame.anchor_y),
            )
        else:
            self.preview.set_frame(
                None,
                (0, 0),
            )

        self.preview.set_overlay(
            str(self.facing.currentData() or "down"),
            self._active_hitbox(),
            self.minimum_range.value(),
            self.maximum_range.value(),
            self.knockback.value(),
            self.damage_amount.value(),
        )

        self.preview.set_projectile(
            *self._projectile_preview_visual(
                str(self.facing.currentData() or "down")
            )
        )

    def _projectile_preview_visual(
        self,
        facing: str,
    ) -> tuple[
        QImage | None,
        tuple[int, int] | None,
        tuple[int, int] | None,
        bool,
    ]:
        if self.kind.currentData() != "projectile":
            return None, None, None, False

        definition_id = self.projectile.currentData()

        if not isinstance(definition_id, str) or not definition_id:
            return None, None, None, False

        service = ProjectileAuthoringService(
            self.workspace
        )

        entry = next(
            (
                entry
                for entry in service.entries()
                if entry.definition_id == definition_id
            ),
            None,
        )

        if entry is None or not entry.visual_id:
            return None, None, None, False

        visual = (
            self._visual_resolver.visuals
            .resolve_static_sprite(entry.visual_id)
        )

        if visual is None:
            return None, None, None, False

        data = self._projectile_data

        data = data if isinstance(data, dict) else {}

        offsets = data.get("spawnOffsets")

        offsets = offsets if isinstance(offsets, dict) else {}

        values = offsets.get(facing)

        values = values if isinstance(values, dict) else {}

        offset = (
            int(values.get("x", 0) or 0),
            int(values.get("y", 0) or 0),
        )

        canonical = data.get("canonicalFacing")

        canonical = (
            canonical if canonical in FACING_QUARTERS else "up"
        )

        turns = quarter_turns(canonical, facing)

        anchor = rotated_anchor(
            visual.image.width(),
            visual.image.height(),
            (visual.anchor_x, visual.anchor_y),
            turns,
        )

        behind = (
            data.get("renderLayer") == "world"
        )

        # Flight simulation: the projectile exists only from its
        # spawnProjectile tick, travels along the facing at the
        # definition's speed, and disappears after its lifetime.
        spawn_tick = min(
            (
                event[0]
                for event in self._events
                if len(event) > 1
                and event[1] == "spawnProjectile"
            ),
            default=None,
        )

        if spawn_tick is not None:
            if self._selected_tick < spawn_tick:
                return None, None, None, False

            elapsed = self._selected_tick - spawn_tick

            lifetime = int(
                data.get("lifetimeTicks", 0) or 0
            )

            if lifetime > 0 and elapsed >= lifetime:
                return None, None, None, False

            speed = int(
                data.get("speedPixelsPerTick", 0) or 0
            )

            direction = DIRECTION_VECTORS.get(
                facing,
                (0, 1),
            )

            offset = (
                offset[0] + direction[0] * speed * elapsed,
                offset[1] + direction[1] * speed * elapsed,
            )

        return (
            rotate_image_quarter_turns(visual.image, turns),
            anchor,
            offset,
            behind,
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
            [(tick, kind) for tick, kind, *_rest in self._events],
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

        self.projectile_edit.setEnabled(
            not melee
            and self.projectile.currentData() is not None
        )

        self._refresh_event_animations()

        if 0 <= self._selected_event < len(self._events):
            kind = self._events[self._selected_event][1]

            self._sync_event_animation(
                kind,
                self._events[self._selected_event][2],
            )
        else:
            self._sync_event_animation(
                str(self.event_kind.currentData() or ""),
                "",
            )

        self._update_preview()

    def _collect_timeline(
        self,
    ) -> list[dict[str, object]]:
        events: list[dict[str, object]] = []

        for tick, kind, animation_id, offset_x, offset_y in self._events:

            if kind == "playEffect":
                events.append(
                    {
                        "tick": tick,
                        "kind": kind,
                        "animationId": animation_id,
                        "offsetX": offset_x,
                        "offsetY": offset_y,
                    }
                )

                continue

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
