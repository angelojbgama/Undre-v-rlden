"""First-class Item authoring UI for the Content Studio."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QIcon, QImage, QPixmap
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QGridLayout,
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

from ..interaction.drag_payload import StudioDragPayload
from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..model.world_project import WorldProject
from ..services.item_authoring_service import (
    DEFAULT_STACK_LIMIT,
    ItemAuthoringService,
)
from ..services.localization import Translator
from ..services.item_visual_service import (
    ItemVisualSelection,
    ItemVisualService,
)
from .icon_registry import icon
from .item_visual_picker import ItemVisualPickerDialog
from .widgets import PayloadListWidget


ITEM_LIBRARY_MARKER = "item-library-ui-v1"

CATEGORY_KEYS = {
    "consumable": "item_category_consumable",
    "equipment": "item_category_equipment",
    "key": "item_category_key",
    "misc": "item_category_misc",
}


class ItemDefinitionDialog(QDialog):
    """Create or edit one authored Item."""

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
        self.asset_root = asset_root
        self.translate = translator
        self.definition = definition
        self.service = ItemAuthoringService(
            workspace
        )

        self.created_item_id = ""
        self._visual_id = ""
        self._visual_selection: ItemVisualSelection | None = None
        self._loading = True
        self._previous_category = "misc"

        self.name = QLineEdit(
            self
        )

        self.item_id = QLineEdit(
            "item.new_item",
            self,
        )

        self.visual_label = QLabel(
            self.translate(
                "item_visual_none"
            ),
            self,
        )

        self.visual_label.setWordWrap(
            True
        )

        self.choose_visual_button = QPushButton(
            self.translate(
                "item_choose_visual"
            ),
            self,
        )

        self.choose_visual_button.clicked.connect(
            self._choose_visual
        )

        visual_row = QWidget(
            self
        )

        visual_layout = QHBoxLayout(
            visual_row
        )

        visual_layout.setContentsMargins(
            0,
            0,
            0,
            0,
        )

        visual_layout.addWidget(
            self.visual_label,
            1,
        )

        visual_layout.addWidget(
            self.choose_visual_button,
        )

        self.category = QComboBox(
            self
        )

        for category in (
            "misc",
            "consumable",
            "equipment",
            "key",
        ):
            self.category.addItem(
                self.translate(
                    CATEGORY_KEYS[category]
                ),
                category,
            )

        self.stack_limit = QSpinBox(
            self
        )

        self.stack_limit.setRange(
            1,
            999_999_999,
        )

        self.stack_limit.setValue(
            DEFAULT_STACK_LIMIT
        )

        form = QFormLayout()

        form.addRow(
            self.translate(
                "item_name"
            ),
            self.name,
        )

        form.addRow(
            self.translate(
                "item_id"
            ),
            self.item_id,
        )

        form.addRow(
            self.translate(
                "item_visual"
            ),
            visual_row,
        )

        form.addRow(
            self.translate(
                "item_category"
            ),
            self.category,
        )

        form.addRow(
            self.translate(
                "item_stack_limit"
            ),
            self.stack_limit,
        )

        help_label = QLabel(
            self.translate(
                "item_creation_help"
            ),
            self,
        )

        help_label.setWordWrap(
            True
        )

        help_label.setProperty("muted", True)

        self.buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel
            | QDialogButtonBox.StandardButton.Ok,
            parent=self,
        )

        self.buttons.accepted.connect(
            self._save
        )

        self.buttons.rejected.connect(
            self.reject
        )

        layout = QVBoxLayout(
            self
        )

        layout.addLayout(
            form
        )

        layout.addWidget(
            help_label
        )

        layout.addStretch(
            1
        )

        layout.addWidget(
            self.buttons
        )

        self.category.currentIndexChanged.connect(
            self._category_changed
        )

        if definition is not None:
            self._load_definition(
                definition
            )

            self.item_id.setReadOnly(
                True
            )
        else:
            self.category.setCurrentIndex(
                self.category.findData(
                    "misc"
                )
            )

        self._loading = False
        self._previous_category = str(
            self.category.currentData()
            or "misc"
        )

        self._category_changed()

        self.setWindowTitle(
            self.translate(
                "configure_item"
                if definition is not None
                else "create_item"
            )
        )

        self.resize(
            620,
            320,
        )

    def _load_definition(
        self,
        definition: ContentDefinition,
    ) -> None:
        self.name.setText(
            definition.display_name
        )

        self.item_id.setText(
            definition.definition_id
        )

        visual_id = definition.data.get(
            "visualId"
        )

        if isinstance(
            visual_id,
            str,
        ):
            self.set_visual_id(
                visual_id
            )

        category = str(
            definition.data.get(
                "category",
                "misc",
            )
        )

        category_index = self.category.findData(
            category
        )

        if category_index >= 0:
            self.category.setCurrentIndex(
                category_index
            )

        stack = definition.data.get(
            "stackLimit",
            DEFAULT_STACK_LIMIT,
        )

        if (
            isinstance(stack, int)
            and not isinstance(stack, bool)
            and stack > 0
        ):
            self.stack_limit.setValue(
                min(
                    stack,
                    self.stack_limit.maximum(),
                )
            )

    def _update_visual_label(
        self,
    ) -> None:
        self.visual_label.setText(
            self._visual_id
            or self.translate(
                "item_visual_none"
            )
        )

    def set_visual_id(
        self,
        visual_id: str,
    ) -> None:
        self._visual_selection = None
        self._visual_id = (
            visual_id.strip()
        )
        self._update_visual_label()

    def set_visual_selection(
        self,
        selection: ItemVisualSelection | None,
    ) -> None:
        if selection is None:
            self._visual_selection = None
            return

        item_id = (
            self.item_id.text().strip()
        )

        prepared = ItemVisualService(
            self.workspace
        ).prepare_selection(
            item_id,
            selection,
        )

        self._visual_selection = selection
        self._visual_id = (
            prepared.visual_id
        )

        self._update_visual_label()

    def visual_id(
        self,
    ) -> str:
        return self._visual_id

    def _choose_visual(
        self,
    ) -> None:
        item_id = self.item_id.text().strip()

        if (
            not item_id.startswith("item.")
            or item_id == "item."
        ):
            QMessageBox.warning(
                self,
                self.windowTitle(),
                self.translate(
                    "item_id_before_visual"
                ),
            )

            return

        dialog = ItemVisualPickerDialog(
            self.workspace,
            item_id,
            self,
        )

        if (
            dialog.exec()
            != QDialog.DialogCode.Accepted
        ):
            return

        selection = dialog.selected_selection()

        if selection is not None:
            try:
                self.set_visual_selection(
                    selection
                )
            except ValueError as error:
                QMessageBox.warning(
                    self,
                    self.windowTitle(),
                    str(error),
                )

    def _category_changed(
        self,
        unused: object = None,
    ) -> None:
        del unused

        category = str(
            self.category.currentData()
            or "misc"
        )

        if category == "equipment":
            self.stack_limit.setValue(
                1
            )

            self.stack_limit.setEnabled(
                False
            )
        else:
            self.stack_limit.setEnabled(
                True
            )

            if not self._loading:
                if (
                    category == "key"
                    and self._previous_category != "key"
                ):
                    self.stack_limit.setValue(
                        1
                    )

                elif (
                    self._previous_category
                    in {
                        "equipment",
                        "key",
                    }
                    and category
                    in {
                        "misc",
                        "consumable",
                    }
                    and self.stack_limit.value() == 1
                ):
                    self.stack_limit.setValue(
                        DEFAULT_STACK_LIMIT
                    )

        self._previous_category = category

    def commit(
        self,
    ) -> ContentDefinition:
        item_id = self.item_id.text().strip()

        visual_id = self._visual_id
        generated_visual = None

        if self._visual_selection is not None:
            prepared = ItemVisualService(
                self.workspace
            ).prepare_selection(
                item_id,
                self._visual_selection,
            )

            visual_id = (
                prepared.visual_id
            )

            generated_visual = (
                prepared.generated_data
            )

        if self.definition is None:
            result = self.service.create_item(
                self.name.text(),
                item_id,
                visual_id,
                str(
                    self.category.currentData()
                    or "misc"
                ),
                self.stack_limit.value(),
                generated_visual=generated_visual,
            )
        else:
            result = self.service.update_item(
                self.definition.definition_id,
                display_name=self.name.text(),
                visual_id=visual_id,
                category=str(
                    self.category.currentData()
                    or "misc"
                ),
                stack_limit=self.stack_limit.value(),
                generated_visual=generated_visual,
            )

        self._visual_id = visual_id
        self._visual_selection = None
        self._update_visual_label()

        self.created_item_id = (
            result.definition_id
        )

        return result

    def _save(
        self,
    ) -> None:
        try:
            self.commit()
        except ValueError as error:
            QMessageBox.warning(
                self,
                self.windowTitle(),
                str(error),
            )

            return

        self.accept()


class ItemLibraryWidget(QWidget):
    """Search, author and place Item definitions."""

    selected = Signal(object)
    place_requested = Signal(str, str)
    changed = Signal()
    status_changed = Signal(str)

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None,
        translator: Translator | None = None,
        project: WorldProject | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(
            parent
        )

        self.workspace = workspace
        self.asset_root = asset_root
        self.project = project
        self.translate = (
            translator
            or Translator()
        )

        self.service = ItemAuthoringService(
            workspace
        )

        self.search = QLineEdit(
            self
        )

        self.search.textChanged.connect(
            self.refresh
        )

        self.items = PayloadListWidget(
            self
        )

        self.items.payload_factory = (
            self._drag_payload
        )

        self.items.currentItemChanged.connect(
            self._selection_changed
        )

        self.create_button = QPushButton(
            self
        )

        self.create_button.setIcon(
            icon("add")
        )

        self.create_button.clicked.connect(
            self.create_item
        )

        self.configure_button = QPushButton(
            self
        )

        self.configure_button.setIcon(
            icon("configure")
        )

        self.configure_button.clicked.connect(
            self.configure_current
        )

        self.delete_button = QPushButton(
            self
        )

        self.delete_button.setIcon(
            icon("delete")
        )

        self.delete_button.clicked.connect(
            self.delete_current
        )

        self.place_button = QPushButton(
            self
        )

        self.place_button.setIcon(
            icon("place")
        )

        self.place_button.clicked.connect(
            self.place_current
        )

        self.preview = QLabel(
            self.translate(
                "no_image"
            ),
            self,
        )

        self.preview.setAlignment(
            Qt.AlignmentFlag.AlignCenter
        )

        self.preview.setMinimumSize(
            300,
            300,
        )

        self.preview.setStyleSheet(
            "background:#161b22;"
            "color:#aeb8c4;"
            "border:1px solid #34404d;"
        )

        self.details = QLabel(
            self,
        )

        self.details.setWordWrap(
            True
        )

        # Full-width rows keep the PT labels readable at the default
        # narrow panel width; destructive action goes last
        # (see UI/UX audit G6).
        buttons = QGridLayout()

        buttons.addWidget(
            self.create_button,
            0,
            0,
        )

        buttons.addWidget(
            self.configure_button,
            0,
            1,
        )

        buttons.addWidget(
            self.place_button,
            1,
            0,
            1,
            2,
        )

        buttons.addWidget(
            self.delete_button,
            2,
            0,
            1,
            2,
        )

        left = QWidget(
            self
        )

        left_layout = QVBoxLayout(
            left
        )

        left_layout.setContentsMargins(
            0,
            0,
            0,
            0,
        )

        left_layout.addWidget(
            self.search
        )

        left_layout.addWidget(
            self.items,
            1,
        )

        left_layout.addLayout(
            buttons
        )

        right = QWidget(
            self
        )

        right_layout = QVBoxLayout(
            right
        )

        right_layout.setContentsMargins(
            0,
            0,
            0,
            0,
        )

        right_layout.addWidget(
            self.preview,
            1,
        )

        right_layout.addWidget(
            self.details
        )

        splitter = QSplitter(
            Qt.Orientation.Horizontal,
            self,
        )

        splitter.addWidget(
            left
        )

        splitter.addWidget(
            right
        )

        splitter.setStretchFactor(
            1,
            1,
        )

        splitter.setSizes(
            [
                320,
                620,
            ]
        )

        layout = QVBoxLayout(
            self
        )

        layout.addWidget(
            splitter
        )

        self.retranslate(
            self.translate
        )

    def set_context(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None,
        project: WorldProject | None = None,
    ) -> None:
        self.workspace = workspace
        self.asset_root = asset_root

        if project is not None:
            self.project = project

        self.service.set_context(
            workspace
        )

        self.refresh()

    def retranslate(
        self,
        translator: Translator,
    ) -> None:
        self.translate = translator

        self.search.setPlaceholderText(
            self.translate(
                "search_items"
            )
        )

        self.create_button.setText(
            self.translate(
                "create_item"
            )
        )

        self.configure_button.setText(
            self.translate(
                "configure_item"
            )
        )

        self.delete_button.setText(
            self.translate(
                "delete_item"
            )
        )

        self.place_button.setText(
            self.translate(
                "place_item"
            )
        )

        self.refresh()

    def refresh(
        self,
        unused: object = None,
    ) -> None:
        del unused

        current_id = self._current_id()

        self.items.blockSignals(
            True
        )

        self.items.clear()

        definitions = (
            self.service.items(
                self.search.text()
            )
            if self.workspace is not None
            else ()
        )

        for definition in definitions:
            category = str(
                definition.data.get(
                    "category",
                    "misc",
                )
            )

            category_label = self.translate(
                CATEGORY_KEYS.get(
                    category,
                    "item_category_misc",
                )
            )

            entry = QListWidgetItem(
                f"{definition.display_name}  "
                f"[{definition.definition_id}]"
                f"  —  {category_label}"
            )

            entry.setData(
                Qt.ItemDataRole.UserRole,
                definition.definition_id,
            )

            icon = self._item_icon(
                definition
            )

            if not icon.isNull():
                entry.setIcon(
                    QIcon(
                        icon
                    )
                )

            self.items.addItem(
                entry
            )

            if (
                definition.definition_id
                == current_id
            ):
                self.items.setCurrentItem(
                    entry
                )

        if (
            self.items.currentItem()
            is None
            and self.items.count()
        ):
            self.items.setCurrentRow(
                0
            )

        self.items.blockSignals(
            False
        )

        self._selection_changed(
            self.items.currentItem(),
            None,
        )

    def create_item(
        self,
    ) -> None:
        if self.workspace is None:
            return

        if (
            not self.workspace.definitions(
                "staticSprites"
            )
            and not self.workspace.definitions(
                "animations"
            )
        ):
            QMessageBox.information(
                self,
                self.translate(
                    "create_item"
                ),
                self.translate(
                    "item_needs_visual"
                ),
            )

            return

        dialog = ItemDefinitionDialog(
            self.workspace,
            self.asset_root,
            self.translate,
            parent=self,
        )

        if (
            dialog.exec()
            != QDialog.DialogCode.Accepted
        ):
            return

        self.refresh()
        self.select_item(
            dialog.created_item_id
        )

        self.changed.emit()

        self.status_changed.emit(
            self.translate(
                "item_created"
            )
        )

    def configure_current(
        self,
    ) -> None:
        if self.workspace is None:
            return

        definition = self.workspace.find(
            "items",
            self._current_id(),
        )

        if (
            definition is None
            or definition.origin
            != "project"
        ):
            return

        dialog = ItemDefinitionDialog(
            self.workspace,
            self.asset_root,
            self.translate,
            definition=definition,
            parent=self,
        )

        if (
            dialog.exec()
            != QDialog.DialogCode.Accepted
        ):
            return

        self.refresh()

        self.select_item(
            dialog.created_item_id
        )

        self.changed.emit()

        self.status_changed.emit(
            self.translate(
                "item_configured"
            )
        )

    def delete_current(
        self,
        unused: object = None,
        *,
        confirm: bool = True,
    ) -> bool:
        del unused

        if self.workspace is None:
            return False

        item_id = self._current_id()

        definition = self.workspace.find(
            "items",
            item_id,
        )

        if (
            definition is None
            or definition.origin
            != "project"
        ):
            return False

        pickup = self.service.pickup_for_item(
            item_id
        )

        pickup_id = (
            pickup.definition_id
            if pickup is not None
            else ""
        )

        map_usages = self._project_usages(
            {
                item_id,
                pickup_id,
            }
        )

        if map_usages:
            QMessageBox.warning(
                self,
                self.translate(
                    "delete_item"
                ),
                self.translate(
                    "item_delete_map_usage",
                    maps=", ".join(
                        map_usages
                    ),
                ),
            )

            return False

        if confirm:
            answer = QMessageBox.question(
                self,
                self.translate(
                    "delete_item"
                ),
                self.translate(
                    "item_delete_confirm",
                    item=definition.display_name,
                ),
                QMessageBox.StandardButton.Yes
                | QMessageBox.StandardButton.No,
            )

            if (
                answer
                != QMessageBox.StandardButton.Yes
            ):
                return False

        try:
            self.service.delete_item(
                item_id
            )
        except ValueError as error:
            QMessageBox.warning(
                self,
                self.translate(
                    "delete_item"
                ),
                str(error),
            )

            return False

        self.refresh()

        self.changed.emit()

        self.status_changed.emit(
            self.translate(
                "item_deleted"
            )
        )

        return True

    def place_current(
        self,
    ) -> None:
        if self.workspace is None:
            return

        item_id = self._current_id()

        if not item_id:
            return

        pickup = self.service.pickup_for_item(
            item_id
        )

        if pickup is None:
            self.status_changed.emit(
                self.translate(
                    "item_missing_pickup"
                )
            )

            return

        self.place_requested.emit(
            "pickups",
            pickup.definition_id,
        )

    def select_item(
        self,
        item_id: str,
    ) -> None:
        for index in range(
            self.items.count()
        ):
            item = self.items.item(
                index
            )

            if (
                item.data(
                    Qt.ItemDataRole.UserRole
                )
                == item_id
            ):
                self.items.setCurrentItem(
                    item
                )

                return

    def _current_id(
        self,
    ) -> str:
        current = self.items.currentItem()

        return (
            str(
                current.data(
                    Qt.ItemDataRole.UserRole
                )
            )
            if current
            else ""
        )

    def _selection_changed(
        self,
        current: QListWidgetItem | None,
        unused: QListWidgetItem | None,
    ) -> None:
        del unused

        item_id = (
            str(
                current.data(
                    Qt.ItemDataRole.UserRole
                )
            )
            if current
            else ""
        )

        definition = (
            self.workspace.find(
                "items",
                item_id,
            )
            if (
                self.workspace is not None
                and item_id
            )
            else None
        )

        editable = (
            definition is not None
            and definition.origin
            == "project"
        )

        self.configure_button.setEnabled(
            editable
        )

        self.delete_button.setEnabled(
            editable
        )

        self.place_button.setEnabled(
            definition is not None
        )

        self.selected.emit(
            definition
        )

        self._show_item(
            definition
        )

    def _show_item(
        self,
        definition: ContentDefinition | None,
    ) -> None:
        if definition is None:
            self.preview.setPixmap(
                QPixmap()
            )

            self.preview.setText(
                self.translate(
                    "no_items"
                )
            )

            self.details.clear()

            return

        pixmap = self._item_icon(
            definition,
            280,
        )

        if pixmap.isNull():
            self.preview.setPixmap(
                QPixmap()
            )

            self.preview.setText(
                self.translate(
                    "image_unavailable"
                )
            )
        else:
            self.preview.setText(
                ""
            )

            self.preview.setPixmap(
                pixmap
            )

        category = str(
            definition.data.get(
                "category",
                "misc",
            )
        )

        stack = definition.data.get(
            "stackLimit",
            1,
        )

        pickup = self.service.pickup_for_item(
            definition.definition_id
        )

        pickup_id = (
            pickup.definition_id
            if pickup is not None
            else self.translate(
                "item_missing_pickup"
            )
        )

        self.details.setText(
            "\n".join(
                [
                    definition.display_name,
                    definition.definition_id,
                    self.translate(
                        CATEGORY_KEYS.get(
                            category,
                            "item_category_misc",
                        )
                    ),
                    self.translate(
                        "item_stack_summary",
                        count=stack,
                    ),
                    self.translate(
                        "item_pickup_summary",
                        pickup=pickup_id,
                    ),
                ]
            )
        )

    def _item_icon(
        self,
        definition: ContentDefinition,
        size: int = 48,
    ) -> QPixmap:
        if self.workspace is None:
            return QPixmap()

        visual_id = definition.data.get(
            "visualId"
        )

        if not isinstance(
            visual_id,
            str,
        ):
            return QPixmap()

        sprite = self.workspace.find(
            "staticSprites",
            visual_id,
        )

        if sprite is None:
            return QPixmap()

        image_id = sprite.data.get(
            "imageId"
        )

        if not isinstance(
            image_id,
            str,
        ):
            return QPixmap()

        image_definition = self.workspace.find(
            "visualImages",
            image_id,
        )

        if image_definition is None:
            return QPixmap()

        relative = image_definition.data.get(
            "relativePath"
        )

        if not isinstance(
            relative,
            str,
        ):
            return QPixmap()

        root = (
            self.asset_root
            if image_definition.data.get(
                "root"
            ) == "gameAssets"
            else self.workspace.root
        )

        if root is None:
            return QPixmap()

        image = QImage(
            str(
                root / relative
            )
        )

        if image.isNull():
            return QPixmap()

        source = sprite.data.get(
            "source"
        )

        if not isinstance(
            source,
            dict,
        ):
            return QPixmap()

        width = int(
            source.get(
                "width",
                0,
            )
        )

        height = int(
            source.get(
                "height",
                0,
            )
        )

        if (
            width <= 0
            or height <= 0
        ):
            return QPixmap()

        crop = image.copy(
            int(
                source.get(
                    "x",
                    0,
                )
            ),
            int(
                source.get(
                    "y",
                    0,
                )
            ),
            width,
            height,
        )

        if crop.isNull():
            return QPixmap()

        return QPixmap.fromImage(
            crop
        ).scaled(
            size,
            size,
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation,
        )

    def _drag_payload(
        self,
        items: list[QListWidgetItem],
    ) -> StudioDragPayload | None:
        if (
            not items
            or self.workspace is None
        ):
            return None

        item_id = str(
            items[0].data(
                Qt.ItemDataRole.UserRole
            )
        )

        pickup = self.service.pickup_for_item(
            item_id
        )

        if pickup is None:
            return None

        return StudioDragPayload.content(
            "pickups",
            pickup.definition_id,
        )

    def _project_usages(
        self,
        references: set[str],
    ) -> list[str]:
        references = {
            value
            for value in references
            if value
        }

        if (
            not references
            or self.project is None
        ):
            return []

        result: list[str] = []

        for document in self.project.maps:
            if self._contains_reference(
                document.data,
                references,
            ):
                result.append(
                    document.map_id
                )

        return result

    @classmethod
    def _contains_reference(
        cls,
        value: object,
        references: set[str],
    ) -> bool:
        if isinstance(
            value,
            str,
        ):
            return value in references

        if isinstance(
            value,
            list,
        ):
            return any(
                cls._contains_reference(
                    child,
                    references,
                )
                for child in value
            )

        if isinstance(
            value,
            dict,
        ):
            return any(
                cls._contains_reference(
                    child,
                    references,
                )
                for child in value.values()
            )

        return False