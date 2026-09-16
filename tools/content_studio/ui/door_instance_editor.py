from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QGroupBox,
    QLabel,
    QPushButton,
    QVBoxLayout,
)

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import MapDocument
from ..services.door_instance_service import DoorInstanceService
from ..services.localization import Translator


class DoorInstanceEditor(QGroupBox):
    """Contextual editor for one WorldObject door placement."""

    configuration_requested = Signal(object)
    status_changed = Signal(str)

    def __init__(
        self,
        translator: Translator,
        parent=None,
    ) -> None:
        super().__init__(parent)

        self.translate = translator
        self.document: MapDocument | None = None
        self.workspace: ContentWorkspace | None = None
        self.object_id: int | None = None

        self._loading = True

        self.mode_label = QLabel()
        self.state_label = QLabel()
        self.key_label = QLabel()
        self.consume_label = QLabel()
        self.persistence_label = QLabel()
        self.open_attack_label = QLabel()
        self.open_encounter_label = QLabel()

        self.mode = QComboBox()
        self.mode.setObjectName(
            "doorInstanceMode"
        )

        self.initial_state = QComboBox()
        self.initial_state.setObjectName(
            "doorInitialState"
        )

        self.required_key = QComboBox()
        self.required_key.setObjectName(
            "doorRequiredKey"
        )

        self.consume_key = QCheckBox()
        self.consume_key.setObjectName(
            "doorConsumeKey"
        )

        self.persistence = QComboBox()
        self.persistence.setObjectName(
            "doorPersistence"
        )

        self.open_attack = QComboBox()
        self.open_attack.setObjectName(
            "doorOpenAttack"
        )

        self.open_encounter = QComboBox()
        self.open_encounter.setObjectName(
            "doorOpenEncounter"
        )

        self.apply_button = QPushButton()
        self.apply_button.setObjectName(
            "doorApplyConfiguration"
        )

        self.help = QLabel()
        self.help.setWordWrap(
            True
        )

        form = QFormLayout()

        form.addRow(
            self.mode_label,
            self.mode,
        )

        form.addRow(
            self.state_label,
            self.initial_state,
        )

        form.addRow(
            self.key_label,
            self.required_key,
        )

        form.addRow(
            self.consume_label,
            self.consume_key,
        )

        form.addRow(
            self.persistence_label,
            self.persistence,
        )

        form.addRow(
            self.open_attack_label,
            self.open_attack,
        )

        form.addRow(
            self.open_encounter_label,
            self.open_encounter,
        )

        layout = QVBoxLayout(
            self
        )

        layout.addLayout(
            form
        )

        layout.addWidget(
            self.help
        )

        layout.addWidget(
            self.apply_button
        )

        self.mode.currentIndexChanged.connect(
            self._mode_changed
        )

        self.initial_state.currentIndexChanged.connect(
            self._state_changed
        )

        self.required_key.currentIndexChanged.connect(
            self._key_changed
        )

        self.apply_button.clicked.connect(
            self._apply
        )

        self.retranslate(
            translator
        )

        self._loading = False

        self.clear()

    def clear(
        self,
    ) -> None:
        self.document = None
        self.workspace = None
        self.object_id = None

        self.hide()

    def set_context(
        self,
        document: MapDocument,
        workspace: ContentWorkspace,
        object_id: int,
    ) -> bool:
        service = DoorInstanceService(
            document,
            workspace,
        )

        try:
            config = service.configuration(
                object_id
            )
        except ValueError:
            self.clear()
            return False

        self.document = document
        self.workspace = workspace
        self.object_id = int(
            object_id
        )

        self._loading = True

        self._reload_keys(
            service,
            config.required_item_id,
        )

        self._reload_open_attack(
            service,
            config.open_attack_id,
        )

        self._reload_open_encounter(
            service,
            config.encounter_id,
        )

        self._set_data(
            self.mode,
            (
                "defaults"
                if config.uses_definition_defaults
                else "custom"
            ),
        )

        self._set_data(
            self.initial_state,
            config.initial_state,
        )

        self._set_data(
            self.required_key,
            config.required_item_id,
        )

        self.consume_key.setChecked(
            config.consume_item
        )

        self._set_data(
            self.persistence,
            config.persistence,
        )

        self._loading = False

        self._sync_enabled()

        self.show()

        return True

    def retranslate(
        self,
        translator: Translator,
    ) -> None:
        self.translate = translator

        self.setTitle(
            self.translate(
                "door_instance_title"
            )
        )

        self.mode_label.setText(
            self.translate(
                "door_instance_mode"
            )
        )

        self.state_label.setText(
            self.translate(
                "door_initial_state"
            )
        )

        self.key_label.setText(
            self.translate(
                "door_required_key"
            )
        )

        self.consume_label.setText(
            self.translate(
                "door_consume_key"
            )
        )

        self.persistence_label.setText(
            self.translate(
                "door_persistence"
            )
        )

        self.open_attack_label.setText(
            self.translate(
                "door_open_attack"
            )
        )

        self.open_encounter_label.setText(
            self.translate(
                "door_open_encounter"
            )
        )

        current_mode = (
            self.mode.currentData()
        )

        self.mode.blockSignals(
            True
        )

        self.mode.clear()

        self.mode.addItem(
            self.translate(
                "door_instance_defaults"
            ),
            "defaults",
        )

        self.mode.addItem(
            self.translate(
                "door_instance_custom"
            ),
            "custom",
        )

        self._set_data(
            self.mode,
            current_mode or "defaults",
        )

        self.mode.blockSignals(
            False
        )

        current_state = (
            self.initial_state.currentData()
        )

        self.initial_state.blockSignals(
            True
        )

        self.initial_state.clear()

        for value, key in (
            (
                "closed",
                "door_state_closed",
            ),
            (
                "locked",
                "door_state_locked",
            ),
            (
                "open",
                "door_state_open",
            ),
        ):
            self.initial_state.addItem(
                self.translate(
                    key
                ),
                value,
            )

        self._set_data(
            self.initial_state,
            current_state or "closed",
        )

        self.initial_state.blockSignals(
            False
        )

        current_persistence = (
            self.persistence.currentData()
        )

        self.persistence.blockSignals(
            True
        )

        self.persistence.clear()

        self.persistence.addItem(
            self.translate(
                "door_persistence_persistent"
            ),
            "persistent",
        )

        self.persistence.addItem(
            self.translate(
                "door_persistence_reset"
            ),
            "resetOnMapEnter",
        )

        self._set_data(
            self.persistence,
            current_persistence or "persistent",
        )

        self.persistence.blockSignals(
            False
        )

        self.apply_button.setText(
            self.translate(
                "door_apply_configuration"
            )
        )

        self.help.setText(
            self.translate(
                "door_instance_help"
            )
        )

        if (
            self.document is not None
            and self.workspace is not None
            and self.object_id is not None
        ):
            try:
                service = DoorInstanceService(
                    self.document,
                    self.workspace,
                )

                config = service.configuration(
                    self.object_id
                )

                self._reload_keys(
                    service,
                    config.required_item_id,
                )

                self._reload_open_attack(
                    service,
                    config.open_attack_id,
                )

                self._reload_open_encounter(
                    service,
                    config.encounter_id,
                )
            except ValueError:
                pass

        self._sync_enabled()

    def _reload_open_attack(
        self,
        service: DoorInstanceService,
        selected: str | None,
    ) -> None:
        self.open_attack.blockSignals(
            True
        )

        self.open_attack.clear()

        self.open_attack.addItem(
            self.translate(
                "door_open_condition_none"
            ),
            None,
        )

        for definition in service.available_attacks():
            self.open_attack.addItem(
                f"{definition.display_name} "
                f"[{definition.definition_id}]",
                definition.definition_id,
            )

        self._set_data(
            self.open_attack,
            selected,
        )

        self.open_attack.blockSignals(
            False
        )

    def _reload_open_encounter(
        self,
        service: DoorInstanceService,
        selected: str | None,
    ) -> None:
        self.open_encounter.blockSignals(
            True
        )

        self.open_encounter.clear()

        self.open_encounter.addItem(
            self.translate(
                "door_open_condition_none"
            ),
            None,
        )

        for encounter_id in service.available_encounters():
            self.open_encounter.addItem(
                encounter_id,
                encounter_id,
            )

        self._set_data(
            self.open_encounter,
            selected,
        )

        self.open_encounter.blockSignals(
            False
        )

    def _reload_keys(
        self,
        service: DoorInstanceService,
        selected: str | None,
    ) -> None:
        self.required_key.blockSignals(
            True
        )

        self.required_key.clear()

        self.required_key.addItem(
            self.translate(
                "door_no_required_key"
            ),
            None,
        )

        for definition in service.available_keys():
            self.required_key.addItem(
                f"{definition.display_name} "
                f"[{definition.definition_id}]",
                definition.definition_id,
            )

        self._set_data(
            self.required_key,
            selected,
        )

        self.required_key.blockSignals(
            False
        )

    @staticmethod
    def _set_data(
        combo: QComboBox,
        value: object,
    ) -> None:
        index = combo.findData(
            value
        )

        if index < 0:
            index = 0

        combo.setCurrentIndex(
            index
        )

    def _mode_changed(
        self,
        unused: int,
    ) -> None:
        del unused

        if self._loading:
            return

        self._sync_enabled()

    def _state_changed(
        self,
        unused: int,
    ) -> None:
        del unused

        if self._loading:
            return

        if (
            self.initial_state.currentData()
            != "locked"
        ):
            self._loading = True

            self._set_data(
                self.required_key,
                None,
            )

            self.consume_key.setChecked(
                False
            )

            self._loading = False

        self._sync_enabled()

    def _key_changed(
        self,
        unused: int,
    ) -> None:
        del unused

        if self._loading:
            return

        if (
            self.required_key.currentData()
            is None
        ):
            self.consume_key.setChecked(
                False
            )

        self._sync_enabled()

    def _sync_enabled(
        self,
    ) -> None:
        custom = (
            self.mode.currentData()
            == "custom"
        )

        locked = (
            self.initial_state.currentData()
            == "locked"
        )

        has_key = (
            self.required_key.currentData()
            is not None
        )

        self.initial_state.setEnabled(
            custom
        )

        self.required_key.setEnabled(
            custom
            and locked
        )

        self.consume_key.setEnabled(
            custom
            and locked
            and has_key
        )

        # Open conditions belong to the placement, not the lock state.
        self.open_attack.setEnabled(
            custom
        )

        self.open_encounter.setEnabled(
            custom
        )

        # Persistence belongs to the placement, not to the definition.
        self.persistence.setEnabled(
            True
        )

        self.apply_button.setEnabled(
            self.object_id is not None
        )

    def _apply(
        self,
    ) -> None:
        if self.object_id is None:
            return

        uses_defaults = (
            self.mode.currentData()
            == "defaults"
        )

        initial_state = str(
            self.initial_state.currentData()
            or "closed"
        )

        required_item_id = (
            self.required_key.currentData()
            if (
                not uses_defaults
                and initial_state == "locked"
            )
            else None
        )

        consume_item = bool(
            not uses_defaults
            and initial_state == "locked"
            and required_item_id is not None
            and self.consume_key.isChecked()
        )

        persistence = str(
            self.persistence.currentData()
            or "persistent"
        )

        open_attack_id = (
            self.open_attack.currentData()
            if not uses_defaults
            else None
        )

        encounter_id = (
            self.open_encounter.currentData()
            if not uses_defaults
            else None
        )

        self.configuration_requested.emit({
            "uses_definition_defaults": uses_defaults,
            "initial_state": initial_state,
            "required_item_id": required_item_id,
            "consume_item": consume_item,
            "persistence": persistence,
            "open_attack_id": (
                open_attack_id
                if isinstance(open_attack_id, str)
                else None
            ),
            "encounter_id": (
                encounter_id
                if isinstance(encounter_id, str)
                else None
            ),
        })
