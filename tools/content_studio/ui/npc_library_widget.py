"""First-class NPC authoring UI for the Content Studio."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor, QPainter, QPixmap
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFormLayout,
    QGroupBox,
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
from ..services.localization import Translator
from ..services.npc_authoring_service import NpcAuthoringService
from .icon_registry import icon
from .studio_visual_resolver import StudioVisualResolver
from .widgets import PayloadListWidget


def _marker_pixmap(color: dict[str, object], width: int = 28, height: int = 40) -> QPixmap:
    pixmap = QPixmap(width, height)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    rgba = QColor(int(color.get("r", 255)), int(color.get("g", 255)),
                  int(color.get("b", 255)), int(color.get("a", 255)))
    painter.fillRect(4, 4, width - 8, height - 8, rgba)
    painter.end()
    return pixmap


class ColorRow(QWidget):
    """Four [0..255] spin boxes editing one r/g/b/a marker color."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.channels: dict[str, QSpinBox] = {}
        row = QHBoxLayout(self)
        row.setContentsMargins(0, 0, 0, 0)
        for channel in ("r", "g", "b", "a"):
            spin = QSpinBox(self)
            spin.setRange(0, 255)
            spin.setValue(255 if channel == "a" else 0)
            self.channels[channel] = spin
            row.addWidget(QLabel(channel.upper(), self))
            row.addWidget(spin)

    def value(self) -> dict[str, int]:
        return {channel: spin.value() for channel, spin in self.channels.items()}

    def load(self, color: object) -> None:
        data = color if isinstance(color, dict) else {}
        for channel, spin in self.channels.items():
            value = data.get(channel, 255 if channel == "a" else 0)
            spin.setValue(value if isinstance(value, int) else 0)


class NpcVisualDialog(QDialog):
    """Create or edit one NPC visual set (marker color + idle bindings)."""

    def __init__(
        self,
        workspace: ContentWorkspace,
        translator: Translator,
        visual_id: str | None = None,
        data: dict[str, object] | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.translate = translator
        self.existing_id = visual_id
        self.service = NpcAuthoringService(workspace)

        self.visual_id = QLineEdit(self)
        self.visual_id.setPlaceholderText("visual.npc.new_npc")

        self.marker_color = ColorRow(self)

        self.default_animation = QLineEdit(self)
        self.default_animation.setPlaceholderText("anim.npc... (opcional)")
        self.facings: dict[str, QLineEdit] = {}
        for facing in ("down", "up", "left", "right"):
            edit = QLineEdit(self)
            edit.setPlaceholderText(f"anim... {facing} (opcional)")
            self.facings[facing] = edit

        form = QFormLayout()
        form.addRow(self.translate("npc_visual_field_id"), self.visual_id)
        form.addRow(self.translate("npc_visual_field_marker"), self.marker_color)
        form.addRow(self.translate("npc_visual_field_default"), self.default_animation)
        for facing in ("down", "up", "left", "right"):
            form.addRow(facing.capitalize(), self.facings[facing])

        hint = QLabel(self.translate("npc_visual_hint"), self)
        hint.setWordWrap(True)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(hint)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel,
            self,
        )
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

        if visual_id:
            self.setWindowTitle(self.translate("npc_visual_edit"))
            self.visual_id.setText(visual_id)
            self.visual_id.setEnabled(False)
            if data:
                self.marker_color.load(data.get("markerColor"))
                idle = data.get("idle") if isinstance(data.get("idle"), dict) else {}
                self.default_animation.setText(str(idle.get("defaultAnimation") or ""))
                for facing, edit in self.facings.items():
                    edit.setText(str(idle.get(facing) or ""))
        else:
            self.setWindowTitle(self.translate("npc_visual_create"))

    def _collect(self) -> dict[str, object]:
        idle: dict[str, str] = {}
        if self.default_animation.text().strip():
            idle["defaultAnimation"] = self.default_animation.text().strip()
        for facing, edit in self.facings.items():
            if edit.text().strip():
                idle[facing] = edit.text().strip()
        return {
            "id": self.visual_id.text().strip(),
            "markerColor": self.marker_color.value(),
            "idle": idle,
        }

    def _save(self) -> None:
        try:
            if self.existing_id:
                self.service.configure_visual(self.existing_id, self._collect())
            else:
                self.service.create_visual_set(self.visual_id.text().strip(), self._collect())
        except ValueError as error:
            QMessageBox.warning(self, "NPC", str(error))
            return
        self.accept()


class NpcEditorDialog(QDialog):
    """Create or edit one authored NPC definition."""

    def __init__(
        self,
        workspace: ContentWorkspace,
        translator: Translator,
        definition: ContentDefinition | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.translate = translator
        self.definition = definition
        self.service = NpcAuthoringService(workspace)

        self.npc_id = QLineEdit(self)
        self.npc_id.setPlaceholderText("npc.new_npc")

        self.visual_set = QComboBox(self)
        self._load_visuals()

        self.visual_button = QPushButton(self.translate("npc_visual_edit"), self)
        self.visual_button.clicked.connect(self._edit_visual)

        self.dialogue = QComboBox(self)
        for dialogue in self.service.dialogues():
            self.dialogue.addItem(dialogue.definition_id, dialogue.definition_id)

        self.interaction_x = QSpinBox(self)
        self.interaction_y = QSpinBox(self)
        for spin in (self.interaction_x, self.interaction_y):
            spin.setRange(-512, 512)
            spin.setValue(-14)
        self.interaction_w = QSpinBox(self)
        self.interaction_h = QSpinBox(self)
        for spin in (self.interaction_w, self.interaction_h):
            spin.setRange(1, 512)
        self.interaction_w.setValue(28)
        self.interaction_h.setValue(22)

        self.enabled = QCheckBox(self.translate("npc_field_enabled"), self)
        self.enabled.setChecked(True)

        self.tags = QLineEdit(self)
        self.tags.setPlaceholderText("npc, story")

        form = QFormLayout()
        form.addRow(self.translate("npc_field_id"), self.npc_id)
        visual_row = QHBoxLayout()
        visual_row.addWidget(self.visual_set, 1)
        visual_row.addWidget(self.visual_button)
        visual_holder = QWidget(self)
        visual_holder.setLayout(visual_row)
        form.addRow(self.translate("npc_field_visual"), visual_holder)
        form.addRow(self.translate("npc_field_dialogue"), self.dialogue)
        form.addRow(self.translate("npc_field_interaction"), self._interaction_row())
        form.addRow("", self.enabled)
        form.addRow(self.translate("npc_field_tags"), self.tags)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel,
            self,
        )
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

        if definition is None:
            self.setWindowTitle(self.translate("npc_create"))
        else:
            self.setWindowTitle(self.translate("npc_configure"))
            self._load(definition)

    def _interaction_row(self) -> QWidget:
        holder = QWidget(self)
        row = QHBoxLayout(holder)
        row.setContentsMargins(0, 0, 0, 0)
        row.addWidget(QLabel("x", self))
        row.addWidget(self.interaction_x)
        row.addWidget(QLabel("y", self))
        row.addWidget(self.interaction_y)
        row.addWidget(QLabel("w", self))
        row.addWidget(self.interaction_w)
        row.addWidget(QLabel("h", self))
        row.addWidget(self.interaction_h)
        return holder

    def _load_visuals(self) -> None:
        self.visual_set.clear()
        for entry in self.service.visual_sets():
            label = str(entry.get("id"))
            if not entry.get("idle"):
                label += "  [marker]"
            self.visual_set.addItem(label, entry)

    def _edit_visual(self) -> None:
        selected = self.visual_set.currentData()
        visual_id = str(selected.get("id")) if isinstance(selected, dict) else None
        dialog = NpcVisualDialog(
            self.workspace, self.translate,
            visual_id=visual_id,
            data=selected if isinstance(selected, dict) else None,
            parent=self,
        )
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self._load_visuals()
            new_id = (dialog.visual_id.text().strip()
                      if not self.existing_id_filter(visual_id) else visual_id)
            index = self.visual_set.findData(
                self.service.find_visual(new_id) or {"id": new_id})
            if index >= 0:
                self.visual_set.setCurrentIndex(index)

    def existing_id_filter(self, visual_id: str | None) -> bool:
        return visual_id is not None and self.service.find_visual(visual_id) is not None \
            and any(str(v.get("id")) == visual_id
                    for v in self.service.visual_sets()
                    if self.workspace.find("npcVisuals", str(v.get("id"))))

    def _load(self, definition: ContentDefinition) -> None:
        data = definition.data
        self.npc_id.setText(definition.definition_id)
        self.npc_id.setEnabled(False)
        index = self.visual_set.findData(
            self.service.find_visual(str(data.get("visualSetId"))) or {"id": data.get("visualSetId")})
        if index < 0:
            # Builtin visuals may not be listed if overridden; fall back to id match.
            for candidate in range(self.visual_set.count()):
                entry = self.visual_set.itemData(candidate)
                if isinstance(entry, dict) and entry.get("id") == data.get("visualSetId"):
                    index = candidate
                    break
        self.visual_set.setCurrentIndex(max(index, 0))
        dialogue_index = self.dialogue.findData(data.get("defaultDialogueId"))
        self.dialogue.setCurrentIndex(max(dialogue_index, 0))
        box = data.get("interaction") if isinstance(data.get("interaction"), dict) else {}
        self.interaction_x.setValue(int(box.get("x", -14) or -14))
        self.interaction_y.setValue(int(box.get("y", -28) or -28))
        self.interaction_w.setValue(int(box.get("width", 28) or 28))
        self.interaction_h.setValue(int(box.get("height", 22) or 22))
        self.enabled.setChecked(bool(data.get("interactionEnabled", True)))
        tags = data.get("tags", [])
        self.tags.setText(", ".join(str(tag) for tag in tags) if isinstance(tags, list) else "")

    def _collect(self) -> dict[str, object]:
        selected = self.visual_set.currentData()
        tags = [tag.strip() for tag in self.tags.text().split(",") if tag.strip()]
        return {
            "id": self.npc_id.text().strip(),
            "visualSetId": (selected.get("id") if isinstance(selected, dict)
                            else self.visual_set.currentText()),
            "interaction": {
                "x": self.interaction_x.value(),
                "y": self.interaction_y.value(),
                "width": self.interaction_w.value(),
                "height": self.interaction_h.value(),
            },
            "interactionEnabled": self.enabled.isChecked(),
            "defaultDialogueId": self.dialogue.currentData(),
            "tags": tags,
        }

    def _save(self) -> None:
        try:
            if self.definition is None:
                self.service.create_npc(self.npc_id.text().strip(), self._collect())
            else:
                self.service.configure(self.definition.definition_id, self._collect())
        except ValueError as error:
            QMessageBox.warning(self, "NPC", str(error))
            return
        self.accept()


class NpcSpritePreview(QWidget):
    """Idle sprite strips (one per facing) plus marker fallback and checks."""

    _FACINGS = ("down", "up", "left", "right")

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        asset_root: Path | None,
        translator: Translator,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.translate = translator
        self.resolver = StudioVisualResolver()
        self.resolver.set_context(workspace, asset_root)

        self.strip_labels: dict[str, QLabel] = {}
        self.marker = QLabel(self)
        self.diagnostics = QLabel(self)
        self.diagnostics.setWordWrap(True)
        self.diagnostics.setTextFormat(Qt.TextFormat.RichText)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        strip_row = QHBoxLayout()
        for facing in self._FACINGS:
            label = QLabel(facing, self)
            label.setMinimumHeight(40)
            label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.strip_labels[facing] = label
            strip_row.addWidget(label)
        strip_row.addWidget(self.marker)
        layout.addLayout(strip_row)
        layout.addWidget(self.diagnostics, 1)
        self.clear()

    def set_context(
        self, workspace: ContentWorkspace | None, asset_root: Path | None,
    ) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.resolver.set_context(workspace, asset_root)
        self.clear()

    def clear(self) -> None:
        for label in self.strip_labels.values():
            label.clear()
        self.marker.clear()
        self.diagnostics.setText("")

    def show_enemy(self, visual_set: dict[str, object] | None,
                   diagnostics: list[dict[str, str]]) -> None:
        idle = visual_set.get("idle") if isinstance(visual_set, dict) else None
        for facing in self._FACINGS:
            label = self.strip_labels[facing]
            animation_id = (idle.get(facing) if isinstance(idle, dict) else None)
            strip = (self._render_strip(str(animation_id))
                     if isinstance(animation_id, str) and animation_id else None)
            if strip is not None:
                label.setPixmap(strip)
            else:
                label.clear()
        marker = visual_set.get("markerColor") if isinstance(visual_set, dict) else None
        self.marker.setPixmap(_marker_pixmap(marker if isinstance(marker, dict) else {}))
        if not diagnostics:
            self.diagnostics.setText(
                f"<span style='color:#7ee787'>✓ {self.translate('npc_visual_ok')}</span>")
            return
        rows = []
        for diagnostic in diagnostics[:12]:
            color = "#f85149" if diagnostic["severity"] == "error" else "#d29922"
            sign = "✗" if diagnostic["severity"] == "error" else "⚠"
            rows.append(
                f"<span style='color:{color}'>{sign} "
                f"{diagnostic['message']} <i>({diagnostic['path']})</i></span>")
        self.diagnostics.setText("<br>".join(rows))

    def _render_strip(self, animation_id: str):
        if self.workspace is None:
            return None
        animation = self.workspace.find("animations", animation_id)
        if animation is None:
            return None
        frames = animation.data.get("frames")
        if not isinstance(frames, list) or not frames:
            return None
        from PySide6.QtCore import Qt as QtCore
        from PySide6.QtGui import QPainter

        cell = 32
        count = min(len(frames), 6)
        thumbnails = QPixmap()
        painter: QPainter | None = None
        for index in range(count):
            resolved = self.resolver.resolve_animation_frame(animation_id, index)
            if resolved is None or resolved.image.isNull():
                if painter is not None:
                    painter.end()
                return None
            if thumbnails.isNull():
                thumbnails = QPixmap(cell * count, cell)
                thumbnails.fill(QtCore.transparent)
                painter = QPainter(thumbnails)
            source = frames[index].get("source")
            if isinstance(source, dict):
                frame_image = resolved.image.copy(
                    int(source.get("x", 0)), int(source.get("y", 0)),
                    max(1, int(source.get("width", 1))),
                    max(1, int(source.get("height", 1))))
            else:
                frame_image = resolved.image
            scaled = frame_image.scaled(
                cell - 4, cell - 4,
                QtCore.AspectRatioMode.KeepAspectRatio,
                QtCore.TransformationMode.FastTransformation)
            if painter is not None:
                painter.drawPixmap(index * cell + 2, 2, QPixmap.fromImage(scaled))
        if painter is not None:
            painter.end()
        return thumbnails


class NpcLibraryWidget(QWidget):
    """Search, author and inspect NPC definitions."""

    changed = Signal()
    status_changed = Signal(str)

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        translator: Translator | None = None,
        parent: QWidget | None = None,
        asset_root: Path | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root: Path | None = asset_root
        self.translate = translator or Translator()
        self.service = NpcAuthoringService(workspace)

        self.search = QLineEdit(self)
        self.search.setPlaceholderText(self.translate("npc_search"))
        self.search.textChanged.connect(self.refresh)

        self.npcs_list = PayloadListWidget(self)
        self.npcs_list.currentItemChanged.connect(self._selection_changed)

        self.create_button = QPushButton(self)
        self.create_button.setIcon(icon("add"))
        self.create_button.setToolTip(self.translate("npc_create"))
        self.create_button.clicked.connect(self.create_npc)

        self.configure_button = QPushButton(self)
        self.configure_button.setIcon(icon("configure"))
        self.configure_button.setToolTip(self.translate("npc_configure"))
        self.configure_button.clicked.connect(self.configure_current)

        self.delete_button = QPushButton(self)
        self.delete_button.setIcon(icon("delete"))
        self.delete_button.setToolTip(self.translate("npc_delete"))
        self.delete_button.clicked.connect(self.delete_current)

        button_row = QHBoxLayout()
        button_row.addWidget(self.create_button)
        button_row.addWidget(self.configure_button)
        button_row.addWidget(self.delete_button)
        button_row.addStretch(1)

        self.details = QLabel(self.translate("npc_none"), self)
        self.details.setWordWrap(True)
        self.details.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.details.setMinimumSize(220, 160)
        self.details.setStyleSheet(
            "background:#161b22;"
            "color:#aeb8c4;"
            "border:1px solid #34404d;"
        )

        self.preview = NpcSpritePreview(workspace, self.asset_root, self.translate)

        right_panel = QWidget(self)
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.addWidget(self.details)
        right_layout.addWidget(self.preview)

        splitter = QSplitter(Qt.Orientation.Horizontal, self)
        splitter.addWidget(list_panel_holder := QWidget(self))
        list_layout = QVBoxLayout(list_panel_holder)
        list_layout.setContentsMargins(0, 0, 0, 0)
        list_layout.addWidget(self.search)
        list_layout.addWidget(self.npcs_list, 1)
        list_layout.addLayout(button_row)
        splitter.addWidget(right_panel)
        splitter.setStretchFactor(0, 1)

        layout = QVBoxLayout(self)
        layout.addWidget(splitter)

        self.refresh()

    # -- context -----------------------------------------------------------

    def set_context(
        self, workspace: ContentWorkspace | None,
        asset_root: Path | None = None,
    ) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.service.set_context(workspace)
        self.preview.set_context(workspace, asset_root)
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.preview.translate = translator
        self.search.setPlaceholderText(self.translate("npc_search"))
        self.create_button.setToolTip(self.translate("npc_create"))
        self.configure_button.setToolTip(self.translate("npc_configure"))
        self.delete_button.setToolTip(self.translate("npc_delete"))
        self.refresh()

    # -- population --------------------------------------------------------

    def refresh(self) -> None:
        # Art may have changed on disk since the last pass.
        self.preview.resolver.invalidate()
        self.npcs_list.blockSignals(True)
        self.npcs_list.clear()
        query = self.search.text() if hasattr(self, "search") else ""
        for npc in self.service.npcs(query):
            dialogue = npc.data.get("defaultDialogueId", "")
            label = npc.definition_id
            if isinstance(dialogue, str) and dialogue:
                label += f"  [{dialogue}]"
            item = QListWidgetItem(label, self.npcs_list)
            item.setData(Qt.ItemDataRole.UserRole, npc)
        self.npcs_list.blockSignals(False)
        if self.npcs_list.count() == 0:
            self.details.setText(self.translate("npc_none"))

    def _selection_changed(self, current: QListWidgetItem | None, _previous=None) -> None:
        definition = current.data(Qt.ItemDataRole.UserRole) if current else None
        if definition is None:
            self.details.setText(self.translate("npc_none"))
            self.preview.clear()
            return
        self._show_details(definition)
        self._refresh_preview(definition)

    def _refresh_preview(self, definition: ContentDefinition) -> None:
        diagnostics = self.service.verify_visual(
            definition.definition_id, self.asset_root)
        self.preview.show_enemy(
            self.service.find_visual(str(definition.data.get("visualSetId"))) or {},
            diagnostics,
        )
        errors = [d for d in diagnostics if d["severity"] == "error"]
        if errors:
            self.status_changed.emit(
                self.translate("npc_visual_problems").format(count=len(errors)))

    def _show_details(self, definition: ContentDefinition) -> None:
        data = definition.data
        lines = [
            f"<b>{self.translate('npc_field_visual')}</b>: {data.get('visualSetId', '')}",
            f"<b>{self.translate('npc_field_dialogue')}</b>: "
            f"{data.get('defaultDialogueId', '')}",
            f"<b>{self.translate('npc_field_enabled')}</b>: "
            f"{bool(data.get('interactionEnabled', True))}",
        ]
        tags = data.get("tags", [])
        if isinstance(tags, list) and tags:
            lines.append(
                f"<b>{self.translate('npc_field_tags')}</b>: "
                + ", ".join(str(tag) for tag in tags))
        placed_in = self.service.placements(definition.definition_id)
        if placed_in:
            lines.append(
                f"<br><b>{self.translate('npc_placed_in')}</b><br>"
                + "<br>".join(placed_in))
        text = f"<b>{definition.definition_id}</b><br><br>" + "<br>".join(lines)
        self.details.setText(text)

    # -- CRUD ---------------------------------------------------------------

    def create_npc(self) -> None:
        if self.workspace is None:
            return
        dialog = NpcEditorDialog(self.workspace, self.translate, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("npc_created"))
            self.refresh()

    def configure_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        dialog = NpcEditorDialog(
            self.workspace, self.translate, definition=definition, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("npc_configured"))
            self.refresh()

    def delete_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        confirm = QMessageBox.question(
            self,
            self.translate("npc_delete"),
            self.translate("npc_delete_confirm").format(npc=definition.definition_id),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if confirm != QMessageBox.StandardButton.Yes:
            return
        try:
            self.service.delete(definition.definition_id)
        except ValueError as error:
            QMessageBox.warning(self, self.translate("npc_delete"), str(error))
            return
        self.changed.emit()
        self.status_changed.emit(self.translate("npc_deleted"))
        self.refresh()

    def _current_definition(self) -> ContentDefinition | None:
        current = self.npcs_list.currentItem()
        return current.data(Qt.ItemDataRole.UserRole) if current else None
