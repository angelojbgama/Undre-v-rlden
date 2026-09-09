from __future__ import annotations

from collections.abc import Iterable
from collections.abc import Mapping

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import ENTITY_CATEGORIES, MapDocument
from .drag_payload import StudioDragPayload
from .selection_controller import Selection


class MapEditingService:
    """Command-oriented facade over the existing MapDocument authored model."""

    def __init__(self, document: MapDocument | None = None, layer_index: int = 0,
                 workspace: ContentWorkspace | None = None) -> None:
        self.document = document
        self.layer_index = layer_index
        self.workspace = workspace

    def set_document(self, document: MapDocument | None) -> None:
        self.document = document

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    def set_layer(self, layer_index: int) -> None:
        self.layer_index = layer_index

    def paint_tiles(self, cells: Iterable[tuple[int, int]], tileset_id: str, source_index: int,
                    flags: int = 0, brush: Iterable[tuple[int, int, str, int, int]] | None = None) -> None:
        document = self._require_document()
        self._validate_tileset(tileset_id)
        coordinates = list(cells)
        if brush is not None:
            first = coordinates[0] if coordinates else (0, 0)
            document.paint_brush(self.layer_index, first[0], first[1], brush)
        else:
            document.set_tiles(self.layer_index, coordinates, tileset_id, source_index, flags)

    def erase_tiles(self, cells: Iterable[tuple[int, int]]) -> None:
        document = self._require_document()
        document.set_tiles(self.layer_index, cells, None)

    def apply_tile_assignments(self, assignments: Mapping[tuple[int, int], tuple[str, int, int] | None],
                               label: str = "Paint Tiles") -> bool:
        """Apply many concrete references as one authored command.

        Terrain painting uses this entry point so neighbor updates are a single
        undoable gesture.  It intentionally writes only the existing UMAP
        ``tileReferences`` and layer cell arrays.
        """
        document = self._require_document()
        values = {(int(x), int(y)): value for (x, y), value in assignments.items()
                  if 0 <= int(x) < document.width and 0 <= int(y) < document.height}
        if not values:
            return False
        for value in values.values():
            if value is not None:
                self._validate_tileset(str(value[0]))
        before = document.snapshot()

        def operation() -> None:
            target = document.layers[self.layer_index].setdefault("cells", [])
            assert isinstance(target, list)
            for (x, y), value in values.items():
                target[y * document.width + x] = None if value is None else document.tile_reference(
                    str(value[0]), int(value[1]), int(value[2]))

        document.mutate(label, operation)
        return before != document.data

    def paint_rectangle(self, start: tuple[int, int], end: tuple[int, int], tileset_id: str,
                        source_index: int, flags: int = 0,
                        brush: Iterable[tuple[int, int, str, int, int]] | None = None) -> None:
        cells = self.rectangle_cells(start, end)
        if brush is not None:
            document = self._require_document()
            document.paint_brush(self.layer_index, start[0], start[1], brush)
        else:
            self.paint_tiles(cells, tileset_id, source_index, flags)

    def fill_tiles(self, origin: tuple[int, int], tileset_id: str, source_index: int, flags: int = 0) -> None:
        document = self._require_document()
        if not self._in_bounds(*origin):
            return
        cells = document.layers[self.layer_index].get("cells", [])
        if not isinstance(cells, list):
            return
        target = cells[origin[1] * document.width + origin[0]]
        replacement = document.find_tile_reference(tileset_id, source_index, flags)
        if target == replacement:
            return
        pending = [origin]
        visited: set[tuple[int, int]] = set()
        while pending:
            current = pending.pop()
            if current in visited or not self._in_bounds(*current):
                continue
            visited.add(current)
            if cells[current[1] * document.width + current[0]] != target:
                continue
            pending.extend(((current[0] - 1, current[1]), (current[0] + 1, current[1]),
                            (current[0], current[1] - 1), (current[0], current[1] + 1)))
        document.set_tiles(self.layer_index, visited, tileset_id, source_index, flags)

    def set_collision(self, cells: Iterable[tuple[int, int]], solid: bool) -> None:
        self._require_document().set_collision(cells, solid)

    def fill_collision(self, origin: tuple[int, int], solid: bool) -> None:
        document = self._require_document()
        if not self._in_bounds(*origin):
            return
        collision = document.data.get("collision", [])
        if not isinstance(collision, list):
            return
        target = bool(collision[origin[1] * document.width + origin[0]])
        if target == solid:
            return
        pending = [origin]
        visited: set[tuple[int, int]] = set()
        while pending:
            current = pending.pop()
            if current in visited or not self._in_bounds(*current):
                continue
            visited.add(current)
            if bool(collision[current[1] * document.width + current[0]]) != target:
                continue
            pending.extend(((current[0] - 1, current[1]), (current[0] + 1, current[1]),
                            (current[0], current[1] - 1), (current[0], current[1] + 1)))
        document.set_collision(visited, solid)

    def place_payload(self, payload: StudioDragPayload, world: tuple[int, int],
                      facing: str = "down") -> Selection | None:
        if payload.kind in {"ContentDefinition", "Reference"}:
            if payload.category not in ENTITY_CATEGORIES:
                return None
            return self.place_entity(payload.category, payload.definition_id, world[0], world[1], facing)
        if payload.kind == "MapElement":
            return self.place_map_element(payload.map_element, world)
        return None

    def place_entity(self, category: str, definition_id: str, x: int, y: int,
                     facing: str = "down") -> Selection:
        if category not in ENTITY_CATEGORIES:
            raise ValueError(f"unknown placeable entity category: {category}")
        identifier = self._require_document().add_entity(category, definition_id, x, y, facing)
        return Selection(category, identifier)

    def move_entity(self, category: str, identifier: int, x: int, y: int) -> None:
        self._require_document().move_entity(category, identifier, x, y)

    def delete_entity(self, category: str, identifier: int) -> None:
        self._require_document().delete_entity(category, identifier)

    def place_map_element(self, element: str, world: tuple[int, int], width: int | None = None,
                          height: int | None = None) -> Selection:
        document = self._require_document()
        element = element.strip()
        tile_size = document.tile_size
        if element in {"player_spawn", "spawn", "Player Spawn"}:
            identifier = document.add_player_spawn(document.next_spawn_id(), world[0], world[1])
            return Selection("playerSpawns", identifier)
        if element in {"region", "trigger_region", "Region"}:
            identifier = self._next_string_id("regions", "region.editor")
            identifier = document.add_region(identifier, world[0], world[1], width or tile_size, height or tile_size)
            return Selection("regions", identifier)
        if element in {"map_transition", "transition", "link", "Map Transition"}:
            identifier = self._next_string_id("links", "link.editor")
            # Target references remain empty until the author selects them in the
            # Inspector; a transition must never guess an arbitrary destination.
            identifier = document.add_link(identifier, world[0], world[1], width or tile_size, height or tile_size)
            return Selection("links", identifier)
        raise ValueError(f"unknown map element: {element}")

    def move_map_element(self, category: str, identifier: str | int, x: int, y: int) -> None:
        document = self._require_document()
        if category == "playerSpawns":
            document.mutate("Move Player Spawn", lambda: self._set_position("playerSpawns", identifier, x, y))
        elif category == "regions":
            document.move_region(str(identifier), x, y)
        elif category == "links":
            document.mutate("Move Map Link", lambda: self._set_link_position(str(identifier), x, y))
        else:
            raise ValueError(f"unknown map element category: {category}")

    def delete_map_element(self, category: str, identifier: str | int) -> None:
        document = self._require_document()
        values = document.data.get(category)
        if category not in {"playerSpawns", "regions", "links"} or not isinstance(values, list):
            raise ValueError(f"unknown map element category: {category}")
        document.mutate(f"Delete {category}", lambda: values.__setitem__(slice(None), [
            value for value in values if not (isinstance(value, dict) and value.get("id") == identifier)
        ]))

    def place_stamp(self, origin: tuple[int, int], cells: Iterable[tuple[int, int, str, int, int]]) -> None:
        self._require_document().place_pattern(self.layer_index, origin[0], origin[1], cells)

    @staticmethod
    def rectangle_cells(start: tuple[int, int], end: tuple[int, int]) -> list[tuple[int, int]]:
        return [(x, y) for y in range(min(start[1], end[1]), max(start[1], end[1]) + 1)
                for x in range(min(start[0], end[0]), max(start[0], end[0]) + 1)]

    def _require_document(self) -> MapDocument:
        if self.document is None:
            raise ValueError("no map document is active")
        if self.layer_index < 0 or self.layer_index >= len(self.document.layers):
            raise IndexError("layer index out of range")
        return self.document

    def can_use_tileset(self, tileset_id: str) -> tuple[bool, str]:
        if self.workspace is None or self.document is None:
            return True, ""
        definition = self.workspace.find("tilesets", tileset_id)
        if definition is None:
            return False, f"tileset is not defined: {tileset_id}"
        size = definition.data.get("tileSize")
        if not isinstance(size, int) or isinstance(size, bool):
            return False, f"tileset has invalid tileSize: {tileset_id}"
        if size != self.document.tile_size:
            return False, f"tileset {tileset_id} uses {size}px tiles; map requires {self.document.tile_size}px"
        return True, ""

    def _validate_tileset(self, tileset_id: str) -> None:
        valid, message = self.can_use_tileset(tileset_id)
        if not valid:
            raise ValueError(message)

    def _in_bounds(self, x: int, y: int) -> bool:
        document = self._require_document()
        return 0 <= x < document.width and 0 <= y < document.height

    def _next_string_id(self, collection: str, prefix: str) -> str:
        document = self._require_document()
        existing = {str(value.get("id")) for value in document.data.get(collection, [])
                    if isinstance(value, dict)}
        index = 1
        while f"{prefix}.{index}" in existing:
            index += 1
        return f"{prefix}.{index}"

    def _set_position(self, category: str, identifier: str | int, x: int, y: int) -> None:
        document = self._require_document()
        value = next((entry for entry in document.data.get(category, [])
                      if isinstance(entry, dict) and entry.get("id") == identifier), None)
        if value is None:
            raise ValueError("map element was not found")
        position = value.setdefault("position", {})
        if not isinstance(position, dict):
            raise ValueError("map element position is invalid")
        position.update({"x": x, "y": y})

    def _set_link_position(self, identifier: str, x: int, y: int) -> None:
        document = self._require_document()
        value = next((entry for entry in document.data.get("links", [])
                      if isinstance(entry, dict) and entry.get("id") == identifier), None)
        if value is None or not isinstance(value.get("trigger"), dict):
            raise ValueError("map link was not found")
        value["trigger"].update({"x": x, "y": y})
