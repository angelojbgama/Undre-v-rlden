from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import QCheckBox, QComboBox, QDialog, QFormLayout, QGridLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton, QSpinBox, QVBoxLayout, QWidget

from ...model.content_workspace import ContentWorkspace
from ...services.localization import Translator
from ...services.tile_semantic_catalog import TileSemanticCatalog
from ...model.tile_semantics import EDGES, ROLES, TOPOLOGIES
from ..tile_thumbnails import tile_pixmap


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
        self.asset_root: Path | None = None
        self._tileset_images: dict[str, QImage] = {}
        self.tileset_id = ""; self.source_index = 0
        self.semantic_id = QLineEdit(); self.family = QComboBox(); self.family.setEditable(True)
        self.role = QComboBox(); self.role.addItems(ROLES)
        self.topology = QComboBox(); self.topology.addItems(TOPOLOGIES)
        self.preferred_layer = QLineEdit()
        self.variant_weight = QSpinBox(); self.variant_weight.setRange(1, 100)
        self.north = QComboBox(); self.east = QComboBox(); self.south = QComboBox(); self.west = QComboBox()
        for widget in (self.north, self.east, self.south, self.west):
            widget.addItems(EDGES)
        self.flip_x = QCheckBox(self.translate("flip_x_allowed"))
        self.save_button = QPushButton(self.translate("save_semantic")); self.save_button.clicked.connect(self.save_semantic)
        # SE2: tile header so the author always sees which cell is edited.
        self.tile_preview = QLabel("·")
        self.tile_preview.setFixedSize(48, 48)
        self.tile_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.tile_preview.setStyleSheet("border: 1px solid palette(mid); background: palette(base);")
        self.tile_header = QLabel()
        header = QHBoxLayout()
        header.addWidget(self.tile_preview)
        header.addWidget(self.tile_header, 1)
        form = QFormLayout(); form.addRow(self.translate("tile_name_id"), self.semantic_id); form.addRow(self.translate("family"), self.family); form.addRow(self.translate("role"), self.role); form.addRow(self.translate("topology"), self.topology); form.addRow(self.translate("preferred_layer"), self.preferred_layer); form.addRow(self.translate("terrain_rule_weight"), self.variant_weight)
        # SE1: the four edge constraints are spatial — lay them out as a
        # compass around the tile instead of four loose combos.
        form.addRow(self.translate("semantic_edges"), self._build_edge_grid())
        layout = QVBoxLayout(self); layout.addLayout(header); layout.addLayout(form); layout.addWidget(self.flip_x); layout.addWidget(self.save_button); layout.addStretch(1)
        self._refresh_tile_preview()

    def _build_edge_grid(self) -> QWidget:
        grid = QGridLayout()
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(4)
        grid.setVerticalSpacing(4)
        for combo in (self.north, self.east, self.south, self.west):
            combo.setMaximumWidth(120)
        inert_style = "border: 1px solid palette(mid); background: palette(window); color: palette(mid);"
        for row, column in ((0, 0), (0, 2), (1, 0), (1, 2), (2, 0), (2, 2)):
            filler = QLabel("·")
            filler.setAlignment(Qt.AlignmentFlag.AlignCenter)
            filler.setFixedSize(56, 28)
            filler.setStyleSheet(inert_style)
            grid.addWidget(filler, row, column)
        self.center_cell = QLabel("·")
        self.center_cell.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.center_cell.setFixedSize(56, 28)
        self.center_cell.setStyleSheet("border: 1px solid palette(highlight); background: palette(base);")
        grid.addWidget(self.center_cell, 1, 1)
        grid.addWidget(self.north, 0, 1)
        grid.addWidget(self.east, 1, 2)
        grid.addWidget(self.south, 2, 1)
        grid.addWidget(self.west, 1, 0)
        panel = QWidget()
        panel.setLayout(grid)
        return panel

    def set_asset_root(self, asset_root: Path | None) -> None:
        if asset_root == self.asset_root:
            return
        self.asset_root = asset_root
        self._tileset_images.clear()
        self._refresh_tile_preview()

    def _refresh_tile_preview(self) -> None:
        pixmap = tile_pixmap(self.workspace, self.asset_root, self.tileset_id, self.source_index, self._tileset_images)
        if pixmap is not None:
            self.center_cell.setPixmap(pixmap.scaled(
                44, 44, Qt.AspectRatioMode.IgnoreAspectRatio,
                Qt.TransformationMode.FastTransformation))
            self.tile_preview.setPixmap(pixmap.scaled(
                44, 44, Qt.AspectRatioMode.IgnoreAspectRatio,
                Qt.TransformationMode.FastTransformation))
            self.tile_preview.setText("")
        else:
            self.center_cell.setText("·")
            self.tile_preview.setText("·")
        self.tile_header.setText(
            self.translate("semantic_tile_header").format(
                tileset=self.tileset_id or "—", index=self.source_index))

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace; self.catalog.set_workspace(workspace)

    def set_selection(self, tileset_id: str, source_index: int) -> None:
        self.tileset_id = tileset_id; self.source_index = int(source_index)
        semantic = next(iter(self.catalog.by_reference(tileset_id, self.source_index)), None)
        if semantic:
            self.semantic_id.setText(semantic.definition_id); self._set_family(semantic.family); self._set(self.role, semantic.role); self._set(self.topology, semantic.topology); self.preferred_layer.setText(semantic.preferred_layer); self.variant_weight.setValue(max(1, semantic.variant_weight)); self._set(self.north, semantic.north); self._set(self.east, semantic.east); self._set(self.south, semantic.south); self._set(self.west, semantic.west); self.flip_x.setChecked(semantic.flip_x_allowed)
        else:
            self.semantic_id.setText(f"semantic.{tileset_id}.{self.source_index}"); self._set_family(""); self._set(self.role, "unknown"); self._set(self.topology, "unknown"); self.preferred_layer.clear(); self.variant_weight.setValue(1); [self._set(widget, "unknown") for widget in (self.north, self.east, self.south, self.west)]; self.flip_x.setChecked(False)
        self._refresh_tile_preview()

    def save_semantic(self) -> None:
        if self.workspace is None or not self.tileset_id or not self.semantic_id.text().strip():
            return
        data = {"id": self.semantic_id.text().strip(), "tilesetId": self.tileset_id, "sourceIndex": self.source_index, "family": self.family.currentText().strip(), "role": str(self.role.currentText()), "topology": str(self.topology.currentText()), "north": str(self.north.currentText()), "east": str(self.east.currentText()), "south": str(self.south.currentText()), "west": str(self.west.currentText()), "preferredLayer": self.preferred_layer.text().strip(), "flipXAllowed": self.flip_x.isChecked(), "visualConfidence": "confirmed", "semanticConfidence": "probable", "gameplayConfidence": "unverified", "variantWeight": self.variant_weight.value()}
        existing = next(iter(self.catalog.by_reference(self.tileset_id, self.source_index)), None)
        try:
            if existing and existing.source_definition:
                target = existing.source_definition
                if existing.definition_id != data["id"]:
                    target = self.workspace.rename_definition(target, str(data["id"]))
                self.workspace.replace_definition(target, data)
            else:
                created = self.workspace.create_definition("tileSemantics", str(data["id"]))
                self.workspace.replace_definition(created, data)
        except ValueError:
            return
        self.catalog.invalidate(); self.saved.emit()

    def _set_family(self, family: str) -> None:
        families = sorted(value.family for value in self.catalog.families() if value.family)
        self.family.blockSignals(True)
        self.family.clear()
        self.family.addItem("")
        self.family.addItems(families)
        self.family.setCurrentText(family)
        self.family.blockSignals(False)

    @staticmethod
    def _set(widget: QComboBox, value: str) -> None:
        index = widget.findText(value)
        widget.setCurrentIndex(index if index >= 0 else 0)


class TileSemanticDialog(QDialog):
    """Focused modal entry point for classifying a raw imported atlas tile."""

    def __init__(self, workspace: ContentWorkspace, tileset_id: str, source_index: int,
                 translator: Translator | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator or Translator()
        self.setWindowTitle(self.translate("edit_tile_semantic"))
        self.editor = TileSemanticEditor(workspace, TileSemanticCatalog(workspace), self.translate, self)
        self.editor.set_selection(tileset_id, source_index)
        self.editor.saved.connect(self.accept)
        layout = QVBoxLayout(self)
        layout.addWidget(self.editor)
