from __future__ import annotations

from PySide6.QtCore import Qt, Signal
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

STATUS_KEYS = {
    "builtin": "attack_status_builtin",
    "override": "attack_status_override",
    "authored": "attack_status_authored",
}


class AttackDefinitionDialog(QDialog):
    """Structured editor for one attack definition."""

    def __init__(
        self,
        workspace: ContentWorkspace,
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

        self.setWindowTitle(
            definition_id
        )

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
            self.kind,
        )

        form.addRow(
            self.translate("attack_damage"),
            self.damage_amount,
        )

        form.addRow(
            self.translate("attack_knockback"),
            self.knockback,
        )

        form.addRow(
            self.translate("attack_total_ticks"),
            self.total_ticks,
        )

        form.addRow(
            self.translate("attack_cooldown"),
            self.cooldown_ticks,
        )

        form.addRow(
            self.translate("attack_min_range"),
            self.minimum_range,
        )

        form.addRow(
            self.translate("attack_max_range"),
            self.maximum_range,
        )

        form.addRow(
            self.translate("attack_visual_action"),
            self.visual_action,
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
            self.projectile,
        )

        timeline_group = QGroupBox(
            self.translate("attack_timeline")
        )

        timeline_layout = QVBoxLayout(
            timeline_group
        )

        self.timeline = QListWidget()

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

        timeline_layout.addLayout(
            event_row
        )

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

        layout = QVBoxLayout(self)

        layout.addLayout(form)

        layout.addWidget(melee_group)

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

        self.kind.currentIndexChanged.connect(
            self._sync_enabled
        )

        self._load()

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

    def _append_event(
        self,
        tick: int,
        kind: str,
    ) -> None:
        item = QListWidgetItem(
            f"{tick} · {kind}"
        )

        item.setData(
            Qt.ItemDataRole.UserRole,
            (tick, kind),
        )

        self.timeline.addItem(item)

    def _add_event(self) -> None:
        self._append_event(
            self.event_tick.value(),
            str(self.event_kind.currentData()),
        )

    def _remove_event(self) -> None:
        for item in self.timeline.selectedItems():
            self.timeline.takeItem(
                self.timeline.row(item)
            )

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

    def _collect_timeline(
        self,
    ) -> list[dict[str, object]]:
        events: list[dict[str, object]] = []

        for row in range(self.timeline.count()):
            tick, kind = self.timeline.item(
                row
            ).data(Qt.ItemDataRole.UserRole)

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
    ) -> None:
        super().__init__(parent)

        self.translate = (
            translator or Translator()
        )

        self.workspace = workspace

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
    ) -> None:
        self.workspace = workspace
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
