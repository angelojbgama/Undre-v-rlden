"""Indexed access to authored ``tileSemantics`` definitions."""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass

from ..model.content_workspace import ContentWorkspace
from ..model.tile_semantics import EDGES, ROLES, TOPOLOGIES, TileSemantic, TerrainFamily, as_semantics
from ..model.types import Diagnostic


@dataclass(frozen=True, slots=True)
class SemanticLookupKey:
    family: str
    role: str
    topology: str = ""


class TileSemanticCatalog:
    """A reusable index rebuilt after authored semantic mutations.

    The catalog is deliberately a view over ``ContentWorkspace``.  It never
    owns or serializes a parallel set of semantic definitions.
    """

    def __init__(self, workspace: ContentWorkspace | None = None) -> None:
        self.workspace = workspace
        self._semantics: tuple[TileSemantic, ...] = ()
        self._by_family: dict[str, tuple[TileSemantic, ...]] = {}
        self._by_family_role: dict[tuple[str, str], tuple[TileSemantic, ...]] = {}
        self._by_family_role_topology: dict[tuple[str, str, str], tuple[TileSemantic, ...]] = {}
        self._by_reference: dict[tuple[str, int], tuple[TileSemantic, ...]] = {}
        self._diagnostics: list[Diagnostic] = []
        self._dirty = True

    def set_workspace(self, workspace: ContentWorkspace | None) -> None:
        if workspace is not self.workspace:
            self.workspace = workspace
            self.invalidate()

    def invalidate(self) -> None:
        self._dirty = True

    def rebuild(self) -> None:
        definitions = self.workspace.definitions("tileSemantics") if self.workspace else []
        semantics = as_semantics(definitions)
        by_family: dict[str, list[TileSemantic]] = defaultdict(list)
        by_role: dict[tuple[str, str], list[TileSemantic]] = defaultdict(list)
        by_topology: dict[tuple[str, str, str], list[TileSemantic]] = defaultdict(list)
        by_reference: dict[tuple[str, int], list[TileSemantic]] = defaultdict(list)
        diagnostics: list[Diagnostic] = []
        tilesets = {
            value.definition_id: value for value in (self.workspace.definitions("tilesets") if self.workspace else [])
        }
        for semantic in semantics:
            path = semantic.definition_id
            if not semantic.family:
                diagnostics.append(Diagnostic("warning", "tile semantic has no terrain family", path, "semantic_family_missing", semantic.definition_id))
            if semantic.role not in ROLES:
                diagnostics.append(Diagnostic("warning", f"unknown tile semantic role: {semantic.role}", path, "semantic_role_unknown", semantic.definition_id))
            if semantic.topology not in TOPOLOGIES:
                diagnostics.append(Diagnostic("warning", f"unknown tile semantic topology: {semantic.topology}", path, "semantic_topology_unknown", semantic.definition_id))
            if any(value not in EDGES for value in semantic.edges.values()):
                diagnostics.append(Diagnostic("warning", "tile semantic has an unknown edge profile", path, "semantic_edge_unknown", semantic.definition_id))
            tileset = tilesets.get(semantic.tileset_id)
            if tileset is None:
                diagnostics.append(Diagnostic("error", f"semantic references missing tileset: {semantic.tileset_id}", path, "semantic_tileset_missing", semantic.definition_id))
            else:
                count = max(0, int(tileset.data.get("columns", 0))) * max(0, int(tileset.data.get("rows", 0)))
                if semantic.source_index < 0 or semantic.source_index >= count:
                    diagnostics.append(Diagnostic("error", f"sourceIndex {semantic.source_index} exceeds tileset grid ({count} tiles)", path, "semantic_source_out_of_range", semantic.definition_id))
            reference = semantic.reference
            by_reference[reference].append(semantic)
            if len(by_reference[reference]) > 1:
                diagnostics.append(Diagnostic("warning", f"duplicate semantic mapping for {reference[0]} / {reference[1]}", path, "semantic_duplicate_reference", semantic.definition_id))
            if semantic.family:
                by_family[semantic.family].append(semantic)
                by_role[(semantic.family, semantic.role)].append(semantic)
                by_topology[(semantic.family, semantic.role, semantic.topology)].append(semantic)
        self._semantics = semantics
        self._by_family = {key: tuple(value) for key, value in by_family.items()}
        self._by_family_role = {key: tuple(value) for key, value in by_role.items()}
        self._by_family_role_topology = {key: tuple(value) for key, value in by_topology.items()}
        self._by_reference = {key: tuple(value) for key, value in by_reference.items()}
        self._diagnostics = diagnostics
        self._dirty = False

    def _ensure(self) -> None:
        if self._dirty:
            self.rebuild()

    @property
    def diagnostics(self) -> tuple[Diagnostic, ...]:
        self._ensure()
        return tuple(self._diagnostics)

    @property
    def semantics(self) -> tuple[TileSemantic, ...]:
        self._ensure()
        return self._semantics

    def families(self) -> tuple[TerrainFamily, ...]:
        self._ensure()
        return tuple(TerrainFamily(key, value) for key, value in sorted(self._by_family.items()))

    def by_family(self, family: str) -> tuple[TileSemantic, ...]:
        self._ensure()
        return self._by_family.get(family, ())

    def by_family_role(self, family: str, role: str) -> tuple[TileSemantic, ...]:
        self._ensure()
        return self._by_family_role.get((family, role), ())

    def by_family_role_topology(self, family: str, role: str, topology: str) -> tuple[TileSemantic, ...]:
        self._ensure()
        return self._by_family_role_topology.get((family, role, topology), ())

    def by_reference(self, tileset_id: str, source_index: int) -> tuple[TileSemantic, ...]:
        self._ensure()
        return self._by_reference.get((tileset_id, source_index), ())

    def find(self, definition_id: str) -> TileSemantic | None:
        self._ensure()
        return next((value for value in self._semantics if value.definition_id == definition_id), None)

    def validate_preferred_layers(self, available_layers: set[str]) -> list[Diagnostic]:
        self._ensure()
        return [Diagnostic("warning", f"preferredLayer is unavailable: {value.preferred_layer}", value.definition_id,
                           "semantic_preferred_layer_unavailable", value.definition_id)
                for value in self._semantics if value.preferred_layer and value.preferred_layer not in available_layers]
