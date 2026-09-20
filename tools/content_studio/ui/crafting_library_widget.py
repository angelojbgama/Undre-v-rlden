"""First-class Crafting Recipe authoring UI for the Content Studio."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QIcon, QPixmap
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.crafting_authoring_service import (
    CraftingAuthoringService,
    MAX_RECIPE_INPUTS,
    MAX_RECIPE_OUTPUTS,
)
from ..services.localization import Translator
from .icon_registry import icon
from .studio_visual_resolver import StudioVisualResolver
from .widgets import PayloadListWidget


CRAFTING_LIBRARY_MARKER = "crafting-library-ui-v1"


class ItemComboPicker(QComboBox):
    """Combo picker listing authored/builtin Items as display name + id."""

    def __init__(
        self,
        workspace: ContentWorkspace,
        resolver: StudioVisualResolver | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._workspace = workspace
        self._resolver = resolver
        self._reload()

    def set_workspace(self, workspace: ContentWorkspace) -> None:
        self._workspace = workspace
        self._reload()

    def _reload(self) -> None:
        current = self.currentData()
        self.clear()
        for item in self._workspace.definitions("items"):
            item_id = item.definition_id
            label = f"{item.display_name} ({item_id})" if item.display_name else item_id
            self.addItem(QIcon(), label, item_id)
            visual_id = item.data.get("visualId")
            if self._resolver is not None and isinstance(visual_id, str) and visual_id:
                resolved = self._resolver.resolve_static_sprite(visual_id)
                if resolved is not None and not resolved.image.isNull():
                    self.setItemIcon(self.count() - 1, QIcon(QPixmap.fromImage(resolved.image)))
        if current:
            self.setCurrentIndex(max(self.findData(current), 0))

    def selected_item_id(self) -> str:
        value = self.currentData()
        return value if isinstance(value, str) else ""


class IngredientRow(QWidget):
    """One editable [ item picker ][ quantity ][ remove ] row."""

    removed = Signal()

    def __init__(
        self,
        workspace: ContentWorkspace,
        resolver: StudioVisualResolver | None,
        translator: Translator,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.translate = translator
        self.picker = ItemComboPicker(workspace, resolver, self)
        self.quantity = QSpinBox(self)
        self.quantity.setRange(1, 999_999_999)
        self.quantity.setValue(1)
        self.remove_button = QPushButton(self)
        self.remove_button.setIcon(icon("delete"))
        self.remove_button.setToolTip(self.translate("recipe_remove"))
        self.remove_button.clicked.connect(self.removed.emit)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self.picker, 1)
        layout.addWidget(self.quantity)
        layout.addWidget(self.remove_button)

    def value(self) -> dict:
        return {
            "itemId": self.picker.selected_item_id(),
            "quantity": self.quantity.value(),
        }

    def load(self, item_id: str, quantity: int) -> None:
        self.picker.setCurrentIndex(max(self.picker.findData(item_id), 0))
        self.quantity.setValue(max(int(quantity), 1))


class CraftingRecipeDialog(QDialog):
    """Create or edit one authored Crafting Recipe."""

    def __init__(
        self,
        workspace: ContentWorkspace,
        asset_root: Path | None,
        translator: Translator,
        definition: ContentDefinition | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.translate = translator
        self.definition = definition
        self.service = CraftingAuthoringService(workspace)
        self.resolver = StudioVisualResolver()
        self.resolver.set_context(workspace, asset_root)
        self.created_recipe_id = ""

        self.name = QLineEdit(self)
        self.recipe_id = QLineEdit("recipe.new_recipe", self)

        self._input_rows: list[IngredientRow] = []
        self._output_rows: list[IngredientRow] = []

        self.inputs_host = QVBoxLayout()
        self.outputs_host = QVBoxLayout()

        self.add_input_button = QPushButton(self.translate("recipe_add_input"), self)
        self.add_input_button.clicked.connect(lambda: self._add_row(self._input_rows, self.inputs_host))
        self.add_output_button = QPushButton(self.translate("recipe_add_output"), self)
        self.add_output_button.clicked.connect(lambda: self._add_row(self._output_rows, self.outputs_host))

        self.preview = QLabel(self)
        self.preview.setProperty("muted", True)
        self.preview.setWordWrap(True)

        form = QFormLayout()
        form.addRow(self.translate("recipe_name"), self.name)
        form.addRow(self.translate("recipe_id"), self.recipe_id)
        inputs_group = QWidget(self)
        inputs_group.setLayout(self._wrapped(self.inputs_host, self.add_input_button))
        form.addRow(self.translate("recipe_inputs"), inputs_group)
        outputs_group = QWidget(self)
        outputs_group.setLayout(self._wrapped(self.outputs_host, self.add_output_button))
        form.addRow(self.translate("recipe_outputs"), outputs_group)
        form.addRow(self.translate("recipe_preview"), self.preview)

        self.buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok,
            parent=self,
        )
        self.buttons.accepted.connect(self._save)
        self.buttons.rejected.connect(self.reject)

        help_label = QLabel(self.translate("recipe_creation_help"), self)
        help_label.setWordWrap(True)
        help_label.setProperty("muted", True)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(help_label)
        layout.addStretch(1)
        layout.addWidget(self.buttons)

        if definition is not None:
            self._load_definition(definition)
            self.recipe_id.setReadOnly(True)
            self.setWindowTitle(self.translate("configure_recipe"))
        else:
            self.setWindowTitle(self.translate("create_recipe"))
            self.name.textChanged.connect(self._suggest_id)
            self._add_row(self._input_rows, self.inputs_host)
            self._add_row(self._input_rows, self.inputs_host)
            self._add_row(self._output_rows, self.outputs_host)

    @staticmethod
    def _wrapped(rows: QVBoxLayout, add_button: QPushButton) -> QVBoxLayout:
        host = QVBoxLayout()
        host.setContentsMargins(0, 0, 0, 0)
        host.addLayout(rows)
        host.addWidget(add_button)
        return host

    def _suggest_id(self, text: str) -> None:
        if self.recipe_id.isReadOnly() or self.recipe_id.text().startswith("recipe."):
            return
        slug = "".join(character if character.isalnum() else "_" for character in text.strip().lower())
        slug = "_".join(part for part in slug.split("_") if part)
        if slug:
            self.recipe_id.setText(f"recipe.{slug}")

    def _add_row(self, rows: list[IngredientRow], host: QVBoxLayout) -> IngredientRow:
        limit = MAX_RECIPE_INPUTS if rows is self._input_rows else MAX_RECIPE_OUTPUTS
        if len(rows) >= limit:
            return None
        row = IngredientRow(self.workspace, self.resolver, self.translate, self)
        row.removed.connect(lambda: self._remove_row(rows, host, row))
        rows.append(row)
        host.addWidget(row)
        self._sync_limits()
        return row

    def _remove_row(self, rows: list[IngredientRow], host: QVBoxLayout, row: IngredientRow) -> None:
        if len(rows) <= 1 and rows is self._output_rows:
            return
        if len(rows) <= 2 and rows is self._input_rows:
            return
        rows.remove(row)
        host.removeWidget(row)
        row.deleteLater()
        self._sync_limits()

    def _sync_limits(self) -> None:
        self.add_input_button.setEnabled(len(self._input_rows) < MAX_RECIPE_INPUTS)
        self.add_output_button.setEnabled(len(self._output_rows) < MAX_RECIPE_OUTPUTS)
        for row in self._input_rows:
            row.remove_button.setEnabled(len(self._input_rows) > 2)
        for row in self._output_rows:
            row.remove_button.setEnabled(len(self._output_rows) > 1)
        self._update_preview()

    def _update_preview(self) -> None:
        def side(rows: list[IngredientRow]) -> list[str]:
            result = []
            for row in rows:
                item = self.workspace.find("items", row.picker.selected_item_id())
                label = item.display_name if item else ""
                result.append(f"{row.quantity.value()}x {label or row.picker.selected_item_id() or '?'}")
            return result

        text = "\n+\n".join(side(self._input_rows))
        text += "\n\n↓\n\n"
        text += "\n+\n".join(side(self._output_rows))
        self.preview.setText(text)

    def _load_definition(self, definition: ContentDefinition) -> None:
        descriptor = self.workspace.find("authoringDescriptors", definition.definition_id)
        self.name.setText(
            str(descriptor.data.get("displayName", "")) if descriptor else definition.display_name)
        self.recipe_id.setText(definition.definition_id)
        for item_id, quantity, rows, host in self._rows_loader(definition):
            row = self._add_row(rows, host)
            if row is not None:
                row.load(item_id, quantity)
        self._sync_limits()

    def _rows_loader(self, definition: ContentDefinition):
        for entry in definition.data.get("inputs", []) or []:
            yield str(entry.get("itemId", "")), int(entry.get("quantity", 1)), self._input_rows, self.inputs_host
        for entry in definition.data.get("outputs", []) or []:
            yield str(entry.get("itemId", "")), int(entry.get("quantity", 1)), self._output_rows, self.outputs_host

    def _collect(self, rows: list[IngredientRow]) -> list[dict]:
        return [row.value() for row in rows]

    def _save(self) -> None:
        recipe_id = self.recipe_id.text().strip()
        inputs = self._collect(self._input_rows)
        outputs = self._collect(self._output_rows)
        try:
            if self.definition is None:
                self.service.create_recipe(
                    self.name.text(), recipe_id, inputs, outputs)
                self.created_recipe_id = self.service.find(recipe_id).definition_id
            else:
                self.service.update_recipe(
                    self.definition.definition_id,
                    display_name=self.name.text(),
                    inputs=inputs,
                    outputs=outputs,
                )
        except ValueError as error:
            QMessageBox.warning(self, "Crafting", str(error))
            return
        self.accept()


class CraftingLibraryWidget(QWidget):
    """Search, author and inspect Crafting Recipe definitions."""

    selected = Signal(object)
    changed = Signal()
    status_changed = Signal(str)

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None = None,
        translator: Translator | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.translate = translator or Translator()
        self.service = CraftingAuthoringService(workspace)
        self.resolver = StudioVisualResolver()

        self.search = QLineEdit(self)
        self.search.setPlaceholderText(self.translate("search_recipes"))
        self.search.textChanged.connect(self.refresh)

        self.recipes = PayloadListWidget(self)
        self.recipes.currentItemChanged.connect(self._selection_changed)

        self.create_button = QPushButton(self)
        self.create_button.setIcon(icon("add"))
        self.create_button.setToolTip(self.translate("create_recipe"))
        self.create_button.clicked.connect(self.create_recipe)

        self.configure_button = QPushButton(self)
        self.configure_button.setIcon(icon("configure"))
        self.configure_button.setToolTip(self.translate("configure_recipe"))
        self.configure_button.clicked.connect(self.configure_current)

        self.delete_button = QPushButton(self)
        self.delete_button.setIcon(icon("delete"))
        self.delete_button.setToolTip(self.translate("delete_recipe"))
        self.delete_button.clicked.connect(self.delete_current)

        buttons = QHBoxLayout()
        buttons.addWidget(self.create_button)
        buttons.addWidget(self.configure_button)
        buttons.addWidget(self.delete_button)
        buttons.addStretch(1)

        self.details = QLabel(self.translate("no_recipes"), self)
        self.details.setWordWrap(True)
        self.details.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.details.setMinimumSize(200, 200)
        self.details.setStyleSheet(
            "background:#161b22;"
            "color:#aeb8c4;"
            "border:1px solid #34404d;"
        )

        list_panel = QWidget(self)
        list_layout = QVBoxLayout(list_panel)
        list_layout.setContentsMargins(0, 0, 0, 0)
        list_layout.addWidget(self.search)
        list_layout.addWidget(self.recipes, 1)
        list_layout.addLayout(buttons)

        splitter = QSplitter(Qt.Orientation.Horizontal, self)
        splitter.addWidget(list_panel)
        splitter.addWidget(self.details)
        splitter.setStretchFactor(0, 1)

        layout = QVBoxLayout(self)
        layout.addWidget(splitter)

        self.refresh()

    # -- context -----------------------------------------------------------

    def set_context(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None = None,
    ) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.service.set_context(workspace)
        self.resolver.set_context(workspace, asset_root)
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.search.setPlaceholderText(self.translate("search_recipes"))
        self.create_button.setToolTip(self.translate("create_recipe"))
        self.configure_button.setToolTip(self.translate("configure_recipe"))
        self.delete_button.setToolTip(self.translate("delete_recipe"))
        self.refresh()

    # -- population --------------------------------------------------------

    def refresh(self) -> None:
        self.recipes.blockSignals(True)
        self.recipes.clear()
        query = self.search.text() if hasattr(self, "search") else ""
        for recipe in self.service.recipes(query):
            label = f"{recipe.display_name} ({recipe.definition_id})"
            item = QListWidgetItem(label, self.recipes)
            item.setData(Qt.ItemDataRole.UserRole, recipe)
        self.recipes.blockSignals(False)
        if self.recipes.count() == 0:
            self.details.setText(self.translate("no_recipes"))

    def _selection_changed(self, current: QListWidgetItem | None, _previous=None) -> None:
        definition = current.data(Qt.ItemDataRole.UserRole) if current else None
        self.selected.emit(definition)
        if definition is None:
            self.details.setText(self.translate("no_recipes"))
            return
        self._show_details(definition)

    def _show_details(self, definition: ContentDefinition) -> None:
        def describe(entries: object) -> list[str]:
            if not isinstance(entries, list):
                return []
            lines = []
            for entry in entries:
                if not isinstance(entry, dict):
                    continue
                item = self.workspace.find("items", str(entry.get("itemId", ""))) if self.workspace else None
                label = item.display_name if item else str(entry.get("itemId", ""))
                lines.append(f"{entry.get('quantity', 1)}x {label}")
            return lines

        inputs = describe(definition.data.get("inputs"))
        outputs = describe(definition.data.get("outputs"))
        text = f"<b>{definition.display_name}</b><br><code>{definition.definition_id}</code>"
        text += f"<br><br><b>{self.translate('recipe_inputs')}</b><br>" + "<br>".join(inputs)
        text += "<br>↓"
        text += f"<br><b>{self.translate('recipe_outputs')}</b><br>" + "<br>".join(outputs)
        self.details.setText(text)

    # -- CRUD ---------------------------------------------------------------

    def create_recipe(self) -> None:
        if self.workspace is None:
            return
        dialog = CraftingRecipeDialog(self.workspace, self.asset_root, self.translate, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("recipe_created"))
            self.refresh()

    def configure_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        dialog = CraftingRecipeDialog(
            self.workspace, self.asset_root, self.translate, definition=definition, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("recipe_configured"))
            self.refresh()

    def delete_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        confirm = QMessageBox.question(
            self,
            self.translate("delete_recipe"),
            self.translate("recipe_delete_confirm").format(recipe=definition.display_name),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if confirm != QMessageBox.StandardButton.Yes:
            return
        try:
            self.service.delete_recipe(definition.definition_id)
        except ValueError as error:
            QMessageBox.warning(self, self.translate("delete_recipe"), str(error))
            return
        self.changed.emit()
        self.status_changed.emit(self.translate("recipe_deleted"))
        self.refresh()

    def _current_definition(self) -> ContentDefinition | None:
        current = self.recipes.currentItem()
        return current.data(Qt.ItemDataRole.UserRole) if current else None
