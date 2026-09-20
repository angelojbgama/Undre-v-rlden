from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import QIcon
from PySide6.QtWidgets import (
    QButtonGroup, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QGridLayout,
    QHBoxLayout, QLabel, QLineEdit, QMessageBox, QPushButton, QSizePolicy,
    QSplitter, QTabWidget, QVBoxLayout, QWidget,
)

from ...model.content_workspace import ContentWorkspace
from ...model.world_project import WorldProject
from ...services.localization import Translator
from ...services.terrain_composition import TerrainCompositionService
from ...services.terrain_rule_service import (
    RULE_SLOT_LABELS, TerrainRuleService,
)
from ...services.tile_semantic_catalog import TileSemanticCatalog
from ..tilesets.tile_atlas_widget import TileAtlasListWidget, TileAtlasWidget
from .terrain_pattern_editor import TerrainPatternEditor
from .terrain_variant_editor import TerrainVariantEditor


class TerrainRuleDialog(QDialog):
    """CRUD editor for a Smart Terrain rule.

    A rule is composed through the strategy it configures: floor rules edit an
    unlimited weighted 1x1 variant list, wall rules keep the 3x3 connectivity
    interface, and the pattern tab shows which authored stamps the family
    accepts as NxM patterns.  Everything remains a view over the authored
    ``tileSemantics`` and ``stamps``; there is no second database.
    """

    def __init__(self, workspace: ContentWorkspace, asset_root: Path | None,
                 tileset_id: str, translator: Translator | None = None,
                 parent: QWidget | None = None,
                 project: WorldProject | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.tileset_id = tileset_id
        self.project = project
        self.translate = translator or Translator()
        self.service = TerrainRuleService()
        self.composition = TerrainCompositionService(TileSemanticCatalog(workspace), workspace)
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
        # Smart Terrain roles are semantic authoring data.
        # Physical collision is configured independently on each tileset tile.
        self.role.addItem(self.translate("floor"), "floor")
        self.role.addItem(self.translate("wall"), "wall")
        # Floor variants are the most common Smart Terrain use case.  Wall
        # topology remains available explicitly in the same compact dialog.
        self.role.setCurrentIndex(0)
        self.role.currentIndexChanged.connect(self._role_changed)
        self.help = QLabel(self.translate("terrain_rule_floor_hint"))
        self.help.setWordWrap(True)

        self.atlas = TileAtlasWidget()
        self.atlas.setMinimumWidth(0)
        self.atlas.set_context(workspace, asset_root)
        self.atlas.set_tileset(tileset_id)
        self.atlas.selected.connect(self._atlas_selected)

        # --- Connectivity editor (wall): the visual 3x3 interface ---------
        self.slots: dict[str, QPushButton] = {}
        self.assignments: dict[str, int] = {}
        self.weights: dict[str, int] = {}
        self.active_slot = "center"
        self.slot_group = QButtonGroup(self)
        self.slot_group.setExclusive(True)
        grid = QGridLayout()
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(4)
        grid.setVerticalSpacing(4)
        for row, slot_row in enumerate((
            ("north_west", "north", "north_east"),
            ("west", "center", "east"),
            ("south_west", "south", "south_east"),
        )):
            for column, slot in enumerate(slot_row):
                button = QPushButton()
                button.setCheckable(True)
                button.setFixedSize(TileAtlasListWidget.CELL_SIZE, TileAtlasListWidget.CELL_SIZE)
                button.setIconSize(QSize(32, 32))
                button.setStyleSheet(
                    "QPushButton { border: 1px solid palette(mid); border-radius: 4px; padding: 3px; }"
                    "QPushButton:hover { border-color: palette(highlight); background: palette(alternate-base); }"
                    "QPushButton:checked { border: 2px solid palette(highlight); background: palette(alternate-base); }"
                )
                button.clicked.connect(lambda checked=False, value=slot: self._select_slot(value))
                self.slot_group.addButton(button)
                self.slots[slot] = button
                grid.addWidget(button, row, column)
        self.slot_panel = self._wrap(grid)
        self.boundary_panel = QWidget()
        boundary_layout = QVBoxLayout(self.boundary_panel)
        boundary_layout.setContentsMargins(0, 0, 0, 0)
        boundary_layout.addWidget(self.slot_panel, 0, Qt.AlignmentFlag.AlignLeft)
        boundary_layout.addStretch(1)

        # --- Variant editor (floor): unlimited weighted 1x1 list ----------
        self.variant_editor = TerrainVariantEditor(self._tile_icon, self.translate)
        self.variant_editor.variants_changed.connect(self._variants_changed)
        self.variant_editor.add_requested.connect(self._add_variant_from_atlas)

        # --- Pattern editor: stamps accepted as family patterns -----------
        self.pattern_editor = TerrainPatternEditor(self.translate)

        self.tabs = QTabWidget()
        self.tabs.addTab(self.variant_editor, self.translate("terrain_rule_tab_variants"))
        self.tabs.addTab(self.boundary_panel, self.translate("terrain_rule_tab_boundary"))
        self.tabs.addTab(self.pattern_editor, self.translate("terrain_rule_tab_patterns"))

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

        self.controls_panel = QWidget()
        controls_layout = QVBoxLayout(self.controls_panel)
        controls_layout.setContentsMargins(0, 0, 0, 0)
        controls_layout.addLayout(form)
        controls_layout.addWidget(self.help)
        controls_layout.addWidget(self.tabs, 1)

        self.content_splitter = QSplitter(Qt.Orientation.Horizontal)
        self.content_splitter.setChildrenCollapsible(False)
        self.content_splitter.addWidget(self.controls_panel)
        self.content_splitter.addWidget(self.atlas)
        self.content_splitter.setStretchFactor(0, 0)
        self.content_splitter.setStretchFactor(1, 1)
        self.content_splitter.setSizes([340, 420])

        layout = QVBoxLayout(self)
        layout.addWidget(QLabel(f"{self.translate('terrain_rule_editor')} — {display_name}"))
        layout.addWidget(self.content_splitter, 1)
        # One standard button row: primary action beside Cancel (audit DL6).
        button_row = QHBoxLayout()
        button_row.addWidget(self.save_button)
        button_row.addStretch(1)
        button_row.addWidget(buttons)
        layout.addLayout(button_row)
        self.setWindowTitle(self.translate("terrain_rule_editor"))
        self.resize(800, 560)

        self._populate_rules()

    @staticmethod
    def _wrap(grid: QGridLayout) -> QWidget:
        panel = QWidget()
        panel.setLayout(grid)
        panel.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
        return panel

    def _tile_icon(self, source_index: int, tileset_id: str = "") -> QIcon:
        del tileset_id
        if 0 <= source_index < self.atlas.tiles.count():
            item = self.atlas.tiles.item(source_index)
            if item is not None:
                return item.icon()
        return QIcon()

    def _populate_rules(self, selected: tuple[str, str] | None = None) -> None:
        rules = self.service.list_rules(self.workspace, self.tileset_id)
        self._loading = True
        self.rule_selector.clear()
        self.rule_selector.addItem(self.translate("terrain_rule_none"), None)
        for rule in rules:
            role_label = self.translate(rule.role)
            count = f"{rule.assigned_slots}/9" if rule.role == "wall" else str(rule.assigned_slots)
            self.rule_selector.addItem(
                f"{rule.family} / {role_label} ({count})",
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
        self.weights = self.service.load_weights(self.workspace, self.tileset_id, family, role)
        self.variant_editor.set_variants([
            (variant.source_index, variant.weight)
            for variant in self.service.load_variants(self.workspace, self.tileset_id, family, role)
        ])
        self._refresh_patterns(family)
        self._loading = False
        self._select_slot(self.active_slot)
        self._refresh_slots()

    def _refresh_patterns(self, family: str) -> None:
        self.pattern_editor.set_patterns(
            self.composition.patterns_for(family),
            lambda source_index, tileset_id: self._tile_icon(source_index, tileset_id))

    def _set_new_state(self) -> None:
        self._loading = True
        self._original_rule = None
        self.family.setText(self._next_family_name())
        self._set_role("floor")
        self.assignments = {}
        self.weights = {}
        self.variant_editor.set_variants([])
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
            self.translate(
                "terrain_rule_delete_confirm", family=family,
                role=self.translate(role)),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if answer != QMessageBox.StandardButton.Yes:
            return
        try:
            deleted = self.service.delete_rule(self.workspace, self.tileset_id, family, role, self.project)
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
        self.tabs.setCurrentIndex(0 if role == "floor" else 1)

    def _select_slot(self, slot: str) -> None:
        self.active_slot = slot
        self.slots[slot].setChecked(True)
        self._refresh_slots()

    def _atlas_selected(self, tileset_id: str, source_index: int, flags: int) -> None:
        if tileset_id != self.tileset_id:
            return
        del flags
        if str(self.role.currentData() or "wall") == "floor":
            self.variant_editor.add_variant(int(source_index))
            return
        self.assignments[self.active_slot] = int(source_index)
        self.weights.setdefault(self.active_slot, 1)
        self._refresh_slots()

    def _add_variant_from_atlas(self) -> None:
        selected = self.atlas.tiles.selectedItems()
        if not selected:
            return
        source_index = int(selected[0].data(Qt.ItemDataRole.UserRole))
        self.variant_editor.add_variant(source_index)

    def _variants_changed(self) -> None:
        pass

    def _clear_active_slot(self) -> None:
        self.assignments.pop(self.active_slot, None)
        self.weights.pop(self.active_slot, None)
        self._refresh_slots()

    def _role_changed(self) -> None:
        # Role is part of the rule identity and selects the composition
        # editor: floors edit variants, walls edit connectivity slots.
        if self._loading:
            return
        is_floor = str(self.role.currentData() or "wall") == "floor"
        self.tabs.setCurrentIndex(0 if is_floor else 1)
        self._refresh_slots()

    def _refresh_slots(self) -> None:
        is_floor = str(self.role.currentData() or "wall") == "floor"
        self.help.setText(self.translate("terrain_rule_floor_hint" if is_floor else "terrain_rule_slots_hint"))
        for slot, button in self.slots.items():
            source_index = self.assignments.get(slot)
            button.setText("" if source_index is not None else RULE_SLOT_LABELS[slot])
            button.setToolTip(self.translate("terrain_rule_click_atlas"))
            icon = self._tile_icon(source_index) if source_index is not None else QIcon()
            button.setIcon(icon)

    def _save(self) -> None:
        family = self.family.text().strip()
        role = str(self.role.currentData() or "wall")
        previous_family, previous_role = self._original_rule or (None, None)
        try:
            if role == "floor":
                self.service.save_variants(
                    self.workspace, self.tileset_id, family,
                    self.variant_editor.variants(),
                    previous_family, previous_role,
                )
            else:
                self.service.save_rule(
                    self.workspace, self.tileset_id, family, role, self.assignments,
                    previous_family, previous_role, weights=self.weights,
                )
        except ValueError as error:
            QMessageBox.warning(self, self.translate("terrain_rule_editor"), str(error))
            return
        self.accept()
