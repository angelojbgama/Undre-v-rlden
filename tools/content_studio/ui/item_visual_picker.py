"""Visual picker used by Item authoring dialogs."""

from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QLabel,
    QMessageBox,
    QVBoxLayout,
)

from ..model.content_workspace import ContentWorkspace
from ..services.item_visual_service import (
    ItemVisualSelection,
    ItemVisualService,
)


class ItemVisualPickerDialog(QDialog):
    """Choose an existing StaticSprite or materialize an Animation frame."""

    visual_selected = Signal(str)

    def __init__(
        self,
        workspace: ContentWorkspace,
        item_id: str,
        parent=None,
    ) -> None:
        super().__init__(
            parent
        )

        self.workspace = workspace
        self.item_id = item_id
        self.service = ItemVisualService(
            workspace
        )

        self._selected_visual_id = ""
        self._selected_selection: ItemVisualSelection | None = None

        self.setWindowTitle(
            "Selecionar visual do item"
        )

        self.resize(
            520,
            180,
        )

        layout = QVBoxLayout(
            self
        )

        description = QLabel(
            "Use um StaticSprite existente ou um frame de animação."
        )

        description.setWordWrap(
            True
        )

        layout.addWidget(
            description
        )

        self.visuals = QComboBox(
            self
        )

        layout.addWidget(
            self.visuals
        )

        self._populate()

        self.buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel,
            parent=self,
        )

        self.buttons.accepted.connect(
            self._accept_selection
        )

        self.buttons.rejected.connect(
            self.reject
        )

        layout.addWidget(
            self.buttons
        )

        ok_button = self.buttons.button(
            QDialogButtonBox.StandardButton.Ok
        )

        if ok_button is not None:
            ok_button.setEnabled(
                self.visuals.count() > 0
            )

    def _populate(
        self,
    ) -> None:
        self.visuals.clear()

        for definition in self.service.static_sprites():
            self.visuals.addItem(
                f"Sprite: {definition.display_name} "
                f"({definition.definition_id})",
                (
                    "static",
                    definition.definition_id,
                    -1,
                ),
            )

        for frame in self.service.animation_frames():
            self.visuals.addItem(
                f"Frame: {frame.animation_id} "
                f"#{frame.frame_index}",
                (
                    "animation",
                    frame.animation_id,
                    frame.frame_index,
                ),
            )

    def commit_selection(
        self,
    ) -> str:
        """Prepare the selected visual without changing the workspace."""

        data = self.visuals.currentData()

        if (
            not isinstance(data, tuple)
            or len(data) != 3
        ):
            raise ValueError(
                "nenhum visual selecionado"
            )

        kind, definition_id, frame_index = data

        if kind == "static":
            selection = ItemVisualSelection(
                kind="static",
                definition_id=str(
                    definition_id
                ),
            )

        elif kind == "animation":
            selection = ItemVisualSelection(
                kind="animation",
                definition_id=str(
                    definition_id
                ),
                frame_index=int(
                    frame_index
                ),
            )

        else:
            raise ValueError(
                f"tipo de visual desconhecido: {kind}"
            )

        prepared = self.service.prepare_selection(
            self.item_id,
            selection,
        )

        self._selected_selection = selection
        self._selected_visual_id = (
            prepared.visual_id
        )

        return self._selected_visual_id

    def selected_selection(
        self,
    ) -> ItemVisualSelection | None:
        return self._selected_selection

    def selected_visual_id(
        self,
    ) -> str:
        return self._selected_visual_id

    def _accept_selection(
        self,
    ) -> None:
        try:
            visual_id = (
                self.commit_selection()
            )
        except ValueError as error:
            QMessageBox.warning(
                self,
                self.windowTitle(),
                str(error),
            )
            return

        self.visual_selected.emit(
            visual_id
        )

        self.accept()
