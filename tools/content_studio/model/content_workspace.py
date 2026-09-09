from __future__ import annotations

import copy
from pathlib import Path
from typing import Iterable

from ..formats.content_json import CONTENT_CATEGORIES, ID_FIELDS, decode_content, iter_definitions, write_content
from .commands import Command, CommandHistory
from .types import ContentDefinition, ContentFile, Diagnostic, JsonValue


DISPLAY_CATEGORY = "authoringDescriptors"


class ContentWorkspace:
    """Authored workspace index independent from the compiled C++ registry.

    A malformed definition is retained whenever its surrounding JSON is readable.
    That is deliberate: authoring discovery must not disappear because a different
    definition cannot compile.
    """

    def __init__(self, root: Path, files: list[ContentFile], diagnostics: list[Diagnostic] | None = None) -> None:
        self.root = root
        self.files = files
        self.diagnostics = diagnostics or []
        self._base_diagnostics = list(self.diagnostics)
        self.history = CommandHistory()
        self._definitions: list[ContentDefinition] = []
        self._rebuild_index()

    @classmethod
    def open(cls, root: Path) -> "ContentWorkspace":
        root = root.expanduser().resolve()
        diagnostics: list[Diagnostic] = []
        if not root.is_dir():
            return cls(root, [], [Diagnostic("error", "content workspace directory does not exist", str(root), "workspace_root_missing")])
        paths = sorted((path for path in root.rglob("*.json") if path.is_file()), key=lambda path: path.as_posix())
        if not paths:
            return cls(root, [], [Diagnostic("error", "workspace contains no JSON source files", str(root), "empty_workspace")])
        files: list[ContentFile] = []
        for path in paths:
            decoded = decode_content(path)
            if decoded.data is not None:
                files.append(ContentFile(path, decoded.data, False, True))
            diagnostics.extend(decoded.diagnostics)
        return cls(root, files, diagnostics)

    @classmethod
    def new(cls, root: Path) -> "ContentWorkspace":
        root = root.expanduser().resolve()
        data: dict[str, JsonValue] = {"format": "dungeon-underworld-content", "version": 5}
        data.update({name: [] for name in CONTENT_CATEGORIES})
        return cls(root, [ContentFile(root / "content.json", data, True, True)])

    @classmethod
    def from_builtin_json(cls, path: Path) -> "ContentWorkspace":
        decoded = decode_content(path)
        file = ContentFile(path, decoded.data or {}, False, decoded.data is not None, "builtin")
        return cls(path.parent, [file], decoded.diagnostics)

    def _rebuild_index(self) -> None:
        self.diagnostics = list(self._base_diagnostics)
        self._definitions.clear()
        seen: dict[tuple[str, str], ContentDefinition] = {}
        for content_file in self.files:
            for definition in iter_definitions(content_file):
                key = (definition.category, definition.definition_id)
                if key in seen:
                    self.diagnostics.append(Diagnostic(
                        "error",
                        f"{definition.definition_id} is already defined in {seen[key].source_path}",
                        definition.category,
                        "duplicate_definition",
                        definition.definition_id,
                        definition.source_path,
                    ))
                    continue
                seen[key] = definition
                self._definitions.append(definition)
        descriptor_names = {
            definition.definition_id: str(definition.data.get("displayName", ""))
            for definition in self._definitions
            if definition.category == DISPLAY_CATEGORY
        }
        for definition in self._definitions:
            definition.display_name = descriptor_names.get(definition.definition_id, "") or self._fallback_name(definition.definition_id)

    @staticmethod
    def _fallback_name(definition_id: str) -> str:
        value = definition_id.rsplit(".", 1)[-1].replace("_", " ").replace("-", " ")
        return value[:1].upper() + value[1:]

    def definitions(self, category: str | None = None, query: str = "") -> list[ContentDefinition]:
        return [definition for definition in self._definitions
                if (category is None or definition.category == category) and definition.matches(query)]

    def find(self, category: str, definition_id: str) -> ContentDefinition | None:
        return next((value for value in self._definitions if value.category == category and value.definition_id == definition_id), None)

    def category_counts(self) -> dict[str, int]:
        return {category: len(self.definitions(category)) for category in CONTENT_CATEGORIES}

    def update(self, definition: ContentDefinition, path: str, value: JsonValue) -> None:
        source_path = definition.source_path
        def operation() -> None:
            target: object = definition.data
            components = [component for component in path.split(".") if component]
            if not components:
                raise ValueError("definition field path is empty")
            for component in components[:-1]:
                if not isinstance(target, dict) or component not in target:
                    raise KeyError(path)
                target = target[component]
            if not isinstance(target, dict):
                raise TypeError(path)
            target[components[-1]] = value
            self._mark_file_dirty(source_path)
        self.mutate("Edit Definition", operation)

    def replace_definition(self, definition: ContentDefinition, data: dict[str, JsonValue]) -> None:
        source_path = definition.source_path
        def operation() -> None:
            definition.data.clear()
            definition.data.update(copy.deepcopy(data))
            self._mark_file_dirty(source_path)
        self.mutate("Edit Definition", operation)

    def create_definition(self, category: str, definition_id: str, file_path: Path | None = None) -> ContentDefinition:
        if category not in CONTENT_CATEGORIES:
            raise ValueError(f"unknown content category: {category}")
        if self.find(category, definition_id):
            raise ValueError(f"definition already exists: {definition_id}")
        target_file = file_path or (self.files[0].path if self.files else self.root / "content.json")
        created: ContentDefinition | None = None
        def operation() -> None:
            nonlocal created
            content_file = next((value for value in self.files if value.path == target_file), None)
            if content_file is None:
                data: dict[str, JsonValue] = {"format": "dungeon-underworld-content", "version": 5}
                data.update({name: [] for name in CONTENT_CATEGORIES})
                content_file = ContentFile(target_file, data, True, True)
                self.files.append(content_file)
            entry = default_definition(category, definition_id)
            values = content_file.data.setdefault(category, [])
            if not isinstance(values, list):
                raise ValueError(f"category {category} is not an array")
            values.append(entry)
            content_file.dirty = True
        self.mutate("Create Definition", operation)
        self._rebuild_index()
        result = self.find(category, definition_id)
        if result is None:
            raise RuntimeError("created definition could not be indexed")
        return result

    def delete_definition(self, definition: ContentDefinition) -> None:
        content_file = next((value for value in self.files if value.path == definition.source_path), None)
        if content_file is None:
            raise ValueError("definition source file is unavailable")
        values = content_file.data.get(definition.category)
        if not isinstance(values, list) or definition.data not in values:
            raise ValueError("definition is unavailable")
        def operation() -> None:
            values.remove(definition.data)
            content_file.dirty = True
        self.mutate("Delete Definition", operation)
        self._rebuild_index()

    def find_usages(self, definition_id: str) -> list[ContentDefinition]:
        result: list[ContentDefinition] = []
        for definition in self._definitions:
            if definition.definition_id == definition_id:
                continue
            if _contains_text(definition.data, definition_id):
                result.append(definition)
        return result

    def validate_local(self, definition: ContentDefinition) -> list[Diagnostic]:
        issues: list[Diagnostic] = []
        visited: set[tuple[str, str]] = set()

        def require(category: str, target: object, field_path: str) -> None:
            if not isinstance(target, str) or not target:
                return
            key = (category, target)
            if key in visited:
                return
            visited.add(key)
            candidate = self.find(category, target)
            if candidate is None:
                issues.append(Diagnostic("error", f"missing dependency: {target}", field_path,
                                         "missing_dependency", definition.definition_id, definition.source_path))
                return
            visit(candidate, field_path)

        def visit(candidate: ContentDefinition, prefix: str) -> None:
            data = candidate.data
            category = candidate.category
            required_fields = {
                "enemies": ("visualSetId", "behaviorProfileId", "faction", "maximumHealth", "movementSpeedSubpixelsPerTick", "collisionBody", "hurtbox", "attackIds", "rewardProfileId"),
                "npcs": ("visualSetId", "interaction", "defaultDialogueId", "tags"),
                "objects": ("visualSetId",),
                "pickups": ("visualId", "collectionBounds", "payload"),
            }.get(category, ())
            for field in required_fields:
                if field not in data:
                    issues.append(Diagnostic("error", f"missing field: {field}", f"{prefix}.{field}", "missing_field", definition.definition_id, definition.source_path))
            if category == "enemies":
                require("enemyVisuals", data.get("visualSetId"), f"{prefix}.visualSetId")
                require("behaviors", data.get("behaviorProfileId"), f"{prefix}.behaviorProfileId")
                for index, value in enumerate(data.get("attackIds", [])):
                    require("attacks", value, f"{prefix}.attackIds[{index}]")
                require("rewardProfiles", data.get("rewardProfileId"), f"{prefix}.rewardProfileId")
            elif category == "npcs":
                require("npcVisuals", data.get("visualSetId"), f"{prefix}.visualSetId")
                require("dialogues", data.get("defaultDialogueId"), f"{prefix}.defaultDialogueId")
            elif category == "objects":
                require("objectVisuals", data.get("visualSetId"), f"{prefix}.visualSetId")
                _require_object_capabilities(data, prefix, require)
            elif category == "pickups":
                require("staticSprites", data.get("visualId"), f"{prefix}.visualId")
                payload = data.get("payload")
                if isinstance(payload, dict) and payload.get("kind") == "item":
                    require("items", payload.get("itemId"), f"{prefix}.payload.itemId")
            elif category == "items":
                require("staticSprites", data.get("visualId"), f"{prefix}.visualId")
            elif category == "animations":
                require("visualImages", data.get("imageId"), f"{prefix}.imageId")
            elif category == "staticSprites":
                require("visualImages", data.get("imageId"), f"{prefix}.imageId")
            elif category == "enemyVisuals":
                for key, value in _directional_values(data):
                    require("animations", value, f"{prefix}.{key}")
            elif category == "objectVisuals":
                for key, value in data.items():
                    if key.endswith("AnimationId"):
                        require("animations", value, f"{prefix}.{key}")
            elif category == "npcVisuals":
                for key, value in _directional_values(data):
                    require("animations", value, f"{prefix}.{key}")
            elif category in {"rewardProfiles", "rewardGrants", "shops"}:
                for key, value in _walk_key_values(data):
                    if key == "pickupDefinitionId":
                        require("pickups", value, f"{prefix}.{key}")
                    elif key == "itemId":
                        require("items", value, f"{prefix}.{key}")
            elif category == "quests":
                require("rewardGrants", data.get("rewardGrantId"), f"{prefix}.rewardGrantId")

        visit(definition, definition.category)
        return issues

    def save_all(self) -> None:
        if any(content_file.dirty for content_file in self.files):
            for content_file in self.files:
                if content_file.dirty:
                    write_content(content_file.path, content_file.data)
                    content_file.dirty = False

    def snapshot(self) -> list[tuple[Path, dict[str, JsonValue], bool, str]]:
        return [(content_file.path, copy.deepcopy(content_file.data), content_file.dirty, content_file.origin) for content_file in self.files]

    def restore_snapshot(self, snapshot: list[tuple[Path, dict[str, JsonValue], bool, str]]) -> None:
        self.files = [ContentFile(path, copy.deepcopy(data), dirty, True, origin) for path, data, dirty, origin in snapshot]
        self._rebuild_index()

    def mutate(self, label: str, operation: object) -> None:
        before = self.snapshot()
        operation()  # type: ignore[operator]
        after = self.snapshot()
        if before == after:
            return
        self.history.execute(Command(label, self, before, after))
        self._rebuild_index()

    def undo(self) -> bool:
        result = self.history.undo()
        if result:
            self._rebuild_index()
        return result

    def redo(self) -> bool:
        result = self.history.redo()
        if result:
            self._rebuild_index()
        return result

    def _mark_file_dirty(self, path: Path | None) -> None:
        for content_file in self.files:
            if content_file.path == path:
                content_file.dirty = True
                return

    @property
    def dirty(self) -> bool:
        return any(content_file.dirty for content_file in self.files)


def _contains_text(value: JsonValue, needle: str) -> bool:
    if isinstance(value, str):
        return value == needle
    if isinstance(value, list):
        return any(_contains_text(item, needle) for item in value)
    if isinstance(value, dict):
        return any(_contains_text(item, needle) for item in value.values())
    return False


def _walk_key_values(value: JsonValue) -> Iterable[tuple[str, JsonValue]]:
    if isinstance(value, dict):
        for key, item in value.items():
            yield key, item
            yield from _walk_key_values(item)
    elif isinstance(value, list):
        for item in value:
            yield from _walk_key_values(item)


def _directional_values(data: dict[str, JsonValue]) -> Iterable[tuple[str, str]]:
    for key, value in _walk_key_values(data):
        if isinstance(value, str) and (key in {"default", "down", "up", "side"} or key.endswith("AnimationId")):
            yield key, value


def _require_object_capabilities(data: dict[str, JsonValue], prefix: str, require: object) -> None:
    # Object capability references are currently embedded data; this hook keeps
    # local dependency traversal in one place as new capabilities are authored.
    del data, prefix, require


def default_definition(category: str, definition_id: str) -> dict[str, JsonValue]:
    empty_box: JsonValue = {"x": 0, "y": 0, "width": 16, "height": 16}
    defaults: dict[str, dict[str, JsonValue]] = {
        "tilesets": {"id": definition_id, "displayName": definition_id, "relativeAssetPath": "", "tileSize": 16, "columns": 1, "rows": 1},
        "projectiles": {"id": definition_id, "visualId": "", "canonicalFacing": "up", "speedPixelsPerTick": 1, "lifetimeTicks": 60, "hitboxWidth": 4, "hitboxHeight": 4, "spawnOffsets": {"down": {"x": 0, "y": 0}, "up": {"x": 0, "y": 0}, "left": {"x": 0, "y": 0}, "right": {"x": 0, "y": 0}}},
        "attacks": {"id": definition_id, "kind": "meleeHitbox", "damage": {"amount": 1, "knockbackPixels": 0}, "totalTicks": 1, "cooldownTicks": 1, "minimumRangePixels": 0, "maximumRangePixels": 16, "visualActionId": "", "meleeHitboxes": None, "projectileDefinitionId": None, "timeline": [], "shapes": []},
        "behaviors": {"id": definition_id, "detectionRangePixels": 64, "disengageRangePixels": 96, "idleDurationTicks": 60, "wanderDurationTicks": 60},
        "enemies": {"id": definition_id, "visualSetId": "", "behaviorProfileId": "", "faction": "enemy", "maximumHealth": 1, "movementSpeedSubpixelsPerTick": 0, "collisionBody": {"offsetX": -4, "offsetY": -4, "width": 8, "height": 8}, "hurtbox": {"offsetX": -6, "offsetY": -12, "width": 12, "height": 12}, "attackIds": [], "rewardProfileId": None},
        "items": {"id": definition_id, "visualId": "", "category": "misc", "stackLimit": 1, "use": None, "equipment": None},
        "objects": {"id": definition_id, "visualSetId": "", "interactable": None, "container": None, "destructible": None, "bankAccess": None, "door": None, "activation": None},
        "pickups": {"id": definition_id, "visualId": "", "collectionBounds": empty_box, "payload": {"kind": "health", "amount": 1}},
        "npcVisuals": {"id": definition_id, "markerColor": {"r": 255, "g": 255, "b": 255, "a": 255}, "idle": None},
        "npcs": {"id": definition_id, "visualSetId": "", "interaction": {"bounds": empty_box, "enabled": True}, "defaultDialogueId": "", "tags": []},
        "dialogues": {"id": definition_id, "entryNodeId": "", "nodes": []},
        "quests": {"id": definition_id, "title": definition_id, "objectives": [], "tags": [], "rewardGrantId": None},
        "playerProgressions": {"id": definition_id, "baseStats": {"maximumHealth": 3}, "cumulativeExperienceThresholds": []},
        "rewardProfiles": {"id": definition_id, "experience": 0, "loot": []},
        "rewardGrants": {"id": definition_id, "experience": 0, "gold": 0, "items": []},
        "shops": {"id": definition_id, "offers": []},
        "authoringDescriptors": {"definitionId": definition_id, "displayName": definition_id, "category": "enemy", "tags": []},
        "tileSemantics": {"id": definition_id, "tilesetId": "", "sourceIndex": 0, "family": "", "role": "unknown", "topology": "unknown", "north": "unknown", "east": "unknown", "south": "unknown", "west": "unknown", "preferredLayer": "", "flipXAllowed": False, "visualConfidence": "unknown", "semanticConfidence": "unknown", "gameplayConfidence": "unknown"},
        "stamps": {"id": definition_id, "displayName": definition_id, "width": 1, "height": 1, "cells": [], "anchor": {"x": 0, "y": 0}, "flipXAllowed": False, "atomic": True, "confidence": "unknown"},
        "presentationEffects": {"id": definition_id, "lifetime": "transient", "durationTicks": 1, "priority": 0, "cameraShake": None, "overlay": None, "visionMask": None, "fade": None},
        "visualImages": {"id": definition_id, "root": "gameAssets", "relativePath": ""},
        "staticSprites": {"id": definition_id, "imageId": "", "source": None, "anchor": {"x": 0, "y": 0}},
        "animations": {"id": definition_id, "imageId": "", "loop": True, "frames": []},
        "enemyVisuals": {"id": definition_id, "idle": {}, "actions": []},
        "objectVisuals": {"id": definition_id, "idleAnimationId": "", "openedAnimationId": None, "destroyingAnimationId": None, "activationInactiveAnimationId": None, "activationActiveAnimationId": None, "doorLockedAnimationId": None, "doorClosedAnimationId": None, "doorOpenAnimationId": None, "destroyedAnimationId": None},
    }
    return copy.deepcopy(defaults[category])
