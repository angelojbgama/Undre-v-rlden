from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFileDialog, QHBoxLayout, QLabel,
    QPushButton, QSpinBox, QTableWidget, QTableWidgetItem, QVBoxLayout,
)

from ...services.import_service import SUPPORTED_IMAGE_SUFFIXES, TilesetImportRequest
from ...services.localization import Translator
from ...services.tileset_library import BatchTilesetImportRequest, TilesetLibrary


class BatchTilesetImportDialog(QDialog):
    """Reviewable batch request editor shared by file, folder and OS drops."""

    def __init__(self, library: TilesetLibrary, asset_root: Path | None,
                 translator: Translator | None = None, parent: object | None = None) -> None:
        super().__init__(parent)  # type: ignore[arg-type]
        self.library = library
        self.asset_root = asset_root
        self.translate = translator or Translator()
        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels([
            self.translate("source_image"), self.translate("tileset_id"),
            self.translate("tile_size"), self.translate("conflict"),
        ])
        self.table.horizontalHeader().setStretchLastSection(True)
        self.default_size = QSpinBox(); self.default_size.setRange(1, 4096); self.default_size.setValue(16)
        self.recursive = QCheckBox(self.translate("scan_subfolders"))
        add_files = QPushButton(self.translate("add_files")); add_files.clicked.connect(self._add_files)
        add_folder = QPushButton(self.translate("add_folder")); add_folder.clicked.connect(self._add_folder)
        apply_size = QPushButton(self.translate("apply_to_all")); apply_size.clicked.connect(self._apply_size)
        controls = QHBoxLayout(); controls.addWidget(add_files); controls.addWidget(add_folder); controls.addWidget(self.recursive); controls.addStretch(1)
        size_row = QHBoxLayout(); size_row.addWidget(QLabel(self.translate("default_tile_size"))); size_row.addWidget(self.default_size); size_row.addWidget(apply_size); size_row.addStretch(1)
        self.note = QLabel(self.translate("tileset_asset_root_note")); self.note.setWordWrap(True)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self._import); buttons.rejected.connect(self.reject)
        layout = QVBoxLayout(self); layout.addLayout(controls); layout.addLayout(size_row); layout.addWidget(self.note); layout.addWidget(self.table, 1); layout.addWidget(buttons)
        self.setWindowTitle(self.translate("batch_tileset_import")); self.resize(760, 480)

    def add_paths(self, paths: list[Path]) -> None:
        existing = {self.table.item(row, 0).text() for row in range(self.table.rowCount()) if self.table.item(row, 0)}
        for path in paths:
            path = path.expanduser()
            if path.suffix.casefold() not in SUPPORTED_IMAGE_SUFFIXES or str(path) in existing:
                continue
            row = self.table.rowCount(); self.table.insertRow(row)
            self.table.setItem(row, 0, QTableWidgetItem(str(path)))
            self.table.setItem(row, 1, QTableWidgetItem(TilesetLibrary.suggest_id(path)))
            size = QSpinBox(); size.setRange(1, 4096); size.setValue(self.default_size.value()); self.table.setCellWidget(row, 2, size)
            conflict = QComboBox(); conflict.addItem(self.translate("skip"), "skip"); conflict.addItem(self.translate("reimport"), "reimport"); conflict.addItem(self.translate("change_id"), "change_id"); self.table.setCellWidget(row, 3, conflict)
            existing.add(str(path))

    def requests(self) -> list[TilesetImportRequest]:
        return [TilesetImportRequest(Path(self.table.item(row, 0).text()), self.table.item(row, 1).text(),
                                     self.table.cellWidget(row, 2).value(), self.table.cellWidget(row, 2).value(),
                                     asset_root=self.asset_root)
                for row in range(self.table.rowCount())]

    def _add_files(self) -> None:
        paths, _ = QFileDialog.getOpenFileNames(self, self.translate("add_files"), "", "Images (*.png *.jpg *.jpeg *.bmp *.gif)")
        self.add_paths([Path(value) for value in paths])

    def _add_folder(self) -> None:
        folder = QFileDialog.getExistingDirectory(self, self.translate("add_folder"))
        if folder:
            try:
                self.add_paths(TilesetLibrary.discover_files(Path(folder), self.recursive.isChecked()))
            except ValueError:
                return

    def _apply_size(self) -> None:
        for row in range(self.table.rowCount()):
            widget = self.table.cellWidget(row, 2)
            if isinstance(widget, QSpinBox):
                widget.setValue(self.default_size.value())

    def _import(self) -> None:
        entries = self.requests()
        if not entries:
            return
        # Conflict decisions are per row; group by policy so every group uses
        # the same shared importer pipeline.
        imported_any = False
        diagnostics = []
        for policy in ("skip", "reimport", "change_id"):
            selected = tuple(entry for row, entry in enumerate(entries)
                             if self.table.cellWidget(row, 3).currentData() == policy)
            if not selected:
                continue
            result = self.library.import_batch(BatchTilesetImportRequest(selected, policy))
            imported_any = imported_any or bool(result.imported)
            diagnostics.extend(result.diagnostics)
        if diagnostics:
            self.note.setText("\n".join(issue.message for issue in diagnostics))
        if imported_any and not any(issue.is_error for issue in diagnostics):
            self.accept()
