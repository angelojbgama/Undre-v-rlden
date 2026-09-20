from __future__ import annotations

from collections.abc import Callable

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QIcon
from PySide6.QtWidgets import (
    QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QSpinBox,
    QToolButton, QVBoxLayout, QWidget,
)

from ...services.localization import Translator


class TerrainVariantEditor(QWidget):
    """Unlimited weighted 1x1 variant list for a floor rule.

    The 3x3 grid is a connectivity interface, not a variant limit: this
    editor models a floor rule as an open list of ``(sourceIndex, weight)``
    entries.  Identity is the atlas source index inside the rule's tileset —
    never a list position and never a grid slot.
    """

    variants_changed = Signal()
    weight_edited = Signal(int)
    add_requested = Signal()

    def __init__(self, icon_for: Callable[[int], QIcon],
                 translator: Translator | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator or Translator()
        self.icon_for = icon_for
        self._variants: list[tuple[int, int]] = []

        self.variant_list = QListWidget()
        self.variant_list.setMinimumHeight(120)
        self.variant_list.currentRowChanged.connect(self._row_changed)
        self.weight = QSpinBox()
        self.weight.setRange(1, 100)
        self.weight.setValue(1)
        self.weight.setSuffix(self.translate("terrain_rule_weight_suffix"))
        self.weight.valueChanged.connect(self._weight_changed)
        self.add_button = QToolButton()
        self.add_button.setText(self.translate("terrain_rule_add_variant"))
        self.add_button.setToolTip(self.translate("terrain_rule_click_atlas"))
        self.add_button.clicked.connect(self.add_requested)
        self.remove_button = QToolButton()
        self.remove_button.setText(self.translate("terrain_rule_clear_slot"))
        self.remove_button.clicked.connect(self._remove_selected)
        self.empty_hint = QLabel(self.translate("terrain_rule_variant_empty"))
        self.empty_hint.setWordWrap(True)

        weight_row = QHBoxLayout()
        weight_row.addWidget(QLabel(self.translate("terrain_rule_weight")))
        weight_row.addWidget(self.weight)
        weight_row.addStretch(1)
        buttons_row = QHBoxLayout()
        buttons_row.addWidget(self.add_button)
        buttons_row.addWidget(self.remove_button)
        buttons_row.addStretch(1)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.variant_list)
        layout.addWidget(self.empty_hint)
        layout.addLayout(weight_row)
        layout.addLayout(buttons_row)

    def variants(self) -> list[tuple[int, int]]:
        """Current ``(sourceIndex, weight)`` entries in stable source order."""
        return sorted(self._variants)

    def set_variants(self, variants: list[tuple[int, int]]) -> None:
        self._variants = [(int(source), max(1, int(weight))) for source, weight in variants]
        self._reload()

    def add_variant(self, source_index: int) -> bool:
        """Add one atlas tile; adding an existing tile selects it instead."""
        source_index = int(source_index)
        if any(source == source_index for source, _ in self._variants):
            self.select_source(source_index)
            return False
        self._variants.append((source_index, self.weight.value()))
        self._reload()
        self.select_source(source_index)
        return True

    def remove_variant(self, source_index: int) -> None:
        self._variants = [(source, weight) for source, weight in self._variants
                          if source != int(source_index)]
        self._reload()

    def select_source(self, source_index: int) -> None:
        for row in range(self.variant_list.count()):
            if self.variant_list.item(row).data(Qt.ItemDataRole.UserRole) == int(source_index):
                self.variant_list.setCurrentRow(row)
                return

    def selected_source(self) -> int | None:
        item = self.variant_list.currentItem()
        return None if item is None else int(item.data(Qt.ItemDataRole.UserRole))

    def _remove_selected(self) -> None:
        source = self.selected_source()
        if source is None:
            return
        self.remove_variant(source)
        self.variants_changed.emit()

    def _weight_changed(self, value: int) -> None:
        source = self.selected_source()
        if source is None:
            return
        self._variants = [(source_, value if source_ == source else weight)
                          for source_, weight in self._variants]
        self._reload()
        self.weight_edited.emit(value)

    def _row_changed(self, row: int) -> None:
        item = self.variant_list.item(row) if row >= 0 else None
        self.remove_button.setEnabled(item is not None)
        self.weight.setEnabled(item is not None)
        if item is not None:
            source = int(item.data(Qt.ItemDataRole.UserRole))
            weight = next((value for source_, value in self._variants if source_ == source), 1)
            self.weight.blockSignals(True)
            self.weight.setValue(weight)
            self.weight.blockSignals(False)
        self._update_hint()

    def _reload(self) -> None:
        previous = self.selected_source()
        self.variant_list.blockSignals(True)
        self.variant_list.clear()
        total = sum(weight for _, weight in self._variants) or 1
        for source_index, weight in sorted(self._variants):
            item = QListWidgetItem(self.icon_for(source_index),
                                   self.translate("terrain_rule_variant_item",
                                                  index=source_index, weight=weight,
                                                  percent=round(weight * 100 / total)))
            item.setData(Qt.ItemDataRole.UserRole, int(source_index))
            item.setToolTip(self.translate("terrain_rule_variant_tooltip", index=source_index,
                                           weight=weight, percent=round(weight * 100 / total)))
            self.variant_list.addItem(item)
        self.variant_list.blockSignals(False)
        if previous is not None:
            self.select_source(previous)
        self._row_changed(self.variant_list.currentRow())
        self._update_hint()
        self.variants_changed.emit()

    def _update_hint(self) -> None:
        self.empty_hint.setVisible(not self._variants)
