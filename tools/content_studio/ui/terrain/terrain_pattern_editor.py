from __future__ import annotations

from collections.abc import Callable

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QIcon, QPainter, QPixmap
from PySide6.QtWidgets import (
    QLabel, QListWidget, QListWidgetItem, QVBoxLayout, QWidget,
)

from ...services.localization import Translator
from ...services.terrain_composition import StampPattern


def stamp_pixmap(pattern: StampPattern, icon_for: Callable[[int, str], QIcon],
                 cell: int = 20) -> QPixmap:
    """Render a stamp composition as one thumbnail.

    ``icon_for(source_index, tileset_id)`` supplies each cell's tile icon, so
    the thumbnail shows the actual NxM arrangement instead of a single tile.
    """
    columns = max((pattern.width, *(c.offset_x + 1 for c in pattern.cells), 1))
    rows = max((pattern.height, *(c.offset_y + 1 for c in pattern.cells), 1))
    canvas = QPixmap(columns * cell, rows * cell)
    canvas.fill(Qt.GlobalColor.transparent)
    painter = QPainter(canvas)
    for cell_data in pattern.cells:
        icon = icon_for(cell_data.source_index, cell_data.tileset_id)
        if not icon.isNull():
            painter.drawPixmap(cell_data.offset_x * cell, cell_data.offset_y * cell,
                               icon.pixmap(cell, cell))
    painter.end()
    return canvas


class TerrainPatternEditor(QWidget):
    """Lists the stamps a terrain family accepts as NxM patterns.

    Patterns are not authored here — stamps remain the single multi-tile
    database.  This view derives, per family, which stamps Smart Terrain can
    place atomically, so authors never maintain a parallel pattern list.
    """

    pattern_selected = Signal(str)

    def __init__(self, translator: Translator | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator or Translator()
        self.pattern_list = QListWidget()
        self.pattern_list.setMinimumHeight(120)
        self.pattern_list.currentRowChanged.connect(self._row_changed)
        self.status = QLabel()
        self.status.setWordWrap(True)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.pattern_list)
        layout.addWidget(self.status)

    def set_patterns(self, patterns: tuple[StampPattern, ...],
                     icon_for: Callable[[int, str], QIcon]) -> None:
        self.pattern_list.blockSignals(True)
        self.pattern_list.clear()
        for pattern in patterns:
            item = QListWidgetItem(QIcon(stamp_pixmap(pattern, icon_for)),
                                   f"{pattern.definition_id}  ({pattern.width}×{pattern.height})")
            item.setData(Qt.ItemDataRole.UserRole, pattern.definition_id)
            item.setToolTip(self.translate("terrain_rule_pattern_tooltip",
                                           name=pattern.display_name or pattern.definition_id,
                                           width=pattern.width, height=pattern.height,
                                           count=len(pattern.cells)))
            self.pattern_list.addItem(item)
        self.pattern_list.blockSignals(False)
        self.status.setText(self.translate(
            "terrain_rule_patterns_hint" if patterns else "terrain_rule_patterns_empty"))

    def _row_changed(self, row: int) -> None:
        item = self.pattern_list.item(row) if row >= 0 else None
        if item is not None:
            self.pattern_selected.emit(str(item.data(Qt.ItemDataRole.UserRole)))
