"""Tileset library, batch import and safe usage checks."""

from __future__ import annotations

import re
import unicodedata
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import MapDocument
from ..model.types import ContentDefinition, Diagnostic
from ..model.world_project import WorldProject
from .import_service import SUPPORTED_IMAGE_SUFFIXES, TilesetImportRequest, TilesetImportResult, TilesetImporter, calculate_grid


@dataclass(frozen=True, slots=True)
class TilesetUsage:
    tileset_id: str
    kind: str
    owner_id: str
    detail: str = ""
    count: int = 1


class TilesetUsageIndex:
    """Usage scanner aware of UMAP tile references and semantic/stamp data."""

    def __init__(self, project: WorldProject | None = None, workspace: ContentWorkspace | None = None,
                 document: MapDocument | None = None) -> None:
        self.project = project
        self.workspace = workspace
        self.document = document
        self._usages: dict[str, tuple[TilesetUsage, ...]] = {}
        self.rebuild()

    def set_context(self, project: WorldProject | None, workspace: ContentWorkspace | None) -> None:
        self.project = project
        self.workspace = workspace
        self.rebuild()

    def set_document(self, document: MapDocument | None) -> None:
        self.document = document
        self.rebuild()

    def rebuild(self) -> None:
        values: dict[str, list[TilesetUsage]] = defaultdict(list)
        documents = list(self.project.maps) if self.project else []
        if self.document is not None and all(value is not self.document for value in documents):
            documents.append(self.document)
        if documents:
            for document in documents:
                for layer in document.layers:
                    layer_name = str(layer.get("name", "Layer")) if isinstance(layer, dict) else "Layer"
                    counts: dict[str, int] = defaultdict(int)
                    cells = layer.get("cells", []) if isinstance(layer, dict) else []
                    references = document.data.get("tileReferences", [])
                    if isinstance(cells, list) and isinstance(references, list):
                        for index in cells:
                            if isinstance(index, int) and 0 <= index < len(references) and isinstance(references[index], dict):
                                tileset_id = references[index].get("tilesetId")
                                if isinstance(tileset_id, str) and tileset_id:
                                    counts[tileset_id] += 1
                    for tileset_id, count in counts.items():
                        values[tileset_id].append(TilesetUsage(tileset_id, "map", document.map_id, layer_name, count))
        if self.workspace:
            semantic_by_id = {value.definition_id: value for value in self.workspace.definitions("tileSemantics")}
            semantic_counts: dict[str, int] = defaultdict(int)
            for semantic in semantic_by_id.values():
                tileset_id = semantic.data.get("tilesetId")
                if isinstance(tileset_id, str) and tileset_id:
                    semantic_counts[tileset_id] += 1
            for tileset_id, count in semantic_counts.items():
                values[tileset_id].append(TilesetUsage(tileset_id, "tileSemantics", "tileSemantics", count=count))
            stamp_counts: dict[str, int] = defaultdict(int)
            for stamp in self.workspace.definitions("stamps"):
                cells = stamp.data.get("cells", [])
                if not isinstance(cells, list):
                    continue
                for cell in cells:
                    if not isinstance(cell, dict):
                        continue
                    semantic = semantic_by_id.get(str(cell.get("tileId", "")))
                    if semantic and isinstance(semantic.data.get("tilesetId"), str):
                        stamp_counts[str(semantic.data["tilesetId"])] += 1
            for tileset_id, count in stamp_counts.items():
                values[tileset_id].append(TilesetUsage(tileset_id, "stamps", "stamps", count=count))
        self._usages = {key: tuple(item) for key, item in values.items()}

    def usages(self, tileset_id: str) -> tuple[TilesetUsage, ...]:
        return self._usages.get(tileset_id, ())

    def is_used(self, tileset_id: str) -> bool:
        return bool(self.usages(tileset_id))

    def count(self, tileset_id: str) -> int:
        return sum(item.count for item in self.usages(tileset_id))

    def max_source_index(self, tileset_id: str) -> int:
        maximum = -1
        documents = list(self.project.maps) if self.project else []
        if self.document is not None and all(value is not self.document for value in documents):
            documents.append(self.document)
        for document in documents:
            references = document.data.get("tileReferences", [])
            if not isinstance(references, list):
                continue
            used = {index for layer in document.layers for index in (layer.get("cells", []) if isinstance(layer, dict) else [])
                    if isinstance(index, int)}
            for index in used:
                if index < len(references) and isinstance(references[index], dict) and references[index].get("tilesetId") == tileset_id:
                    maximum = max(maximum, int(references[index].get("sourceIndex", -1)))
        if self.workspace:
            maximum = max(maximum, max((int(value.data.get("sourceIndex", -1)) for value in self.workspace.definitions("tileSemantics")
                                        if value.data.get("tilesetId") == tileset_id), default=-1))
        return maximum

    def describe(self, tileset_id: str) -> str:
        usages = self.usages(tileset_id)
        if not usages:
            return ""
        return "; ".join(f"{item.owner_id} ({item.kind}, {item.count})" for item in usages)


@dataclass(frozen=True, slots=True)
class BatchTilesetImportRequest:
    entries: tuple[TilesetImportRequest, ...]
    conflict_policy: str = "skip"
    id_overrides: tuple[tuple[str, str], ...] = ()


@dataclass(slots=True)
class BatchTilesetImportResult:
    imported: list[TilesetImportResult]
    diagnostics: list[Diagnostic]

    @property
    def ok(self) -> bool:
        return not any(issue.is_error for issue in self.diagnostics) and bool(self.imported)


class TilesetLibrary:
    """Project-facing façade used by both dialogs and future headless tooling."""

    def __init__(self, workspace: ContentWorkspace | None = None, project: WorldProject | None = None,
                 importer: TilesetImporter | None = None) -> None:
        self.workspace = workspace
        self.project = project
        self.importer = importer or TilesetImporter()
        self.usage_index = TilesetUsageIndex(project, workspace)

    def set_context(self, workspace: ContentWorkspace | None, project: WorldProject | None = None) -> None:
        self.workspace = workspace
        self.project = project
        self.usage_index.set_context(project, workspace)

    def definitions(self, query: str = "") -> list[ContentDefinition]:
        return self.workspace.definitions("tilesets", query) if self.workspace else []

    @staticmethod
    def suggest_id(path: Path) -> str:
        stem = unicodedata.normalize("NFKD", path.stem).encode("ascii", "ignore").decode("ascii")
        words = [part for part in re.split(r"[^A-Za-z0-9]+", stem.casefold()) if part]
        prefix = "tileset"
        if words and words[0] == prefix:
            words = words[1:]
        return ".".join((prefix, *(words or ["imported"])))

    @staticmethod
    def discover_files(folder: Path, recursive: bool = False) -> list[Path]:
        folder = folder.expanduser()
        if not folder.is_dir():
            raise ValueError("tileset folder does not exist")
        iterator = folder.rglob("*") if recursive else folder.iterdir()
        return sorted((path for path in iterator if path.is_file() and path.suffix.casefold() in SUPPORTED_IMAGE_SUFFIXES), key=lambda value: value.as_posix().casefold())

    def import_batch(self, request: BatchTilesetImportRequest) -> BatchTilesetImportResult:
        if self.workspace is None:
            return BatchTilesetImportResult([], [Diagnostic("error", "repository content is unavailable", code="workspace_missing")])
        policy = request.conflict_policy.casefold()
        if policy not in {"skip", "reimport", "replace", "change_id"}:
            return BatchTilesetImportResult([], [Diagnostic("error", f"unknown conflict policy: {request.conflict_policy}", code="tileset_conflict_policy")])
        overrides = dict(request.id_overrides)
        imported: list[TilesetImportResult] = []
        diagnostics: list[Diagnostic] = []
        seen: set[str] = set()
        for entry in request.entries:
            tileset_id = overrides.get(str(entry.source_image), entry.tileset_id)
            if not tileset_id:
                diagnostics.append(Diagnostic("error", "tileset ID cannot be empty", code="tileset_id_missing"))
                continue
            if tileset_id in seen:
                diagnostics.append(Diagnostic("error", f"duplicate tileset ID in batch: {tileset_id}", code="tileset_batch_duplicate"))
                continue
            seen.add(tileset_id)
            existing = self.workspace.find("tilesets", tileset_id)
            if existing is not None and policy == "skip":
                diagnostics.append(Diagnostic("warning", f"skipped existing tileset: {tileset_id}", code="tileset_conflict", definition_id=tileset_id))
                continue
            if existing is not None and policy == "change_id":
                used_ids = seen | {value.definition_id for value in self.workspace.definitions("tilesets")}
                tileset_id = self._next_id(tileset_id, used_ids)
            if existing is not None and policy in {"reimport", "replace"}:
                guard = self._check_reimport(existing, entry)
                if guard:
                    diagnostics.append(guard)
                    continue
            adjusted = TilesetImportRequest(entry.source_image, tileset_id, entry.tile_width, entry.tile_height,
                                            entry.spacing, entry.margin, entry.asset_root, entry.workspace_root,
                                            entry.copy_to_workspace, entry.display_name,
                                            existing is not None and policy in {"reimport", "replace"})
            result = self.importer.import_tileset(self.workspace, adjusted)
            imported.append(result)
            diagnostics.extend(result.diagnostics or [])
            if result.ok:
                self.usage_index.rebuild()
        return BatchTilesetImportResult(imported, diagnostics)

    def reimport(self, definition_id: str, source_image: Path, asset_root: Path | None,
                 tile_width: int | None = None, tile_height: int | None = None,
                 display_name: str | None = None) -> TilesetImportResult:
        if self.workspace is None:
            return TilesetImportResult(None, None, diagnostics=[Diagnostic("error", "repository content is unavailable", code="workspace_missing")])
        definition = self.workspace.find("tilesets", definition_id)
        if definition is None:
            return TilesetImportResult(None, None, diagnostics=[Diagnostic("error", f"tileset not found: {definition_id}", code="tileset_missing")])
        width = tile_width or int(definition.data.get("tileSize", 16))
        height = tile_height or width
        entry = TilesetImportRequest(source_image, definition_id, width, height, asset_root=asset_root,
                                     display_name=(display_name if display_name is not None else
                                                   str(definition.data.get("displayName", definition_id))),
                                     allow_replace=True)
        guard = self._check_reimport(definition, entry)
        if guard:
            return TilesetImportResult(None, None, diagnostics=[guard])
        result = self.importer.import_tileset(self.workspace, entry)
        self.usage_index.rebuild()
        return result

    def update_properties(self, definition_id: str, display_name: str, tile_size: int,
                          asset_root: Path | None) -> TilesetImportResult:
        if self.workspace is None:
            return TilesetImportResult(None, None, diagnostics=[Diagnostic(
                "error", "repository content is unavailable", code="workspace_missing")])
        definition = self.workspace.find("tilesets", definition_id)
        if definition is None:
            return TilesetImportResult(None, None, diagnostics=[Diagnostic(
                "error", f"tileset not found: {definition_id}", code="tileset_missing")])
        name = display_name.strip()
        if not name:
            return TilesetImportResult(None, None, diagnostics=[Diagnostic(
                "error", "tileset display name cannot be empty", code="tileset_name_missing")])
        if tile_size <= 0:
            return TilesetImportResult(None, None, diagnostics=[Diagnostic(
                "error", "tile size must be positive", code="tileset_size_invalid")])
        current_size = int(definition.data.get("tileSize", 16))
        if tile_size == current_size:
            if definition.data.get("displayName") != name:
                self.workspace.update(definition, "displayName", name)
                definition = self.workspace.find("tilesets", definition_id)
            return TilesetImportResult(
                definition, None, int(definition.data.get("columns", 0)),
                int(definition.data.get("rows", 0)), diagnostics=[] if definition else None,
            )
        self.usage_index.rebuild()
        if self.usage_index.is_used(definition_id):
            return TilesetImportResult(None, None, diagnostics=[Diagnostic(
                "error", f"cannot change tile size while {definition_id} is in use by {self.usage_index.describe(definition_id)}",
                code="tileset_resize_in_use", definition_id=definition_id)])
        relative = definition.data.get("relativeAssetPath")
        if asset_root is None or not isinstance(relative, str):
            return TilesetImportResult(None, None, diagnostics=[Diagnostic(
                "error", "the repository assets directory is unavailable", code="workspace_missing")])
        return self.reimport(
            definition_id, asset_root / relative, asset_root, tile_size, tile_size, name,
        )

    def delete(self, definition_id: str) -> tuple[bool, list[Diagnostic]]:
        if self.workspace is None:
            return False, [Diagnostic("error", "repository content is unavailable", code="workspace_missing")]
        self.usage_index.rebuild()
        usages = self.usage_index.usages(definition_id)
        if usages:
            return False, [Diagnostic("error", f"cannot delete {definition_id}; it is used by {self.usage_index.describe(definition_id)}", code="tileset_in_use", definition_id=definition_id)]
        definition = self.workspace.find("tilesets", definition_id)
        if definition is None:
            return False, [Diagnostic("error", f"tileset not found: {definition_id}", code="tileset_missing")]
        self.workspace.delete_definition(definition)
        return True, []

    def _check_reimport(self, definition: ContentDefinition, request: TilesetImportRequest) -> Diagnostic | None:
        try:
            dimensions = self.importer.inspect(request.source_image)
            columns, rows = calculate_grid(dimensions, request.tile_width, request.tile_height, request.spacing, request.margin)
        except (OSError, ValueError) as error:
            return Diagnostic("error", str(error), code="tileset_reimport")
        maximum = self.usage_index.max_source_index(definition.definition_id)
        if maximum >= columns * rows:
            return Diagnostic("error", f"reimport would invalidate sourceIndex {maximum}; new atlas has {columns * rows} tiles", code="tileset_reimport_used_index", definition_id=definition.definition_id)
        return None

    @staticmethod
    def _next_id(base: str, used: set[str]) -> str:
        candidate = f"{base}.imported"
        index = 2
        while candidate in used:
            candidate = f"{base}.imported{index}"
            index += 1
        return candidate
