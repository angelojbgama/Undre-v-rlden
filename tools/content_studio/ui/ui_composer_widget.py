"""UI Composer — visual authoring for the UI Engine screens (docs/UI_ENGINE.md, UI-3).

Layout/hierarchy/canvas/inspector over the UiAuthoringService. The canvas is a
data-driven preview (layout, meter fills, bindings against Preview Data); the
C++ runtime remains the pixel authority. Preview Data is editor-local state
and is never serialized into authored content.
"""

from __future__ import annotations

from PySide6.QtCore import QPointF, QRectF, Qt, Signal
from PySide6.QtGui import QColor, QPainter, QPen
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QInputDialog,
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..services.localization import Translator
from ..services.ui_authoring_service import UiAuthoringService
from ..services import ui_registry
from .icon_registry import icon

LOGICAL_WIDTH = 272
LOGICAL_HEIGHT = 224
DEFAULT_ZOOM = 2


def resolve_position(layout: dict, content_size: tuple[int, int]) -> tuple[int, int]:
    """Python mirror of ui::resolveNodePosition (logical screen anchors)."""
    width = layout.get("width", content_size[0])
    height = layout.get("height", content_size[1])
    x, y = int(layout.get("offsetX", 0)), int(layout.get("offsetY", 0))
    anchor = layout.get("anchor", "topLeft")
    if "Center" in anchor or anchor == "center":
        x += (LOGICAL_WIDTH - width) // 2
    if anchor.endswith("Right"):
        x += LOGICAL_WIDTH - width
    if anchor.startswith("center") or anchor == "center":
        y += (LOGICAL_HEIGHT - height) // 2
    if anchor.startswith("bottom"):
        y += LOGICAL_HEIGHT - height
    return x, y


class UiCanvas(QWidget):
    """Data-driven preview of the selected screen at 272x224 logical pixels."""

    node_selected = Signal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._screen_data: dict | None = None
        self._preview: dict[str, int] = {}
        self._zoom = DEFAULT_ZOOM
        self._node_rects: list[tuple[QRectF, str]] = []
        self.setFixedSize(LOGICAL_WIDTH * self._zoom, LOGICAL_HEIGHT * self._zoom)

    def set_screen_data(self, screen_data: dict | None) -> None:
        self._screen_data = screen_data
        self.update()

    def set_preview_values(self, values: dict[str, int]) -> None:
        self._preview = values
        self.update()

    def _value(self, path: str, default: int = 0) -> int:
        return int(self._preview.get(path, default))

    def _bound_value(self, node: dict, property: str, default: int) -> int:
        for binding in node.get("bindings", []):
            if binding.get("property") == property:
                return self._value(str(binding.get("source")), default)
        return default

    def mousePressEvent(self, event) -> None:  # noqa: N802 - Qt naming
        position = event.position() / self._zoom
        for rect, node_id in reversed(self._node_rects):
            if rect.contains(position):
                self.node_selected.emit(node_id)
                return
        super().mousePressEvent(event)

    def paintEvent(self, event) -> None:  # noqa: N802 - Qt naming
        painter = QPainter(self)
        painter.scale(self._zoom, self._zoom)
        painter.fillRect(QRectF(0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT), QColor("#0a0a12"))
        self._node_rects.clear()
        if self._screen_data:
            self._paint_node(painter, self._screen_data.get("root", {}), None)
        painter.end()

    def _paint_node(self, painter: QPainter, node: dict, parent: dict | None) -> None:
        layout = node.get("layout", {})
        content = (LOGICAL_WIDTH, LOGICAL_HEIGHT)
        x, y = resolve_position(layout, content)
        zoom = self._zoom
        node_id = str(node.get("id", ""))
        component = str(node.get("component", "group"))
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)

        if component in ("group", "panel"):
            width = int(layout.get("width", LOGICAL_WIDTH))
            height = int(layout.get("height", LOGICAL_HEIGHT))
            rect = QRectF(x, y, width, height)
            painter.setPen(QPen(QColor(90, 100, 140, 200), 0))
            painter.setBrush(QColor(40, 48, 80, 60))
            painter.drawRect(rect)
            self._node_rects.append((rect, node_id))
            painter.setPen(QColor(150, 160, 200))
            painter.drawText(QPointF(x + 2, y + 9), node_id)
            for child in node.get("children", []):
                self._paint_node(painter, child, node)
            return

        if component == "meter":
            meter = node.get("meter", {})
            sprites = meter.get("sprites", {})
            full_id = sprites.get("full") or sprites.get("fill")
            # Preview geometry: 11x10 segments / 24x12 fill until real art
            # metadata is resolvable; the C++ runtime remains the authority.
            box_w, box_h = (11, 10) if meter.get("mode") == "segmented" else (24, 12)
            value = self._bound_value(node, "value", 0)
            maximum = self._bound_value(node, "maximum", 1)
            spacing = int(meter.get("spacing", 0))
            if meter.get("mode") == "segmented":
                stride = box_w + spacing
                total_w = max(1, int(maximum)) * stride
                x, y = resolve_position(layout, (total_w, box_h))
                empty = meter.get("emptyRect", {})
                for index in range(max(0, int(maximum))):
                    ex = x + index * stride
                    if index < value:
                        painter.fillRect(QRectF(ex, y, box_w, box_h), QColor("#d84868"))
                    elif empty:
                        e = empty
                        painter.fillRect(
                            QRectF(ex + int(e.get("offsetX", 0)), y + int(e.get("offsetY", 0)),
                                   int(e.get("width", 9)), int(e.get("height", 8))),
                            QColor(int(e.get("color", {}).get("r", 54)),
                                   int(e.get("color", {}).get("g", 30)),
                                   int(e.get("color", {}).get("b", 38))))
                    else:
                        painter.setPen(QPen(QColor(90, 100, 140), 0))
                        painter.drawRect(QRectF(ex, y, box_w, box_h))
                rect = QRectF(x, y, max(1, int(maximum)) * stride, box_h)
            else:
                fraction = 0.0 if maximum <= 0 else max(0.0, min(1.0, value / maximum))
                x, y = resolve_position(layout, (box_w, box_h))
                painter.setPen(QPen(QColor(90, 100, 140), 0))
                painter.drawRect(QRectF(x, y, box_w, box_h))
                if meter.get("mode") == "fillHorizontal":
                    fill_w = round(box_w * fraction)
                    painter.fillRect(QRectF(x, y, fill_w, box_h), QColor("#58c0d8"))
                    rect = QRectF(x, y, box_w, box_h)
                else:
                    fill_h = round(box_h * fraction)
                    painter.fillRect(QRectF(x, y + box_h - fill_h, box_w, fill_h),
                                     QColor("#58c0d8"))
                    rect = QRectF(x, y, box_w, box_h)
            self._node_rects.append((rect, node_id))
            painter.setPen(QColor(150, 160, 200))
            painter.drawText(QPointF(x, y - 2), f"{node_id} ({full_id or '?'})")
            return

        if component == "text":
            literal = str(node.get("text", ""))
            bound = None
            for binding in node.get("bindings", []):
                if binding.get("property") == "text":
                    bound = self._value(str(binding.get("source")))
            label = str(bound) if bound is not None else literal
            x, y = resolve_position(layout, (0, 0))
            painter.setPen(QColor(230, 230, 240))
            painter.drawText(QPointF(x, y + 8), label)
            metrics = painter.fontMetrics()
            self._node_rects.append((QRectF(x, y, max(8, metrics.horizontalAdvance(label)), 9),
                                     node_id))
            return

        # image / animatedImage preview boxes (art resolution stays with the
        # runtime; the composer previews placement and bindings).
        width = int(layout.get("width", 16))
        height = int(layout.get("height", 16))
        x, y = resolve_position(layout, (width, height))
        rect = QRectF(x, y, width, height)
        painter.setPen(QPen(QColor(120, 130, 170), 0))
        painter.setBrush(QColor(56, 60, 90, 120))
        painter.drawRect(rect)
        if component == "animatedImage":
            painter.drawLine(QPointF(x, y), QPointF(x + width, y + height))
        self._node_rects.append((rect, node_id))
        painter.setPen(QColor(150, 160, 200))
        painter.drawText(QPointF(x + 1, y + 8), node_id)
        del zoom, parent


class UiComposerWidget(QWidget):
    """Screens + hierarchy + canvas + inspector for authored uiScreens."""

    def __init__(self, workspace: ContentWorkspace, translator: Translator,
                 parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.translator = translator
        self.service = UiAuthoringService(workspace)
        self._selected_screen: str | None = None
        self._selected_node: str | None = None
        # True while the inspector rebuilds: setValue/setCurrentText would
        # otherwise fire commit handlers with freshly loaded values.
        self._reloading = False
        self._preview_values: dict[str, int] = {
            "player.health.current": 3,
            "player.health.max": 5,
            "player.gold": 152,
            "player.ammo.amount": 7,
        }
        self._build_ui()
        self.refresh()

    # -- UI construction ---------------------------------------------------

    def _build_ui(self) -> None:
        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.setContentsMargins(4, 4, 4, 4)
        self.screens_list = QListWidget()
        self.screens_list.currentRowChanged.connect(self._screen_row_changed)
        screen_buttons = QHBoxLayout()
        self.add_screen_button = QPushButton(self.translator("ui_composer_add_screen"))
        self.add_screen_button.setIcon(icon("add"))
        self.add_screen_button.clicked.connect(self._add_screen)
        self.remove_screen_button = QPushButton(self.translator("ui_composer_remove_screen"))
        self.remove_screen_button.setIcon(icon("delete"))
        self.remove_screen_button.clicked.connect(self._remove_screen)
        screen_buttons.addWidget(self.add_screen_button)
        screen_buttons.addWidget(self.remove_screen_button)
        left_layout.addWidget(QLabel(self.translator("ui_composer_screens")))
        left_layout.addWidget(self.screens_list, 1)
        left_layout.addLayout(screen_buttons)
        self.hierarchy = QTreeWidget()
        self.hierarchy.setHeaderLabel(self.translator("ui_composer_hierarchy"))
        self.hierarchy.currentItemChanged.connect(self._hierarchy_changed)
        node_buttons = QHBoxLayout()
        self.add_node_button = QPushButton(self.translator("ui_composer_add_node"))
        self.add_node_button.setIcon(icon("add"))
        self.add_node_button.clicked.connect(self._add_node)
        self.remove_node_button = QPushButton(self.translator("ui_composer_remove_node"))
        self.remove_node_button.setIcon(icon("delete"))
        self.remove_node_button.clicked.connect(self._remove_node)
        node_buttons.addWidget(self.add_node_button)
        node_buttons.addWidget(self.remove_node_button)
        left_layout.addWidget(self.hierarchy, 2)
        left_layout.addLayout(node_buttons)

        center = QWidget()
        center_layout = QVBoxLayout(center)
        self.canvas = UiCanvas()
        self.canvas.node_selected.connect(self._select_node)
        center_layout.addWidget(self.canvas, 0, Qt.AlignmentFlag.AlignHCenter)
        preview_form = QFormLayout()
        self._preview_spins: dict[str, QSpinBox] = {}
        for path, value in self._preview_values.items():
            spin = QSpinBox()
            spin.setRange(-1_000_000, 1_000_000)
            spin.setValue(value)
            spin.valueChanged.connect(self._preview_changed)
            self._preview_spins[path] = spin
            preview_form.addRow(QLabel(path), spin)
        preview_form.addRow(QLabel(self.translator("ui_composer_preview_hint")))
        center_layout.addLayout(preview_form)
        center_layout.addStretch(1)

        self.inspector = QWidget()
        self._inspector_layout = QFormLayout(self.inspector)
        self.validation_list = QListWidget()
        validation_layout = QVBoxLayout()
        validation_layout.addWidget(QLabel(self.translator("ui_composer_validation")))
        validation_layout.addWidget(self.validation_list)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        right_layout.setContentsMargins(4, 4, 4, 4)
        right_layout.addWidget(self.inspector)
        right_layout.addLayout(validation_layout)
        right_layout.addStretch(1)

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(left)
        splitter.addWidget(center)
        splitter.addWidget(right)
        splitter.setSizes([280, 620, 340])
        root_layout = QVBoxLayout(self)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.addWidget(splitter)

    # -- context / refresh -------------------------------------------------

    def set_context(self, workspace: ContentWorkspace, translator: Translator) -> None:
        self.workspace = workspace
        self.translator = translator
        self.service.set_context(workspace)
        self.refresh()

    def refresh(self) -> None:
        self.screens_list.blockSignals(True)
        self.screens_list.clear()
        selected_row = -1
        for index, screen in enumerate(self.service.screens()):
            item = QListWidgetItem(str(screen.data.get("id", "?")))
            self.screens_list.addItem(item)
            if screen.definition_id == self._selected_screen:
                selected_row = index
        self.screens_list.blockSignals(False)
        if selected_row >= 0:
            self.screens_list.setCurrentRow(selected_row)
        else:
            self._selected_screen = None
            self.screens_list.setCurrentRow(-1)
        # Selection changes are swallowed while signals are blocked, so every
        # pane reloads explicitly here.
        self._reload_hierarchy()
        self._reload_inspector()
        self._reload_validation()
        self._update_canvas()

    # -- screens -----------------------------------------------------------

    def _screen_row_changed(self, row: int) -> None:
        screens = self.service.screens()
        if 0 <= row < len(screens):
            self._selected_screen = screens[row].definition_id
        else:
            self._selected_screen = None
        self._selected_node = None
        self._reload_hierarchy()
        self._reload_inspector()
        self._reload_validation()
        self._update_canvas()

    def _add_screen(self) -> None:
        base_id, ok = self._prompt_text(self.translator("ui_composer_new_screen"), self.translator("ui_composer_screen_id"), "screen.nova")
        if not ok:
            return
        try:
            self.service.create_screen(base_id)
        except ValueError as error:
            QMessageBox.warning(self, self.tr("UI Composer"), str(error))
            return
        self._selected_screen = base_id if base_id.startswith("screen.") else f"screen.{base_id}"
        self.refresh()

    def _remove_screen(self) -> None:
        if not self._selected_screen:
            return
        if QMessageBox.question(self, self.tr("UI Composer"),
                                self.translator("ui_composer_remove_screen_question")) != QMessageBox.StandardButton.Yes:
            return
        self.service.delete_screen(self._selected_screen)
        self._selected_screen = None
        self.refresh()

    # -- hierarchy ---------------------------------------------------------

    def _reload_hierarchy(self) -> None:
        self.hierarchy.blockSignals(True)
        self.hierarchy.clear()
        screen = self.service.find(self._selected_screen) if self._selected_screen else None
        if screen is not None:
            def add_children(parent_item: QTreeWidgetItem, node: dict) -> None:
                for child in node.get("children", []):
                    item = QTreeWidgetItem([f"{child.get('id', '?')} — {child.get('component', '?')}"])
                    item.setData(0, Qt.ItemDataRole.UserRole, str(child.get("id", "")))
                    parent_item.addChild(item)
                    add_children(item, child)

            root = screen.data.get("root", {})
            root_item = QTreeWidgetItem([f"{root.get('id', '?')} — {root.get('component', '?')}"])
            root_item.setData(0, Qt.ItemDataRole.UserRole, str(root.get("id", "")))
            self.hierarchy.addTopLevelItem(root_item)
            add_children(root_item, root)
            root_item.setExpanded(True)
        self.hierarchy.blockSignals(False)

    def _hierarchy_changed(self, current: QTreeWidgetItem | None,
                           _previous: QTreeWidgetItem | None) -> None:
        self._selected_node = current.data(0, Qt.ItemDataRole.UserRole) if current else None
        self._reload_inspector()

    def _select_node(self, node_id: str) -> None:
        self._selected_node = node_id
        matches = self.hierarchy.findItems(node_id, Qt.MatchFlag.MatchRecursive | Qt.MatchFlag.MatchExactly, 0)
        if matches:
            self.hierarchy.setCurrentItem(matches[0])
        self._reload_inspector()

    def _add_node(self) -> None:
        if not self._selected_screen:
            return
        node_id, ok = self._prompt_text(self.translator("ui_composer_new_node"), self.translator("ui_composer_node_id"), "hud.novo")
        if not ok:
            return
        component, ok = self._prompt_choice(self.translator("ui_composer_component"), ui_registry.COMPONENTS, "image")
        if not ok:
            return
        parent_id = None
        if self._selected_node and self._selected_node != self._root_id():
            parent_id = self._selected_node
        try:
            self.service.add_node(self._selected_screen, node_id.strip(), component,
                                  parent_id=parent_id)
        except ValueError as error:
            QMessageBox.warning(self, self.tr("UI Composer"), str(error))
            return
        self._reload_hierarchy()
        self._reload_validation()
        self._update_canvas()
        self._select_node(node_id.strip())

    def _remove_node(self) -> None:
        if not self._selected_screen or not self._selected_node:
            return
        try:
            self.service.remove_node(self._selected_screen, self._selected_node)
        except ValueError as error:
            QMessageBox.warning(self, self.tr("UI Composer"), str(error))
            return
        self._selected_node = None
        self._reload_hierarchy()
        self._reload_inspector()
        self._reload_validation()
        self._update_canvas()

    def _root_id(self) -> str:
        screen = self.service.find(self._selected_screen)
        return str(screen.data.get("root", {}).get("id", "")) if screen else ""

    # -- inspector ---------------------------------------------------------

    def _reload_inspector(self) -> None:
        self._reloading = True
        try:
            self._reload_inspector_fields()
        finally:
            self._reloading = False

    def _reload_inspector_fields(self) -> None:
        while self._inspector_layout.count():
            item = self._inspector_layout.takeAt(0)
            widget = item.widget()
            if widget is not None:
                widget.deleteLater()

        screen = self.service.find(self._selected_screen) if self._selected_screen else None
        if screen is None or not self._selected_node:
            self._inspector_layout.addRow(QLabel(self.translator("ui_composer_select_node")))
            return
        node = self._find_node(screen.data.get("root", {}), self._selected_node)
        if node is None:
            return

        header = QLabel(f"{node.get('id')} — {node.get('component')}")
        self._inspector_layout.addRow(header)

        self._component_combo = QComboBox()
        self._component_combo.addItems(list(ui_registry.COMPONENTS))
        self._component_combo.setCurrentText(str(node.get("component")))
        self._component_combo.currentTextChanged.connect(self._commit_component)
        self._inspector_layout.addRow(self.translator("ui_composer_component"), self._component_combo)

        layout = node.get("layout", {})
        self._anchor_combo = QComboBox()
        self._anchor_combo.addItems(list(ui_registry.ANCHORS))
        self._anchor_combo.setCurrentText(str(layout.get("anchor", "topLeft")))
        self._anchor_combo.currentTextChanged.connect(self._commit_anchor)
        self._inspector_layout.addRow(self.translator("ui_composer_anchor"), self._anchor_combo)

        self._offset_x = QSpinBox()
        self._offset_x.setRange(-4096, 4096)
        self._offset_x.setValue(int(layout.get("offsetX", 0)))
        self._offset_x.valueChanged.connect(self._commit_layout)
        self._offset_y = QSpinBox()
        self._offset_y.setRange(-4096, 4096)
        self._offset_y.setValue(int(layout.get("offsetY", 0)))
        self._offset_y.valueChanged.connect(self._commit_layout)
        offset_row = QHBoxLayout()
        offset_row.addWidget(self._offset_x)
        offset_row.addWidget(self._offset_y)
        self._inspector_layout.addRow(self.translator("ui_composer_offset"), offset_row)

        self._width_spin = QSpinBox()
        self._width_spin.setRange(0, 4096)
        self._width_spin.setValue(int(layout.get("width", 0)))
        self._width_spin.valueChanged.connect(self._commit_layout)
        self._height_spin = QSpinBox()
        self._height_spin.setRange(0, 4096)
        self._height_spin.setValue(int(layout.get("height", 0)))
        self._height_spin.valueChanged.connect(self._commit_layout)
        size_row = QHBoxLayout()
        size_row.addWidget(self._width_spin)
        size_row.addWidget(self._height_spin)
        self._inspector_layout.addRow(self.translator("ui_composer_size"), size_row)

        component = str(node.get("component"))
        if component == "image":
            self._sprite_edit = QLineEdit(str(node.get("sprite", "")))
            self._sprite_edit.editingFinished.connect(self._commit_sprite)
            self._inspector_layout.addRow(self.translator("ui_composer_sprite"), self._sprite_edit)
        if component == "animatedImage":
            self._animation_edit = QLineEdit(str(node.get("animation", "")))
            self._animation_edit.editingFinished.connect(self._commit_animation)
            self._inspector_layout.addRow(self.translator("ui_composer_animation"), self._animation_edit)
        if component == "text":
            self._text_edit = QLineEdit(str(node.get("text", "")))
            self._text_edit.editingFinished.connect(self._commit_text)
            self._inspector_layout.addRow(self.translator("ui_composer_text"), self._text_edit)
        if component == "meter":
            meter = node.get("meter", {})
            self._meter_mode = QComboBox()
            self._meter_mode.addItems(list(ui_registry.METER_MODES))
            self._meter_mode.setCurrentText(str(meter.get("mode", "segmented")))
            self._meter_mode.currentTextChanged.connect(self._commit_meter)
            self._inspector_layout.addRow(self.translator("ui_composer_meter_mode"), self._meter_mode)
            self._meter_sprites = QLineEdit(
                " ".join(f"{role}:{sprite}" for role, sprite in meter.get("sprites", {}).items()))
            self._meter_sprites.editingFinished.connect(self._commit_meter_sprites)
            self._inspector_layout.addRow(
                self.translator("ui_composer_meter_sprites"), self._meter_sprites)
            self._meter_spacing = QSpinBox()
            self._meter_spacing.setRange(-128, 128)
            self._meter_spacing.setValue(int(meter.get("spacing", 0)))
            self._meter_spacing.valueChanged.connect(self._commit_meter)
            self._inspector_layout.addRow(self.translator("ui_composer_meter_spacing"), self._meter_spacing)

        self._bindings_list = QListWidget()
        for binding in node.get("bindings", []):
            self._bindings_list.addItem(
                QListWidgetItem(f"{binding.get('property')} <- {binding.get('source')}"))
        self._inspector_layout.addRow(self.translator("ui_composer_bindings"), self._bindings_list)
        self._binding_property = QComboBox()
        self._binding_property.addItems(
            list(ui_registry.COMPONENT_PROPERTIES.get(component, ())))
        self._binding_source = QComboBox()
        self._binding_source.addItems(list(ui_registry.BINDING_PATHS))
        bind_row = QHBoxLayout()
        bind_button = QPushButton(self.translator("ui_composer_bind"))
        bind_button.setIcon(icon("links"))
        bind_button.clicked.connect(self._commit_binding)
        unbind_button = QPushButton(self.translator("ui_composer_unbind"))
        unbind_button.setIcon(icon("unbind"))
        unbind_button.clicked.connect(self._commit_unbind)
        bind_row.addWidget(self._binding_property)
        bind_row.addWidget(self._binding_source)
        bind_row.addWidget(bind_button)
        bind_row.addWidget(unbind_button)
        self._inspector_layout.addRow(self.translator("ui_composer_new_binding"), bind_row)

        self._states_list = QListWidget()
        for state in node.get("states", []):
            condition = state.get("condition", {})
            self._states_list.addItem(QListWidgetItem(
                f"{state.get('id')} {condition.get('operator', '')} "
                f"{condition.get('source', '')} {condition.get('value', '')}".strip()))
        self._inspector_layout.addRow(self.translator("ui_composer_states"), self._states_list)
        state_row = QHBoxLayout()
        add_state_button = QPushButton(self.translator("ui_composer_add_state"))
        add_state_button.setIcon(icon("add"))
        add_state_button.clicked.connect(self._commit_add_state)
        remove_state_button = QPushButton(self.translator("ui_composer_remove_state"))
        remove_state_button.setIcon(icon("delete"))
        remove_state_button.clicked.connect(self._commit_remove_state)
        state_row.addWidget(add_state_button)
        state_row.addWidget(remove_state_button)
        self._inspector_layout.addRow(self.translator("ui_composer_states"), state_row)

        self._actions_list = QListWidget()
        for action in node.get("actions", []):
            self._actions_list.addItem(
                QListWidgetItem(f"{action.get('event')} -> {action.get('action')}"))
        self._action_combo = QComboBox()
        self._action_combo.addItems(list(ui_registry.ACTIONS))
        add_action_button = QPushButton(self.translator("ui_composer_new_action"))
        add_action_button.setIcon(icon("add"))
        add_action_button.clicked.connect(self._commit_add_action)
        action_row = QHBoxLayout()
        action_row.addWidget(self._action_combo)
        action_row.addWidget(add_action_button)
        self._inspector_layout.addRow(self.translator("ui_composer_actions"), self._actions_list)
        self._inspector_layout.addRow(self.translator("ui_composer_new_action"), action_row)

    # -- commit handlers ---------------------------------------------------

    def _try(self, operation) -> None:
        try:
            operation()
        except ValueError as error:
            QMessageBox.warning(self, self.tr("UI Composer"), str(error))
            return
        self._reload_validation()
        self._update_canvas()

    def _commit_component(self, value: str) -> None:
        if self._reloading or not self._selected_node:
            return
        self._try(lambda: self.service.set_component(self._selected_screen, self._selected_node, value))
        self._reload_hierarchy()
        self._reload_inspector()

    def _commit_anchor(self, value: str) -> None:
        if self._reloading or not self._selected_node:
            return
        self._try(lambda: self.service.set_layout(self._selected_screen, self._selected_node,
                                                  anchor=value))

    def _commit_layout(self) -> None:
        if self._reloading or not self._selected_node:
            return
        width = self._width_spin.value()
        height = self._height_spin.value()
        self._try(lambda: self.service.set_layout(
            self._selected_screen, self._selected_node,
            offset_x=self._offset_x.value(), offset_y=self._offset_y.value(),
            width=width if width > 0 else None,
            height=height if height > 0 else None))

    def _commit_sprite(self) -> None:
        if self._reloading:
            return
        text = self._sprite_edit.text().strip()
        self._try(lambda: self.service.set_sprite(
            self._selected_screen, self._selected_node, text or None))

    def _commit_animation(self) -> None:
        if self._reloading:
            return
        text = self._animation_edit.text().strip()
        self._try(lambda: self.service.set_animation(
            self._selected_screen, self._selected_node, text or None))

    def _commit_text(self) -> None:
        if self._reloading:
            return
        self._try(lambda: self.service.set_text(
            self._selected_screen, self._selected_node,
            self._text_edit.text() or None))

    def _commit_meter(self) -> None:
        if self._reloading or not self._selected_node:
            return
        self._try(lambda: self.service.set_meter(
            self._selected_screen, self._selected_node,
            mode=self._meter_mode.currentText(),
            spacing=self._meter_spacing.value()))

    def _commit_meter_sprites(self) -> None:
        if self._reloading:
            return
        sprites: dict[str, str] = {}
        for token in self._meter_sprites.text().split():
            if ":" in token:
                role, sprite_id = token.split(":", 1)
                sprites[role.strip()] = sprite_id.strip()
        self._try(lambda: self.service.set_meter(
            self._selected_screen, self._selected_node, sprites=sprites))

    def _commit_binding(self) -> None:
        if self._reloading or not self._selected_node:
            return
        self._try(lambda: self.service.bind_property(
            self._selected_screen, self._selected_node,
            self._binding_property.currentText(), self._binding_source.currentText()))
        self._reload_inspector()

    def _commit_unbind(self) -> None:
        if self._reloading or not self._selected_node or not self._bindings_list.currentItem():
            return
        property = self._bindings_list.currentItem().text().split(" <- ")[0].split(" ")[0]
        self._try(lambda: self.service.unbind_property(
            self._selected_screen, self._selected_node, property))
        self._reload_inspector()

    def _commit_add_state(self) -> None:
        state_id, ok = self._prompt_text(self.tr("Novo estado"), self.tr("Id do estado"),
                                         "lowHealth")
        if not ok:
            return
        threshold, ok = self._prompt_text(self.tr("Estado"), self.tr("Limite (%)"), "30")
        if not ok:
            return
        self._try(lambda: self.service.add_state(
            self._selected_screen, self._selected_node, state_id,
            visual={"tint": {"r": 255, "g": 64, "b": 64, "a": 255}},
            condition={"source": "player.health.percentage",
                       "operator": "lessOrEqual", "value": int(threshold)}))
        self._reload_inspector()

    def _commit_remove_state(self) -> None:
        current = self._states_list.currentItem()
        if not current:
            return
        state_id = current.text().split(" ")[0]
        self._try(lambda: self.service.remove_state(
            self._selected_screen, self._selected_node, state_id))
        self._reload_inspector()

    def _commit_add_action(self) -> None:
        self._try(lambda: self.service.add_action(
            self._selected_screen, self._selected_node,
            self._action_combo.currentText()))
        self._reload_inspector()

    # -- validation / preview ---------------------------------------------

    def _reload_validation(self) -> None:
        self.validation_list.clear()
        if not self._selected_screen:
            return
        try:
            issues = self.service.validate_screen(self._selected_screen)
        except ValueError as error:
            issues = [str(error)]
        for issue in issues:
            self.validation_list.addItem(QListWidgetItem(issue))
        if not issues:
            self.validation_list.addItem(QListWidgetItem(self.tr("OK")))

    def _preview_changed(self) -> None:
        for path, spin in self._preview_spins.items():
            self._preview_values[path] = spin.value()
        self._update_canvas()

    def _update_canvas(self) -> None:
        screen = self.service.find(self._selected_screen) if self._selected_screen else None
        self.canvas.set_preview_values(self._preview_values)
        self.canvas.set_screen_data(dict(screen.data) if screen else None)

    # -- helpers -----------------------------------------------------------

    @staticmethod
    def _find_node(node: dict, node_id: str) -> dict | None:
        if node.get("id") == node_id:
            return node
        for child in node.get("children", []):
            found = UiComposerWidget._find_node(child, node_id)
            if found is not None:
                return found
        return None

    def _prompt_text(self, title: str, label: str, default: str) -> tuple[str, bool]:
        text, ok = QInputDialog.getText(self, title, label, text=default)
        return text.strip(), bool(ok)

    def _prompt_choice(self, title: str, options, default: str) -> tuple[str, bool]:
        dialog = QDialog(self)
        dialog.setWindowTitle(title)
        combo = QComboBox()
        combo.addItems(list(options))
        combo.setCurrentText(default)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok |
                                   QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        layout = QVBoxLayout(dialog)
        layout.addWidget(combo)
        layout.addWidget(buttons)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            return combo.currentText(), True
        return "", False
