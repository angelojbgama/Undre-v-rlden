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

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.enemy_authoring_service import EnemyAuthoringService
from ..services.localization import Translator
from .icon_registry import icon
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


class EnemyLibraryWidget(QWidget):
    """Search, author and inspect enemy definitions."""

    changed = Signal()
    status_changed = Signal(str)

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        translator: Translator | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
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

        splitter = QSplitter(Qt.Orientation.Horizontal, self)
        splitter.addWidget(list_panel)
        splitter.addWidget(self.details)
        splitter.setStretchFactor(0, 1)

        layout = QVBoxLayout(self)
        layout.addWidget(splitter)

        self.refresh()

    # -- context -----------------------------------------------------------

    def set_context(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace
        self.service.set_context(workspace)
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.search.setPlaceholderText(self.translate("enemy_search"))
        self.create_button.setToolTip(self.translate("enemy_create"))
        self.configure_button.setToolTip(self.translate("enemy_configure"))
        self.delete_button.setToolTip(self.translate("enemy_delete"))
        self.refresh()

    # -- population --------------------------------------------------------

    def refresh(self) -> None:
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
            return
        self._show_details(definition)

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
