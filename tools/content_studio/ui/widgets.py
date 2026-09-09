from __future__ import annotations

import re
from collections.abc import Callable
from pathlib import Path
from typing import Any

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QImage, QPixmap
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QFormLayout, QGridLayout, QGroupBox, QHBoxLayout, QLabel, QLineEdit,
    QListWidget, QListWidgetItem, QPushButton, QScrollArea, QSpinBox, QVBoxLayout, QWidget,
)

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue
from ..services.assets import AssetCatalog

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


class StructuredInspector(QWidget):
    changed = Signal(str, object)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._root: JsonValue | None = None
        self._prefix = ""
        self._workspace: ContentWorkspace | None = None
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

    def _clear_form(self) -> None:
        while self._form.rowCount():
            self._form.removeRow(0)

    def _populate(self, value: JsonValue, path: str) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                child_path = f"{path}.{key}" if path else key
                if isinstance(child, (dict, list)):
                    group = QGroupBox(key)
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
        if isinstance(value, bool):
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
        elif isinstance(value, str) and self._reference_category(field_name):
            editor = self._reference_editor(field_name, value, path)
        else:
            editor = QLineEdit(str(value))
            editor.editingFinished.connect(lambda p=path, control=editor: self._commit(p, control.text()))  # type: ignore[attr-defined]
        form.addRow(QLabel(pretty_path(label)), editor)

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

    def _reference_editor(self, field_name: str, value: str, path: str) -> QWidget:
        combo = QComboBox()
        combo.addItem("(none)", "")
        categories = self._reference_category(field_name) or ()
        if self._workspace:
            for category in categories:
                for definition in self._workspace.definitions(category):
                    combo.addItem(f"{definition.display_name} [{definition.definition_id}]", definition.definition_id)
        if value and combo.findData(value) < 0:
            combo.insertItem(1, f"Missing [{value}]", value)
        combo.setCurrentIndex(max(0, combo.findData(value)))
        combo.currentIndexChanged.connect(lambda unused, p=path, control=combo: self._commit(p, str(control.currentData() or "")))
        return combo

    def _commit(self, path: str, value: JsonValue) -> None:
        if self._root is None:
            return
        old = get_path(self._root, path)
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

    def __init__(self, workspace: ContentWorkspace | None = None, allowed: tuple[str, ...] | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace = workspace
        self.allowed = allowed
        self._selected: ContentDefinition | None = None
        self.search = QLineEdit()
        self.search.setPlaceholderText("Search display name / definitionId")
        self.search.textChanged.connect(self.refresh)
        self.category = QComboBox()
        self.category.currentIndexChanged.connect(self.refresh)
        self.list = QListWidget()
        self.list.currentItemChanged.connect(self._selection_changed)
        self.create_button = QPushButton("Create")
        self.delete_button = QPushButton("Delete")
        self.duplicate_button = QPushButton("Duplicate")
        self.place_button = QPushButton("Place in Map")
        self.create_button.clicked.connect(self._create)
        self.delete_button.clicked.connect(self._delete)
        self.duplicate_button.clicked.connect(self._duplicate)
        self.place_button.clicked.connect(self._place)
        # Two rows keep the search/category controls and actions inside a narrow
        # dock.  A single horizontal row used by the old Win32 editor could
        # paint over the Object/Pickup tabs when the dock was resized.
        buttons = QGridLayout()
        buttons.addWidget(self.create_button, 0, 0)
        buttons.addWidget(self.delete_button, 0, 1)
        buttons.addWidget(self.duplicate_button, 1, 0)
        buttons.addWidget(self.place_button, 1, 1)
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
        self.refresh()

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
        for definition in self.workspace.definitions(selected_category or None, self.search.text()):
            invalid = self.workspace.validate_local(definition)
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

    def _place(self) -> None:
        if self._selected and self._selected.category in {"enemies", "npcs", "objects", "pickups"}:
            self.place_requested.emit(self._selected.category, self._selected.definition_id)


class TilePalette(QWidget):
    """Small authored tileset selector shared by the map canvas and browser."""

    selected = Signal(str, int, int)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.workspace: ContentWorkspace | None = None
        self.tilesets = QComboBox()
        self.source_index = QSpinBox()
        self.source_index.setRange(0, 65535)
        self.source_index.valueChanged.connect(self._emit_selection)
        self.tilesets.currentIndexChanged.connect(self._tileset_changed)
        self.preview = QLabel("No tileset selected")
        self.preview.setWordWrap(True)
        self.preview.setMinimumHeight(44)
        layout = QFormLayout(self)
        layout.addRow("Tileset", self.tilesets)
        layout.addRow("Source tile", self.source_index)
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

    def _tileset_changed(self, index: int) -> None:
        if index < 0 or not self.workspace:
            self.preview.setText("No tileset selected")
            return
        definition = self.workspace.find("tilesets", str(self.tilesets.itemData(index)))
        if not definition:
            return
        columns = max(1, int(definition.data.get("columns", 1)))
        rows = max(1, int(definition.data.get("rows", 1)))
        self.source_index.setRange(0, columns * rows - 1)
        self.preview.setText(f"{definition.display_name}\n{columns} × {rows} tiles")
        self._emit_selection()

    def _emit_selection(self) -> None:
        if self.tilesets.currentIndex() >= 0:
            self.selected.emit(str(self.tilesets.currentData()), self.source_index.value(), 0)


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
        self.remove_button = QPushButton("Remove")
        self.add_button.clicked.connect(self._add)
        self.rename_button.clicked.connect(self._rename)
        self.up_button.clicked.connect(lambda: self._move(-1))
        self.down_button.clicked.connect(lambda: self._move(1))
        self.remove_button.clicked.connect(self._remove)
        buttons = QHBoxLayout()
        for button in (self.add_button, self.rename_button, self.up_button, self.down_button, self.remove_button):
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
            self.list.addItems([str(layer.get("name", "Layer")) for layer in self.document.layers])  # type: ignore[attr-defined]

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
        name, accepted = QInputDialog.getText(self, "Rename Layer", "Layer name:", text=self.list.currentItem().text())
        if accepted and name.strip():
            self.document.rename_layer(index, name.strip())  # type: ignore[attr-defined]
            self.refresh(); self.changed.emit()

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
        layout = QHBoxLayout(self); layout.addWidget(left_widget); layout.addWidget(self.inspector, 1)

    def set_document(self, document: object | None) -> None:
        self.document = document
        self.refresh()

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
            value.update({"targetMapId": "", "targetSpawnId": ""})
        elif self.collection == "encounters":
            value.update({"enemyDefinitionId": "", "count": 1, "regionId": ""})
        elif self.collection == "worldRules":
            value.update({"once": False, "trigger": {}, "conditions": [], "actions": []})
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


class AssetBrowser(QWidget):
    selected = Signal(object)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.catalog = AssetCatalog()
        self.search = QLineEdit(); self.search.setPlaceholderText("Search assets")
        self.search.textChanged.connect(self.refresh)
        self.list = QListWidget(); self.list.currentItemChanged.connect(self._selected)
        layout = QVBoxLayout(self); layout.addWidget(self.search); layout.addWidget(self.list)

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
