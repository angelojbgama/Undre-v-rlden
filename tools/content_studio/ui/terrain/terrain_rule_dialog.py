from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon
from PySide6.QtWidgets import (
    QButtonGroup, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QGridLayout,
    QLabel, QLineEdit, QMessageBox, QPushButton, QVBoxLayout, QWidget,
)

from ...model.content_workspace import ContentWorkspace
from ...services.localization import Translator
from ...services.terrain_rule_service import (
    RULE_SLOT_LABELS, RULE_SLOTS, RULE_SLOT_TOPOLOGY, TerrainRuleService,
)
from ..tilesets.tile_atlas_widget import TileAtlasWidget


class TerrainRuleDialog(QDialog):
    """Visual 3x3 assignment editor backed by existing tile semantics."""

    def __init__(self, workspace: ContentWorkspace, asset_root: Path | None,
                 tileset_id: str, translator: Translator | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.tileset_id = tileset_id
        self.translate = translator or Translator()
        self.service = TerrainRuleService()
        definition = workspace.find("tilesets", tileset_id)
        display_name = definition.display_name if definition else tileset_id
        default_family = f"terrain.{tileset_id.removeprefix('tileset.')}"
        existing = next((value for value in workspace.definitions("tileSemantics")
                         if value.data.get("tilesetId") == tileset_id and value.data.get("family")), None)
        if existing is not None:
            default_family = str(existing.data.get("family", default_family))

        self.family = QLineEdit(default_family)
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
        self.assignments = self.service.load_assignments(workspace, tileset_id, default_family, "wall")
        self.active_slot = "center"
        self.slot_group = QButtonGroup(self)
        self.slot_group.setExclusive(True)
        grid = QGridLayout()
        for row, slot_row in enumerate((("north_west", "north", "north_east"),
                                        ("west", "center", "east"),
                                        ("south_west", "south", "south_east"))):
            for column, slot in enumerate(slot_row):
                button = QPushButton()
                button.setCheckable(True)
                button.setMinimumSize(82, 70)
                button.clicked.connect(lambda checked=False, value=slot: self._select_slot(value))
                self.slot_group.addButton(button)
                self.slots[slot] = button
                grid.addWidget(button, row, column)
        self._select_slot(self.active_slot)
        self._refresh_slots()

        self.atlas.set_context(workspace, asset_root)
        self.atlas.set_tileset(tileset_id)
        self.atlas.selected.connect(self._atlas_selected)

        form = QFormLayout()
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
        self.resize(900, 650)

    def _select_slot(self, slot: str) -> None:
        self.active_slot = slot
        button = self.slots[slot]
        button.setChecked(True)
        self._refresh_slots()

    def _atlas_selected(self, tileset_id: str, source_index: int, flags: int) -> None:
        if tileset_id != self.tileset_id:
            return
        del flags
        self.assignments[self.active_slot] = int(source_index)
        self._refresh_slots()

    def _role_changed(self) -> None:
        self.assignments = self.service.load_assignments(
            self.workspace, self.tileset_id, self.family.text(),
            str(self.role.currentData() or "wall"),
        )
        self._refresh_slots()

    def _refresh_slots(self) -> None:
        for slot, button in self.slots.items():
            source_index = self.assignments.get(slot)
            button.setText(f"{RULE_SLOT_LABELS[slot]}\n{source_index if source_index is not None else '—'}")
            button.setToolTip(f"{RULE_SLOT_TOPOLOGY[slot]} — {self.translate('terrain_rule_click_atlas')}")
            if source_index is not None and 0 <= source_index < self.atlas.tiles.count():
                icon: QIcon = self.atlas.tiles.item(source_index).icon()
                button.setIcon(icon)
                button.setIconSize(button.sizeHint())
            else:
                button.setIcon(QIcon())
            button.setStyleSheet("QPushButton:checked { border: 2px solid #f0c674; }")

    def _save(self) -> None:
        try:
            self.service.save_rule(self.workspace, self.tileset_id, self.family.text(),
                                   str(self.role.currentData() or "wall"), self.assignments)
        except ValueError as error:
            QMessageBox.warning(self, self.translate("terrain_rule_editor"), str(error))
            return
        self.accept()
