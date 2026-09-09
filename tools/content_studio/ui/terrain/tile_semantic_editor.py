from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import QCheckBox, QComboBox, QFormLayout, QLabel, QLineEdit, QPushButton, QVBoxLayout, QWidget

from ...model.content_workspace import ContentWorkspace
from ...services.localization import Translator
from ...services.tile_semantic_catalog import TileSemanticCatalog
from ...model.tile_semantics import EDGES, ROLES, TOPOLOGIES


class TileSemanticEditor(QWidget):
    """Visual editor for one existing or newly classified atlas cell."""

    saved = Signal()

    def __init__(self, workspace: ContentWorkspace | None = None,
                 catalog: TileSemanticCatalog | None = None,
                 translator: Translator | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.catalog = catalog or TileSemanticCatalog(workspace)
        self.translate = translator or Translator()
        self.tileset_id = ""; self.source_index = 0
        self.semantic_id = QLineEdit(); self.family = QLineEdit()
        self.role = QComboBox(); self.role.addItems(ROLES)
        self.topology = QComboBox(); self.topology.addItems(TOPOLOGIES)
        self.preferred_layer = QLineEdit()
        self.north = QComboBox(); self.east = QComboBox(); self.south = QComboBox(); self.west = QComboBox()
        for widget in (self.north, self.east, self.south, self.west): widget.addItems(EDGES)
        self.flip_x = QCheckBox(self.translate("flip_x_allowed"))
        self.save_button = QPushButton(self.translate("save_semantic")); self.save_button.clicked.connect(self.save_semantic)
        form = QFormLayout(); form.addRow(self.translate("semantic_id"), self.semantic_id); form.addRow(self.translate("family"), self.family); form.addRow(self.translate("role"), self.role); form.addRow(self.translate("topology"), self.topology); form.addRow(self.translate("preferred_layer"), self.preferred_layer); form.addRow(self.translate("north"), self.north); form.addRow(self.translate("east"), self.east); form.addRow(self.translate("south"), self.south); form.addRow(self.translate("west"), self.west)
        layout = QVBoxLayout(self); layout.addWidget(QLabel(self.translate("semantic_editor"))); layout.addLayout(form); layout.addWidget(self.flip_x); layout.addWidget(self.save_button); layout.addStretch(1)

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace; self.catalog.set_workspace(workspace)

    def set_selection(self, tileset_id: str, source_index: int) -> None:
        self.tileset_id = tileset_id; self.source_index = int(source_index)
        semantic = next(iter(self.catalog.by_reference(tileset_id, self.source_index)), None)
        if semantic:
            self.semantic_id.setText(semantic.definition_id); self.family.setText(semantic.family); self._set(self.role, semantic.role); self._set(self.topology, semantic.topology); self.preferred_layer.setText(semantic.preferred_layer); self._set(self.north, semantic.north); self._set(self.east, semantic.east); self._set(self.south, semantic.south); self._set(self.west, semantic.west); self.flip_x.setChecked(semantic.flip_x_allowed)
        else:
            self.semantic_id.setText(f"semantic.{tileset_id}.{self.source_index}"); self.family.clear(); self._set(self.role, "unknown"); self._set(self.topology, "unknown"); self.preferred_layer.clear(); [self._set(widget, "unknown") for widget in (self.north, self.east, self.south, self.west)]; self.flip_x.setChecked(False)

    def save_semantic(self) -> None:
        if self.workspace is None or not self.tileset_id or not self.semantic_id.text().strip():
            return
        data = {"id": self.semantic_id.text().strip(), "tilesetId": self.tileset_id, "sourceIndex": self.source_index, "family": self.family.text().strip(), "role": str(self.role.currentText()), "topology": str(self.topology.currentText()), "north": str(self.north.currentText()), "east": str(self.east.currentText()), "south": str(self.south.currentText()), "west": str(self.west.currentText()), "preferredLayer": self.preferred_layer.text().strip(), "flipXAllowed": self.flip_x.isChecked(), "visualConfidence": "confirmed", "semanticConfidence": "probable", "gameplayConfidence": "unverified"}
        existing = next(iter(self.catalog.by_reference(self.tileset_id, self.source_index)), None)
        try:
            if existing and existing.source_definition:
                self.workspace.replace_definition(existing.source_definition, data)
            else:
                created = self.workspace.create_definition("tileSemantics", str(data["id"]))
                self.workspace.replace_definition(created, data)
        except ValueError:
            return
        self.catalog.invalidate(); self.saved.emit()

    @staticmethod
    def _set(widget: QComboBox, value: str) -> None:
        index = widget.findText(value)
        widget.setCurrentIndex(index if index >= 0 else 0)
