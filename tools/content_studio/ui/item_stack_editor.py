"""Reusable authored ItemStack editor.

Used first by object/chest initialContents, but intentionally independent
from chest semantics so it can later be reused by shops, rewards and banks.
"""

from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QComboBox,
    QDoubleSpinBox,
    QHBoxLayout,
    QPushButton,
    QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..services.item_authoring_service import ItemAuthoringService


class ItemStackEditor(QWidget):
    """Edit or create one {itemId, quantity} pair."""

    stack_changed = Signal(object)
    add_requested = Signal(object)
    remove_requested = Signal()

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        stack: dict[str, object] | None = None,
        *,
        add_mode: bool = False,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(
            parent
        )

        self.workspace = workspace
        self.add_mode = bool(
            add_mode
        )

        self.service = (
            ItemAuthoringService(
                workspace
            )
            if workspace is not None
            else None
        )

        self._loading = True

        self.item_picker = QComboBox(
            self
        )

        self.quantity = QDoubleSpinBox(
            self
        )

        self.quantity.setDecimals(
            0
        )

        self.quantity.setSingleStep(
            1
        )

        self.quantity.setRange(
            1,
            1,
        )

        self.action_button = QPushButton(
            self
        )

        if self.add_mode:
            self.item_picker.setObjectName(
                "initialContentsItemPicker"
            )

            self.quantity.setObjectName(
                "initialContentsAddQuantity"
            )

            self.action_button.setObjectName(
                "initialContentsAddButton"
            )

            self.action_button.setText(
                "Add Item"
            )
        else:
            self.item_picker.setObjectName(
                "initialContentsRequiredItemReference"
            )

            self.quantity.setObjectName(
                "initialContentsQuantity"
            )

            self.action_button.setObjectName(
                "initialContentsRemoveButton"
            )

            self.action_button.setText(
                "Remove"
            )

        layout = QHBoxLayout(
            self
        )

        layout.setContentsMargins(
            0,
            0,
            0,
            0,
        )

        layout.addWidget(
            self.item_picker,
            1,
        )

        layout.addWidget(
            self.quantity,
        )

        layout.addWidget(
            self.action_button,
        )

        self._populate_items()

        if stack is None:
            self._load_add_default()
        else:
            self._load_stack(
                stack
            )

        self.item_picker.currentIndexChanged.connect(
            self._item_changed
        )

        self.quantity.editingFinished.connect(
            self._quantity_edited
        )

        self.action_button.clicked.connect(
            self._action
        )

        self._loading = False

        self._sync_quantity()

    def _populate_items(
        self,
    ) -> None:
        if self.service is None:
            return

        try:
            items = self.service.items()
        except ValueError:
            return

        for definition in items:
            self.item_picker.addItem(
                f"{definition.display_name} "
                f"[{definition.definition_id}]",
                definition.definition_id,
            )

    def _load_add_default(
        self,
    ) -> None:
        if self.item_picker.count() > 0:
            self.item_picker.setCurrentIndex(
                0
            )

            self.quantity.setValue(
                1
            )

            return

        self.item_picker.addItem(
            "Create an Item in the Items tab first",
            None,
        )

        self.item_picker.setEnabled(
            False
        )

        self.quantity.setEnabled(
            False
        )

        self.action_button.setEnabled(
            False
        )

    def _load_stack(
        self,
        stack: dict[str, object],
    ) -> None:
        item_id = stack.get(
            "itemId"
        )

        quantity = stack.get(
            "quantity",
            1,
        )

        if (
            not isinstance(
                quantity,
                int,
            )
            or isinstance(
                quantity,
                bool,
            )
            or quantity <= 0
        ):
            quantity = 1

        self.quantity.setRange(
            1,
            float(
                max(
                    1,
                    quantity,
                )
            ),
        )

        self.quantity.setValue(
            float(
                quantity
            )
        )

        if (
            isinstance(
                item_id,
                str,
            )
            and item_id
        ):
            index = (
                self.item_picker.findData(
                    item_id
                )
            )

            if index < 0:
                self.item_picker.insertItem(
                    0,
                    f"Missing [{item_id}]",
                    item_id,
                )

                index = 0

            self.item_picker.setCurrentIndex(
                index
            )

            return

        self.item_picker.setCurrentIndex(
            -1
        )

        self.item_picker.setPlaceholderText(
            "Select an Item"
        )

    def _selected_stack_limit(
        self,
    ) -> int | None:
        item_id = (
            self.item_picker.currentData()
        )

        if (
            self.service is None
            or not isinstance(
                item_id,
                str,
            )
            or not item_id
        ):
            return None

        try:
            return self.service.stack_limit(
                item_id
            )
        except ValueError:
            return None

    def _sync_quantity(
        self,
    ) -> None:
        limit = self._selected_stack_limit()

        current = max(
            1,
            int(
                round(
                    self.quantity.value()
                )
            ),
        )

        self.quantity.blockSignals(
            True
        )

        if limit is None:
            self.quantity.setRange(
                1,
                float(
                    current
                ),
            )

            self.quantity.setValue(
                float(
                    current
                )
            )

            self.quantity.setEnabled(
                False
            )

            if self.add_mode:
                self.action_button.setEnabled(
                    False
                )
        else:
            self.quantity.setRange(
                1,
                float(
                    limit
                ),
            )

            self.quantity.setValue(
                float(
                    min(
                        current,
                        limit,
                    )
                )
            )

            self.quantity.setEnabled(
                True
            )

            if self.add_mode:
                self.action_button.setEnabled(
                    True
                )

        self.quantity.blockSignals(
            False
        )

    def current_stack(
        self,
    ) -> dict[str, object] | None:
        if self.service is None:
            return None

        item_id = (
            self.item_picker.currentData()
        )

        if (
            not isinstance(
                item_id,
                str,
            )
            or not item_id
        ):
            return None

        quantity = int(
            round(
                self.quantity.value()
            )
        )

        try:
            return dict(
                self.service.validate_stack(
                    item_id,
                    quantity,
                )
            )
        except ValueError:
            return None

    def _item_changed(
        self,
        unused: int,
    ) -> None:
        del unused

        if self._loading:
            return

        self._sync_quantity()

        if self.add_mode:
            return

        stack = self.current_stack()

        if stack is not None:
            self.stack_changed.emit(
                stack
            )

    def _quantity_edited(
        self,
    ) -> None:
        if (
            self._loading
            or self.add_mode
        ):
            return

        stack = self.current_stack()

        if stack is not None:
            self.stack_changed.emit(
                stack
            )

    def _action(
        self,
    ) -> None:
        if self.add_mode:
            stack = self.current_stack()

            if stack is not None:
                self.add_requested.emit(
                    stack
                )

            return

        self.remove_requested.emit()
