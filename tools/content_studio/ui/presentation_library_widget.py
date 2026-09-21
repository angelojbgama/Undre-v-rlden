"""First-class Presentation Effect authoring UI for the Content Studio."""

from __future__ import annotations

from PySide6.QtCore import Qt, Signal
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
from ..services.presentation_authoring_service import (
    COMPOSITION_LAYERS,
    LIFETIMES,
    MAX_ALPHA,
    MAX_DURATION_TICKS,
    MAX_PRIORITY,
    MAX_SHAKE_AMPLITUDE,
    MAX_VISION_RADIUS,
    MIN_PRIORITY,
    OVERLAY_MODES,
    PresentationAuthoringService,
)
from .icon_registry import icon
from .widgets import PayloadListWidget


class ColorRow(QWidget):
    """Four [ 0..255 ] spin boxes editing one r/g/b/a color."""

    def __init__(
        self,
        translator: Translator,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.translate = translator
        self.channels: dict[str, QSpinBox] = {}
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        for channel in ("r", "g", "b", "a"):
            spin = QSpinBox(self)
            spin.setRange(0, MAX_ALPHA)
            self.channels[channel] = spin
            layout.addWidget(QLabel(channel.upper(), self))
            layout.addWidget(spin)

    def value(self) -> dict[str, int]:
        return {
            channel: spin.value()
            for channel, spin in self.channels.items()
        }

    def load(self, color: object) -> None:
        data = color if isinstance(color, dict) else {}
        for channel, spin in self.channels.items():
            value = data.get(channel, MAX_ALPHA if channel == "a" else 0)
            spin.setValue(value if isinstance(value, int) else 0)


class PresentationEffectDialog(QDialog):
    """Create or edit one authored Presentation Effect."""

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
        self.service = PresentationAuthoringService(workspace)

        self.effect_id = QLineEdit("effect.new_effect", self)

        self.lifetime = QComboBox(self)
        for lifetime in LIFETIMES:
            self.lifetime.addItem(
                self.translate(f"pe_lifetime_{lifetime}"),
                lifetime,
            )

        self.duration = QSpinBox(self)
        self.duration.setRange(1, MAX_DURATION_TICKS)
        self.duration.setValue(12)

        self.priority = QSpinBox(self)
        self.priority.setRange(MIN_PRIORITY, MAX_PRIORITY)

        # Primitives: each check enables its authored group. Persistent
        # effects cannot carry camera shake or fades (mirrors the runtime).
        self.use_shake = QCheckBox(self.translate("pe_primitive_shake"), self)
        self.shake_amplitude = QSpinBox(self)
        self.shake_amplitude.setRange(1, MAX_SHAKE_AMPLITUDE)
        self.shake_amplitude.setValue(4)
        self.use_shake.toggled.connect(self.shake_amplitude.setEnabled)

        self.use_overlay = QCheckBox(self.translate("pe_primitive_overlay"), self)
        self.overlay_mode = QComboBox(self)
        for mode in OVERLAY_MODES:
            self.overlay_mode.addItem(self.translate(f"pe_overlay_mode_{mode}"), mode)
        self.overlay_pulse = QSpinBox(self)
        self.overlay_pulse.setRange(1, MAX_DURATION_TICKS)
        self.overlay_pulse.setValue(60)
        self.overlay_layer = QComboBox(self)
        for layer in COMPOSITION_LAYERS:
            self.overlay_layer.addItem(self.translate(f"pe_overlay_layer_{layer}"), layer)
        self.overlay_color = ColorRow(self.translate, self)
        self.use_overlay.toggled.connect(self._sync_overlay)

        self.use_vision = QCheckBox(self.translate("pe_primitive_vision"), self)
        self.vision_inner = QSpinBox(self)
        self.vision_inner.setRange(0, MAX_VISION_RADIUS)
        self.vision_inner.setValue(48)
        self.vision_outer = QSpinBox(self)
        self.vision_outer.setRange(1, MAX_VISION_RADIUS)
        self.vision_outer.setValue(72)
        self.vision_alpha = QSpinBox(self)
        self.vision_alpha.setRange(0, MAX_ALPHA)
        self.vision_alpha.setValue(220)
        self.vision_color = ColorRow(self.translate, self)
        self.use_vision.toggled.connect(self._sync_vision)

        self.use_fade = QCheckBox(self.translate("pe_primitive_fade"), self)
        self.fade_start = QSpinBox(self)
        self.fade_start.setRange(0, MAX_ALPHA)
        self.fade_end = QSpinBox(self)
        self.fade_end.setRange(0, MAX_ALPHA)
        self.fade_color = ColorRow(self.translate, self)
        self.use_fade.toggled.connect(self._sync_fade)

        self.lifetime.currentIndexChanged.connect(self._sync_lifetime)

        form = QFormLayout()
        form.addRow(self.translate("pe_id"), self.effect_id)
        form.addRow(self.translate("pe_lifetime"), self.lifetime)
        form.addRow(self.translate("pe_duration"), self.duration)
        form.addRow(self.translate("pe_priority"), self.priority)

        shake_group = QGroupBox(self.translate("pe_primitive_shake"), self)
        shake_form = QFormLayout(shake_group)
        shake_form.addRow(self.translate("pe_amplitude"), self.shake_amplitude)
        self._bind_group(self.use_shake, shake_group)

        overlay_group = QGroupBox(self.translate("pe_primitive_overlay"), self)
        overlay_form = QFormLayout(overlay_group)
        overlay_form.addRow(self.translate("pe_overlay_mode"), self.overlay_mode)
        overlay_form.addRow(self.translate("pe_overlay_pulse_period"), self.overlay_pulse)
        overlay_form.addRow(self.translate("pe_overlay_layer"), self.overlay_layer)
        overlay_form.addRow(self.translate("pe_color"), self.overlay_color)
        self._bind_group(self.use_overlay, overlay_group)

        vision_group = QGroupBox(self.translate("pe_primitive_vision"), self)
        vision_form = QFormLayout(vision_group)
        vision_form.addRow(self.translate("pe_inner_radius"), self.vision_inner)
        vision_form.addRow(self.translate("pe_outer_radius"), self.vision_outer)
        vision_form.addRow(self.translate("pe_outside_alpha"), self.vision_alpha)
        vision_form.addRow(self.translate("pe_color"), self.vision_color)
        self._bind_group(self.use_vision, vision_group)

        fade_group = QGroupBox(self.translate("pe_primitive_fade"), self)
        fade_form = QFormLayout(fade_group)
        fade_form.addRow(self.translate("pe_fade_start_alpha"), self.fade_start)
        fade_form.addRow(self.translate("pe_fade_end_alpha"), self.fade_end)
        fade_form.addRow(self.translate("pe_color"), self.fade_color)
        self._bind_group(self.use_fade, fade_group)

        self.buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel | QDialogButtonBox.StandardButton.Ok,
            parent=self,
        )
        self.buttons.accepted.connect(self._save)
        self.buttons.rejected.connect(self.reject)

        help_label = QLabel(self.translate("pe_help"), self)
        help_label.setWordWrap(True)
        help_label.setProperty("muted", True)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        for group in (shake_group, overlay_group, vision_group, fade_group):
            layout.addWidget(group)
        layout.addWidget(help_label)
        layout.addStretch(1)
        layout.addWidget(self.buttons)

        if definition is not None:
            self._load_definition(definition)
            self.effect_id.setReadOnly(True)
            self.setWindowTitle(self.translate("pe_configure"))
        else:
            self.setWindowTitle(self.translate("pe_create"))
        self._sync_lifetime()

    @staticmethod
    def _bind_group(check: QCheckBox, group: QGroupBox) -> None:
        check.toggled.connect(group.setEnabled)
        group.setChecked(False)
        group.setEnabled(False)

    def _sync_lifetime(self) -> None:
        persistent = self._current_lifetime() == "persistent"
        self.duration.setEnabled(not persistent)
        self.use_shake.setEnabled(not persistent)
        if persistent:
            self.use_shake.setChecked(False)
            self.use_fade.setChecked(False)
        self._sync_shake()
        self._sync_fade()

    def _sync_shake(self) -> None:
        self.shake_amplitude.setEnabled(self.use_shake.isChecked())

    def _sync_overlay(self) -> None:
        enabled = self.use_overlay.isChecked()
        for widget in (
            self.overlay_mode,
            self.overlay_pulse,
            self.overlay_layer,
            self.overlay_color,
        ):
            widget.setEnabled(enabled)

    def _sync_vision(self) -> None:
        enabled = self.use_vision.isChecked()
        for widget in (
            self.vision_inner,
            self.vision_outer,
            self.vision_alpha,
            self.vision_color,
        ):
            widget.setEnabled(enabled)

    def _sync_fade(self) -> None:
        enabled = self.use_fade.isChecked()
        for widget in (self.fade_start, self.fade_end, self.fade_color):
            widget.setEnabled(enabled)

    def _current_lifetime(self) -> str:
        value = self.lifetime.currentData()
        return value if isinstance(value, str) else "transient"

    def _load_definition(self, definition: ContentDefinition) -> None:
        data = definition.data
        self.effect_id.setText(definition.definition_id)
        lifetime = data.get("lifetime", "transient")
        index = max(self.lifetime.findData(str(lifetime)), 0)
        self.lifetime.setCurrentIndex(index)
        self.duration.setValue(
            int(data.get("durationTicks", 12) or 12))
        self.priority.setValue(
            int(data.get("priority", 0) or 0))

        shake = data.get("cameraShake")
        if isinstance(shake, dict):
            self.use_shake.setChecked(True)
            self.shake_amplitude.setValue(
                int(shake.get("amplitudePixels", 4) or 4))

        overlay = data.get("overlay")
        if isinstance(overlay, dict):
            self.use_overlay.setChecked(True)
            mode_index = max(
                self.overlay_mode.findData(str(overlay.get("mode", "constant"))), 0)
            self.overlay_mode.setCurrentIndex(mode_index)
            self.overlay_pulse.setValue(
                int(overlay.get("pulsePeriodTicks", 60) or 60))
            layer_index = max(
                self.overlay_layer.findData(str(overlay.get("layer", "world"))), 0)
            self.overlay_layer.setCurrentIndex(layer_index)
            self.overlay_color.load(overlay.get("color"))

        vision = data.get("visionMask")
        if isinstance(vision, dict):
            self.use_vision.setChecked(True)
            self.vision_inner.setValue(
                int(vision.get("innerRadiusPixels", 0) or 0))
            self.vision_outer.setValue(
                int(vision.get("outerRadiusPixels", 72) or 72))
            self.vision_alpha.setValue(
                int(vision.get("outsideAlpha", 220) or 220))
            self.vision_color.load(vision.get("color"))

        fade = data.get("fade")
        if isinstance(fade, dict):
            self.use_fade.setChecked(True)
            self.fade_start.setValue(
                int(fade.get("startAlpha", 0) or 0))
            self.fade_end.setValue(
                int(fade.get("endAlpha", 255) or 255))
            self.fade_color.load(fade.get("color"))

    def _collect(self) -> dict[str, object]:
        data: dict[str, object] = {
            "id": self.effect_id.text().strip(),
            "lifetime": self._current_lifetime(),
            "durationTicks": self.duration.value(),
            "priority": self.priority.value(),
        }
        if self.use_shake.isChecked():
            data["cameraShake"] = {
                "amplitudePixels": self.shake_amplitude.value(),
            }
        if self.use_overlay.isChecked():
            data["overlay"] = {
                "color": self.overlay_color.value(),
                "mode": self.overlay_mode.currentData(),
                "pulsePeriodTicks": self.overlay_pulse.value(),
                "layer": self.overlay_layer.currentData(),
            }
        if self.use_vision.isChecked():
            data["visionMask"] = {
                "innerRadiusPixels": self.vision_inner.value(),
                "outerRadiusPixels": self.vision_outer.value(),
                "outsideAlpha": self.vision_alpha.value(),
                "color": self.vision_color.value(),
            }
        if self.use_fade.isChecked():
            data["fade"] = {
                "color": self.fade_color.value(),
                "startAlpha": self.fade_start.value(),
                "endAlpha": self.fade_end.value(),
            }
        return data

    def _save(self) -> None:
        effect_id = self.effect_id.text().strip()
        try:
            if self.definition is None:
                self.service.create_effect(effect_id, self._collect())
            else:
                self.service.configure(effect_id, self._collect())
        except ValueError as error:
            QMessageBox.warning(self, "Presentation", str(error))
            return
        self.accept()


class PresentationLibraryWidget(QWidget):
    """Search, author and inspect presentation effect definitions."""

    selected = Signal(object)
    changed = Signal()
    status_changed = Signal(str)

    def __init__(
        self,
        workspace: ContentWorkspace | None,
        asset_root: object | None = None,
        translator: Translator | None = None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.translate = translator or Translator()
        self.service = PresentationAuthoringService(workspace)

        self.search = QLineEdit(self)
        self.search.setPlaceholderText(self.translate("pe_search"))
        self.search.textChanged.connect(self.refresh)

        self.effects_list = PayloadListWidget(self)
        self.effects_list.currentItemChanged.connect(self._selection_changed)

        self.create_button = QPushButton(self)
        self.create_button.setIcon(icon("add"))
        self.create_button.setToolTip(self.translate("pe_create"))
        self.create_button.clicked.connect(self.create_effect)

        self.configure_button = QPushButton(self)
        self.configure_button.setIcon(icon("configure"))
        self.configure_button.setToolTip(self.translate("pe_configure"))
        self.configure_button.clicked.connect(self.configure_current)

        self.delete_button = QPushButton(self)
        self.delete_button.setIcon(icon("delete"))
        self.delete_button.setToolTip(self.translate("pe_delete"))
        self.delete_button.clicked.connect(self.delete_current)

        buttons = QHBoxLayout()
        buttons.addWidget(self.create_button)
        buttons.addWidget(self.configure_button)
        buttons.addWidget(self.delete_button)
        buttons.addStretch(1)

        self.details = QLabel(self.translate("pe_none"), self)
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
        list_layout.addWidget(self.effects_list, 1)
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
        asset_root: object | None = None,
    ) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.service.set_context(workspace)
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.search.setPlaceholderText(self.translate("pe_search"))
        self.create_button.setToolTip(self.translate("pe_create"))
        self.configure_button.setToolTip(self.translate("pe_configure"))
        self.delete_button.setToolTip(self.translate("pe_delete"))
        self.refresh()

    # -- population --------------------------------------------------------

    def refresh(self) -> None:
        self.effects_list.blockSignals(True)
        self.effects_list.clear()
        query = self.search.text() if hasattr(self, "search") else ""
        for effect in self.service.effects(query):
            label = effect.definition_id
            lifetime = effect.data.get("lifetime", "")
            if lifetime:
                label += f"  [{lifetime}]"
            item = QListWidgetItem(label, self.effects_list)
            item.setData(Qt.ItemDataRole.UserRole, effect)
        self.effects_list.blockSignals(False)
        if self.effects_list.count() == 0:
            self.details.setText(self.translate("pe_none"))

    def _selection_changed(self, current: QListWidgetItem | None, _previous=None) -> None:
        definition = current.data(Qt.ItemDataRole.UserRole) if current else None
        self.selected.emit(definition)
        if definition is None:
            self.details.setText(self.translate("pe_none"))
            return
        self._show_details(definition)

    def _show_details(self, definition: ContentDefinition) -> None:
        lines: list[str] = []
        lifetime = definition.data.get("lifetime", "")
        lines.append(
            f"<b>{self.translate('pe_lifetime')}</b>: {lifetime}")
        if lifetime == "transient":
            lines.append(
                f"<b>{self.translate('pe_duration')}</b>: "
                f"{definition.data.get('durationTicks', 0)}")
        lines.append(
            f"<b>{self.translate('pe_priority')}</b>: "
            f"{definition.data.get('priority', 0)}")

        primitives = (
            ("cameraShake", "pe_primitive_shake", self._describe_shake),
            ("overlay", "pe_primitive_overlay", self._describe_overlay),
            ("visionMask", "pe_primitive_vision", self._describe_vision),
            ("fade", "pe_primitive_fade", self._describe_fade),
        )
        for key, label_key, describe in primitives:
            value = definition.data.get(key)
            if isinstance(value, dict):
                lines.append(
                    f"<br><b>{self.translate(label_key)}</b>"
                    f"<br>{describe(value)}")

        referencing = self.service.referenced_by(definition.definition_id)
        if referencing:
            lines.append(
                f"<br><b>{self.translate('pe_referenced_by')}</b><br>"
                + "<br>".join(referencing))

        text = f"<b>{definition.definition_id}</b><br><br>" + "<br>".join(lines)
        self.details.setText(text)

    def _describe_shake(self, shake: dict) -> str:
        return f"amplitudePixels = {shake.get('amplitudePixels', 0)}"

    def _describe_overlay(self, overlay: dict) -> str:
        color = overlay.get("color", {})
        channels = self._rgba(color)
        text = (
            f"mode = {overlay.get('mode', 'constant')}, "
            f"layer = {overlay.get('layer', 'world')}"
        )
        if overlay.get("mode") == "pulse":
            text += f", period = {overlay.get('pulsePeriodTicks', 0)}"
        text += f"<br>rgba({channels})"
        return text

    def _describe_vision(self, vision: dict) -> str:
        return (
            f"inner = {vision.get('innerRadiusPixels', 0)}, "
            f"outer = {vision.get('outerRadiusPixels', 0)}, "
            f"outsideAlpha = {vision.get('outsideAlpha', 255)}"
        )

    def _describe_fade(self, fade: dict) -> str:
        return (
            f"alpha {fade.get('startAlpha', 0)} -> "
            f"{fade.get('endAlpha', 0)}, "
            f"rgba({self._rgba(fade.get('color', {}))})"
        )

    @staticmethod
    def _rgba(color: dict) -> str:
        return (
            f"{color.get('r', 0)},{color.get('g', 0)},"
            f"{color.get('b', 0)},{color.get('a', 255)}"
        )

    # -- CRUD ---------------------------------------------------------------

    def create_effect(self) -> None:
        if self.workspace is None:
            return
        dialog = PresentationEffectDialog(
            self.workspace, self.translate, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("pe_created"))
            self.refresh()

    def configure_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        dialog = PresentationEffectDialog(
            self.workspace, self.translate,
            definition=definition, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.changed.emit()
            self.status_changed.emit(self.translate("pe_configured"))
            self.refresh()

    def delete_current(self) -> None:
        definition = self._current_definition()
        if definition is None:
            return
        confirm = QMessageBox.question(
            self,
            self.translate("pe_delete"),
            self.translate("pe_delete_confirm").format(
                effect=definition.definition_id),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
        )
        if confirm != QMessageBox.StandardButton.Yes:
            return
        try:
            self.service.delete(definition.definition_id)
        except ValueError as error:
            QMessageBox.warning(self, self.translate("pe_delete"), str(error))
            return
        self.changed.emit()
        self.status_changed.emit(self.translate("pe_deleted"))
        self.refresh()

    def _current_definition(self) -> ContentDefinition | None:
        current = self.effects_list.currentItem()
        return current.data(Qt.ItemDataRole.UserRole) if current else None
