from __future__ import annotations

from pathlib import Path

from PySide6.QtGui import QIcon
from PySide6.QtWidgets import (
    QButtonGroup, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QGridLayout,
    QHBoxLayout, QLabel, QLineEdit, QMessageBox, QPushButton, QVBoxLayout, QWidget,
)

from ...model.content_workspace import ContentWorkspace
from ...services.localization import Translator
from ...services.terrain_rule_service import (
    RULE_SLOT_LABELS, TerrainRuleService,
)
from ..tilesets.tile_atlas_widget import TileAtlasWidget


class TerrainRuleDialog(QDialog):
    """CRUD editor for a visual Smart Terrain rule.

    Rules are still derived from authored ``tileSemantics``.  The selector is
    only a convenient view over generated semantic definitions; it is not a
    second database or a new serialized format.
    """

    def __init__(self, workspace: ContentWorkspace, asset_root: Path | None,
                 tileset_id: str, translator: Translator | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.tileset_id = tileset_id
        self.translate = translator or Translator()
        self.service = TerrainRuleService()
        self._loading = False
        self._original_rule: tuple[str, str] | None = None
        definition = workspace.find("tilesets", tileset_id)
        display_name = definition.display_name if definition else tileset_id
        self.default_family = f"terrain.{tileset_id.removeprefix('tileset.')}"
        existing_semantic = next((value for value in workspace.definitions("tileSemantics")
                                  if value.data.get("tilesetId") == tileset_id and value.data.get("family")), None)
        if existing_semantic is not None:
            self.default_family = str(existing_semantic.data.get("family", self.default_family))

        self.rule_selector = QComboBox()
        self.rule_selector.currentIndexChanged.connect(self._rule_selected)
        self.new_button = QPushButton(self.translate("terrain_rule_new"))
        self.new_button.clicked.connect(self._new_rule)
        self.delete_button = QPushButton(self.translate("terrain_rule_delete"))
        self.delete_button.clicked.connect(self._delete_rule)

        self.family = QLineEdit()
        self.role = QComboBox()
        self.role.addItem(self.translate("floor"), "floor")
        self.role.addItem(self.translate("wall"), "wall")
        self.role.setCurrentIndex(1)
        self.role.currentIndexChanged.connect(self._role_changed)
        self.help = QLabel(self.translate("terrain_rule_slots_hint"))
        self.help.setWordWrap(True)
        self.atlas = TileAtlasWidget()
        self.atlas.setMinimumWidth(360)
        self.slots: dict[str, QPushButton] = {}
        self.assignments: dict[str, int] = {}
        self.active_slot = "center"
        self.slot_group = QButtonGroup(self)
        self.slot_group.setExclusive(True)
        grid = QGridLayout()
        for row, slot_row in enumerate((
            ("north_west", "north", "north_east"),
            ("west", "center", "east"),
            ("south_west", "south", "south_east"),
        )):
            for column, slot in enumerate(slot_row):
                button = QPushButton()
                button.setCheckable(True)
                button.setMinimumSize(82, 70)
                button.clicked.connect(lambda checked=False, value=slot: self._select_slot(value))
                self.slot_group.addButton(button)
                self.slots[slot] = button
                grid.addWidget(button, row, column)

        self.atlas.set_context(workspace, asset_root)
        self.atlas.set_tileset(tileset_id)
        self.atlas.selected.connect(self._atlas_selected)

        form = QFormLayout()
        form.addRow(self.translate("terrain_rule_existing"), self.rule_selector)
        rule_buttons = QHBoxLayout()
        rule_buttons.addWidget(self.new_button)
        rule_buttons.addWidget(self.delete_button)
        form.addRow("", rule_buttons)
        form.addRow(self.translate("terrain_rule_family"), self.family)
        form.addRow(self.translate("terrain_rule_role"), self.role)

        self.save_button = QPushButton(self.translate("terrain_rule_save"))
        self.save_button.clicked.connect(self._save)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Cancel)
        buttons.rejected.connect(self.reject)
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel(f"{self.translate('terrain_rule_editor')} — {display_name}"))
        layout.addLayout(form)
        layout.addWidget(self.help)
        layout.addWidget(QLabel(self.translate("terrain_rule_slots")))
        layout.addLayout(grid)
        layout.addWidget(self.atlas, 1)
        layout.addWidget(self.save_button)
        layout.addWidget(buttons)
        self.setWindowTitle(self.translate("terrain_rule_editor"))
        self.resize(900, 700)

        self._populate_rules()

    def _populate_rules(self, selected: tuple[str, str] | None = None) -> None:
        rules = self.service.list_rules(self.workspace, self.tileset_id)
        self._loading = True
        self.rule_selector.clear()
        self.rule_selector.addItem(self.translate("terrain_rule_none"), None)
        for rule in rules:
            self.rule_selector.addItem(f"{rule.family} / {rule.role} ({rule.assigned_slots}/9)",
                                       (rule.family, rule.role))
        index = 0
        if selected is not None:
            for candidate in range(self.rule_selector.count()):
                if self.rule_selector.itemData(candidate) == selected:
                    index = candidate
                    break
        elif rules:
            # Opening the editor on a configured tileset should immediately
            # show its first existing rule.  ``New`` remains explicit when a
            # fresh rule is wanted.
            index = 1
        self.rule_selector.setCurrentIndex(index)
        self._loading = False
        self._rule_selected()

    def _rule_selected(self) -> None:
        if self._loading:
            return
        value = self.rule_selector.currentData()
        if not isinstance(value, (tuple, list)) or len(value) != 2:
            self._set_new_state()
            return
        family, role = str(value[0]), str(value[1])
        self._loading = True
        self._original_rule = (family, role)
        self.family.setText(family)
        self._set_role(role)
        self.assignments = self.service.load_assignments(self.workspace, self.tileset_id, family, role)
        self._loading = False
        self._select_slot(self.active_slot)
        self._refresh_slots()

    def _set_new_state(self) -> None:
        self._loading = True
        self._original_rule = None
        self.family.setText(self._next_family_name())
        self._set_role("wall")
        self.assignments = {}
        self._loading = False
        self._select_slot(self.active_slot)
        self._refresh_slots()

    def _new_rule(self) -> None:
        self._loading = True
        self.rule_selector.setCurrentIndex(0)
        self._loading = False
        self._set_new_state()

    def _delete_rule(self) -> None:
        if self._original_rule is None:
            return
        family, role = self._original_rule
        answer = QMessageBox.question(
            self, self.translate("terrain_rule_delete"),
            self.translate("terrain_rule_delete_confirm", family=family, role=role),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        try:
            deleted = self.service.delete_rule(self.workspace, self.tileset_id, family, role)
        except ValueError as error:
            QMessageBox.warning(self, self.translate("terrain_rule_editor"), str(error))
            return
        if deleted:
            self._populate_rules()

    def _next_family_name(self, base: str | None = None) -> str:
        root = (base or self.default_family).strip() or "terrain.rule"
        existing = {value.family for value in self.service.list_rules(self.workspace, self.tileset_id)}
        candidate = root
        number = 2
        while candidate in existing:
            candidate = f"{root}.{number}"
            number += 1
        return candidate

    def _set_role(self, role: str) -> None:
        index = self.role.findData(role)
        if index >= 0:
            self.role.setCurrentIndex(index)

    def _select_slot(self, slot: str) -> None:
        self.active_slot = slot
        self.slots[slot].setChecked(True)
        self._refresh_slots()

    def _atlas_selected(self, tileset_id: str, source_index: int, flags: int) -> None:
        if tileset_id != self.tileset_id:
            return
        del flags
        self.assignments[self.active_slot] = int(source_index)
        self._refresh_slots()

    def _role_changed(self) -> None:
        # Role is part of the rule identity.  Keep the current assignments so
        # changing Wall to Floor is a normal CRUD edit/rename, not a destructive
        # reload of the visual work already configured in this dialog.
        if not self._loading:
            self._refresh_slots()

    def _refresh_slots(self) -> None:
        for slot, button in self.slots.items():
            source_index = self.assignments.get(slot)
            button.setText(f"{RULE_SLOT_LABELS[slot]}\n{source_index if source_index is not None else '—'}")
            button.setToolTip(self.translate("terrain_rule_click_atlas"))
            if source_index is not None and 0 <= source_index < self.atlas.tiles.count():
                item = self.atlas.tiles.item(source_index)
                icon: QIcon = item.icon() if item is not None else QIcon()
                button.setIcon(icon)
                button.setIconSize(button.sizeHint())
            else:
                button.setIcon(QIcon())
            button.setStyleSheet("QPushButton:checked { border: 2px solid #f0c674; }")

    def _save(self) -> None:
        family = self.family.text().strip()
        role = str(self.role.currentData() or "wall")
        previous_family, previous_role = self._original_rule or (None, None)
        try:
            self.service.save_rule(
                self.workspace, self.tileset_id, family, role, self.assignments,
                previous_family, previous_role,
            )
        except ValueError as error:
            QMessageBox.warning(self, self.translate("terrain_rule_editor"), str(error))
            return
        self.accept()
