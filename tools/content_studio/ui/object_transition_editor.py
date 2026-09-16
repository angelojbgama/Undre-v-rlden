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

from ..model.map_document import MapDocument
from ..model.world_project import WorldProject
from ..services.localization import Translator
from ..services.object_transition_service import ObjectTransitionService


class ObjectTransitionEditor(QGroupBox):
    configuration_requested = Signal(object)
    status_changed = Signal(str)

    def __init__(
        self,
        translator: Translator,
        parent=None,
    ) -> None:
        super().__init__(parent)

        self.translate = translator
        self.project: WorldProject | None = None
        self.document: MapDocument | None = None
        self.object_id: int | None = None
        self._loading = True

        self.enabled = QCheckBox()
        self.enabled.setObjectName(
            "objectTransitionEnabled"
        )

        self.target_map = QComboBox()
        self.target_map.setObjectName(
            "objectTransitionTargetMap"
        )

        self.target_spawn = QComboBox()
        self.target_spawn.setObjectName(
            "objectTransitionTargetSpawn"
        )

        self.map_label = QLabel()
        self.spawn_label = QLabel()

        self.help = QLabel()
        self.help.setWordWrap(
            True
        )

        self.apply_button = QPushButton()
        self.apply_button.setObjectName(
            "objectTransitionApply"
        )

        form = QFormLayout()

        form.addRow(
            self.enabled
        )

        form.addRow(
            self.map_label,
            self.target_map,
        )

        form.addRow(
            self.spawn_label,
            self.target_spawn,
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

        self.enabled.toggled.connect(
            self._sync_enabled
        )

        self.target_map.currentIndexChanged.connect(
            self._target_map_changed
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
        self.project = None
        self.document = None
        self.object_id = None
        self.hide()

    def set_context(
        self,
        project: WorldProject,
        document: MapDocument,
        object_id: int,
    ) -> bool:
        service = ObjectTransitionService(
            project,
            document,
        )

        try:
            config = service.configuration(
                object_id
            )
        except ValueError:
            self.clear()
            return False

        self.project = project
        self.document = document
        self.object_id = int(
            object_id
        )

        self._loading = True

        self.enabled.setChecked(
            config.enabled
        )

        self._reload_maps(
            service,
            config.target_map_id,
        )

        selected_map = (
            config.target_map_id
            or self.target_map.currentData()
        )

        self._reload_spawns(
            service,
            selected_map
            if isinstance(selected_map, str)
            else None,
            config.target_spawn_id,
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
                "object_transition_title"
            )
        )

        self.enabled.setText(
            self.translate(
                "object_transition_enabled"
            )
        )

        self.map_label.setText(
            self.translate(
                "object_transition_target_map"
            )
        )

        self.spawn_label.setText(
            self.translate(
                "object_transition_target_spawn"
            )
        )

        self.apply_button.setText(
            self.translate(
                "object_transition_apply"
            )
        )

        self.help.setText(
            self.translate(
                "object_transition_help"
            )
        )

    def _reload_maps(
        self,
        service: ObjectTransitionService,
        selected: str | None,
    ) -> None:
        self.target_map.blockSignals(
            True
        )
        self.target_map.clear()

        values = list(
            service.available_maps()
        )

        if selected and selected not in values:
            values.append(
                selected
            )

        for map_id in values:
            self.target_map.addItem(
                map_id,
                map_id,
            )

        self._set_data(
            self.target_map,
            selected,
        )

        self.target_map.blockSignals(
            False
        )

    def _reload_spawns(
        self,
        service: ObjectTransitionService,
        map_id: str | None,
        selected: str | None,
    ) -> None:
        self.target_spawn.blockSignals(
            True
        )
        self.target_spawn.clear()

        values: list[str] = []

        if map_id:
            try:
                values.extend(
                    service.available_spawns(
                        map_id
                    )
                )
            except ValueError:
                pass

        if selected and selected not in values:
            values.append(
                selected
            )

        for spawn_id in values:
            self.target_spawn.addItem(
                spawn_id,
                spawn_id,
            )

        self._set_data(
            self.target_spawn,
            selected,
        )

        self.target_spawn.blockSignals(
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

        if combo.count() > 0:
            combo.setCurrentIndex(
                index
            )

    def _target_map_changed(
        self,
        unused: int,
    ) -> None:
        del unused

        if (
            self._loading
            or self.project is None
            or self.document is None
        ):
            return

        service = ObjectTransitionService(
            self.project,
            self.document,
        )

        map_id = self.target_map.currentData()

        self._reload_spawns(
            service,
            map_id
            if isinstance(map_id, str)
            else None,
            None,
        )

        self._sync_enabled()

    def _sync_enabled(
        self,
        unused: bool | None = None,
    ) -> None:
        del unused

        active = (
            self.object_id is not None
            and self.enabled.isChecked()
        )

        self.target_map.setEnabled(
            active
        )

        self.target_spawn.setEnabled(
            active
        )

        self.apply_button.setEnabled(
            self.object_id is not None
        )

    def _apply(
        self,
    ) -> None:
        if self.object_id is None:
            return

        enabled = self.enabled.isChecked()

        target_map_id = (
            self.target_map.currentData()
            if enabled
            else None
        )

        target_spawn_id = (
            self.target_spawn.currentData()
            if enabled
            else None
        )

        self.configuration_requested.emit({
            "enabled": enabled,
            "target_map_id": (
                target_map_id
                if isinstance(target_map_id, str)
                else None
            ),
            "target_spawn_id": (
                target_spawn_id
                if isinstance(target_spawn_id, str)
                else None
            ),
        })
