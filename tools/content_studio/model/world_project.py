from __future__ import annotations

from pathlib import Path

from ..formats.umap import MAP_FORMAT, load_map
from ..formats.uworld import WORLD_FORMAT, WORLD_VERSION, decode_world, load_world, new_world, write_world
from .map_document import MapDocument
from .types import Diagnostic, JsonValue


class WorldProject:
    def __init__(self, maps: list[MapDocument], entry_map_id: str, path: Path | None = None) -> None:
        if not maps:
            raise ValueError("world project must contain at least one map")
        self.maps = maps
        self.entry_map_id = entry_map_id
        self.path = path
        self.active_index = 0
        self.dirty = False

    @classmethod
    def new(cls, map_id: str = "map.untitled", width: int = 32, height: int = 24, tile_size: int = 16) -> "WorldProject":
        return cls([MapDocument.new(map_id, width, height, tile_size, False)], map_id)

    @classmethod
    def open(cls, path: Path) -> tuple["WorldProject | None", list[Diagnostic]]:
        if path.suffix.casefold() == ".uworld":
            decoded = load_world(path)
            if decoded.data is None:
                return None, decoded.diagnostics
            maps = decoded.data.get("maps", [])
            if not isinstance(maps, list):
                return None, decoded.diagnostics
            documents = [MapDocument(value, path) for value in maps if isinstance(value, dict)]
            project = cls(documents, str(decoded.data.get("entryMapId", "")), path)
            return project, decoded.diagnostics
        document, diagnostics = MapDocument.open(path)
        if document is None:
            return None, diagnostics
        return cls([document], document.map_id, path), diagnostics

    @property
    def active_map(self) -> MapDocument:
        return self.maps[self.active_index]

    def map_by_id(self, map_id: str) -> MapDocument | None:
        return next((value for value in self.maps if value.map_id == map_id), None)

    def select_map(self, map_id: str) -> None:
        for index, value in enumerate(self.maps):
            if value.map_id == map_id:
                self.active_index = index
                return
        raise ValueError(f"unknown map: {map_id}")

    def add_map(self, document: MapDocument) -> None:
        if self.map_by_id(document.map_id):
            raise ValueError(f"map already exists: {document.map_id}")
        self.maps.append(document)
        self.active_index = len(self.maps) - 1
        self.dirty = True

    def import_map(self, path: Path) -> None:
        document, diagnostics = MapDocument.open(path)
        if document is None or diagnostics:
            message = diagnostics[0].message if diagnostics else "could not import map"
            raise ValueError(message)
        self.add_map(document)

    def remove_map(self, map_id: str) -> None:
        if len(self.maps) <= 1:
            raise ValueError("a world project must keep at least one map")
        if map_id == self.entry_map_id:
            raise ValueError("cannot remove the entry map")
        index = next((index for index, value in enumerate(self.maps) if value.map_id == map_id), None)
        if index is None:
            raise ValueError("map was not found")
        self.maps.pop(index)
        self.active_index = min(self.active_index, len(self.maps) - 1)
        self.dirty = True

    def set_entry_map(self, map_id: str) -> None:
        if not self.map_by_id(map_id):
            raise ValueError("entry map must exist")
        self.entry_map_id = map_id
        self.dirty = True

    def validate_cross_map(self) -> list[Diagnostic]:
        issues: list[Diagnostic] = []
        ids = {document.map_id for document in self.maps}
        if self.entry_map_id not in ids:
            issues.append(Diagnostic("error", "entryMapId does not reference an existing map", "entryMapId", "missing_entry_map", map_id=self.entry_map_id, source_path=self.path))
        for document in self.maps:
            issues.extend(document.validate_structural())
            spawn_ids = {str(value.get("id")) for value in document.data.get("playerSpawns", []) if isinstance(value, dict)}
            for index, link in enumerate(document.data.get("links", [])):
                if not isinstance(link, dict):
                    continue
                target_map = str(link.get("targetMapId", ""))
                if target_map not in ids:
                    issues.append(Diagnostic("error", f"link target map does not exist: {target_map}", f"maps[{document.map_id}].links[{index}].targetMapId", "missing_target_map", map_id=document.map_id, source_path=document.path))
                elif str(link.get("targetSpawnId", "")) not in {str(value.get("id")) for value in (self.map_by_id(target_map).data.get("playerSpawns", []) if self.map_by_id(target_map) else []) if isinstance(value, dict)}:
                    issues.append(Diagnostic("error", "link target spawn does not exist", f"maps[{document.map_id}].links[{index}].targetSpawnId", "missing_target_spawn", map_id=document.map_id, source_path=document.path))
        return issues

    def authored_data(self) -> dict[str, JsonValue]:
        return {"format": WORLD_FORMAT, "version": WORLD_VERSION, "entryMapId": self.entry_map_id, "maps": [document.data for document in self.maps]}

    def save(self, path: Path | None = None) -> None:
        target = path or self.path
        if target is None:
            raise ValueError("project has no path; use save_as")
        if target.suffix.casefold() == ".umap" and len(self.maps) == 1:
            self.maps[0].save(target)
        else:
            if target.suffix.casefold() != ".uworld":
                raise ValueError("multi-map projects must be saved as .uworld")
            write_world(target, self.authored_data())
            for document in self.maps:
                document.dirty = False
        self.path = target
        self.dirty = False

    def save_as(self, path: Path) -> None:
        self.save(path)

    def has_unsaved_changes(self) -> bool:
        return self.dirty or any(document.dirty for document in self.maps)

