"""Studio preferences dialog (audit G11): language, theme and asset root."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtWidgets import (
    QComboBox, QDialog, QDialogButtonBox, QFileDialog, QFormLayout, QHBoxLayout,
    QLineEdit, QPushButton, QVBoxLayout, QWidget,
)

from ..model.types import ProjectPreferences
from ..services.localization import Translator
from . import theme
from .icon_registry import IconSize, icon


class SettingsDialog(QDialog):
    """Single place for language, theme and asset root preferences."""

    def __init__(self, preferences: ProjectPreferences, translator: Translator,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.translate = translator
        self._preferences = preferences
        self._theme_mode = preferences.theme

        self.language = QComboBox()
        for code, label in (("pt-BR", "Português (Brasil)"), ("en-US", "English")):
            self.language.addItem(label, code)
        if self.language.findData(preferences.language) >= 0:
            self.language.setCurrentIndex(self.language.findData(preferences.language))

        self.theme_mode = QComboBox()
        for mode, label in (
            (theme.THEME_SYSTEM, self.translate("theme_system")),
            (theme.THEME_LIGHT, self.translate("theme_light")),
            (theme.THEME_DARK, self.translate("theme_dark")),
        ):
            self.theme_mode.addItem(label, mode)
            self.theme_mode.setItemIcon(self.theme_mode.count() - 1, icon(f"theme_{mode}"))
        self.theme_mode.setIconSize(IconSize.SMALL)
        if self.theme_mode.findData(preferences.theme) >= 0:
            self.theme_mode.setCurrentIndex(self.theme_mode.findData(preferences.theme))

        self.asset_root = QLineEdit(preferences.asset_root)
        browse = QPushButton(self.translate("settings_browse"))
        browse.clicked.connect(self._browse)
        asset_row = QHBoxLayout()
        asset_row.addWidget(self.asset_root, 1)
        asset_row.addWidget(browse)

        form = QFormLayout()
        form.addRow(self.translate("settings_language"), self.language)
        form.addRow(self.translate("settings_theme"), self.theme_mode)
        form.addRow(self.translate("settings_asset_root"), asset_row)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(buttons)
        self.setWindowTitle(self.translate("settings_title"))
        self.setMinimumWidth(440)

    def _browse(self) -> None:
        directory = QFileDialog.getExistingDirectory(self, self.translate("settings_asset_root"))
        if directory:
            self.asset_root.setText(directory)

    def commit(self) -> ProjectPreferences:
        """Write the dialog values back into the preferences object."""
        self._preferences.language = str(self.language.currentData() or "pt-BR")
        self._preferences.theme = theme.normalize_mode(self.theme_mode.currentData())
        self._preferences.asset_root = self.asset_root.text().strip()
        return self._preferences
