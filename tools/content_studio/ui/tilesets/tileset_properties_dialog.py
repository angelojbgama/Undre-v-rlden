from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QFormLayout, QLabel, QLineEdit,
    QListWidget, QListWidgetItem, QSpinBox, QVBoxLayout, QWidget,
)

from ...services.import_service import calculate_grid
from ...services.localization import Translator
from ...services.tileset_library import TilesetLibrary


class TilesetPropertiesDialog(QDialog):
    """Edit one imported tileset through the existing validated import path."""

    def __init__(self, library: TilesetLibrary, tileset_id: str, asset_root: Path | None,
                 translator: Translator | None = None, parent: QWidget | None = None,
                 folder_groups: dict[str, list[str]] | None = None) -> None:
        super().__init__(parent)
        self.library = library
        self.tileset_id = tileset_id
        self.asset_root = asset_root
        self.translate = translator or Translator()
        definition = library.workspace.find("tilesets", tileset_id) if library.workspace else None
        if definition is None:
            raise ValueError(f"tileset not found: {tileset_id}")
        self.relative_path = str(definition.data.get("relativeAssetPath", ""))
        self.name = QLineEdit(str(definition.data.get("displayName", tileset_id)))
        self.tile_size = QSpinBox(); self.tile_size.setRange(1, 4096)
        self.tile_size.setValue(max(1, int(definition.data.get("tileSize", 16))))
        self.grid = QLabel()
        self.message = QLabel(); self.message.setWordWrap(True)
        self.folder_memberships = QListWidget()
        for folder, member_ids in sorted((folder_groups or {}).items(), key=lambda value: value[0].casefold()):
            item = QListWidgetItem(folder)
            item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
            item.setCheckState(
                Qt.CheckState.Checked if tileset_id in member_ids else Qt.CheckState.Unchecked)
            self.folder_memberships.addItem(item)
        form = QFormLayout()
        form.addRow(self.translate("tileset_id"), QLabel(tileset_id))
        form.addRow(self.translate("display_name"), self.name)
        form.addRow(self.translate("source_image"), QLabel(self.relative_path))
        form.addRow(self.translate("tile_size"), self.tile_size)
        form.addRow(self.translate("tileset_grid"), self.grid)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self._save); buttons.rejected.connect(self.reject)
        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(QLabel(self.translate("tileset_folder_memberships")))
        layout.addWidget(self.folder_memberships)
        folder_hint = QLabel(self.translate("tileset_folder_memberships_hint")); folder_hint.setWordWrap(True)
        layout.addWidget(folder_hint)
        layout.addWidget(self.message)
        layout.addWidget(buttons)
        self.tile_size.valueChanged.connect(self._refresh_grid)
        self.setWindowTitle(self.translate("manage_tileset"))
        self._refresh_grid()
        self.resize(520, 440)

    def selected_folders(self) -> set[str]:
        return {
            self.folder_memberships.item(index).text()
            for index in range(self.folder_memberships.count())
            if self.folder_memberships.item(index).checkState() == Qt.CheckState.Checked
        }

    def _source_path(self) -> Path | None:
        return self.asset_root / self.relative_path if self.asset_root and self.relative_path else None

    def _refresh_grid(self) -> None:
        source = self._source_path()
        if source is None:
            self.grid.setText(self.translate("image_unavailable"))
            return
        try:
            dimensions = self.library.importer.inspect(source)
            columns, rows = calculate_grid(dimensions, self.tile_size.value(), self.tile_size.value())
            self.grid.setText(f"{columns} × {rows} tiles")
            self.message.clear()
        except (OSError, ValueError) as error:
            self.grid.setText(self.translate("invalid"))
            self.message.setText(str(error))

    def _save(self) -> None:
        result = self.library.update_properties(
            self.tileset_id, self.name.text(), self.tile_size.value(), self.asset_root,
        )
        if result.ok:
            self.accept()
            return
        self.message.setText("\n".join(issue.message for issue in result.diagnostics or []))
