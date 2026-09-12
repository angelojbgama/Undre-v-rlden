from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QRect, QTimer, Qt, Signal
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import (
    QComboBox, QDialog, QDialogButtonBox, QFormLayout, QGroupBox, QHBoxLayout,
    QLabel, QLineEdit, QListWidget, QListWidgetItem, QMessageBox, QPushButton,
    QSplitter, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.localization import Translator
from ..services.player_authoring_service import (
    PlayerAuthoringRequest, PlayerAuthoringService,
)


_DIRECTIONS = ("down", "up", "side")
_ACTIONS = ("idle", "walk", "hurt", "sword", "bow")


class PlayerDefinitionDialog(QDialog):
    """Dedicated Player profile editor built from existing animations."""

    def __init__(self, workspace: ContentWorkspace, asset_root: Path | None,
                 translator: Translator,
                 definition: ContentDefinition | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.translate = translator
        self.definition = definition
        self.service = PlayerAuthoringService()
        self.created_player_id = ""

        self.name = QLineEdit()
        self.player_id = QLineEdit("player.hero")
        self.progression = QComboBox()
        self.progression.setEditable(True)
        self.progression.addItem(
            "progression.player.default", "progression.player.default")
        for value in workspace.definitions("playerProgressions"):
            if self.progression.findData(value.definition_id) < 0:
                self.progression.addItem(
                    f"{value.display_name}  [{value.definition_id}]",
                    value.definition_id)

        self.animation_fields: dict[str, dict[str, QComboBox]] = {}
        animations = workspace.definitions("animations")
        visual_layout = QVBoxLayout()
        for action in _ACTIONS:
            optional = action not in {"idle", "walk"}
            group = QGroupBox(self.translate(f"player_{action}"))
            form = QFormLayout(group)
            fields: dict[str, QComboBox] = {}
            for direction in _DIRECTIONS:
                combo = QComboBox()
                if optional:
                    combo.addItem(
                        self.translate("player_animation_none"), "")
                for animation in animations:
                    combo.addItem(
                        f"{animation.display_name}  "
                        f"[{animation.definition_id}]",
                        animation.definition_id)
                combo.currentIndexChanged.connect(self._preview_reset)
                fields[direction] = combo
                form.addRow(
                    self.translate(f"player_direction_{direction}"), combo)
            self.animation_fields[action] = fields
            visual_layout.addWidget(group)
        visual_layout.addStretch(1)

        identity = QGroupBox(self.translate("player_identity"))
        identity_form = QFormLayout(identity)
        identity_form.addRow(self.translate("player_name"), self.name)
        identity_form.addRow(self.translate("player_id"), self.player_id)
        identity_form.addRow(
            self.translate("player_progression"), self.progression)

        self.preview_action = QComboBox()
        for action in _ACTIONS:
            self.preview_action.addItem(
                self.translate(f"player_{action}"), action)
        self.preview_direction = QComboBox()
        for direction in _DIRECTIONS:
            self.preview_direction.addItem(
                self.translate(f"player_direction_{direction}"), direction)
        self.preview_action.currentIndexChanged.connect(self._preview_reset)
        self.preview_direction.currentIndexChanged.connect(self._preview_reset)

        preview_controls = QHBoxLayout()
        preview_controls.addWidget(
            QLabel(self.translate("player_preview_action")))
        preview_controls.addWidget(self.preview_action)
        preview_controls.addWidget(
            QLabel(self.translate("player_preview_direction")))
        preview_controls.addWidget(self.preview_direction)
        preview_controls.addStretch(1)

        self.preview = QLabel(self.translate("no_image"))
        self.preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview.setMinimumSize(360, 360)
        self.preview.setStyleSheet(
            "background: #161b22; color: #aeb8c4; "
            "border: 1px solid #34404d;")
        self.preview_help = QLabel(self.translate("player_visual_help"))
        self.preview_help.setWordWrap(True)
        self.preview_help.setStyleSheet("color: #aeb8c4;")

        self._preview_frames: list[dict[str, object]] = []
        self._preview_image = QImage()
        self._preview_index = 0
        self._preview_timer = QTimer(self)
        self._preview_timer.setSingleShot(True)
        self._preview_timer.timeout.connect(self._advance_preview)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel |
            QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)

        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.addWidget(identity)
        left_layout.addLayout(visual_layout)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.addLayout(preview_controls)
        right_layout.addWidget(self.preview, 1)
        right_layout.addWidget(self.preview_help)

        split = QSplitter(Qt.Orientation.Horizontal)
        split.addWidget(left)
        split.addWidget(right)
        split.setStretchFactor(1, 1)
        split.setSizes([520, 520])

        layout = QVBoxLayout(self)
        layout.addWidget(split, 1)
        layout.addWidget(buttons)
        self.resize(1120, 820)
        self.setWindowTitle(
            self.translate("configure_player")
            if definition else self.translate("create_player"))

        if definition is not None:
            self._load_definition(definition)
            self.player_id.setReadOnly(True)
        else:
            self._suggest_defaults()
        self._preview_reset()

    def _suggest_defaults(self) -> None:
        candidates = {
            "idle": ("player.idle", "idle.player"),
            "walk": ("player.walk", "walk.player"),
            "hurt": ("player.hurt", "hurt.player"),
            "sword": ("player.sword", "sword.player"),
            "bow": ("player.bow", "bow.player"),
        }
        for action, stems in candidates.items():
            for direction in _DIRECTIONS:
                combo = self.animation_fields[action][direction]
                for index in range(combo.count()):
                    definition_id = str(combo.itemData(index) or "")
                    lowered = definition_id.casefold()
                    if (any(stem in lowered for stem in stems)
                            and direction in lowered):
                        combo.setCurrentIndex(index)
                        break

    def _set_combo(self, combo: QComboBox, definition_id: str) -> None:
        index = combo.findData(definition_id)
        if index >= 0:
            combo.setCurrentIndex(index)

    def _load_definition(self, definition: ContentDefinition) -> None:
        request = self.service.request_for(self.workspace, definition)
        self.name.setText(request.display_name)
        self.player_id.setText(request.player_id)
        index = self.progression.findData(request.progression_id)
        if index >= 0:
            self.progression.setCurrentIndex(index)
        else:
            self.progression.setEditText(request.progression_id)
        for action in _ACTIONS:
            for direction in _DIRECTIONS:
                self._set_combo(
                    self.animation_fields[action][direction],
                    str(getattr(request, f"{action}_{direction}")))

    def _selected_animation_id(self) -> str:
        action = str(self.preview_action.currentData() or "idle")
        direction = str(self.preview_direction.currentData() or "down")
        return str(
            self.animation_fields[action][direction].currentData() or "")

    def _source_image(self, animation: ContentDefinition) -> QImage:
        image = self.workspace.find(
            "visualImages", str(animation.data.get("imageId", "")))
        if image is None:
            return QImage()
        root = (
            self.asset_root
            if image.data.get("root") == "gameAssets"
            else self.workspace.root)
        relative = image.data.get("relativePath")
        return (
            QImage(str(root / relative))
            if root and isinstance(relative, str)
            else QImage())

    def _preview_reset(self, unused: object = None) -> None:
        del unused
        self._preview_timer.stop()
        animation = self.workspace.find(
            "animations", self._selected_animation_id())
        if animation is None:
            self._preview_frames = []
            self._preview_image = QImage()
            self.preview.setPixmap(QPixmap())
            self.preview.setText(
                self.translate("player_animation_none"))
            return
        frames = animation.data.get("frames", [])
        self._preview_frames = (
            [value for value in frames if isinstance(value, dict)]
            if isinstance(frames, list) else [])
        self._preview_image = self._source_image(animation)
        self._preview_index = 0
        self._draw_preview_frame()
        if self._preview_frames:
            self._schedule_next_frame()

    def _draw_preview_frame(self) -> None:
        if self._preview_image.isNull() or not self._preview_frames:
            self.preview.setPixmap(QPixmap())
            self.preview.setText(self.translate("image_unavailable"))
            return
        frame = self._preview_frames[
            min(self._preview_index, len(self._preview_frames) - 1)]
        source = frame.get("source", {})
        if not isinstance(source, dict):
            return
        width = max(1, int(source.get("width", 1)))
        height = max(1, int(source.get("height", 1)))
        cropped = self._preview_image.copy(QRect(
            int(source.get("x", 0)), int(source.get("y", 0)),
            width, height))
        self.preview.setText("")
        self.preview.setPixmap(QPixmap.fromImage(cropped).scaled(
            320, 320, Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation))

    def _schedule_next_frame(self) -> None:
        if not self._preview_frames:
            return
        frame = self._preview_frames[
            min(self._preview_index, len(self._preview_frames) - 1)]
        ticks = max(1, int(frame.get("durationTicks", 1)))
        self._preview_timer.start(max(16, ticks * 16))

    def _advance_preview(self) -> None:
        if not self._preview_frames:
            return
        self._preview_index = (
            self._preview_index + 1) % len(self._preview_frames)
        self._draw_preview_frame()
        self._schedule_next_frame()

    def _request(self) -> PlayerAuthoringRequest:
        values: dict[str, str] = {}
        for action in _ACTIONS:
            for direction in _DIRECTIONS:
                values[f"{action}_{direction}"] = str(
                    self.animation_fields[action][direction].currentData()
                    or "")
        progression_id = str(
            self.progression.currentData()
            or self.progression.currentText()).strip()
        return PlayerAuthoringRequest(
            player_id=self.player_id.text().strip(),
            display_name=self.name.text().strip(),
            progression_id=progression_id,
            **values,
        )

    def _save(self) -> None:
        try:
            request = self._request()
            result = (
                self.service.update(self.workspace, request)
                if self.definition else
                self.service.create(self.workspace, request))
            self.created_player_id = result.definition_id
            self.accept()
        except ValueError as error:
            QMessageBox.warning(
                self, self.windowTitle(), str(error))


class PlayerLibraryWidget(QWidget):
    changed = Signal()
    status_changed = Signal(str)

    def __init__(self, workspace: ContentWorkspace | None,
                 asset_root: Path | None, translator: Translator,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.translate = translator
        self.service = PlayerAuthoringService()

        self.search = QLineEdit()
        self.search.setPlaceholderText(
            self.translate("search_players"))
        self.search.textChanged.connect(self.refresh)

        self.list = QListWidget()
        self.list.itemDoubleClicked.connect(
            lambda unused: self.edit_selected())
        self.list.currentItemChanged.connect(
            self._selection_changed)

        self.create_button = QPushButton(
            self.translate("create_player"))
        self.edit_button = QPushButton(
            self.translate("configure_player"))
        self.delete_button = QPushButton(
            self.translate("delete"))
        self.create_button.clicked.connect(self.create_player)
        self.edit_button.clicked.connect(self.edit_selected)
        self.delete_button.clicked.connect(self.delete_selected)

        row = QHBoxLayout()
        row.addWidget(self.create_button)
        row.addWidget(self.edit_button)
        row.addWidget(self.delete_button)

        self._help = QLabel(
            self.translate("player_library_help"))
        self._help.setWordWrap(True)
        self._help.setStyleSheet("color: #aeb8c4;")

        layout = QVBoxLayout(self)
        layout.addWidget(self.search)
        layout.addWidget(self.list, 1)
        layout.addLayout(row)
        layout.addWidget(self._help)
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.search.setPlaceholderText(
            self.translate("search_players"))
        self.create_button.setText(
            self.translate("create_player"))
        self.edit_button.setText(
            self.translate("configure_player"))
        self.delete_button.setText(self.translate("delete"))
        self._help.setText(
            self.translate("player_library_help"))

    def set_context(self, workspace: ContentWorkspace | None,
                    asset_root: Path | None) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.refresh()

    def refresh(self, unused: object = None) -> None:
        del unused
        selected_id = ""
        current = self.list.currentItem()
        if current:
            selected_id = str(
                current.data(Qt.ItemDataRole.UserRole) or "")
        self.list.clear()
        if self.workspace is None:
            self.edit_button.setEnabled(False)
            self.delete_button.setEnabled(False)
            return
        query = self.search.text().strip()
        for definition in self.workspace.definitions(
                "players", query):
            item = QListWidgetItem(
                f"{definition.display_name}\n"
                f"{definition.definition_id}")
            item.setData(
                Qt.ItemDataRole.UserRole,
                definition.definition_id)
            self.list.addItem(item)
            if definition.definition_id == selected_id:
                self.list.setCurrentItem(item)
        self._selection_changed(
            self.list.currentItem(), None)

    def _selected_definition(self) -> ContentDefinition | None:
        if self.workspace is None:
            return None
        item = self.list.currentItem()
        if item is None:
            return None
        return self.workspace.find(
            "players",
            str(item.data(Qt.ItemDataRole.UserRole) or ""))

    def _selection_changed(
            self, current: QListWidgetItem | None,
            previous: QListWidgetItem | None) -> None:
        del previous
        enabled = current is not None
        self.edit_button.setEnabled(enabled)
        self.delete_button.setEnabled(enabled)

    def create_player(self) -> None:
        if self.workspace is None:
            return
        if not self.workspace.definitions("animations"):
            QMessageBox.information(
                self, self.translate("create_player"),
                self.translate("player_needs_animations"))
            return
        dialog = PlayerDefinitionDialog(
            self.workspace, self.asset_root,
            self.translate, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.refresh()
            self.changed.emit()
            self.status_changed.emit(
                self.translate("player_created"))

    def edit_selected(self) -> None:
        definition = self._selected_definition()
        if definition is None or self.workspace is None:
            return
        dialog = PlayerDefinitionDialog(
            self.workspace, self.asset_root,
            self.translate, definition=definition, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.refresh()
            self.changed.emit()
            self.status_changed.emit(
                self.translate("player_configured"))

    def delete_selected(self) -> None:
        definition = self._selected_definition()
        if definition is None or self.workspace is None:
            return
        message = self.translate(
            "player_delete_confirm").format(
                player=definition.display_name)
        answer = QMessageBox.question(
            self, self.translate("delete"), message)
        if answer != QMessageBox.StandardButton.Yes:
            return
        try:
            self.service.delete(
                self.workspace, definition)
        except ValueError as error:
            QMessageBox.warning(
                self, self.translate("delete"), str(error))
            return
        self.refresh()
        self.changed.emit()
        self.status_changed.emit(
            self.translate("player_deleted"))
