"""Structured diagnostics panel for the Content Studio (audit S2).

Replaces the raw text dump: every diagnostic renders with a severity
icon, relative source path in the tooltip and, when the diagnostic
carries a definition id, double-click navigation to that definition.
"""

from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor
from PySide6.QtWidgets import QComboBox, QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QVBoxLayout, QWidget

from ..model.types import Diagnostic
from .icon_registry import IconSize, icon
from ..services.localization import Translator

_SEVERITY_ICONS = {
    "error": "diag_error",
    "warning": "diag_warning",
    "info": "diag_info",
}

_SEVERITY_COLORS = {
    "error": "#e5534b",
    "warning": "#d9a017",
    "info": None,  # theme text color
}

_FILTERS = (("all", "filter_all"), ("error", "filter_errors"),
            ("warning", "filter_warnings"), ("info", "filter_info"))


class DiagnosticsPanel(QWidget):
    """Severity-colored diagnostics list with filter and navigation."""

    definition_requested = Signal(str)

    def __init__(self, parent: QWidget | None = None, translator: Translator | None = None) -> None:
        super().__init__(parent)
        self.translate = translator or Translator()
        self._diagnostics: list[Diagnostic] = []
        self._filter = "all"

        self.title = QLabel(self.translate("diagnostics_title"))
        self.counts = QLabel()
        self.counts.setProperty("muted", True)
        self.filter = QComboBox()
        for _value, label_key in _FILTERS:
            self.filter.addItem(self.translate(label_key))
        self.filter.currentIndexChanged.connect(self._filter_changed)

        header = QHBoxLayout()
        header.addWidget(self.title)
        header.addStretch(1)
        header.addWidget(self.counts)
        header.addWidget(self.filter)

        self.list = QListWidget()
        self.list.setUniformItemSizes(False)
        self.list.setIconSize(IconSize.SMALL)
        self.list.itemActivated.connect(self._item_activated)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 2, 4, 4)
        layout.setSpacing(2)
        layout.addLayout(header)
        layout.addWidget(self.list)

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.title.setText(self.translate("diagnostics_title"))
        for index, (_value, label_key) in enumerate(_FILTERS):
            self.filter.setItemText(index, self.translate(label_key))
        self.refresh()

    def set_diagnostics(self, diagnostics: list[Diagnostic]) -> None:
        self._diagnostics = list(diagnostics)
        self.refresh()

    def _filter_changed(self, index: int) -> None:
        self._filter = _FILTERS[max(0, index)][0]
        self.refresh()

    def _severity_counts(self) -> dict[str, int]:
        counts = {"error": 0, "warning": 0, "info": 0}
        for diagnostic in self._diagnostics:
            severity = diagnostic.severity.lower()
            counts[severity] = counts.get(severity, 0) + 1
        return counts

    def refresh(self) -> None:
        self.list.clear()
        counts = self._severity_counts()
        self.counts.setText(self.translate("diagnostics_counts").format(**counts))
        for diagnostic in self._diagnostics:
            severity = diagnostic.severity.lower()
            if self._filter != "all" and severity != self._filter:
                continue
            icon_name = _SEVERITY_ICONS.get(severity, "diag_info")
            item = QListWidgetItem(icon(icon_name), diagnostic.message)
            color = _SEVERITY_COLORS.get(severity)
            if color:
                item.setForeground(QColor(color))
            source = diagnostic.source_path
            if source is not None:
                item.setToolTip(f"{severity}: {source}")
            elif diagnostic.path:
                item.setToolTip(f"{severity}: {diagnostic.path}")
            item.setData(Qt.ItemDataRole.UserRole, diagnostic.definition_id)
            self.list.addItem(item)
        if not self._diagnostics:
            empty = QListWidgetItem(self.translate("diagnostics_empty"))
            empty.setForeground(Qt.GlobalColor.gray)
            self.list.addItem(empty)

    def _item_activated(self, item: QListWidgetItem) -> None:
        definition_id = str(item.data(Qt.ItemDataRole.UserRole) or "")
        if definition_id:
            self.definition_requested.emit(definition_id)
