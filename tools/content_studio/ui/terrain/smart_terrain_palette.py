from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QComboBox, QLabel, QPushButton, QSpinBox, QVBoxLayout, QWidget

from ...model.content_workspace import ContentWorkspace
from ...model.tile_semantics import TerrainProfile, TerrainSelection
from ...services.tile_semantic_catalog import TileSemanticCatalog
from ...services.localization import Translator


class SmartTerrainPalette(QWidget):
    terrain_selected = Signal(object)
    room_requested = Signal(object)

    def __init__(self, catalog: TileSemanticCatalog | None = None,
                 translator: Translator | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator or Translator()
        self.catalog = catalog or TileSemanticCatalog()
        self.workspace: ContentWorkspace | None = None
        self.family = QComboBox(); self.family.currentTextChanged.connect(self._selection_changed)
        self.role = QComboBox(); self.role.addItem(self.translate("floor"), "floor"); self.role.addItem(self.translate("wall"), "wall"); self.role.currentIndexChanged.connect(self._selection_changed)
        self.seed = QSpinBox(); self.seed.setRange(-2_147_483_648, 2_147_483_647); self.seed.setValue(0); self.seed.valueChanged.connect(self._selection_changed)
        self.room = QPushButton(self.translate("room_brush")); self.room.clicked.connect(self._room_requested)
        self.status = QLabel(); self.status.setWordWrap(True)
        layout = QVBoxLayout(self); layout.addWidget(QLabel(self.translate("smart_terrain"))); layout.addWidget(QLabel(self.translate("terrain_family"))); layout.addWidget(self.family); layout.addWidget(QLabel(self.translate("terrain_role"))); layout.addWidget(self.role); layout.addWidget(QLabel(self.translate("terrain_seed"))); layout.addWidget(self.seed); layout.addWidget(self.room); layout.addWidget(self.status); layout.addStretch(1)

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace
        self.catalog.set_workspace(workspace)
        self.family.blockSignals(True); self.family.clear(); self.family.addItems([item.family for item in self.catalog.families()]); self.family.blockSignals(False)
        self._selection_changed()

    def refresh(self) -> None:
        self.set_workspace(self.workspace)

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.room.setText(self.translate("room_brush"))
        self.role.setItemText(0, self.translate("floor")); self.role.setItemText(1, self.translate("wall"))
        self._selection_changed()

    def selection(self) -> TerrainSelection | None:
        family = self.family.currentText().strip()
        return TerrainSelection(family, str(self.role.currentData() or "floor"), self.seed.value()) if family else None

    def profile(self) -> TerrainProfile | None:
        family = self.family.currentText().strip()
        if not family:
            return None
        return TerrainProfile(f"terrain.{family}.room", TerrainSelection(family, "floor", self.seed.value()), TerrainSelection(family, "wall", self.seed.value()))

    def _selection_changed(self) -> None:
        selection = self.selection()
        if selection:
            self.terrain_selected.emit(selection)
            self.status.setText(self.translate("terrain_active", family=selection.family, role=selection.role))
        else:
            self.status.setText(self.translate("no_terrain_family"))

    def _room_requested(self) -> None:
        profile = self.profile()
        if profile:
            self.room_requested.emit(profile)
