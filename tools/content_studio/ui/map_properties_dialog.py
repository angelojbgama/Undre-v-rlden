from __future__ import annotations

from dataclasses import dataclass

from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QLabel, QLineEdit,
    QMessageBox, QSpinBox, QVBoxLayout, QWidget,
)

from ..model.map_document import MapDocument
from ..services.localization import Translator


@dataclass(frozen=True, slots=True)
class MapProperties:
    map_id: str
    width: int
    height: int
    tile_size: int
    include_player_spawn: bool = False
    folder: str = ""


class MapPropertiesDialog(QDialog):
    """Single form shared by new-map and existing-map workflows."""

    def __init__(self, translator: Translator, document: MapDocument | None = None,
                 suggested_id: str = "map.new", folders: list[str] | None = None,
                 current_folder: str = "", parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator
        self.document = document
        self.map_id = QLineEdit(document.map_id if document else suggested_id)
        self.width_tiles = self._spin(document.width if document else 32, 1, 4096)
        self.height_tiles = self._spin(document.height if document else 24, 1, 4096)
        self.tile_size = self._spin(document.tile_size if document else 16, 1, 512)
        self.folder = QComboBox()
        self.folder.setEditable(True)
        self.folder.addItem(self.translate("map_no_folder"), "")
        for folder in sorted({value.strip() for value in (folders or []) if value.strip()}, key=str.casefold):
            self.folder.addItem(folder, folder)
        if current_folder.strip():
            index = self.folder.findData(current_folder.strip())
            if index >= 0:
                self.folder.setCurrentIndex(index)
            else:
                self.folder.setEditText(current_folder.strip())
        self.player_spawn = QCheckBox(self.translate("map_create_player_spawn"))
        self.player_spawn.setChecked(False)
        self.player_spawn.setVisible(document is None)

        form = QFormLayout()
        form.addRow(self.translate("map_name_id"), self.map_id)
        form.addRow(self.translate("map_folder"), self.folder)
        form.addRow(self.translate("map_width_tiles"), self.width_tiles)
        form.addRow(self.translate("map_height_tiles"), self.height_tiles)
        form.addRow(self.translate("tile_size"), self.tile_size)
        if document is None:
            form.addRow("", self.player_spawn)

        hint = QLabel(self.translate("map_resize_hint"))
        hint.setWordWrap(True)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self._accept_if_valid)
        buttons.rejected.connect(self.reject)
        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(hint)
        layout.addWidget(buttons)
        self.setWindowTitle(self.translate("map_edit_properties") if document else self.translate("map_create"))
        self.setMinimumWidth(420)

    @staticmethod
    def _spin(value: int, minimum: int, maximum: int) -> QSpinBox:
        spin = QSpinBox()
        spin.setRange(minimum, maximum)
        spin.setValue(value)
        return spin

    def properties(self) -> MapProperties:
        folder = self.folder.currentData()
        if self.folder.isEditable() and self.folder.currentText() != self.translate("map_no_folder"):
            folder = self.folder.currentText().strip()
        return MapProperties(
            self.map_id.text().strip(), self.width_tiles.value(), self.height_tiles.value(),
            self.tile_size.value(), self.player_spawn.isChecked(), str(folder or "").strip())

    def _accept_if_valid(self) -> None:
        if not self.map_id.text().strip():
            QMessageBox.warning(self, self.translate("map_edit_properties"), self.translate("map_id_required"))
            self.map_id.setFocus()
            return
        self.accept()
