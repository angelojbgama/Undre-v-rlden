"""First-class Enemy authoring UI for the Content Studio."""

from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from pathlib import Path

from PySide6.QtGui import QPixmap

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.enemy_authoring_service import EnemyAuthoringService
from ..services.localization import Translator
from .icon_registry import icon
from .studio_visual_resolver import StudioVisualResolver
from .widgets import PayloadListWidget


class FootprintRow(QWidget):
    """Four spin boxes editing one offsetX/offsetY/width/height footprint."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.offset_x = QSpinBox(self)
        self.offset_y = QSpinBox(self)
        self.width = QSpinBox(self)
        self.height = QSpinBox(self)
        for spin in (self.offset_x, self.offset_y):
            spin.setRange(-512, 512)
        self.width.setRange(1, 512)
        self.height.setRange(1, 512)
        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        for spin in (self.offset_x, self.offset_y, self.width, self.height):
            row.addWidget(spin)

    def value(self) -> dict[str, int]:
        return {
            "offsetX": self.offset_x.value(),
            "offsetY": self.offset_y.value(),
            "width": self.width.value(),
            "height": self.height.value(),
        }

    def load(self, box: object) -> None:
        data = box if isinstance(box, dict) else {}
        self.offset_x.setValue(int(data.get("offsetX", 0) or 0))
        self.offset_y.setValue(int(data.get("offsetY", 0) or 0))
        self.width.setValue(int(data.get("width", 10) or 10))
        self.height.setValue(int(data.get("height", 8) or 8))


class EnemyEditorDialog(QDialog):
    """Create or edit one authored enemy definition."""

    def __init__(
        self,
        workspace: ContentWorkspace,
        translator: Translator,
        definition: ContentDefinition | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.translate = translator
        self.definition = definition
        self.service = EnemyAuthoringService(workspace)

        self.enemy_id = QLineEdit(self)
        self.enemy_id.setPlaceholderText("enemy.new_enemy")

        self.visual_set = QComboBox(self)
        self.behavior = QComboBox(self)
        self.reward_profile = QComboBox(self)
        for combo, entries in (
            (self.visual_set, self.service.visual_sets()),
            (self.behavior, self.service.behaviors()),
            (self.reward_profile, self.service.reward_profiles()),
        ):
            combo.addItem("—", None)
            for entry in entries:
                combo.addItem(entry.definition_id, entry.definition_id)

        self.health = QSpinBox(self)
        self.health.setRange(1, 100000)
        self.health.setValue(3)
        self.speed = QSpinBox(self)
        self.speed.setRange(1, 100000)
        self.speed.setValue(96)

        self.collision_body = FootprintRow(self)
        self.hurtbox = FootprintRow(self)

        self.attacks = QListWidget(self)
        self.attacks.setSelectionMode(QListWidget.SelectionMode.MultiSelection)
        for attack in self.service.attacks():
            item = QListWidgetItem(attack.definition_id, self.attacks)
            item.setData(Qt.ItemDataRole.UserRole, attack.definition_id)

        form = QFormLayout()
        form.addRow(self.translate("enemy_field_id"), self.enemy_id)
        form.addRow(self.translate("enemy_field_visual"), self.visual_set)
        form.addRow(self.translate("enemy_field_behavior"), self.behavior)
        form.addRow(self.translate("enemy_field_health"), self.health)
        form.addRow(self.translate("enemy_field_speed"), self.speed)
        form.addRow(self.translate("enemy_field_collision"), self.collision_body)
        form.addRow(self.translate("enemy_field_hurtbox"), self.hurtbox)
        form.addRow(self.translate("enemy_field_reward"), self.reward_profile)

        attacks_group = QGroupBox(self.translate("enemy_field_attacks"), self)
        attacks_layout = QVBoxLayout(attacks_group)
        attacks_layout.addWidget(self.attacks)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(attacks_group)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel,
            self,
        )
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

        if definition is None:
            self.setWindowTitle(self.translate("enemy_create"))
        else:
            self.setWindowTitle(self.translate("enemy_configure"))
            self._load(definition)
        self._adjust_id_placeholder()

    def _adjust_id_placeholder(self) -> None:
        if self.definition is None:
            self.enemy_id.setText(self.enemy_id.text().strip() or "enemy.new_enemy")
            self.enemy_id.setEnabled(True)
        else:
            self.enemy_id.setText(self.definition.definition_id)
            self.enemy_id.setEnabled(False)

    def _combo_select(self, combo: QComboBox, value: object) -> None:
        index = combo.findData(value)
        combo.setCurrentIndex(index if index >= 0 else 0)

    def _load(self, definition: ContentDefinition) -> None:
        data = definition.data
        self._combo_select(self.visual_set, data.get("visualSetId"))
        self._combo_select(self.behavior, data.get("behaviorProfileId"))
        self._combo_select(self.reward_profile, data.get("rewardProfileId"))
        self.health.setValue(int(data.get("maximumHealth", 3) or 3))
        self.speed.setValue(int(data.get("movementSpeedSubpixelsPerTick", 96) or 96))
        self.collision_body.load(data.get("collisionBody"))
        self.hurtbox.load(data.get("hurtbox"))
        attack_ids = data.get("attackIds", [])
        for index in range(self.attacks.count()):
            item = self.attacks.item(index)
            item.setSelected(item.data(Qt.ItemDataRole.UserRole) in attack_ids)

    def _selected_attacks(self) -> list[str]:
        return [item.data(Qt.ItemDataRole.UserRole)
                for item in self.attacks.selectedItems()]

    def _collect(self) -> dict[str, object]:
        reward = self.reward_profile.currentData()
        return {
            "id": self.enemy_id.text().strip(),
            "visualSetId": self.visual_set.currentData(),
            "behaviorProfileId": self.behavior.currentData(),
            "faction": "enemy",
            "maximumHealth": self.health.value(),
            "movementSpeedSubpixelsPerTick": self.speed.value(),
            "collisionBody": self.collision_body.value(),
            "hurtbox": self.hurtbox.value(),
            "attackIds": self._selected_attacks(),
            "rewardProfileId": reward if isinstance(reward, str) else None,
        }

    def _save(self) -> None:
        try:
            if self.definition is None:
                self.service.create_enemy(self.enemy_id.text().strip(), self._collect())
            else:
                self.service.configure(self.definition.definition_id, self._collect())
        except ValueError as error:
            QMessageBox.warning(self, "Enemy", str(error))
            return
        self.accept()


class EnemySpritePreview(QWidget):
    """Live sprite strips for the selected enemy's visual set.

    Renders idle / hurt / death thumbnails (first frames, down facing) plus
    a diagnostics list from ``EnemyAuthoringService.verify_visual`` so the
    author can confirm the art is present, sliced and anchored coherently
    before placing the enemy.
    """

    _CLIP_ORDER = ("idle", "hurt", "death", "move")

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None,
        translator: Translator,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.translate = translator
        self.resolver = StudioVisualResolver()
        self.resolver.set_context(workspace, asset_root)
        self.current_enemy_id: str | None = None

        from PySide6.QtWidgets import QLabel

        self.strip_labels: dict[str, QLabel] = {}
        self.diagnostics = QLabel(self)
        self.diagnostics.setWordWrap(True)
        self.diagnostics.setTextFormat(Qt.TextFormat.RichText)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        for clip in self._CLIP_ORDER:
            label = QLabel(self)
            label.setMinimumHeight(40)
            label.setProperty("muted", True)
            self.strip_labels[clip] = label
            layout.addWidget(label)
        layout.addWidget(self.diagnostics, 1)
        self.clear()

    def set_context(
        self, workspace: ContentWorkspace | None, asset_root: Path | None,
    ) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.resolver.set_context(workspace, asset_root)
        self.clear()

    def clear(self) -> None:
        self.current_enemy_id = None
        for label in self.strip_labels.values():
            label.clear()
        self.diagnostics.setText("")

    def show_enemy(
        self, enemy_id: str, visual_set_id: object,
        diagnostics: list[dict[str, str]] | None = None,
    ) -> None:
        self.current_enemy_id = enemy_id
        visual_set = (self.workspace.find("enemyVisuals", str(visual_set_id))
                      if self.workspace is not None and isinstance(visual_set_id, str)
                      else None)
        for clip in self._CLIP_ORDER:
            label = self.strip_labels[clip]
            strip = self._render_clip(visual_set, clip)
            if strip is None:
                label.clear()
            else:
                label.setPixmap(strip)
        if diagnostics is None:
            diagnostics = (self.service_diagnostics(enemy_id)
                           if hasattr(self, "service_diagnostics") else [])
        self._show_diagnostics(diagnostics)

    def service_diagnostics(self, enemy_id: str) -> list[dict[str, str]]:
        return []

    def _render_clip(self, visual_set, clip: str):
        if visual_set is None or self.workspace is None:
            return None
        clip_data = visual_set.data.get(clip)
        if not isinstance(clip_data, dict):
            return None
        animation_id = clip_data.get("down")
        if not isinstance(animation_id, str) or not animation_id:
            return None
        animation = self.workspace.find("animations", animation_id)
        if animation is None:
            return None
        frames = animation.data.get("frames")
        if not isinstance(frames, list) or not frames:
            return None

        from PySide6.QtGui import QPainter
        from PySide6.QtCore import Qt as QtCore

        thumbnails = QPixmap()
        painter: QPainter | None = None

        cell = 32
        count = min(len(frames), 6)
        try:
            return self._compose_strip(animation_id, frames, count, cell)
        except Exception:
            if painter is not None:
                painter.end()
            return None

    def _compose_strip(self, animation_id, frames, count, cell):
        from PySide6.QtGui import QPainter
        from PySide6.QtCore import Qt as QtCore

        thumbnails = QPixmap()
        painter: QPainter | None = None
        for index in range(count):
            resolved = self.resolver.resolve_animation_frame(animation_id, index)
            if resolved is None or resolved.image.isNull():
                return None
            source = frames[index].get("source", {}) if isinstance(frames[index], dict) else {}
            width = int(source.get("width", resolved.image.width()) or resolved.image.width())
            height = int(source.get("height", resolved.image.height()) or resolved.image.height())
            if thumbnails.isNull():
                thumbnails = QPixmap(cell * count, cell)
                thumbnails.fill(QtCore.transparent)
                painter = QPainter(thumbnails)
            source = frames[index].get("source")
            if isinstance(source, dict):
                frame_image = resolved.image.copy(
                    int(source.get("x", 0)), int(source.get("y", 0)),
                    max(1, int(source.get("width", 1))),
                    max(1, int(source.get("height", 1))))
            else:
                frame_image = resolved.image
            scaled = frame_image.scaled(
                cell - 4, cell - 4,
                QtCore.AspectRatioMode.KeepAspectRatio,
                QtCore.TransformationMode.FastTransformation)
            if painter is not None:
                painter.drawPixmap(index * cell + 2, 2, QPixmap.fromImage(scaled))
        if painter is not None:
            painter.end()
            painter = None
        return thumbnails

    def _show_diagnostics(self, diagnostics: list[dict[str, str]]) -> None:
        if not diagnostics:
            self.diagnostics.setText(
                f"<span style='color:#7ee787'>✓ {self.translate('enemy_visual_ok')}</span>")
            return
        rows = []
        for diagnostic in diagnostics[:12]:
            color = "#f85149" if diagnostic["severity"] == "error" else "#d29922"
            icon_sign = "✗" if diagnostic["severity"] == "error" else "⚠"
            rows.append(
                f"<span style='color:{color}'>{icon_sign} "
                f"{diagnostic['message']} <i>({diagnostic['path']})</i></span>")
        self.diagnostics.setText("<br>".join(rows))


class EnemyLibraryWidget(QWidget):
    """Search, author and inspect enemy definitions."""

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
        self.workspace = workspace
        self.asset_root: Path | None = asset_root
        self.translate = translator or Translator()
        self.service = EnemyAuthoringService(workspace)

        self.search = QLineEdit(self)
        self.search.setPlaceholderText(self.translate("enemy_search"))
        self.search.textChanged.connect(self.refresh)

        self.enemies_list = PayloadListWidget(self)
        self.enemies_list.currentItemChanged.connect(self._selection_changed)

        self.create_button = QPushButton(self)
        self.create_button.setIcon(icon("add"))
        self.create_button.setToolTip(self.translate("enemy_create"))
        self.create_button.clicked.connect(self.create_enemy)

        self.configure_button = QPushButton(self)
        self.configure_button.setIcon(icon("configure"))
        self.configure_button.setToolTip(self.translate("enemy_configure"))
        self.configure_button.clicked.connect(self.configure_current)

        self.delete_button = QPushButton(self)
        self.delete_button.setIcon(icon("delete"))
        self.delete_button.setToolTip(self.translate("enemy_delete"))
        self.delete_button.clicked.connect(self.delete_current)

        button_row = QHBoxLayout()
        button_row.addWidget(self.create_button)
        button_row.addWidget(self.configure_button)
        button_row.addWidget(self.delete_button)
        button_row.addStretch(1)

        self.details = QLabel(self.translate("enemy_none"), self)
        self.details.setWordWrap(True)
        self.details.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.details.setMinimumSize(220, 220)
        self.details.setStyleSheet(
            "background:#161b22;"
            "color:#aeb8c4;"
            "border:1px solid #34404d;"
        )

        list_panel = QWidget(self)
        list_layout = QVBoxLayout(list_panel)
        list_layout.setContentsMargins(0, 0, 0, 0)
        list_layout.addWidget(self.search)
        list_layout.addWidget(self.enemies_list, 1)
        list_layout.addLayout(button_row)

        self.preview = EnemySpritePreview(workspace, self.asset_root, self.translate)

        right_panel = QWidget(self)
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.addWidget(self.details)
        right_layout.addWidget(self.preview)

        splitter = QSplitter(Qt.Orientation.Horizontal, self)
        splitter.addWidget(list_panel)
        splitter.addWidget(right_panel)
        splitter.setStretchFactor(0, 1)

        layout = QVBoxLayout(self)
        layout.addWidget(splitter)

        self.refresh()

    # -- context -----------------------------------------------------------

    def set_context(
        self, workspace: ContentWorkspace | None,
        asset_root: Path | None = None,
    ) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.service.set_context(workspace)
        self.preview.set_context(workspace, asset_root)
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.preview.translate = translator
        self.search.setPlaceholderText(self.translate("enemy_search"))
        self.create_button.setToolTip(self.translate("enemy_create"))
        self.configure_button.setToolTip(self.translate("enemy_configure"))
        self.delete_button.setToolTip(self.translate("enemy_delete"))
        self.refresh()

    # -- population --------------------------------------------------------

    def refresh(self) -> None:
        # Art may have changed on disk since the last pass; drop cached
        # source images so previews reflect the current files.
        self.preview.resolver.invalidate()
        self.enemies_list.blockSignals(True)
        self.enemies_list.clear()
        query = self.search.text() if hasattr(self, "search") else ""
        for enemy in self.service.enemies(query):
            behavior = enemy.data.get("behaviorProfileId", "")
            label = enemy.definition_id
            if isinstance(behavior, str) and behavior:
                label += f"  [{behavior}]"
            item = QListWidgetItem(label, self.enemies_list)
            item.setData(Qt.ItemDataRole.UserRole, enemy)
        self.enemies_list.blockSignals(False)
        if self.enemies_list.count() == 0:
            self.details.setText(self.translate("enemy_none"))

    def _selection_changed(self, current: QListWidgetItem | None, _previous=None) -> None:
        definition = current.data(Qt.ItemDataRole.UserRole) if current else None
        if definition is None:
            self.details.setText(self.translate("enemy_none"))
            self.preview.clear()
            return
        self._show_details(definition)
        self._refresh_preview(definition)

    def _refresh_preview(self, definition: ContentDefinition) -> None:
        diagnostics = self.service.verify_visual(
            definition.definition_id, self.asset_root)
        self.preview.show_enemy(
            definition.definition_id,
            definition.data.get("visualSetId"),
            diagnostics,
        )
        errors = [d for d in diagnostics if d["severity"] == "error"]
        if errors:
            self.status_changed.emit(
                self.translate("enemy_visual_problems").format(count=len(errors)))

    def _show_details(self, definition: ContentDefinition) -> None:
        data = definition.data
        lines = [
            f"<b>{self.translate('enemy_field_visual')}</b>: "
            f"{data.get('visualSetId', '')}",
            f"<b>{self.translate('enemy_field_behavior')}</b>: "
            f"{data.get('behaviorProfileId', '')}",
            f"<b>{self.translate('enemy_field_health')}</b>: "
            f"{data.get('maximumHealth', 0)}",
            f"<b>{self.translate('enemy_field_speed')}</b>: "
            f"{data.get('movementSpeedSubpixelsPerTick', 0)}",
        ]
        attacks = data.get("attackIds", [])
        if isinstance(attacks, list):
            lines.append(
                f"<b>{self.translate('enemy_field_attacks')}</b>: "
                + ", ".join(str(attack) for attack in attacks))
        reward = data.get("rewardProfileId")
        if isinstance(reward, str) and reward:
            lines.append(
                f"<b>{self.translate('enemy_field_reward')}</b>: {reward}")
        placed_in = self.service.placements(definition.definition_id)
        if placed_in:
            lines.append(
                f"<br><b>{self.translate('enemy_placed_in')}</b><br>"
                + "<br>".join(placed_in))
        text = f"<b>{definition.definition_id}</b><br><br>" + "<br>".join(lines)
        self.details.setText(text)

    # -- CRUD ---------------------------------------------------------------

    def create_enemy(self) -> None:
        if self.workspace is None:
            return
        dialog = EnemyEditorDialog(self.workspace, self.translate, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("enemy_created"))
            self.refresh()

    def configure_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        dialog = EnemyEditorDialog(
            self.workspace, self.translate, definition=definition, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("enemy_configured"))
            self.refresh()

    def delete_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        confirm = QMessageBox.question(
            self,
            self.translate("enemy_delete"),
            self.translate("enemy_delete_confirm").format(
                enemy=definition.definition_id),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if confirm != QMessageBox.StandardButton.Yes:
            return
        try:
            self.service.delete(definition.definition_id)
        except ValueError as error:
            QMessageBox.warning(self, self.translate("enemy_delete"), str(error))
            return
        self.changed.emit()
        self.status_changed.emit(self.translate("enemy_deleted"))
        self.refresh()

    def _current_definition(self) -> ContentDefinition | None:
        current = self.enemies_list.currentItem()
        return current.data(Qt.ItemDataRole.UserRole) if current else None
