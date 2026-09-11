from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QImage
from PySide6.QtWidgets import (
    QDialog, QDialogButtonBox, QFileDialog, QFormLayout, QHBoxLayout, QLabel,
    QLineEdit, QMessageBox, QPushButton, QSpinBox, QSplitter, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..services.import_service import ImportService, TilesetImportRequest
from ..services.localization import Translator
from .frame_grid_preview import FrameGridPreview


class TilesetImportDialog(QDialog):
    """Small visual recipe editor for the first (tileset-only) importer."""

    def __init__(self, workspace: ContentWorkspace, asset_root: Path | None,
                 import_service: ImportService | None = None,
                 translator: Translator | None = None,
                 parent: object | None = None) -> None:
        super().__init__(parent)  # type: ignore[arg-type]
        self.workspace = workspace
        self.asset_root = asset_root
        self.import_service = import_service or ImportService()
        self.translate = translator or Translator()
        self.source = QLineEdit()
        self.source.textChanged.connect(self._refresh_preview)
        browse = QPushButton(self.translate("browse"))
        browse.clicked.connect(self._browse)
        source_row = QHBoxLayout(); source_row.addWidget(self.source, 1); source_row.addWidget(browse)
        self.tileset_id = QLineEdit("tileset.authored")
        self.display_name = QLineEdit()
        self.tile_width = self._spin(16)
        self.tile_height = self._spin(16)
        self.spacing = self._spin(0)
        self.margin = self._spin(0)
        for control in (self.tile_width, self.tile_height, self.spacing, self.margin):
            control.valueChanged.connect(self._refresh_preview)
        self.preview = FrameGridPreview(self.translate("no_image"))
        self.details = QLabel()
        self.details.setWordWrap(True)
        self.frame_help = QLabel(self.translate("frame_preview_help"))
        self.frame_help.setWordWrap(True)
        self.frame_help.setStyleSheet("color: #aeb8c4;")
        self.frame_bounds = QLabel()
        self.frame_bounds.setWordWrap(True)
        form = QFormLayout()
        form.addRow(self.translate("source_image"), source_row)
        form.addRow(self.translate("tileset_id"), self.tileset_id)
        form.addRow(self.translate("display_name"), self.display_name)
        form.addRow(self.translate("tile_width"), self.tile_width)
        form.addRow(self.translate("tile_height"), self.tile_height)
        form.addRow(self.translate("spacing"), self.spacing)
        form.addRow(self.translate("margin"), self.margin)
        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.addLayout(form)
        asset_note = QLabel(self.translate("tileset_asset_root_note")); asset_note.setWordWrap(True)
        left_layout.addWidget(asset_note)
        left_layout.addStretch(1)
        right = QWidget()
        right_layout = QVBoxLayout(right)
        preview_title = QLabel(self.translate("frames_preview"))
        preview_title.setStyleSheet("font-weight: bold;")
        right_layout.addWidget(preview_title)
        right_layout.addWidget(self.preview, 1)
        right_layout.addWidget(self.details)
        right_layout.addWidget(self.frame_help)
        right_layout.addWidget(self.frame_bounds)
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(left)
        splitter.addWidget(right)
        splitter.setStretchFactor(1, 1)
        splitter.setSizes([340, 600])
        layout = QVBoxLayout(self)
        layout.addWidget(splitter, 1)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self._import)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
        self.setWindowTitle(self.translate("tileset_import"))
        self.resize(980, 620)

    @staticmethod
    def _spin(value: int) -> QSpinBox:
        spin = QSpinBox(); spin.setRange(0, 4096); spin.setValue(value)
        return spin

    def _browse(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, self.translate("source_image"), "", "Images (*.png *.jpg *.jpeg *.bmp *.gif)")
        if path:
            self.source.setText(path)

    def _request(self) -> TilesetImportRequest:
        return TilesetImportRequest(
            Path(self.source.text()), self.tileset_id.text(), self.tile_width.value(), self.tile_height.value(),
            self.spacing.value(), self.margin.value(), asset_root=self.asset_root,
            display_name=self.display_name.text(),
        )

    def _refresh_preview(self) -> None:
        source = Path(self.source.text()) if self.source.text() else None
        if source is None:
            self.preview.clear_image(self.translate("no_image")); self.details.clear(); self.frame_bounds.clear(); return
        try:
            dimensions = self.import_service.inspect_image(source)
            request = self._request()
            from ..services.import_service import calculate_grid
            columns, rows = calculate_grid(dimensions, request.tile_width, request.tile_height, request.spacing, request.margin)
            image = QImage(str(source))
            if image.isNull():
                raise ValueError(self.translate("image_unavailable"))
            left = request.margin
            top = request.margin
            self.preview.show_image(image, (
                left, top, request.tile_width, request.tile_height,
                request.spacing, columns, rows,
            ))
            self.details.setText(self.translate(
                "grid_summary", width=dimensions.width, height=dimensions.height,
                columns=columns, rows=rows, frames=columns * rows))
            self.frame_bounds.setText(self.translate(
                "frame_bounds", left=left, right=left + request.tile_width - 1,
                top=top, bottom=top + request.tile_height - 1,
                width=request.tile_width, height=request.tile_height))
        except (OSError, ValueError) as error:
            self.preview.clear_image(self.translate("invalid_image")); self.details.setText(str(error)); self.frame_bounds.clear()

    def _import(self) -> None:
        result = self.import_service.import_tileset(self.workspace, self._request())
        if not result.ok:
            QMessageBox.warning(self, self.translate("tileset_import"), "\n".join(issue.message for issue in result.diagnostics or []))
            return
        self.accept()
