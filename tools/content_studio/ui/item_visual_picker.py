"""Visual picker used by Item authoring dialogs."""

from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QLabel,
    QVBoxLayout,
)

from ..model.content_workspace import ContentWorkspace
from ..services.item_visual_service import ItemVisualService


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
            selected = self.service.select_static_sprite(
                str(definition_id)
            )

            self._selected_visual_id = (
                selected.definition_id
            )

            return self._selected_visual_id

        if kind == "animation":
            selected = self.service.materialize_animation_frame(
                self.item_id,
                str(definition_id),
                int(frame_index),
            )

            self._selected_visual_id = (
                selected.definition_id
            )

            return self._selected_visual_id

        raise ValueError(
            f"tipo de visual desconhecido: {kind}"
        )

    def selected_visual_id(
        self,
    ) -> str:
        return self._selected_visual_id

    def _accept_selection(
        self,
    ) -> None:
        visual_id = self.commit_selection()

        self.visual_selected.emit(
            visual_id
        )

        self.accept()