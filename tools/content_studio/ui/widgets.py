from __future__ import annotations

import re
from collections.abc import Callable
from pathlib import Path
from typing import Any

from PySide6.QtCore import QMimeData, Qt, Signal
from PySide6.QtGui import QDrag, QImage, QPixmap, QIcon
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QFormLayout, QGridLayout, QGroupBox, QHBoxLayout, QLabel, QLineEdit,
    QListWidget, QListWidgetItem, QPushButton, QScrollArea, QSpinBox, QVBoxLayout, QWidget,
    QTabWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.authored_entity_index import AuthoredEntityIndex
from ..model.types import ContentDefinition, JsonValue
from ..interaction.drag_payload import StudioDragPayload
from ..services.assets import AssetCatalog
from ..services.localization import Translator

_PATH_PART = re.compile(r"([^.[\]]+)|\[([0-9]+)\]")
ENUM_VALUES: dict[str, tuple[str, ...]] = {
    "facing": ("down", "up", "left", "right"),
    "canonicalFacing": ("down", "up", "left", "right"),
    "faction": ("player", "enemy", "environment", "neutral"),
    "kind": ("meleeHitbox", "projectile", "health", "currency", "item"),
    "category": ("consumable", "equipment", "key", "misc", "enemy", "object", "pickup", "npc"),
    "persistence": ("persistent", "resetOnMapEnter"),
    "root": ("gameAssets", "contentWorkspace"),
    "lifetime": ("transient", "persistent"),
    "role": ("unknown", "floor", "wall", "decoration", "water"),
    "topology": ("unknown", "interior", "north", "east", "south", "west", "corner"),
}


def flatten_scalars(value: JsonValue, prefix: str = "") -> list[tuple[str, JsonValue]]:
    result: list[tuple[str, JsonValue]] = []
    if isinstance(value, dict):
        for key, child in value.items():
            path = f"{prefix}.{key}" if prefix else key
            result.extend(flatten_scalars(child, path))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            result.extend(flatten_scalars(child, f"{prefix}[{index}]"))
        if not value:
            result.append((prefix, "<empty list>"))
    else:
        result.append((prefix, value))
    return result


def _parts(path: str) -> list[str | int]:
    result: list[str | int] = []
    for match in _PATH_PART.finditer(path):
        result.append(match.group(1) if match.group(1) is not None else int(match.group(2)))
    return result


def get_path(value: JsonValue, path: str) -> JsonValue:
    current: JsonValue = value
    for part in _parts(path):
        current = current[part]  # type: ignore[index]
    return current


def set_path(value: JsonValue, path: str, replacement: JsonValue) -> None:
    parts = _parts(path)
    if not parts:
        raise ValueError("empty field path")
    current: Any = value
    for part in parts[:-1]:
        current = current[part]
    current[parts[-1]] = replacement


def pretty_path(path: str) -> str:
    return path.replace(".", " / ").replace("[", " [").replace("]", "]")


class PayloadListWidget(QListWidget):
    """List widget that emits the shared typed drag payload."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setDragEnabled(True)
        self.payload_factory: Callable[[list[QListWidgetItem]], StudioDragPayload | None] | None = None

    def startDrag(self, supported_actions: Qt.DropActions) -> None:  # type: ignore[override]
        if self.payload_factory is None:
            return
        payload = self.payload_factory(self.selectedItems())
        if payload is None:
            return
        mime = QMimeData()
        payload.put_mime_data(mime)
        drag = QDrag(self)
        drag.setMimeData(mime)
        drag.exec(Qt.DropAction.CopyAction)


class StructuredInspector(QWidget):
    changed = Signal(str, object)
    collection_changed = Signal(str, str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._root: JsonValue | None = None
        self._prefix = ""
        self._workspace: ContentWorkspace | None = None
        self._map_ids: list[str] = []
        self._form = QFormLayout()
        self._form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        self._title = QLabel()
        self._title.setWordWrap(True)
        self._title.setStyleSheet("font-weight: bold; padding: 4px;")
        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        body = QWidget()
        body.setLayout(self._form)
        self._scroll.setWidget(body)
        layout = QVBoxLayout(self)
        layout.addWidget(self._title)
        layout.addWidget(self._scroll, 1)

    def clear(self, title: str = "No selection") -> None:
        self._root = None
        self._title.setText(title)
        self._clear_form()

    def set_object(self, title: str, value: JsonValue, prefix: str = "") -> None:
        self._root = value
        self._prefix = prefix
        self._title.setText(title)
        self._clear_form()
        self._populate(value, "")

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self._workspace = workspace

    def set_map_ids(self, map_ids: list[str]) -> None:
        self._map_ids = list(map_ids)

    def _clear_form(self) -> None:
        while self._form.rowCount():
            self._form.removeRow(0)

    def _populate(self, value: JsonValue, path: str) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                child_path = f"{path}.{key}" if path else key
                if isinstance(child, (dict, list)):
                    group = QGroupBox(key)
                    if isinstance(child, list):
                        group_layout = QVBoxLayout(group)
                        group_form = QFormLayout()
                        self._populate_into(group_form, child, child_path)
                        group_layout.addLayout(group_form)
                        self._collection_buttons(group_layout, child_path)
                    else:
                        group_form = QFormLayout(group)
                        self._populate_into(group_form, child, child_path)
                    self._form.addRow(group)
                else:
                    self._add_editor(self._form, key, child_path, child)
        elif isinstance(value, list):
            self._populate_into(self._form, value, path)
        else:
            self._add_editor(self._form, path, path, value)

    def _populate_into(self, form: QFormLayout, value: JsonValue, path: str) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                child_path = f"{path}.{key}" if path else key
                if isinstance(child, (dict, list)):
                    group = QGroupBox(key)
                    if isinstance(child, list):
                        group_layout = QVBoxLayout(group)
                        nested = QFormLayout()
                        self._populate_into(nested, child, child_path)
                        group_layout.addLayout(nested)
                        self._collection_buttons(group_layout, child_path)
                    else:
                        nested = QFormLayout(group)
                        self._populate_into(nested, child, child_path)
                    form.addRow(group)
                else:
                    self._add_editor(form, key, child_path, child)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                child_path = f"{path}[{index}]"
                if isinstance(child, (dict, list)):
                    group = QGroupBox(f"{path}[{index}]")
                    if isinstance(child, list):
                        group_layout = QVBoxLayout(group)
                        nested = QFormLayout()
                        self._populate_into(nested, child, child_path)
                        group_layout.addLayout(nested)
                        self._collection_buttons(group_layout, child_path)
                    else:
                        nested = QFormLayout(group)
                        self._populate_into(nested, child, child_path)
                    form.addRow(group)
                else:
                    self._add_editor(form, f"[{index}]", child_path, child)
            if not value:
                form.addRow(QLabel("(empty collection)"))
        else:
            self._add_editor(form, path, path, value)

    def _add_editor(self, form: QFormLayout, label: str, path: str, value: JsonValue) -> None:
        if self._root is None or value == "<empty list>":
            form.addRow(QLabel(label), QLabel(str(value)))
            return
        field_name = path.rsplit(".", 1)[-1].split("[")[0]
        editor: QWidget
        if value is None and self._reference_category(field_name):
            editor = self._reference_editor(field_name, None, path)
        elif value is None:
            optional = QComboBox()
            optional.addItem("(none)", None)
            optional.addItem("Edit object", {})
            optional.currentIndexChanged.connect(
                lambda unused, p=path, control=optional: self._commit(p, control.currentData()))
            editor = optional
        elif isinstance(value, bool):
            editor = QCheckBox()
            editor.setChecked(value)
            editor.toggled.connect(lambda checked, p=path: self._commit(p, checked))
        elif isinstance(value, int):
            editor = QSpinBox()
            editor.setRange(-2_147_483_648, 2_147_483_647)
            editor.setValue(value)
            editor.editingFinished.connect(lambda p=path, control=editor: self._commit(p, control.value()))  # type: ignore[attr-defined]
        elif isinstance(value, str) and field_name in ENUM_VALUES:
            combo = QComboBox()
            combo.addItems(ENUM_VALUES[field_name])
            if value not in ENUM_VALUES[field_name]:
                combo.insertItem(0, value)
            combo.setCurrentText(value)
            combo.currentTextChanged.connect(lambda text, p=path: self._commit(p, text))
            editor = combo
        elif isinstance(value, str) and field_name == "targetMapId" and self._map_ids:
            editor = self._map_reference_editor(value, path)
        elif isinstance(value, str) and self._reference_category(field_name):
            editor = self._reference_editor(field_name, value, path)
        else:
            editor = QLineEdit(str(value))
            editor.editingFinished.connect(lambda p=path, control=editor: self._commit(p, control.text()))  # type: ignore[attr-defined]
        form.addRow(QLabel(pretty_path(label)), editor)

    def _collection_buttons(self, layout: QVBoxLayout, path: str) -> None:
        buttons = QHBoxLayout()
        add = QPushButton("Add")
        remove = QPushButton("Remove Last")
        add.clicked.connect(lambda unused=False, value=path: self.collection_changed.emit(value, "add"))
        remove.clicked.connect(lambda unused=False, value=path: self.collection_changed.emit(value, "remove"))
        buttons.addWidget(add); buttons.addWidget(remove); layout.addLayout(buttons)

    @staticmethod
    def _reference_category(field_name: str) -> tuple[str, ...] | None:
        if field_name in {"id", "definitionId", "targetMapId", "targetSpawnId", "instanceTarget"}:
            return None
        if field_name == "visualSetId":
            return ("enemyVisuals", "npcVisuals", "objectVisuals")
        if field_name in {"visualId", "imageId"}:
            return ("visualImages", "staticSprites", "animations")
        if field_name.endswith("AnimationId"):
            return ("animations",)
        if field_name in {"behaviorProfileId"}:
            return ("behaviors",)
        if field_name in {"attackId", "attackIds", "projectileDefinitionId"}:
            return ("attacks", "projectiles")
        if field_name in {"rewardProfileId"}:
            return ("rewardProfiles",)
        if field_name in {"rewardGrantId"}:
            return ("rewardGrants",)
        if field_name in {"defaultDialogueId"}:
            return ("dialogues",)
        if field_name in {"itemId"}:
            return ("items",)
        if field_name in {"pickupDefinitionId"}:
            return ("pickups",)
        if field_name.endswith("DefinitionId"):
            return tuple(CONTENT_CATEGORY for CONTENT_CATEGORY in ("enemies", "npcs", "objects", "pickups", "items"))
        return None

    def _reference_editor(self, field_name: str, value: object, path: str) -> QWidget:
        combo = QComboBox()
        combo.addItem("(none)", "")
        categories = self._reference_category(field_name) or ()
        if self._workspace:
            for category in categories:
                for definition in self._workspace.definitions(category):
                    combo.addItem(f"{definition.display_name} [{definition.definition_id}]", definition.definition_id)
        if isinstance(value, str) and value and combo.findData(value) < 0:
            combo.insertItem(1, f"Missing [{value}]", value)
        if isinstance(value, str):
            combo.setCurrentIndex(max(0, combo.findData(value)))
        else:
            combo.setCurrentIndex(0)
        optional = value is None
        combo.currentIndexChanged.connect(
            lambda unused, p=path, control=combo, nullable=optional: self._commit(
                p, None if nullable and not control.currentData() else str(control.currentData() or "")))
        return combo

    def _map_reference_editor(self, value: str, path: str) -> QWidget:
        combo = QComboBox(); combo.addItem("(none)", "")
        for map_id in self._map_ids:
            combo.addItem(map_id, map_id)
        if value and combo.findData(value) < 0:
            combo.insertItem(1, f"Missing [{value}]", value)
        combo.setCurrentIndex(max(0, combo.findData(value)))
        combo.currentIndexChanged.connect(lambda unused, p=path, control=combo: self._commit(p, str(control.currentData() or "")))
        return combo

    def _commit(self, path: str, value: JsonValue) -> None:
        if self._root is None:
            return
        try:
            old = get_path(self._root, path)
        except (KeyError, IndexError, TypeError):
            return
        if isinstance(old, int) and not isinstance(old, bool) and isinstance(value, str):
            try:
                value = int(value)
            except ValueError:
                return
        if old == value:
            return
        self.changed.emit(f"{self._prefix}.{path}".strip("."), value)


class ContentBrowser(QWidget):
    selected = Signal(object)
    place_requested = Signal(str, str)
    definition_changed = Signal()
    find_usages_requested = Signal(object)
    back_requested = Signal()

    def __init__(self, workspace: ContentWorkspace | None = None, allowed: tuple[str, ...] | None = None,
                 parent: QWidget | None = None, translator: Translator | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.allowed = allowed
        self.translate = translator or Translator()
        self.index = AuthoredEntityIndex(workspace)
        self._selected: ContentDefinition | None = None
        self.search = QLineEdit()
        self.search.setPlaceholderText("Search display name / definitionId")
        self.search.textChanged.connect(self.refresh)
        self.category = QComboBox()
        self.category.currentIndexChanged.connect(self.refresh)
        self.list = PayloadListWidget()
        self.list.payload_factory = self._drag_payload
        self.list.currentItemChanged.connect(self._selection_changed)
        self.create_button = QPushButton("Create")
        self.delete_button = QPushButton("Delete")
        self.duplicate_button = QPushButton("Duplicate")
        self.rename_button = QPushButton(self.translate("rename"))
        self.place_button = QPushButton("Place in Map")
        self.usages_button = QPushButton("Find Usages")
        self.back_button = QPushButton("Back")
        self.create_button.clicked.connect(self._create)
        self.delete_button.clicked.connect(self._delete)
        self.duplicate_button.clicked.connect(self._duplicate)
        self.rename_button.clicked.connect(self._rename)
        self.place_button.clicked.connect(self._place)
        self.usages_button.clicked.connect(lambda: self.find_usages_requested.emit(self._selected) if self._selected else None)
        self.back_button.clicked.connect(self.back_requested.emit)
        # Two rows keep the search/category controls and actions inside a narrow
        # dock.  A single horizontal row used by the old Win32 editor could
        # paint over the Object/Pickup tabs when the dock was resized.
        buttons = QGridLayout()
        buttons.addWidget(self.create_button, 0, 0)
        buttons.addWidget(self.delete_button, 0, 1)
        buttons.addWidget(self.duplicate_button, 1, 0)
        buttons.addWidget(self.place_button, 1, 1)
        buttons.addWidget(self.rename_button, 2, 0)
        buttons.addWidget(self.usages_button, 3, 0)
        buttons.addWidget(self.back_button, 3, 1)
        buttons.setColumnStretch(0, 1)
        buttons.setColumnStretch(1, 1)
        layout = QVBoxLayout(self)
        layout.addWidget(self.category)
        layout.addWidget(self.search)
        layout.addWidget(self.list, 1)
        layout.addLayout(buttons)
        self.refresh()

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace
        self.index.set_workspace(workspace)
        self.refresh()

    def set_translator(self, translator: Translator) -> None:
        self.translate = translator
        self.rename_button.setText(self.translate("rename"))

    def select_definition(self, category: str, definition_id: str) -> None:
        if self.allowed and category not in self.allowed:
            return
        index = self.category.findData(category)
        if index >= 0:
            self.category.setCurrentIndex(index)
        self.refresh()
        for row in range(self.list.count()):
            item = self.list.item(row)
            if item.data(Qt.ItemDataRole.UserRole) == (self.workspace.find(category, definition_id).key() if self.workspace and self.workspace.find(category, definition_id) else None):
                self.list.setCurrentRow(row)
                return

    def refresh(self) -> None:
        current = self._selected.key() if self._selected else None
        selected_category = self.category.currentData()
        self.category.blockSignals(True)
        self.category.clear()
        categories = self.allowed or (tuple(self.workspace.category_counts()) if self.workspace else ())
        self.category.addItem("All", "")
        for category in categories:
            self.category.addItem(category, category)
        restore_index = self.category.findData(selected_category)
        self.category.setCurrentIndex(restore_index if restore_index >= 0 else 0)
        self.category.blockSignals(False)
        self.list.clear()
        if not self.workspace:
            return
        selected_category = self.category.currentData()
        if self.allowed:
            candidates = [(candidate.definition, list(candidate.diagnostics))
                          for candidate in self.index.candidates(selected_category or None, self.search.text())]
        else:
            candidates = [(definition, self.workspace.validate_local(definition))
                          for definition in self.workspace.definitions(selected_category or None, self.search.text())]
        for definition, invalid in candidates:
            marker = "  [INVALID]" if any(issue.is_error for issue in invalid) else ""
            item = QListWidgetItem(f"{definition.display_name}  [{definition.definition_id}] ({definition.origin}){marker}")
            item.setData(Qt.ItemDataRole.UserRole, definition.key())
            if invalid:
                item.setToolTip("\n".join(issue.message for issue in invalid))
            self.list.addItem(item)
            if current and definition.key() == current:
                self.list.setCurrentItem(item)

    def _selection_changed(self, item: QListWidgetItem | None, unused: QListWidgetItem | None) -> None:
        del unused
        self._selected = None
        if item and self.workspace:
            key = item.data(Qt.ItemDataRole.UserRole)
            self._selected = self.workspace.find(key.category, key.definition_id)
        self.selected.emit(self._selected)

    def _create(self) -> None:
        if not self.workspace:
            return
        category = self.category.currentData() or (self.allowed[0] if self.allowed else "enemies")
        from PySide6.QtWidgets import QInputDialog
        definition_id, accepted = QInputDialog.getText(self, "Create Definition", "DefinitionId:")
        if accepted and definition_id.strip():
            self.workspace.create_definition(category, definition_id.strip())
            self.refresh()
            self.definition_changed.emit()

    def _delete(self) -> None:
        if self.workspace and self._selected and self._selected.origin == "project":
            self.workspace.delete_definition(self._selected)
            self._selected = None
            self.refresh()
            self.definition_changed.emit()

    def _duplicate(self) -> None:
        if not self.workspace or not self._selected:
            return
        from PySide6.QtWidgets import QInputDialog
        definition_id, accepted = QInputDialog.getText(self, "Duplicate Definition", "New DefinitionId:", text=f"{self._selected.definition_id}.copy")
        if accepted and definition_id.strip():
            data = dict(self._selected.data)
            key = "definitionId" if self._selected.category == "authoringDescriptors" else "id"
            data[key] = definition_id.strip()
            self.workspace.create_definition(self._selected.category, definition_id.strip())
            created = self.workspace.find(self._selected.category, definition_id.strip())
            if created:
                self.workspace.replace_definition(created, data)
            self.refresh()
            self.definition_changed.emit()

    def _rename(self) -> None:
        if not self.workspace or not self._selected or self._selected.origin != "project":
            return
        from PySide6.QtWidgets import QInputDialog
        definition_id, accepted = QInputDialog.getText(self, self.translate("rename"), "DefinitionId:", text=self._selected.definition_id)
        if accepted and definition_id.strip():
            self.workspace.rename_definition(self._selected, definition_id.strip())
            self.refresh()
            self.definition_changed.emit()

    def _place(self) -> None:
        if self._selected and self._selected.category in {"enemies", "npcs", "objects", "pickups"}:
            self.place_requested.emit(self._selected.category, self._selected.definition_id)

    def _drag_payload(self, items: list[QListWidgetItem]) -> StudioDragPayload | None:
        if not items:
            return None
        key = items[0].data(Qt.ItemDataRole.UserRole)
        if key is None or not hasattr(key, "category") or not hasattr(key, "definition_id"):
            return None
        return StudioDragPayload.content(str(key.category), str(key.definition_id))


class TilePalette(QWidget):
    """Small authored tileset selector shared by the map canvas and browser."""

    selected = Signal(str, int, int)
    brush_selected = Signal(str, object, int)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace: ContentWorkspace | None = None
        self.asset_root: Path | None = None
        self.tilesets = QComboBox()
        self.source_index = QSpinBox()
        self.source_index.setRange(0, 65535)
        self.source_index.valueChanged.connect(self._emit_selection)
        self.tilesets.currentIndexChanged.connect(self._tileset_changed)
        self.tiles = PayloadListWidget()
        self.tiles.payload_factory = self._drag_payload
        self.tiles.setViewMode(QListWidget.ViewMode.IconMode)
        self.tiles.setResizeMode(QListWidget.ResizeMode.Adjust)
        self.tiles.setMovement(QListWidget.Movement.Static)
        self.tiles.setSelectionMode(QListWidget.SelectionMode.ExtendedSelection)
        self.tiles.setIconSize(QPixmap(32, 32).size())
        self.tiles.setGridSize(QPixmap(40, 40).size())
        self.tiles.itemSelectionChanged.connect(self._palette_selection_changed)
        self.preview = QLabel("No tileset selected")
        self.preview.setWordWrap(True)
        self.preview.setMinimumHeight(44)
        layout = QFormLayout(self)
        layout.addRow("Tileset", self.tilesets)
        layout.addRow("Source tile", self.source_index)
        layout.addRow(self.tiles)
        layout.addRow(self.preview)

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace
        self.tilesets.blockSignals(True)
        self.tilesets.clear()
        if workspace:
            for definition in workspace.definitions("tilesets"):
                self.tilesets.addItem(f"{definition.display_name} [{definition.definition_id}]", definition.definition_id)
        self.tilesets.blockSignals(False)
        self._tileset_changed(self.tilesets.currentIndex())

    def set_asset_root(self, asset_root: Path | None) -> None:
        self.asset_root = asset_root
        self._tileset_changed(self.tilesets.currentIndex())

    def _tileset_changed(self, index: int) -> None:
        if index < 0 or not self.workspace:
            self.preview.setText("No tileset selected")
            self.tiles.clear()
            return
        definition = self.workspace.find("tilesets", str(self.tilesets.itemData(index)))
        if not definition:
            return
        columns = max(1, int(definition.data.get("columns", 1)))
        rows = max(1, int(definition.data.get("rows", 1)))
        self.source_index.setRange(0, columns * rows - 1)
        self.preview.setText(f"{definition.display_name}\n{columns} × {rows} tiles")
        self.tiles.blockSignals(True)
        self.tiles.clear()
        relative = definition.data.get("relativeAssetPath")
        root = self._asset_root_for(definition)
        source_image = QImage(str(root / relative)) if isinstance(relative, str) and root else QImage()
        tile_size = max(1, int(definition.data.get("tileSize", 16)))
        for source_index in range(columns * rows):
            item = QListWidgetItem(str(source_index))
            item.setData(Qt.ItemDataRole.UserRole, source_index)
            if not source_image.isNull():
                x = source_index % columns * tile_size
                y = source_index // columns * tile_size
                tile = source_image.copy(x, y, tile_size, tile_size)
                item.setIcon(QIcon(QPixmap.fromImage(tile).scaled(32, 32, Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.FastTransformation)))
            self.tiles.addItem(item)
        self.tiles.blockSignals(False)
        if self.tiles.count():
            self.tiles.setCurrentRow(0)
        self._emit_selection()

    def _emit_selection(self) -> None:
        if self.tilesets.currentIndex() >= 0:
            tileset_id = str(self.tilesets.currentData())
            self.selected.emit(tileset_id, self.source_index.value(), 0)
            self._palette_selection_changed()

    def _palette_selection_changed(self) -> None:
        if self.tilesets.currentIndex() < 0:
            return
        selected = [int(item.data(Qt.ItemDataRole.UserRole)) for item in self.tiles.selectedItems()]
        if not selected:
            selected = [self.source_index.value()]
        self.source_index.blockSignals(True)
        self.source_index.setValue(selected[0])
        self.source_index.blockSignals(False)
        self.brush_selected.emit(str(self.tilesets.currentData()), selected, 0)

    def _asset_root_for(self, definition: object) -> Path | None:
        if not self.workspace or not hasattr(definition, "data"):
            return None
        data = definition.data  # type: ignore[attr-defined]
        # Tilesets use the strict authored contract: relativeAssetPath is always
        # resolved under the configured game asset root.
        return self.asset_root

    def _drag_payload(self, items: list[QListWidgetItem]) -> StudioDragPayload | None:
        if not items or self.tilesets.currentIndex() < 0:
            return None
        indices = [int(item.data(Qt.ItemDataRole.UserRole)) for item in items]
        return StudioDragPayload.tile_brush(str(self.tilesets.currentData()), indices)


class SemanticPalette(QWidget):
    """Authored semantic tiles and stamps, kept separate from raw atlas cells."""

    tile_selected = Signal(str, int, int)
    stamp_selected = Signal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace: ContentWorkspace | None = None
        self.family = QComboBox(); self.family.currentTextChanged.connect(self.refresh)
        self.tiles = QListWidget(); self.tiles.currentItemChanged.connect(self._tile_selected)
        self.stamps = QListWidget(); self.stamps.currentItemChanged.connect(self._stamp_selected)
        tile_box = QVBoxLayout(); tile_box.addWidget(QLabel("Semantic Tiles")); tile_box.addWidget(self.family); tile_box.addWidget(self.tiles, 1)
        stamp_box = QVBoxLayout(); stamp_box.addWidget(QLabel("Stamps")); stamp_box.addWidget(self.stamps, 1)
        tabs = QTabWidget(); tile_widget = QWidget(); tile_widget.setLayout(tile_box); stamp_widget = QWidget(); stamp_widget.setLayout(stamp_box); tabs.addTab(tile_widget, "Semantics"); tabs.addTab(stamp_widget, "Stamps")
        layout = QVBoxLayout(self); layout.addWidget(tabs)

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace; self.family.blockSignals(True); self.family.clear(); self.family.addItem("All")
        families: set[str] = set()
        if workspace:
            families = {str(value.data.get("family", "")) for value in workspace.definitions("tileSemantics") if value.data.get("family")}
        self.family.addItems(sorted(families)); self.family.blockSignals(False); self.refresh()

    def refresh(self) -> None:
        self.tiles.clear(); self.stamps.clear()
        if not self.workspace: return
        selected_family = self.family.currentText()
        for definition in self.workspace.definitions("tileSemantics"):
            if selected_family != "All" and str(definition.data.get("family", "")) != selected_family: continue
            item = QListWidgetItem(f"{definition.display_name} [{definition.definition_id}]"); item.setData(Qt.ItemDataRole.UserRole, definition.definition_id); self.tiles.addItem(item)
        for definition in self.workspace.definitions("stamps"):
            item = QListWidgetItem(f"{definition.display_name} [{definition.definition_id}]"); item.setData(Qt.ItemDataRole.UserRole, definition.definition_id); self.stamps.addItem(item)

    def _tile_selected(self, item: QListWidgetItem | None, unused: QListWidgetItem | None) -> None:
        del unused
        if not item or not self.workspace: return
        definition = self.workspace.find("tileSemantics", str(item.data(Qt.ItemDataRole.UserRole)))
        if definition and isinstance(definition.data.get("tilesetId"), str): self.tile_selected.emit(str(definition.data["tilesetId"]), int(definition.data.get("sourceIndex", 0)), 0)

    def _stamp_selected(self, item: QListWidgetItem | None, unused: QListWidgetItem | None) -> None:
        del unused
        if item: self.stamp_selected.emit(str(item.data(Qt.ItemDataRole.UserRole)))


class MapElementsPalette(QWidget):
    """Palette for positional map elements, backed by the common drag contract."""

    selected = Signal(object)

    def __init__(self, labels: dict[str, str] | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        labels = labels or {}
        self.elements = PayloadListWidget()
        entries = (
            ("player_spawn", labels.get("player_spawn", "Player Spawn")),
            ("map_transition", labels.get("map_transition", "Map Transition")),
            ("region", labels.get("region", "Region / Trigger")),
        )
        for element, label in entries:
            item = QListWidgetItem(label)
            item.setData(Qt.ItemDataRole.UserRole, element)
            self.elements.addItem(item)
        self.elements.payload_factory = self._drag_payload
        self.elements.currentItemChanged.connect(self._selection_changed)
        layout = QVBoxLayout(self)
        layout.addWidget(self.elements)
        layout.addWidget(QLabel(labels.get("hint", "Drag an element to the map")))
        self._hint = layout.itemAt(1).widget()

    def retranslate(self, labels: dict[str, str]) -> None:
        labels_by_element = ("player_spawn", "map_transition", "region")
        for index, key in enumerate(labels_by_element):
            if index < self.elements.count():
                self.elements.item(index).setText(labels.get(key, self.elements.item(index).text()))
        self._hint.setText(labels.get("hint", self._hint.text()))

    def _selection_changed(self, item: QListWidgetItem | None, unused: QListWidgetItem | None) -> None:
        del unused
        if item:
            self.selected.emit(StudioDragPayload.map_element_payload(str(item.data(Qt.ItemDataRole.UserRole))))

    def _drag_payload(self, items: list[QListWidgetItem]) -> StudioDragPayload | None:
        if not items:
            return None
        return StudioDragPayload.map_element_payload(str(items[0].data(Qt.ItemDataRole.UserRole)))


class MapBrowser(QWidget):
    selected = Signal(str)
    new_requested = Signal()
    import_requested = Signal()
    remove_requested = Signal(str)
    entry_requested = Signal(str)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.list = QListWidget()
        self.list.currentTextChanged.connect(self.selected.emit)
        self.new_button = QPushButton("New Map")
        self.import_button = QPushButton("Import UMAP")
        self.remove_button = QPushButton("Remove Map")
        self.entry_button = QPushButton("Set Entry")
        self.new_button.clicked.connect(self.new_requested.emit)
        self.import_button.clicked.connect(self.import_requested.emit)
        self.remove_button.clicked.connect(lambda: self.remove_requested.emit(self.list.currentItem().text()) if self.list.currentItem() else None)
        self.entry_button.clicked.connect(lambda: self.entry_requested.emit(self.list.currentItem().text()) if self.list.currentItem() else None)
        buttons = QHBoxLayout()
        for button in (self.new_button, self.import_button, self.remove_button, self.entry_button):
            buttons.addWidget(button)
        layout = QVBoxLayout(self)
        layout.addWidget(QLabel("Maps"))
        layout.addWidget(self.list, 1)
        layout.addLayout(buttons)

    def refresh(self, map_ids: list[str], active: str = "") -> None:
        self.list.blockSignals(True)
        self.list.clear()
        self.list.addItems(map_ids)
        if active:
            matches = self.list.findItems(active, Qt.MatchFlag.MatchExactly)
            if matches:
                self.list.setCurrentItem(matches[0])
        self.list.blockSignals(False)


class LayersPanel(QWidget):
    changed = Signal()
    selected = Signal(int)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.list = QListWidget()
        self.list.currentRowChanged.connect(self.selected.emit)
        self.add_button = QPushButton("Add")
        self.rename_button = QPushButton("Rename")
        self.up_button = QPushButton("Move Up")
        self.down_button = QPushButton("Move Down")
        self.visibility_button = QPushButton("Hide/Show")
        self.remove_button = QPushButton("Remove")
        self.add_button.clicked.connect(self._add)
        self.rename_button.clicked.connect(self._rename)
        self.up_button.clicked.connect(lambda: self._move(-1))
        self.down_button.clicked.connect(lambda: self._move(1))
        self.visibility_button.clicked.connect(self._toggle_visibility)
        self.remove_button.clicked.connect(self._remove)
        buttons = QHBoxLayout()
        for button in (self.add_button, self.rename_button, self.up_button, self.down_button, self.visibility_button, self.remove_button):
            buttons.addWidget(button)
        layout = QVBoxLayout(self)
        layout.addWidget(self.list)
        layout.addLayout(buttons)
        self.document = None

    def set_document(self, document: object | None) -> None:
        self.document = document
        self.refresh()

    def refresh(self) -> None:
        self.list.clear()
        if self.document:
            for layer in self.document.layers:  # type: ignore[attr-defined]
                visible = bool(layer.get("visible", True))
                self.list.addItem(("● " if visible else "○ ") + str(layer.get("name", "Layer")))

    def _add(self) -> None:
        if not self.document:
            return
        from PySide6.QtWidgets import QInputDialog
        name, accepted = QInputDialog.getText(self, "Add Layer", "Layer name:")
        if accepted and name.strip():
            self.document.add_layer(name.strip())  # type: ignore[attr-defined]
            self.refresh(); self.changed.emit()

    def _rename(self) -> None:
        if not self.document or not self.list.currentItem():
            return
        from PySide6.QtWidgets import QInputDialog
        index = self.list.currentRow()
        name, accepted = QInputDialog.getText(self, "Rename Layer", "Layer name:", text=str(self.document.layers[index].get("name", "Layer")))  # type: ignore[attr-defined]
        if accepted and name.strip():
            self.document.rename_layer(index, name.strip())  # type: ignore[attr-defined]
            self.refresh(); self.changed.emit()

    def _toggle_visibility(self) -> None:
        if not self.document or self.list.currentRow() < 0:
            return
        index = self.list.currentRow()
        layer = self.document.layers[index]  # type: ignore[attr-defined]
        self.document.set_layer_visibility(index, not bool(layer.get("visible", True)))  # type: ignore[attr-defined]
        self.refresh(); self.list.setCurrentRow(index); self.changed.emit()

    def _move(self, delta: int) -> None:
        if not self.document or self.list.currentRow() < 0:
            return
        source = self.list.currentRow(); target = source + delta
        if 0 <= target < len(self.document.layers):  # type: ignore[attr-defined]
            self.document.move_layer(source, target)  # type: ignore[attr-defined]
            self.refresh(); self.list.setCurrentRow(target); self.changed.emit()

    def _remove(self) -> None:
        if not self.document or self.list.currentRow() < 0:
            return
        try:
            self.document.remove_layer(self.list.currentRow())  # type: ignore[attr-defined]
        except ValueError:
            return
        self.refresh(); self.changed.emit()


class CollectionPanel(QWidget):
    """Generic typed collection editor for map rules, encounters and links.

    The map owns the schema; this widget only offers list selection and the
    same native scalar inspector used by content definitions and placements.
    """

    changed = Signal()

    def __init__(self, collection: str, label: str, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.collection = collection
        self.setWindowTitle(label)
        self.document = None
        self.entries = QListWidget()
        self.inspector = StructuredInspector()
        self.add_button = QPushButton("Add")
        self.delete_button = QPushButton("Delete")
        self.add_button.clicked.connect(self._add)
        self.delete_button.clicked.connect(self._delete)
        self.entries.currentRowChanged.connect(lambda unused: self._show_current())
        buttons = QHBoxLayout()
        buttons.addWidget(self.add_button); buttons.addWidget(self.delete_button)
        left = QVBoxLayout(); left.addWidget(QLabel(label)); left.addWidget(self.entries, 1); left.addLayout(buttons)
        left_widget = QWidget(); left_widget.setLayout(left); left_widget.setMinimumWidth(180)
        self.inspector.changed.connect(self._edit)
        self.inspector.collection_changed.connect(self._edit_collection)
        layout = QHBoxLayout(self); layout.addWidget(left_widget); layout.addWidget(self.inspector, 1)

    def set_document(self, document: object | None) -> None:
        self.document = document
        self.refresh()

    def set_map_ids(self, map_ids: list[str]) -> None:
        self.inspector.set_map_ids(map_ids)

    def refresh(self) -> None:
        self.entries.clear()
        if not self.document:
            self.inspector.clear()
            return
        values = self.document.data.get(self.collection, [])  # type: ignore[attr-defined]
        if isinstance(values, list):
            for index, value in enumerate(values):
                if isinstance(value, dict):
                    identifier = value.get("id", f"{self.collection}[{index}]")
                    self.entries.addItem(str(identifier))
        self._show_current()

    def _current(self) -> dict[str, JsonValue] | None:
        if not self.document or self.entries.currentRow() < 0:
            return None
        values = self.document.data.get(self.collection, [])  # type: ignore[attr-defined]
        if not isinstance(values, list) or self.entries.currentRow() >= len(values):
            return None
        value = values[self.entries.currentRow()]
        return value if isinstance(value, dict) else None

    def _show_current(self) -> None:
        value = self._current()
        if value is None:
            self.inspector.clear(f"No {self.collection} selected")
        else:
            self.inspector.set_object(str(value.get("id", self.collection)), value)

    def _add(self) -> None:
        if not self.document:
            return
        from PySide6.QtWidgets import QInputDialog
        identifier, accepted = QInputDialog.getText(self, "Add Entry", "ID:")
        if not accepted or not identifier.strip():
            return
        value: dict[str, JsonValue] = {"id": identifier.strip()}
        if self.collection == "links":
            value.update({"trigger": {"x": 0, "y": 0, "width": 16, "height": 16}, "targetMapId": "", "targetSpawnId": ""})
        elif self.collection == "regions":
            value.update({"bounds": {"x": 0, "y": 0, "width": 16, "height": 16}, "environmentEffectId": None})
        elif self.collection == "encounters":
            value.update({"participants": [], "rewardGrantId": None})
        elif self.collection == "worldRules":
            value.update({"once": False, "trigger": {"kind": "mapEntered"}, "conditions": [], "actions": []})
        self.document.mutate(f"Add {self.collection}", lambda: self.document.data.setdefault(self.collection, []).append(value))  # type: ignore[attr-defined]
        self.refresh(); self.changed.emit()

    def _delete(self) -> None:
        if not self.document or self.entries.currentRow() < 0:
            return
        row = self.entries.currentRow()
        self.document.mutate(f"Delete {self.collection}", lambda: self.document.data.get(self.collection, []).pop(row))  # type: ignore[attr-defined]
        self.refresh(); self.changed.emit()

    def _edit(self, path: str, value: object) -> None:
        current = self._current()
        if not self.document or current is None:
            return
        self.document.mutate(f"Edit {self.collection}", lambda: set_path(current, path, value))  # type: ignore[attr-defined]
        self.refresh(); self.changed.emit()

    def _edit_collection(self, path: str, action: str) -> None:
        if not self.document or self.entries.currentRow() < 0:
            return
        try:
            self.document.mutate_collection_entry(self.collection, self.entries.currentRow(), path, action)  # type: ignore[attr-defined]
            self.refresh(); self.changed.emit()
        except (IndexError, TypeError, ValueError):
            return


class AssetBrowser(QWidget):
    selected = Signal(object)
    assign_requested = Signal(object)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.catalog = AssetCatalog()
        self.search = QLineEdit(); self.search.setPlaceholderText("Search assets")
        self.search.textChanged.connect(self.refresh)
        self.list = QListWidget(); self.list.currentItemChanged.connect(self._selected)
        self.assign_button = QPushButton("Assign to selected definition")
        self.assign_button.clicked.connect(self._assign)
        layout = QVBoxLayout(self); layout.addWidget(self.search); layout.addWidget(self.list); layout.addWidget(self.assign_button)

    def set_roots(self, game_root: Path | None, content_root: Path | None) -> None:
        self.catalog.refresh(game_root, content_root); self.refresh()

    def refresh(self) -> None:
        self.list.clear()
        for entry in self.catalog.search(self.search.text()):
            item = QListWidgetItem(f"{entry.root}/{entry.relative_path.as_posix()}")
            item.setData(Qt.ItemDataRole.UserRole, entry)
            self.list.addItem(item)

    def _selected(self, item: QListWidgetItem | None, unused: QListWidgetItem | None) -> None:
        del unused
        self.selected.emit(item.data(Qt.ItemDataRole.UserRole) if item else None)

    def _assign(self) -> None:
        item = self.list.currentItem()
        if item:
            self.assign_requested.emit(item.data(Qt.ItemDataRole.UserRole))
