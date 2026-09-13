from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QSize, QTimer, Qt, Signal
from PySide6.QtGui import QIcon, QImage, QPixmap
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QGridLayout,
    QGroupBox, QHBoxLayout, QLabel, QLineEdit, QListWidget, QListWidgetItem,
    QMessageBox, QPushButton, QScrollArea, QSpinBox, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition
from ..services.localization import Translator
from ..services.player_authoring_service import (
    FrameSequenceSpec, PlayerAuthoringRequest, PlayerAuthoringService,
    PlayerCollisionMaskSpec,
)
from .shape_mask_editor import ShapeMaskEditorDialog


STATE_LABELS = {
    "idle": "Idle / parado",
    "walk": "Walk / andando",
    "hurt": "Hurt / dano",
    "sword": "Sword / espada",
    "bow": "Bow / arco",
    "shield": "Shield / escudo",
    "death": "Death / morrendo",
    "dead": "Dead / morto",
    "sleeping": "Sleeping / dormindo",
    "wake_up": "Wake up / acordando",
}
DIRECTION_LABELS = {
    "down": "Down / baixo",
    "up": "Up / cima",
    "left": "Left / esquerda",
    "right": "Right / direita",
}


class FrameSequenceDialog(QDialog):
    # Builds one animation by appending spritesheet cells in order.

    def __init__(self, workspace: ContentWorkspace, asset_root: Path | None,
                 title: str, initial: FrameSequenceSpec | None = None,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.asset_root = asset_root
        self.result_spec: FrameSequenceSpec | None = None
        self._image = QImage()
        self._frames: list[QImage] = []
        self._selected_indices = list(
            initial.frame_indices if initial else ())
        self._columns = initial.columns if initial else 0
        self._preview_index = 0
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._advance_preview)

        self.image_combo = QComboBox()
        for image in workspace.definitions("visualImages"):
            self.image_combo.addItem(
                f"{image.display_name} [{image.definition_id}]",
                image.definition_id)
        self.frame_width = self._spin(
            initial.frame_width if initial else 32)
        self.frame_height = self._spin(
            initial.frame_height if initial else 32)
        self.spacing = self._spin(
            initial.spacing if initial else 0, 0)
        self.origin_x = self._spin(
            initial.origin_x if initial else 0, 0)
        self.origin_y = self._spin(
            initial.origin_y if initial else 0, 0)
        self.duration = self._spin(
            initial.duration_ticks if initial else 8)
        self.loop = QCheckBox("Loop")
        self.loop.setChecked(initial.loop if initial else True)
        self.mirror = QCheckBox(
            "Espelhar horizontalmente (flip X)")
        self.mirror.setChecked(
            initial.flip_x if initial else False)

        if initial is not None:
            index = self.image_combo.findData(initial.image_id)
            if index >= 0:
                self.image_combo.setCurrentIndex(index)

        self.available = QListWidget()
        self.available.setViewMode(QListWidget.ViewMode.IconMode)
        self.available.setIconSize(QSize(64, 64))
        self.available.setResizeMode(QListWidget.ResizeMode.Adjust)
        self.available.setMovement(QListWidget.Movement.Static)
        self.available.setSpacing(6)
        self.available.itemDoubleClicked.connect(
            lambda unused: self._append_frame())

        self.sequence = QListWidget()
        self.sequence.setIconSize(QSize(48, 48))

        add_button = QPushButton("Adicionar frame →")
        remove_button = QPushButton("Remover")
        up_button = QPushButton("↑")
        down_button = QPushButton("↓")
        clear_button = QPushButton("Limpar")
        add_button.clicked.connect(self._append_frame)
        remove_button.clicked.connect(self._remove_frame)
        up_button.clicked.connect(lambda: self._move_frame(-1))
        down_button.clicked.connect(lambda: self._move_frame(1))
        clear_button.clicked.connect(self._clear_sequence)

        sequence_buttons = QHBoxLayout()
        for button in (
                add_button, remove_button, up_button,
                down_button, clear_button):
            sequence_buttons.addWidget(button)

        self.preview = QLabel("Prévia")
        self.preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview.setMinimumHeight(180)
        self.preview.setStyleSheet(
            "background:#161b22;border:1px solid #34404d;")

        form = QFormLayout()
        form.addRow("Spritesheet importado", self.image_combo)
        form.addRow("Largura do frame", self.frame_width)
        form.addRow("Altura do frame", self.frame_height)
        form.addRow("Spacing", self.spacing)
        form.addRow("Origem X", self.origin_x)
        form.addRow("Origem Y", self.origin_y)
        form.addRow("Duração (ticks)", self.duration)
        form.addRow("", self.loop)
        form.addRow("", self.mirror)

        available_group = QGroupBox(
            "Frames disponíveis — duplo clique adiciona")
        available_layout = QVBoxLayout(available_group)
        available_layout.addWidget(self.available)

        sequence_group = QGroupBox(
            "Sequência — esta ordem será reproduzida")
        sequence_layout = QVBoxLayout(sequence_group)
        sequence_layout.addWidget(self.sequence)
        sequence_layout.addLayout(sequence_buttons)

        layout = QGridLayout(self)
        layout.addLayout(form, 0, 0)
        layout.addWidget(self.preview, 1, 0)
        layout.addWidget(available_group, 0, 1, 2, 1)
        layout.addWidget(sequence_group, 2, 0, 1, 2)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel |
            QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self._accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons, 3, 0, 1, 2)

        self.setWindowTitle(title)
        self.resize(1120, 780)

        self.image_combo.currentIndexChanged.connect(self._refresh_grid)
        for control in (
                self.frame_width, self.frame_height, self.spacing,
                self.origin_x, self.origin_y):
            control.valueChanged.connect(self._refresh_grid)
        self.mirror.toggled.connect(self._refresh_sequence)
        self.duration.valueChanged.connect(self._restart_preview)
        self._refresh_grid()

    @staticmethod
    def _spin(value: int, minimum: int = 1) -> QSpinBox:
        control = QSpinBox()
        control.setRange(minimum, 4096)
        control.setValue(value)
        return control

    def _image_path(self) -> Path | None:
        image_id = str(self.image_combo.currentData() or "")
        definition = self.workspace.find("visualImages", image_id)
        if definition is None:
            return None
        root = (
            self.asset_root
            if definition.data.get("root") == "gameAssets"
            else self.workspace.root)
        relative = definition.data.get("relativePath")
        if root is None or not isinstance(relative, str):
            return None
        return root / relative

    def _refresh_grid(self, unused: object = None) -> None:
        del unused
        path = self._image_path()
        self._image = QImage(str(path)) if path else QImage()
        self.available.clear()
        self._frames.clear()
        if self._image.isNull():
            self.preview.setText(
                "Imagem indisponível. Importe o spritesheet primeiro.")
            return
        width = self.frame_width.value()
        height = self.frame_height.value()
        spacing = self.spacing.value()
        origin_x = self.origin_x.value()
        origin_y = self.origin_y.value()
        pitch_x = width + spacing
        pitch_y = height + spacing
        self._columns = max(
            0, (self._image.width() - origin_x + spacing) // pitch_x)
        rows = max(
            0, (self._image.height() - origin_y + spacing) // pitch_y)
        for row in range(rows):
            for column in range(self._columns):
                x = origin_x + column * pitch_x
                y = origin_y + row * pitch_y
                if x + width > self._image.width() or y + height > self._image.height():
                    continue
                crop = self._image.copy(x, y, width, height)
                self._frames.append(crop)
                frame_index = row * self._columns + column
                item = QListWidgetItem(
                    QIcon(QPixmap.fromImage(self._display_image(crop))),
                    f"#{frame_index}\n({column},{row})")
                item.setData(
                    Qt.ItemDataRole.UserRole, frame_index)
                self.available.addItem(item)
        self._selected_indices = [
            index for index in self._selected_indices
            if 0 <= index < len(self._frames)]
        self._refresh_sequence()

    def _display_image(self, image: QImage) -> QImage:
        return (
            image.mirrored(True, False)
            if self.mirror.isChecked() else image)

    def _frame_image(self, index: int) -> QImage:
        if index < 0 or index >= len(self._frames):
            return QImage()
        return self._display_image(self._frames[index])

    def _append_frame(self) -> None:
        item = self.available.currentItem()
        if item is None:
            return
        self._selected_indices.append(
            int(item.data(Qt.ItemDataRole.UserRole)))
        self._refresh_sequence()

    def _remove_frame(self) -> None:
        row = self.sequence.currentRow()
        if 0 <= row < len(self._selected_indices):
            self._selected_indices.pop(row)
            self._refresh_sequence()

    def _move_frame(self, delta: int) -> None:
        row = self.sequence.currentRow()
        target = row + delta
        if row < 0 or target < 0 or target >= len(self._selected_indices):
            return
        self._selected_indices[row], self._selected_indices[target] = (
            self._selected_indices[target],
            self._selected_indices[row])
        self._refresh_sequence()
        self.sequence.setCurrentRow(target)

    def _clear_sequence(self) -> None:
        self._selected_indices.clear()
        self._refresh_sequence()

    def _refresh_sequence(self, unused: object = None) -> None:
        del unused
        self.sequence.clear()
        for order, index in enumerate(self._selected_indices):
            item = QListWidgetItem(
                QIcon(QPixmap.fromImage(self._frame_image(index))),
                f"{order + 1}. frame #{index}")
            self.sequence.addItem(item)
        self._preview_index = 0
        self._show_preview()
        self._restart_preview()

    def _show_preview(self) -> None:
        if not self._selected_indices:
            self.preview.setPixmap(QPixmap())
            self.preview.setText("Nenhum frame selecionado")
            return
        index = self._selected_indices[
            min(self._preview_index, len(self._selected_indices) - 1)]
        image = self._frame_image(index)
        self.preview.setText("")
        self.preview.setPixmap(QPixmap.fromImage(image).scaled(
            170, 170, Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.FastTransformation))

    def _restart_preview(self, unused: object = None) -> None:
        del unused
        self._timer.stop()
        if len(self._selected_indices) > 1:
            self._timer.start(max(
                16, round(self.duration.value() * 1000 / 60)))

    def _advance_preview(self) -> None:
        if not self._selected_indices:
            return
        self._preview_index = (
            self._preview_index + 1) % len(self._selected_indices)
        self._show_preview()
        self._restart_preview()

    def _accept(self) -> None:
        if not self._selected_indices or self._columns <= 0:
            QMessageBox.warning(
                self, self.windowTitle(),
                "Adicione pelo menos um frame à sequência.")
            return
        self.result_spec = FrameSequenceSpec(
            image_id=str(self.image_combo.currentData() or ""),
            frame_width=self.frame_width.value(),
            frame_height=self.frame_height.value(),
            spacing=self.spacing.value(),
            origin_x=self.origin_x.value(),
            origin_y=self.origin_y.value(),
            duration_ticks=self.duration.value(),
            loop=self.loop.isChecked(),
            flip_x=self.mirror.isChecked(),
            frame_indices=tuple(self._selected_indices),
            columns=self._columns,
        )
        self.accept()


class PlayerDefinitionDialog(QDialog):
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
        self.sequences: dict[str, dict[str, FrameSequenceSpec]] = {}
        self.movement_collision: dict[str, PlayerCollisionMaskSpec] = {}
        self.hurtbox: PlayerCollisionMaskSpec | None = None

        self.name = QLineEdit()
        self.player_id = QLineEdit("player.hero")
        self.progression = QComboBox()
        self.progression.setEditable(True)
        self.progression.addItem(
            "progression.player.default", "progression.player.default")
        for value in workspace.definitions("playerProgressions"):
            if self.progression.findData(value.definition_id) < 0:
                self.progression.addItem(
                    f"{value.display_name} [{value.definition_id}]",
                    value.definition_id)

        identity = QGroupBox("Player")
        identity_form = QFormLayout(identity)
        identity_form.addRow("Nome", self.name)
        identity_form.addRow("ID", self.player_id)
        identity_form.addRow("Progression", self.progression)

        help_label = QLabel(
            "Monte cada estado usando Down, Up, Left e Right. "
            "Left e Right são direções explícitas. Se o spritesheet tiver "
            "somente um lado, use 'Usar oposto espelhado' para copiar a "
            "sequência e inverter apenas o flip X.")
        help_label.setWordWrap(True)
        help_label.setStyleSheet("color:#aeb8c4;")

        self.summary_labels: dict[tuple[str, str], QLabel] = {}
        states = QWidget()
        grid = QGridLayout(states)
        grid.addWidget(QLabel("Estado"), 0, 0)
        for column, direction in enumerate(
                self.service.DIRECTIONS, start=1):
            grid.addWidget(
                QLabel(DIRECTION_LABELS[direction]), 0, column)

        all_states = (
            self.service.CORE_STATES +
            self.service.OPTIONAL_STATES)
        for row, state in enumerate(all_states, start=1):
            grid.addWidget(QLabel(STATE_LABELS[state]), row, 0)
            for column, direction in enumerate(
                    self.service.DIRECTIONS, start=1):
                cell = QWidget()
                cell_layout = QVBoxLayout(cell)
                cell_layout.setContentsMargins(2, 2, 2, 2)
                button = QPushButton("Selecionar frames...")
                summary = QLabel("—")
                summary.setStyleSheet("color:#aeb8c4;")
                button.clicked.connect(
                    lambda unused=False, s=state, d=direction:
                    self._edit_sequence(s, d))
                cell_layout.addWidget(button)
                if direction in ("left", "right"):
                    mirror_button = QPushButton("Usar oposto espelhado")
                    mirror_button.clicked.connect(
                        lambda unused=False, s=state, d=direction:
                        self._use_opposite_mirrored(s, d))
                    cell_layout.addWidget(mirror_button)
                cell_layout.addWidget(summary)
                grid.addWidget(cell, row, column)
                self.summary_labels[(state, direction)] = summary

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(states)

        self.collision_enabled = QCheckBox(
            "Ativar Movement Collision autorável")
        self.collision_enabled.toggled.connect(self._collision_toggled)
        self.collision_buttons: dict[str, QPushButton] = {}
        self.collision_summaries: dict[str, QLabel] = {}
        collision_group = QGroupBox("Movement Collision")
        collision_layout = QGridLayout(collision_group)
        collision_layout.addWidget(self.collision_enabled, 0, 0, 1, 4)
        for column, direction in enumerate(
                self.service.MOVEMENT_DIRECTIONS):
            cell = QWidget()
            cell_layout = QVBoxLayout(cell)
            cell_layout.setContentsMargins(2, 2, 2, 2)
            button = QPushButton(
                f"Editar {DIRECTION_LABELS[direction]}...")
            summary = QLabel("—")
            summary.setStyleSheet("color:#aeb8c4;")
            button.clicked.connect(
                lambda unused=False, d=direction:
                self._edit_collision(d))
            cell_layout.addWidget(button)
            if direction in ("left", "right"):
                mirror_button = QPushButton("Usar oposto espelhado")
                mirror_button.clicked.connect(
                    lambda unused=False, d=direction:
                    self._use_opposite_collision_mirrored(d))
                cell_layout.addWidget(mirror_button)
            cell_layout.addWidget(summary)
            collision_layout.addWidget(cell, 1, column)
            self.collision_buttons[direction] = button
            self.collision_summaries[direction] = summary
        collision_hint = QLabel(
            "A máscara de movimento continua estável por direção e "
            "independente dos frames. Left e Right são dados físicos "
            "separados; 'Usar oposto espelhado' é apenas um atalho de "
            "autoria e não cria dependência automática entre os lados.")
        collision_hint.setWordWrap(True)
        collision_hint.setStyleSheet("color:#aeb8c4;")
        collision_layout.addWidget(collision_hint, 2, 0, 1, 4)

        self.hurtbox_enabled = QCheckBox("Ativar Hurtbox autorável")
        self.hurtbox_enabled.toggled.connect(self._hurtbox_toggled)
        self.hurtbox_button = QPushButton("Editar Hurtbox...")
        self.hurtbox_button.clicked.connect(self._edit_hurtbox)
        self.hurtbox_summary = QLabel("não definida")
        self.hurtbox_summary.setStyleSheet("color:#aeb8c4;")

        hurtbox_group = QGroupBox("Hurtbox / área que recebe dano")
        hurtbox_layout = QGridLayout(hurtbox_group)
        hurtbox_layout.addWidget(self.hurtbox_enabled, 0, 0, 1, 2)
        hurtbox_layout.addWidget(self.hurtbox_button, 1, 0)
        hurtbox_layout.addWidget(self.hurtbox_summary, 1, 1)
        hurtbox_hint = QLabel(
            "A Hurtbox é uma forma de combate estável, separada da "
            "Movement Collision. Ela não muda a cada frame nem altera o "
            "bloqueio contra paredes. Use o primeiro Idle / Down como "
            "referência e ajuste somente a área que deve receber dano.")
        hurtbox_hint.setWordWrap(True)
        hurtbox_hint.setStyleSheet("color:#aeb8c4;")
        hurtbox_layout.addWidget(hurtbox_hint, 2, 0, 1, 2)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Cancel |
            QDialogButtonBox.StandardButton.Ok)
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addWidget(identity)
        layout.addWidget(help_label)
        layout.addWidget(scroll, 1)
        layout.addWidget(collision_group)
        layout.addWidget(hurtbox_group)
        layout.addWidget(buttons)
        self.resize(1220, 820)
        self.setWindowTitle(
            "Configurar Player" if definition else "Adicionar Player")

        if definition is not None:
            self._load_definition(definition)
        else:
            self._collision_toggled(False)
            self._hurtbox_toggled(False)

    def _load_definition(self, definition: ContentDefinition) -> None:
        request = self.service.request_for(
            self.workspace, definition, self.asset_root)
        self.player_id.setText(request.player_id)
        self.player_id.setReadOnly(True)
        self.name.setText(request.display_name)
        index = self.progression.findData(request.progression_id)
        if index >= 0:
            self.progression.setCurrentIndex(index)
        else:
            self.progression.setEditText(request.progression_id)
        self.sequences = {
            state: dict(directions)
            for state, directions in request.sequences.items()
        }
        self.movement_collision = dict(request.movement_collision)
        self.collision_enabled.setChecked(
            request.movement_collision_enabled)
        self._refresh_summaries()
        self._refresh_collision_summaries()
        self._collision_toggled(
            request.movement_collision_enabled)
        self.hurtbox = request.hurtbox
        self.hurtbox_enabled.setChecked(request.hurtbox_enabled)
        self._refresh_hurtbox_summary()
        self._hurtbox_toggled(request.hurtbox_enabled)

    def _refresh_summaries(self) -> None:
        for label in self.summary_labels.values():
            label.setText("—")
        for state, directions in self.sequences.items():
            for direction, spec in directions.items():
                label = self.summary_labels.get((state, direction))
                if label is None:
                    continue
                mirror = " • flip X" if spec.flip_x else ""
                label.setText(
                    f"{len(spec.frame_indices)} frame(s) • "
                    f"{spec.frame_width}x{spec.frame_height}{mirror}")

    def _edit_sequence(self, state: str, direction: str) -> None:
        if not self.workspace.definitions("visualImages"):
            QMessageBox.information(
                self, self.windowTitle(),
                "Importe primeiro o PNG em Spritesheet / Animação.")
            return
        initial = self.sequences.get(state, {}).get(direction)
        dialog = FrameSequenceDialog(
            self.workspace, self.asset_root,
            f"{STATE_LABELS[state]} — {DIRECTION_LABELS[direction]}",
            initial, self)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        if dialog.result_spec is None:
            return
        self.sequences.setdefault(state, {})[
            direction] = dialog.result_spec
        self._refresh_summaries()

    def _use_opposite_mirrored(
            self, state: str, direction: str) -> None:
        if direction not in ("left", "right"):
            return
        opposite = "right" if direction == "left" else "left"
        source = self.sequences.get(state, {}).get(opposite)
        if source is None:
            QMessageBox.information(
                self, self.windowTitle(),
                f"Configure primeiro {DIRECTION_LABELS[opposite]} neste estado.")
            return
        self.sequences.setdefault(state, {})[direction] = FrameSequenceSpec(
            image_id=source.image_id,
            frame_width=source.frame_width,
            frame_height=source.frame_height,
            spacing=source.spacing,
            origin_x=source.origin_x,
            origin_y=source.origin_y,
            duration_ticks=source.duration_ticks,
            loop=source.loop,
            flip_x=not source.flip_x,
            frame_indices=source.frame_indices,
            columns=source.columns,
        )
        self._refresh_summaries()

    def _use_opposite_collision_mirrored(
            self, direction: str) -> None:
        if direction not in ("left", "right"):
            return
        opposite = "right" if direction == "left" else "left"
        source = self.movement_collision.get(opposite)
        if source is None:
            QMessageBox.information(
                self, self.windowTitle(),
                f"Configure primeiro {DIRECTION_LABELS[opposite]} "
                "na Movement Collision.")
            return
        self.movement_collision[direction] = (
            self.service.mirror_mask_horizontal(source))
        self._refresh_collision_summaries()

    def _collision_toggled(self, checked: bool) -> None:
        for button in self.collision_buttons.values():
            button.setEnabled(checked)
        if not checked:
            return
        self._refresh_collision_summaries()

    def _refresh_collision_summaries(self) -> None:
        for direction, label in self.collision_summaries.items():
            spec = self.movement_collision.get(direction)
            if spec is None:
                label.setText("não definida")
                continue
            active = sum(1 for cell in spec.cells if cell)
            label.setText(
                f"{spec.width}x{spec.height} • {active} pixel(s) ativos")

    def _collision_frame_image(self, direction: str) -> QImage | None:
        spec = self.sequences.get("idle", {}).get(direction)
        if spec is None or not spec.frame_indices or spec.columns <= 0:
            return None
        image_definition = self.workspace.find(
            "visualImages", spec.image_id)
        if image_definition is None:
            return None
        root = (
            self.asset_root
            if image_definition.data.get("root") == "gameAssets"
            else self.workspace.root)
        relative = image_definition.data.get("relativePath")
        if root is None or not isinstance(relative, str):
            return None
        image = QImage(str(root / relative))
        if image.isNull():
            return None
        index = spec.frame_indices[0]
        row, column = divmod(index, spec.columns)
        x = spec.origin_x + column * (spec.frame_width + spec.spacing)
        y = spec.origin_y + row * (spec.frame_height + spec.spacing)
        frame = image.copy(
            x, y, spec.frame_width, spec.frame_height)
        if frame.isNull():
            return None
        return (
            frame.mirrored(True, False)
            if spec.flip_x else frame)

    def _default_collision_mask(
            self, direction: str) -> PlayerCollisionMaskSpec | None:
        spec = self.sequences.get("idle", {}).get(direction)
        if spec is None:
            return None
        width = spec.frame_width
        height = spec.frame_height
        return PlayerCollisionMaskSpec(
            width=width,
            height=height,
            origin_x=-(width // 2),
            origin_y=-(height - 1),
            cells=(0,) * (width * height),
        )

    def _edit_collision(self, direction: str) -> None:
        if not self.collision_enabled.isChecked():
            return
        image = self._collision_frame_image(direction)
        if image is None:
            QMessageBox.information(
                self, self.windowTitle(),
                "Configure primeiro o Idle dessa direção. "
                "A máscara usa o primeiro frame Idle como referência visual.")
            return
        current = self.movement_collision.get(direction)
        if current is None:
            current = self._default_collision_mask(direction)
        if current is None:
            return
        mask = {
            "width": current.width,
            "height": current.height,
            "origin": {
                "x": current.origin_x,
                "y": current.origin_y,
            },
            "cells": list(current.cells),
        }
        dialog = ShapeMaskEditorDialog(
            image, mask, self.translate, self,
            title_key="mask_editor_title")
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        result = dialog.result_mask()
        origin = result.get("origin", {})
        cells = result.get("cells", [])
        if not isinstance(origin, dict) or not isinstance(cells, list):
            return
        self.movement_collision[direction] = PlayerCollisionMaskSpec(
            width=int(result.get("width", image.width())),
            height=int(result.get("height", image.height())),
            origin_x=int(origin.get("x", 0)),
            origin_y=int(origin.get("y", 0)),
            cells=tuple(int(cell) for cell in cells),
        )
        self._refresh_collision_summaries()

    def _hurtbox_toggled(self, checked: bool) -> None:
        self.hurtbox_button.setEnabled(checked)
        if checked:
            self._refresh_hurtbox_summary()

    def _refresh_hurtbox_summary(self) -> None:
        if self.hurtbox is None:
            self.hurtbox_summary.setText("não definida")
            return
        active = sum(1 for cell in self.hurtbox.cells if cell)
        self.hurtbox_summary.setText(
            f"{self.hurtbox.width}x{self.hurtbox.height} • "
            f"{active} pixel(s) ativos")

    def _default_hurtbox_mask(self) -> PlayerCollisionMaskSpec | None:
        spec = self.sequences.get("idle", {}).get("down")
        if spec is None:
            return None
        width = spec.frame_width
        height = spec.frame_height

        body_width = max(1, round(width * 14 / 32))
        body_height = max(1, round(height * 22 / 32))
        start_x = max(0, (width - body_width) // 2)
        end_x = min(width, start_x + body_width)
        end_y = max(1, height - 1)
        start_y = max(0, end_y - body_height)

        cells = [0] * (width * height)
        for y in range(start_y, end_y):
            for x in range(start_x, end_x):
                cells[y * width + x] = 1
        return PlayerCollisionMaskSpec(
            width=width,
            height=height,
            origin_x=-(width // 2),
            origin_y=-(height - 1),
            cells=tuple(cells),
        )

    def _edit_hurtbox(self) -> None:
        if not self.hurtbox_enabled.isChecked():
            return
        image = self._collision_frame_image("down")
        if image is None:
            QMessageBox.information(
                self, self.windowTitle(),
                "Configure primeiro Idle / Down. "
                "A Hurtbox usa esse frame como referência visual.")
            return
        current = self.hurtbox or self._default_hurtbox_mask()
        if current is None:
            return
        mask = {
            "width": current.width,
            "height": current.height,
            "origin": {
                "x": current.origin_x,
                "y": current.origin_y,
            },
            "cells": list(current.cells),
        }
        dialog = ShapeMaskEditorDialog(
            image, mask, self.translate, self,
            title_key="mask_editor_title")
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        result = dialog.result_mask()
        origin = result.get("origin", {})
        cells = result.get("cells", [])
        if not isinstance(origin, dict) or not isinstance(cells, list):
            return
        self.hurtbox = PlayerCollisionMaskSpec(
            width=int(result.get("width", image.width())),
            height=int(result.get("height", image.height())),
            origin_x=int(origin.get("x", 0)),
            origin_y=int(origin.get("y", 0)),
            cells=tuple(int(cell) for cell in cells),
        )
        self._refresh_hurtbox_summary()

    def _save(self) -> None:
        progression_id = str(
            self.progression.currentData()
            or self.progression.currentText()).strip()
        request = PlayerAuthoringRequest(
            player_id=self.player_id.text().strip(),
            display_name=self.name.text().strip(),
            progression_id=progression_id,
            sequences=self.sequences,
            movement_collision_enabled=(
                self.collision_enabled.isChecked()),
            movement_collision=dict(self.movement_collision),
            hurtbox_enabled=self.hurtbox_enabled.isChecked(),
            hurtbox=self.hurtbox,
        )
        try:
            self.service.save(
                self.workspace, request,
                editing=self.definition is not None)
        except ValueError as error:
            QMessageBox.warning(
                self, self.windowTitle(), str(error))
            return
        self.accept()


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

        self.search = QLineEdit()
        self.search.setPlaceholderText("Procurar Player")
        self.search.textChanged.connect(self.refresh)
        self.list = QListWidget()
        self.list.currentItemChanged.connect(self._selection_changed)
        self.list.itemDoubleClicked.connect(
            lambda unused: self.edit_selected())

        self.add_button = QPushButton("Adicionar Player...")
        self.edit_button = QPushButton("Configurar Player...")
        self.delete_button = QPushButton(self.translate("delete"))
        self.add_button.clicked.connect(self.add_player)
        self.edit_button.clicked.connect(self.edit_selected)
        self.delete_button.clicked.connect(self.delete_selected)

        row = QHBoxLayout()
        row.addWidget(self.add_button)
        row.addWidget(self.edit_button)
        row.addWidget(self.delete_button)

        hint = QLabel(
            "Importe o spritesheet uma vez e monte aqui cada estado "
            "selecionando apenas os frames necessários.")
        hint.setWordWrap(True)
        hint.setStyleSheet("color:#aeb8c4;")

        layout = QVBoxLayout(self)
        layout.addWidget(self.search)
        layout.addWidget(self.list, 1)
        layout.addLayout(row)
        layout.addWidget(hint)
        self.refresh()

    def retranslate(self, translator: Translator) -> None:
        self.translate = translator
        self.delete_button.setText(self.translate("delete"))

    def set_context(self, workspace: ContentWorkspace | None,
                    asset_root: Path | None) -> None:
        self.workspace = workspace
        self.asset_root = asset_root
        self.refresh()

    def refresh(self, unused: object = None) -> None:
        del unused
        self.list.clear()
        if self.workspace is None:
            return
        for definition in self.workspace.definitions(
                "players", self.search.text().strip()):
            item = QListWidgetItem(
                f"{definition.display_name}\n{definition.definition_id}")
            item.setData(
                Qt.ItemDataRole.UserRole,
                definition.definition_id)
            self.list.addItem(item)
        self._selection_changed(
            self.list.currentItem(), None)

    def _selected(self) -> ContentDefinition | None:
        if self.workspace is None or self.list.currentItem() is None:
            return None
        return self.workspace.find(
            "players",
            str(self.list.currentItem().data(
                Qt.ItemDataRole.UserRole) or ""))

    def _selection_changed(self, current: QListWidgetItem | None,
                           previous: QListWidgetItem | None) -> None:
        del previous
        enabled = current is not None
        self.edit_button.setEnabled(enabled)
        self.delete_button.setEnabled(enabled)

    def add_player(self) -> None:
        if self.workspace is None:
            return
        dialog = PlayerDefinitionDialog(
            self.workspace, self.asset_root, self.translate, parent=self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.refresh()
            self.changed.emit()
            self.status_changed.emit("Player criado")

    def edit_selected(self) -> None:
        definition = self._selected()
        if definition is None or self.workspace is None:
            return
        try:
            dialog = PlayerDefinitionDialog(
                self.workspace, self.asset_root, self.translate,
                definition=definition, parent=self)
        except ValueError as error:
            QMessageBox.warning(
                self, "Configurar Player", str(error))
            return
        if dialog.exec() == QDialog.DialogCode.Accepted:
            self.refresh()
            self.changed.emit()
            self.status_changed.emit("Player atualizado")

    def delete_selected(self) -> None:
        definition = self._selected()
        if definition is None or self.workspace is None:
            return
        if QMessageBox.question(
                self, "Excluir Player",
                f"Excluir {definition.display_name}?") != (
                    QMessageBox.StandardButton.Yes):
            return
        visual_id = str(definition.data.get("visualSetId", ""))
        descriptor = self.workspace.find(
            "authoringDescriptors", definition.definition_id)
        visual = self.workspace.find("playerVisuals", visual_id)
        if descriptor is not None and descriptor.origin == "project":
            self.workspace.delete_definition(descriptor)
        if visual is not None and visual.origin == "project":
            self.workspace.delete_definition(visual)
        self.workspace.delete_definition(definition)
        self.refresh()
        self.changed.emit()
        self.status_changed.emit("Player excluído")
