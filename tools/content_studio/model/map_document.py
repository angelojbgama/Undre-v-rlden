from __future__ import annotations

import copy
import re
from pathlib import Path
from typing import Callable, Iterable

from ..formats.umap import MAP_FORMAT, MAP_VERSION, decode_map, load_map, new_map, write_map
from .commands import Command, CommandHistory
from .scene_timeline import validate_scene
from .types import Diagnostic, JsonValue

ENTITY_CATEGORIES = ("enemies", "npcs", "objects", "pickups")


class MapDocument:
    def __init__(self, data: dict[str, JsonValue], path: Path | None = None) -> None:
        self.data = data
        self.path = path
        self.history = CommandHistory()
        self.dirty = False
        self._revision = 0

    @classmethod
    def new(cls, map_id: str, width: int, height: int, tile_size: int = 16, include_player_spawn: bool = False) -> "MapDocument":
        return cls(new_map(map_id, width, height, tile_size, include_player_spawn))

    @classmethod
    def open(cls, path: Path) -> tuple["MapDocument | None", list[Diagnostic]]:
        decoded = load_map(path)
        if decoded.data is None:
            return None, decoded.diagnostics
        return cls(decoded.data, path), decoded.diagnostics

    @property
    def map_id(self) -> str:
        return str(self.data.get("id", ""))

    @property
    def width(self) -> int:
        return int(self.data.get("width", 0))

    @property
    def height(self) -> int:
        return int(self.data.get("height", 0))

    @property
    def tile_size(self) -> int:
        return int(self.data.get("tileSize", 16))

    @property
    def layers(self) -> list[dict[str, JsonValue]]:
        values = self.data.setdefault("layers", [])
        if not isinstance(values, list):
            raise ValueError("map layers are not an array")
        return values  # type: ignore[return-value]

    def snapshot(self) -> dict[str, JsonValue]:
        return copy.deepcopy(self.data)

    def restore_snapshot(self, snapshot: dict[str, JsonValue]) -> None:
        self.data = copy.deepcopy(snapshot)
        self.dirty = True
        self._revision += 1

    def mutate(self, label: str, operation: Callable[[], None]) -> None:
        before = self.snapshot()
        operation()
        after = self.snapshot()
        if before == after:
            return
        self.history.execute(Command(label, self, before, after))
        self.dirty = True
        self._revision += 1

    def undo(self) -> bool:
        result = self.history.undo()
        if result:
            self.dirty = True
            self._revision += 1
        return result

    def redo(self) -> bool:
        result = self.history.redo()
        if result:
            self.dirty = True
            self._revision += 1
        return result

    def save(self, path: Path | None = None) -> None:
        target = path or self.path
        if target is None:
            raise ValueError("map has no path; use save_as")
        write_map(target, self.data)
        self.path = target
        self.dirty = False

    def set_properties(self, map_id: str, width: int, height: int, tile_size: int) -> None:
        map_id = map_id.strip()
        if not map_id or width <= 0 or height <= 0 or tile_size <= 0:
            raise ValueError("map id, width, height and tile size must be positive")
        old_width, old_height = self.width, self.height

        def resized(values: object, default: JsonValue) -> list[JsonValue]:
            source = values if isinstance(values, list) else []
            target = [copy.deepcopy(default) for _ in range(width * height)]
            for y in range(min(old_height, height)):
                for x in range(min(old_width, width)):
                    source_index = y * old_width + x
                    if source_index < len(source):
                        target[y * width + x] = copy.deepcopy(source[source_index])
            return target

        def operation() -> None:
            for layer in self.layers:
                if isinstance(layer, dict):
                    layer["cells"] = resized(layer.get("cells"), None)
            self.data["collision"] = resized(self.data.get("collision"), 0)
            self.data["id"] = map_id
            self.data["width"] = width
            self.data["height"] = height
            self.data["tileSize"] = tile_size
            bindings = self.data.get("collisionBindings", [])
            if isinstance(bindings, list):
                bindings[:] = [binding for binding in bindings
                                if isinstance(binding, dict)
                                and 0 <= int(binding.get("layer", -1)) < len(self.layers)
                                and 0 <= int(binding.get("x", -1)) < width
                                and 0 <= int(binding.get("y", -1)) < height]

        self.mutate("Edit Map Properties", operation)

    def tile_reference(self, tileset_id: str, source_index: int, flags: int = 0) -> int:
        references = self.data.setdefault("tileReferences", [])
        assert isinstance(references, list)
        for index, reference in enumerate(references):
            if isinstance(reference, dict) and reference.get("tilesetId") == tileset_id and reference.get("sourceIndex") == source_index and reference.get("flags", 0) == flags:
                return index
        references.append({"tilesetId": tileset_id, "sourceIndex": source_index, "flags": flags})
        return len(references) - 1

    def find_tile_reference(self, tileset_id: str, source_index: int, flags: int = 0) -> int | None:
        """Return an existing reference without changing the authored map."""
        references = self.data.get("tileReferences", [])
        if not isinstance(references, list):
            return None
        for index, reference in enumerate(references):
            if (isinstance(reference, dict) and reference.get("tilesetId") == tileset_id
                    and reference.get("sourceIndex") == source_index
                    and reference.get("flags", 0) == flags):
                return index
        return None

    def _remove_collision_binding(self, layer: int, x: int, y: int,
                                  clear_collision: bool = True) -> bool:
        bindings = self.data.get("collisionBindings", [])
        if not isinstance(bindings, list):
            return False
        kept = [value for value in bindings
                if not (isinstance(value, dict)
                        and int(value.get("layer", -1)) == layer
                        and int(value.get("x", -1)) == x
                        and int(value.get("y", -1)) == y)]
        removed = len(kept) != len(bindings)
        if not removed:
            return False
        bindings[:] = kept
        if clear_collision and not any(
                isinstance(value, dict)
                and int(value.get("x", -1)) == x
                and int(value.get("y", -1)) == y
                for value in bindings):
            collision = self.data.get("collision", [])
            if isinstance(collision, list):
                collision[y * self.width + x] = 0
        return True

    def _set_collision_binding(self, layer: int, x: int, y: int,
                               tile_reference: tuple[str, int, int] | None,
                               solid: bool) -> None:
        bindings = self.data.setdefault("collisionBindings", [])
        assert isinstance(bindings, list)
        self._remove_collision_binding(layer, x, y, clear_collision=False)
        collision = self.data.setdefault("collision", [])
        assert isinstance(collision, list)
        if solid and tile_reference is not None:
            tileset_id, source_index, flags = tile_reference
            bindings.append({
                "layer": layer, "x": x, "y": y,
                "tilesetId": tileset_id, "sourceIndex": source_index,
                "flags": flags,
            })
            collision[y * self.width + x] = 1
        elif not any(
                isinstance(value, dict)
                and int(value.get("x", -1)) == x
                and int(value.get("y", -1)) == y
                for value in bindings):
            collision[y * self.width + x] = 0

    def _unlink_tile_collision(self, layer: int, x: int, y: int) -> None:
        self._remove_collision_binding(layer, x, y, clear_collision=False)
        bindings = self.data.get("collisionBindings", [])
        if not isinstance(bindings, list) or not any(
                isinstance(value, dict)
                and int(value.get("x", -1)) == x
                and int(value.get("y", -1)) == y
                for value in bindings):
            collision = self.data.get("collision", [])
            if isinstance(collision, list):
                collision[y * self.width + x] = 0

    def remove_tile_references(self, references_to_remove: Iterable[tuple[str, int]],
                               label: str = "Remove deleted terrain tiles") -> bool:
        """Remove cells that point at deleted semantic tiles, atomically."""
        targets = {(str(tileset_id), int(source_index))
                   for tileset_id, source_index in references_to_remove}
        if not targets:
            return False
        changed = False

        def operation() -> None:
            nonlocal changed
            tile_references = self.data.get("tileReferences", [])
            if not isinstance(tile_references, list):
                return
            removed_indices = {
                index for index, reference in enumerate(tile_references)
                if isinstance(reference, dict)
                and (str(reference.get("tilesetId", "")), int(reference.get("sourceIndex", 0))) in targets
            }
            if not removed_indices:
                return
            removed_cells: set[int] = set()
            bindings = self.data.get("collisionBindings", [])
            removed_binding_cells: set[int] = set()
            if isinstance(bindings, list):
                kept_bindings = []
                for binding in bindings:
                    if (isinstance(binding, dict)
                            and (str(binding.get("tilesetId", "")), int(binding.get("sourceIndex", 0)))
                            in targets):
                        try:
                            removed_binding_cells.add(int(binding.get("y", -1)) * self.width + int(binding.get("x", -1)))
                        except (TypeError, ValueError):
                            pass
                        changed = True
                    else:
                        kept_bindings.append(binding)
                bindings[:] = kept_bindings
            for layer_index, layer in enumerate(self.layers):
                if not isinstance(layer, dict):
                    continue
                cells = layer.get("cells", [])
                if not isinstance(cells, list):
                    continue
                for index, value in enumerate(cells):
                    if value in removed_indices:
                        cells[index] = None
                        removed_cells.add(index)
                        self._unlink_tile_collision(layer_index, index % self.width, index // self.width)
                        changed = True
            collision = self.data.get("collision", [])
            if isinstance(collision, list):
                for index in removed_cells:
                    x, y = index % self.width, index // self.width
                    if index < len(collision) and not any(
                            isinstance(value, dict)
                            and int(value.get("x", -1)) == x
                            and int(value.get("y", -1)) == y
                            for value in (bindings if isinstance(bindings, list) else [])):
                        if collision[index]:
                            changed = True
                        collision[index] = 0
                for index in removed_binding_cells:
                    x, y = index % self.width, index // self.width
                    remaining_bindings = bindings if isinstance(bindings, list) else []
                    if 0 <= x < self.width and 0 <= y < self.height and not any(
                            isinstance(value, dict)
                            and int(value.get("x", -1)) == x
                            and int(value.get("y", -1)) == y
                            for value in remaining_bindings):
                        if collision[index]:
                            changed = True
                        collision[index] = 0
                # Older authoring revisions could erase the visual cell but
                # leave its collision bit behind.  Once no tile is used in
                # the map, those bits cannot belong to a remaining tile and
                # are safe to remove with the deleted terrain reference.
                has_used_tile = any(
                    isinstance(layer, dict)
                    and isinstance(layer.get("cells", []), list)
                    and any(isinstance(value, int) and value >= 0 for value in layer["cells"])
                    for layer in self.layers
                )
                if not removed_cells and not has_used_tile:
                    for index, value in enumerate(collision):
                        if value:
                            collision[index] = 0
                            changed = True
            used = {value for layer in self.layers
                    for value in (layer.get("cells", []) if isinstance(layer, dict) else [])
                    if isinstance(value, int) and value >= 0}
            remap: dict[int, int] = {}
            compacted: list[JsonValue] = []
            for index, reference in enumerate(tile_references):
                if index in removed_indices or index not in used:
                    continue
                remap[index] = len(compacted)
                compacted.append(reference)
            for layer in self.layers:
                cells = layer.get("cells", []) if isinstance(layer, dict) else []
                if isinstance(cells, list):
                    for index, value in enumerate(cells):
                        if isinstance(value, int):
                            cells[index] = remap.get(value)
            if compacted != tile_references:
                changed = True
            self.data["tileReferences"] = compacted

        self.mutate(label, operation)
        return changed

    def set_tile(self, layer: int, x: int, y: int, tileset_id: str | None, source_index: int = 0, flags: int = 0) -> None:
        self._check_tile(x, y)
        if layer < 0 or layer >= len(self.layers):
            raise IndexError("layer index out of range")

        def operation() -> None:
            cells = self.layers[layer].setdefault("cells", [])
            assert isinstance(cells, list)
            value = None if tileset_id is None else self.tile_reference(tileset_id, source_index, flags)
            index = y * self.width + x
            if cells[index] != value:
                self._unlink_tile_collision(layer, x, y)
            cells[index] = value

        self.mutate("Erase Tile" if tileset_id is None else "Paint Tile", operation)

    def set_tiles(self, layer: int, cells: Iterable[tuple[int, int]], tileset_id: str | None, source_index: int = 0, flags: int = 0) -> None:
        if layer < 0 or layer >= len(self.layers):
            raise IndexError("layer index out of range")
        coordinates = [(x, y) for x, y in cells if 0 <= x < self.width and 0 <= y < self.height]
        if not coordinates:
            return

        def operation() -> None:
            reference = None if tileset_id is None else self.tile_reference(tileset_id, source_index, flags)
            target = self.layers[layer].setdefault("cells", [])
            assert isinstance(target, list)
            for x, y in coordinates:
                index = y * self.width + x
                if target[index] != reference:
                    self._unlink_tile_collision(layer, x, y)
                target[index] = reference

        self.mutate("Erase Tiles" if tileset_id is None else "Paint Tiles", operation)

    def paint_brush(self, layer: int, origin_x: int, origin_y: int,
                    brush: Iterable[tuple[int, int, str, int, int]]) -> None:
        """Paint a multi-tile brush at an origin as one undoable command.

        Brush entries are ``(offset_x, offset_y, tileset_id, source_index,
        flags)``.  Entries outside the map are clipped, matching the C++
        authoring command while keeping the source tile references typed.
        """
        entries = [(int(dx), int(dy), str(tileset), int(source), int(flags))
                   for dx, dy, tileset, source, flags in brush]
        if not entries or layer < 0 or layer >= len(self.layers):
            return

        def operation() -> None:
            target = self.layers[layer].setdefault("cells", [])
            assert isinstance(target, list)
            for dx, dy, tileset, source, flags in entries:
                x, y = origin_x + dx, origin_y + dy
                if 0 <= x < self.width and 0 <= y < self.height:
                    index = y * self.width + x
                    value = self.tile_reference(tileset, source, flags)
                    if target[index] != value:
                        self._unlink_tile_collision(layer, x, y)
                    target[index] = value

        self.mutate("Paint Brush", operation)

    def place_pattern(self, layer: int, origin_x: int, origin_y: int,
                      cells: Iterable[tuple[int, int, str, int, int]],
                      label: str = "Place Stamp") -> None:
        """Place a semantic/stamp pattern while preserving one history entry."""
        entries = list(cells)
        if layer < 0 or layer >= len(self.layers) or not entries:
            return

        def operation() -> None:
            target = self.layers[layer].setdefault("cells", [])
            assert isinstance(target, list)
            for dx, dy, tileset, source, flags in entries:
                x, y = origin_x + int(dx), origin_y + int(dy)
                if 0 <= x < self.width and 0 <= y < self.height:
                    index = y * self.width + x
                    value = self.tile_reference(str(tileset), int(source), int(flags))
                    if target[index] != value:
                        self._unlink_tile_collision(layer, x, y)
                    target[index] = value

        self.mutate(label, operation)

    def set_collision(self, cells: Iterable[tuple[int, int]], solid: bool,
                      layer: int | None = None) -> None:
        coordinates = [(x, y) for x, y in cells if 0 <= x < self.width and 0 <= y < self.height]
        if layer is not None and (layer < 0 or layer >= len(self.layers)):
            raise IndexError("layer index out of range")

        def operation() -> None:
            collision = self.data.setdefault("collision", [])
            assert isinstance(collision, list)
            for x, y in coordinates:
                if not solid:
                    bindings = self.data.get("collisionBindings", [])
                    if isinstance(bindings, list):
                        bindings[:] = [value for value in bindings
                                       if not (isinstance(value, dict)
                                               and int(value.get("x", -1)) == x
                                               and int(value.get("y", -1)) == y
                                               and (layer is None or
                                                    int(value.get("layer", -1)) == layer))]
                    collision[y * self.width + x] = 1 if isinstance(bindings, list) and any(
                        isinstance(value, dict)
                        and int(value.get("x", -1)) == x
                        and int(value.get("y", -1)) == y
                        for value in bindings) else 0
                    continue
                reference: tuple[str, int, int] | None = None
                binding_layer = layer
                candidate_layers = (range(len(self.layers)) if layer is None else (layer,))
                references = self.data.get("tileReferences", [])
                index = y * self.width + x
                for candidate in candidate_layers:
                    cells_in_layer = self.layers[candidate].get("cells", [])
                    if (not isinstance(cells_in_layer, list) or not isinstance(references, list)
                            or index >= len(cells_in_layer)):
                        continue
                    tile_index = cells_in_layer[index]
                    if (isinstance(tile_index, int) and 0 <= tile_index < len(references)
                            and isinstance(references[tile_index], dict)):
                        tile = references[tile_index]
                        reference = (str(tile.get("tilesetId", "")),
                                     int(tile.get("sourceIndex", 0)),
                                     int(tile.get("flags", 0)))
                        binding_layer = candidate
                        break
                if reference is None:
                    # Collision authoring is tile-backed.  A blank layer cell
                    # cannot become a new orphaned collision source; an
                    # existing binding on another layer remains authoritative.
                    bindings = self.data.get("collisionBindings", [])
                    if not isinstance(bindings, list) or not any(
                            isinstance(value, dict)
                            and int(value.get("x", -1)) == x
                            and int(value.get("y", -1)) == y
                            for value in bindings):
                        collision[y * self.width + x] = 0
                elif binding_layer is not None:
                    self._set_collision_binding(binding_layer, x, y, reference, True)

        self.mutate("Set Collision" if solid else "Clear Collision", operation)

    def set_layer_visibility(self, index: int, visible: bool) -> None:
        if index < 0 or index >= len(self.layers):
            raise IndexError("layer index out of range")
        self.mutate("Show Layer" if visible else "Hide Layer", lambda: self.layers[index].__setitem__("visible", bool(visible)))

    def add_layer(self, name: str) -> None:
        if not name.strip():
            raise ValueError("layer name cannot be empty")
        if any(isinstance(layer, dict) and layer.get("name") == name for layer in self.layers):
            raise ValueError("layer name already exists")

        def operation() -> None:
            self.layers.append({"name": name, "visible": True, "cells": [None] * (self.width * self.height)})

        self.mutate("Add Layer", operation)

    def rename_layer(self, index: int, name: str) -> None:
        if not name.strip() or index < 0 or index >= len(self.layers):
            raise ValueError("invalid layer name or index")
        self.mutate("Rename Layer", lambda: self.layers[index].__setitem__("name", name))

    def move_layer(self, source: int, target: int) -> None:
        if source < 0 or source >= len(self.layers) or target < 0 or target >= len(self.layers):
            raise IndexError("layer index out of range")

        def operation() -> None:
            layer = self.layers.pop(source)
            self.layers.insert(target, layer)
            bindings = self.data.get("collisionBindings", [])
            if isinstance(bindings, list) and source != target:
                for binding in bindings:
                    if not isinstance(binding, dict):
                        continue
                    binding_layer = int(binding.get("layer", -1))
                    if binding_layer == source:
                        binding["layer"] = target
                    elif source < target and source < binding_layer <= target:
                        binding["layer"] = binding_layer - 1
                    elif target < source and target <= binding_layer < source:
                        binding["layer"] = binding_layer + 1

        self.mutate("Move Layer", operation)

    def remove_layer(self, index: int) -> None:
        if len(self.layers) <= 1:
            raise ValueError("a map must keep at least one layer")
        if index < 0 or index >= len(self.layers):
            raise IndexError("layer index out of range")

        def operation() -> None:
            bindings = self.data.get("collisionBindings", [])
            removed_cells: set[int] = set()
            removed_layer = self.layers[index].get("cells", [])
            if isinstance(removed_layer, list):
                removed_cells.update(cell for cell, value in enumerate(removed_layer)
                                     if isinstance(value, int))
            kept_bindings: list[JsonValue] = []
            if isinstance(bindings, list):
                for binding in bindings:
                    if not isinstance(binding, dict):
                        kept_bindings.append(binding)
                        continue
                    binding_layer = int(binding.get("layer", -1))
                    if binding_layer == index:
                        removed_cells.add(int(binding.get("y", -1)) * self.width +
                                          int(binding.get("x", -1)))
                    else:
                        if binding_layer > index:
                            binding["layer"] = binding_layer - 1
                        kept_bindings.append(binding)
                bindings[:] = kept_bindings
            self.layers.pop(index)
            collision = self.data.get("collision", [])
            if isinstance(collision, list):
                for cell in removed_cells:
                    if 0 <= cell < len(collision) and not any(
                            isinstance(binding, dict)
                            and int(binding.get("x", -1)) == cell % self.width
                            and int(binding.get("y", -1)) == cell // self.width
                            for binding in kept_bindings):
                        collision[cell] = 0

        self.mutate("Remove Layer", operation)

    def copy_tile_rect(self, layer: int, start: tuple[int, int], end: tuple[int, int]) -> dict[str, JsonValue]:
        if layer < 0 or layer >= len(self.layers):
            raise IndexError("layer index out of range")
        left, right = sorted((start[0], end[0])); top, bottom = sorted((start[1], end[1]))
        cells = self.layers[layer].get("cells", [])
        if not isinstance(cells, list):
            raise ValueError("layer cells are not an array")
        return {"width": right - left + 1, "height": bottom - top + 1,
                "cells": [cells[y * self.width + x] for y in range(top, bottom + 1) for x in range(left, right + 1)]}

    def paste_tile_rect(self, layer: int, origin: tuple[int, int], clipboard: dict[str, JsonValue]) -> None:
        width = clipboard.get("width"); height = clipboard.get("height"); cells = clipboard.get("cells")
        if not isinstance(width, int) or not isinstance(height, int) or not isinstance(cells, list):
            raise ValueError("invalid tile clipboard")
        if len(cells) != width * height:
            raise ValueError("tile clipboard dimensions do not match cells")
        if layer < 0 or layer >= len(self.layers):
            raise IndexError("layer index out of range")

        def operation() -> None:
            target = self.layers[layer].setdefault("cells", [])
            assert isinstance(target, list)
            for index, value in enumerate(cells):
                x = origin[0] + index % width; y = origin[1] + index // width
                if 0 <= x < self.width and 0 <= y < self.height:
                    target_index = y * self.width + x
                    if target[target_index] != value:
                        self._unlink_tile_collision(layer, x, y)
                    target[target_index] = value

        self.mutate("Paste Tiles", operation)

    def add_entity(self, category: str, definition_id: str, x: int, y: int, facing: str = "down") -> int:
        if category not in ENTITY_CATEGORIES:
            raise ValueError(f"unknown entity category: {category}")
        persistent_id = self.next_persistent_id()
        if category in {"enemies", "npcs"}:
            value: dict[str, JsonValue] = {"id": persistent_id, "definitionId": definition_id, "position": {"x": x, "y": y}, "facing": facing}
        elif category == "objects":
            value = {"id": persistent_id, "definitionId": definition_id, "position": {"x": x, "y": y}, "initialContents": [], "persistence": "persistent"}
        else:
            value = {"id": persistent_id, "definitionId": definition_id, "visualId": "", "position": {"x": x, "y": y}, "collectionBounds": {"x": 0, "y": 0, "width": 8, "height": 8}, "payload": {"kind": "health", "amount": 1}}

        def operation() -> None:
            values = self.data.setdefault(category, [])
            assert isinstance(values, list)
            values.append(value)

        self.mutate(f"Place {category[:-1].title()}", operation)
        return persistent_id

    def entity(self, category: str, persistent_id: int) -> dict[str, JsonValue] | None:
        values = self.data.get(category, [])
        if not isinstance(values, list):
            return None
        return next((value for value in values if isinstance(value, dict) and value.get("id") == persistent_id), None)  # type: ignore[return-value]

    def move_entity(self, category: str, persistent_id: int, x: int, y: int) -> None:
        value = self.entity(category, persistent_id)
        if value is None:
            raise ValueError("entity was not found")
        self.mutate("Move Entity", lambda: value.__setitem__("position", {"x": x, "y": y}))

    def delete_entity(self, category: str, persistent_id: int) -> None:
        values = self.data.get(category)
        if not isinstance(values, list):
            raise ValueError("entity category is unavailable")
        self.mutate("Delete Entity", lambda: values.__setitem__(slice(None), [value for value in values if not (isinstance(value, dict) and value.get("id") == persistent_id)]))

    def duplicate_entity(self, category: str, persistent_id: int, offset: int = 16) -> int:
        original = self.entity(category, persistent_id)
        if original is None:
            raise ValueError("entity was not found")
        duplicate = copy.deepcopy(original)
        new_id = self.next_persistent_id()
        duplicate["id"] = new_id
        position = duplicate.get("position")
        if isinstance(position, dict):
            duplicate["position"] = {"x": int(position.get("x", 0)) + offset, "y": int(position.get("y", 0)) + offset}
        self.mutate("Duplicate Entity", lambda: self.data[category].append(duplicate))  # type: ignore[index]
        return new_id

    def add_region(self, region_id: str, x: int, y: int, width: int, height: int) -> str:
        if not region_id or width <= 0 or height <= 0:
            raise ValueError("region needs an ID and positive bounds")
        value = {"id": region_id, "bounds": {"x": x, "y": y, "width": width, "height": height}, "environmentEffectId": None}
        self.mutate("Create Region", lambda: self.data.setdefault("regions", []).append(value))  # type: ignore[union-attr]
        return region_id

    def move_region(self, region_id: str, x: int, y: int) -> None:
        region = next((value for value in self.all_collection("regions") if isinstance(value, dict) and value.get("id") == region_id), None)
        if region is None or not isinstance(region.get("bounds"), dict):
            raise ValueError("region was not found")
        self.mutate("Move Region", lambda: (region["bounds"].__setitem__("x", x), region["bounds"].__setitem__("y", y)))

    def resize_region(self, region_id: str, width: int, height: int) -> None:
        if width <= 0 or height <= 0:
            raise ValueError("region bounds must be positive")
        region = next((value for value in self.all_collection("regions") if isinstance(value, dict) and value.get("id") == region_id), None)
        if region is None or not isinstance(region.get("bounds"), dict):
            raise ValueError("region was not found")
        self.mutate("Resize Region", lambda: (region["bounds"].__setitem__("width", width), region["bounds"].__setitem__("height", height)))

    def add_link(self, link_id: str, x: int, y: int, width: int, height: int,
                 target_map_id: str = "", target_spawn_id: str = "") -> str:
        if not link_id.strip() or width <= 0 or height <= 0:
            raise ValueError("map link needs an ID and positive trigger bounds")
        if any(isinstance(value, dict) and value.get("id") == link_id for value in self.all_collection("links")):
            raise ValueError("map link ID already exists")
        value: dict[str, JsonValue] = {"id": link_id.strip(), "trigger": {"x": x, "y": y, "width": width, "height": height}, "targetMapId": target_map_id, "targetSpawnId": target_spawn_id}
        self.mutate("Create Map Link", lambda: self.data.setdefault("links", []).append(value))  # type: ignore[union-attr]
        return link_id.strip()

    def add_player_spawn(self, spawn_id: str, x: int, y: int, facing: str = "down") -> str:
        if not spawn_id.strip():
            raise ValueError("spawn ID cannot be empty")
        if any(isinstance(value, dict) and value.get("id") == spawn_id for value in self.data.get("playerSpawns", [])):
            raise ValueError("spawn ID already exists")
        value: dict[str, JsonValue] = {"id": spawn_id, "position": {"x": x, "y": y}, "facing": facing}
        self.mutate("Create Player Spawn", lambda: self.data.setdefault("playerSpawns", []).append(value))  # type: ignore[union-attr]
        return spawn_id

    def next_spawn_id(self) -> str:
        existing = {str(value.get("id")) for value in self.data.get("playerSpawns", []) if isinstance(value, dict)}
        index = 1
        while f"spawn.editor.{index}" in existing:
            index += 1
        return f"spawn.editor.{index}"

    def next_persistent_id(self) -> int:
        values: list[int] = []
        for category in (*ENTITY_CATEGORIES, "playerSpawns", "links", "regions"):
            entries = self.data.get(category, [])
            if isinstance(entries, list):
                values.extend(int(item["id"]) for item in entries if isinstance(item, dict) and isinstance(item.get("id"), int))
        return max(values, default=0) + 1

    def all_collection(self, collection: str) -> list[dict[str, JsonValue]]:
        values = self.data.setdefault(collection, [])
        if not isinstance(values, list):
            raise ValueError(f"map collection is not an array: {collection}")
        return values  # type: ignore[return-value]

    def mutate_collection_entry(self, collection: str, entry_index: int, path: str, action: str) -> None:
        values = self.all_collection(collection)
        if entry_index < 0 or entry_index >= len(values) or not isinstance(values[entry_index], dict):
            raise IndexError("map collection entry out of range")
        parts = _path_parts(path)

        def operation() -> None:
            current: object = values[entry_index]
            for part in parts:
                current = current[part]  # type: ignore[index]
            if not isinstance(current, list):
                raise TypeError(f"{path} is not an array")
            if action == "add":
                current.append(copy.deepcopy(current[-1]) if current and isinstance(current[-1], dict) else _default_map_collection_value(parts[-1]))
            elif action == "remove":
                if current: current.pop()
            elif action.startswith("remove_at:"):
                remove_index = int(action.split(":", 1)[1])
                if remove_index < 0 or remove_index >= len(current):
                    raise IndexError("collection item out of range")
                current.pop(remove_index)
            else:
                raise ValueError(f"unknown collection action: {action}")

        self.mutate("Add Collection Entry" if action == "add" else "Remove Collection Entry", operation)

    def validate_structural(self) -> list[Diagnostic]:
        decoded = decode_map(self.data, self.path)
        issues = list(decoded.diagnostics)
        ids: set[int] = set()
        for category in ENTITY_CATEGORIES:
            for value in self.data.get(category, []):
                if not isinstance(value, dict):
                    issues.append(Diagnostic("error", f"{category} contains a non-object placement", category, "wrong_type", source_path=self.path))
                    continue
                identifier = value.get("id")
                if not isinstance(identifier, int) or identifier <= 0:
                    issues.append(Diagnostic("error", f"{category} contains an invalid persistent ID", category, "invalid_persistent_id", source_path=self.path))
                elif identifier in ids:
                    issues.append(Diagnostic("error", f"duplicate persistent ID: {identifier}", category, "duplicate_persistent_id", source_path=self.path))
                ids.add(identifier)
        npc_instances = {
            int(value["id"]) for value in self.data.get("npcs", [])
            if isinstance(value, dict) and isinstance(value.get("id"), int)
        }
        enemy_instances = {
            int(value["id"]) for value in self.data.get("enemies", [])
            if isinstance(value, dict) and isinstance(value.get("id"), int)
        }
        scenes = self.data.get("scenes", [])
        if isinstance(scenes, list):
            for index, scene in enumerate(scenes):
                if isinstance(scene, dict):
                    issues.extend(
                        Diagnostic(issue.severity, issue.message,
                                   f"scenes[{index}].{issue.path}" if issue.path else f"scenes[{index}]",
                                   issue.code, issue.definition_id, issue.source_path, self.map_id)
                        for issue in validate_scene(
                            scene, self.width * self.tile_size, self.height * self.tile_size,
                            npc_instances, enemy_instances))
        return issues

    def _check_tile(self, x: int, y: int) -> None:
        if not (0 <= x < self.width and 0 <= y < self.height):
            raise IndexError("tile coordinate is outside the map")


_PATH_PART = re.compile(r"([^.[\]]+)|\[([0-9]+)\]")


def _path_parts(path: str) -> list[str | int]:
    result: list[str | int] = []
    for match in _PATH_PART.finditer(path):
        result.append(match.group(1) if match.group(1) is not None else int(match.group(2)))
    if not result:
        raise ValueError("collection path cannot be empty")
    return result


def _default_map_collection_value(leaf: str | int) -> JsonValue:
    name = str(leaf)
    if name == "participants": return 1
    if name == "conditions": return {"kind": "flagSet", "target": "flag.new"}
    if name == "actions": return {"kind": "setFlag", "target": "flag.new"}
    return {}
