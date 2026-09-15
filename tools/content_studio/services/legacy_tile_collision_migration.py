"""Compatibility migration from legacy map-cell collision to tile Pixel Collision.

The migration is intentionally conservative:

* explicit Pixel Collision masks are authoritative and never overwritten;
* legacy solid tiles without a mask become full-tile binary masks;
* legacy map collision is cleared only after every solid cell can be resolved;
* mixed per-placement collision for the same source tile is rejected because
  tileset-owned collision cannot represent that distinction.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TypeAlias

from ..model.content_workspace import ContentWorkspace
from ..model.map_document import MapDocument
from ..model.types import Diagnostic
from ..model.world_project import WorldProject
from .tile_collision_service import TileCollisionService


TileCollisionKey: TypeAlias = tuple[str, int]


@dataclass(frozen=True, slots=True)
class LegacyTileCollisionMigrationResult:
    changed: bool = False
    created_masks: tuple[TileCollisionKey, ...] = ()
    preserved_masks: tuple[TileCollisionKey, ...] = ()
    migrated_maps: tuple[str, ...] = ()
    diagnostics: tuple[Diagnostic, ...] = ()

    @property
    def ok(self) -> bool:
        return not any(
            issue.is_error
            for issue in self.diagnostics
        )


@dataclass(frozen=True, slots=True)
class _MigrationPlan:
    create_masks: tuple[TileCollisionKey, ...]
    preserve_masks: tuple[TileCollisionKey, ...]
    migrated_maps: tuple[str, ...]
    diagnostics: tuple[Diagnostic, ...]


class LegacyTileCollisionMigrationService:
    """Upgrade old per-map collision only when semantics can be preserved."""

    def migrate(
        self,
        project: WorldProject,
        workspace: ContentWorkspace,
    ) -> LegacyTileCollisionMigrationResult:
        plan = self._analyze(
            project,
            workspace,
        )

        if any(
            issue.is_error
            for issue in plan.diagnostics
        ):
            return LegacyTileCollisionMigrationResult(
                diagnostics=plan.diagnostics,
            )

        created: list[TileCollisionKey] = []

        for tileset_id, source_index in plan.create_masks:
            definition = workspace.find(
                "tilesets",
                tileset_id,
            )

            if definition is None:
                raise RuntimeError(
                    f"validated tileset disappeared: {tileset_id}"
                )

            tile_size = int(
                definition.data["tileSize"]
            )

            entries = definition.data.setdefault(
                "tileCollisions",
                [],
            )

            if not isinstance(entries, list):
                raise RuntimeError(
                    f"{tileset_id} tileCollisions changed during migration"
                )

            entries.append({
                "sourceIndex": source_index,
                "width": tile_size,
                "height": tile_size,
                "cells": [1] * (
                    tile_size *
                    tile_size
                ),
            })

            entries.sort(
                key=lambda value: (
                    int(value.get("sourceIndex", -1))
                    if isinstance(value, dict)
                    else -1
                )
            )

            for content_file in workspace.files:
                if content_file.path == definition.source_path:
                    content_file.dirty = True
                    break

            created.append(
                (tileset_id, source_index)
            )

        migrated: list[str] = []

        target_maps = set(
            plan.migrated_maps
        )

        for document in project.maps:
            if document.map_id not in target_maps:
                continue

            cell_count = (
                document.width *
                document.height
            )

            document.data["collision"] = (
                [0] * cell_count
            )

            document.data["collisionBindings"] = []

            document.dirty = True
            document._revision += 1

            migrated.append(
                document.map_id
            )

        changed = bool(
            created or migrated
        )

        if changed:
            project.dirty = True

        return LegacyTileCollisionMigrationResult(
            changed=changed,
            created_masks=tuple(
                sorted(created)
            ),
            preserved_masks=plan.preserve_masks,
            migrated_maps=tuple(
                sorted(migrated)
            ),
            diagnostics=plan.diagnostics,
        )

    def _analyze(
        self,
        project: WorldProject,
        workspace: ContentWorkspace,
    ) -> _MigrationPlan:
        diagnostics: list[Diagnostic] = []

        existing_masks: set[
            TileCollisionKey
        ] = set()

        collision_service = TileCollisionService(
            workspace
        )

        for definition in workspace.definitions(
            "tilesets"
        ):
            try:
                masks = collision_service.masks(
                    definition.definition_id
                )
            except ValueError as error:
                diagnostics.append(
                    Diagnostic(
                        "error",
                        str(error),
                        "tileCollisions",
                        "invalid_tile_collision",
                        definition.definition_id,
                        definition.source_path,
                    )
                )
                continue

            existing_masks.update(
                (
                    definition.definition_id,
                    mask.source_index,
                )
                for mask in masks
            )

        placements: dict[
            TileCollisionKey,
            list[tuple[str, int, int, int, bool]],
        ] = {}

        legacy_sources: set[
            TileCollisionKey
        ] = set()

        migrated_maps: set[str] = set()

        for document in project.maps:
            self._analyze_map(
                document,
                placements,
                legacy_sources,
                migrated_maps,
                diagnostics,
            )

        create_masks: set[
            TileCollisionKey
        ] = set()

        preserve_masks: set[
            TileCollisionKey
        ] = set()

        for key in sorted(legacy_sources):
            tileset_id, source_index = key

            definition = workspace.find(
                "tilesets",
                tileset_id,
            )

            if definition is None:
                diagnostics.append(
                    Diagnostic(
                        "error",
                        f"legacy collision references missing tileset: {tileset_id}",
                        "collisionBindings",
                        "legacy_collision_missing_tileset",
                        tileset_id,
                    )
                )
                continue

            tile_size = definition.data.get(
                "tileSize"
            )

            columns = definition.data.get(
                "columns"
            )

            rows = definition.data.get(
                "rows"
            )

            if (
                not isinstance(tile_size, int)
                or isinstance(tile_size, bool)
                or tile_size <= 0
                or not isinstance(columns, int)
                or isinstance(columns, bool)
                or columns <= 0
                or not isinstance(rows, int)
                or isinstance(rows, bool)
                or rows <= 0
            ):
                diagnostics.append(
                    Diagnostic(
                        "error",
                        f"legacy collision references invalid tileset: {tileset_id}",
                        "collisionBindings",
                        "legacy_collision_invalid_tileset",
                        tileset_id,
                        definition.source_path,
                    )
                )
                continue

            if (
                source_index < 0
                or source_index >= columns * rows
            ):
                diagnostics.append(
                    Diagnostic(
                        "error",
                        f"legacy collision sourceIndex {source_index} is outside {tileset_id}",
                        "collisionBindings",
                        "legacy_collision_invalid_source",
                        tileset_id,
                        definition.source_path,
                    )
                )
                continue

            if key in existing_masks:
                preserve_masks.add(key)
                continue

            uses = placements.get(
                key,
                [],
            )

            free_uses = [
                value
                for value in uses
                if not value[4]
            ]

            if free_uses:
                first = free_uses[0]

                diagnostics.append(
                    Diagnostic(
                        "error",
                        (
                            f"{tileset_id} tile {source_index} has legacy "
                            "collision in some placements but is walkable in "
                            "others; define Pixel Collision explicitly before "
                            "migration"
                        ),
                        (
                            f"layers[{first[1]}]."
                            f"cells[{first[3]}]"
                        ),
                        "legacy_collision_scope_conflict",
                        tileset_id,
                        map_id=first[0],
                    )
                )

                continue

            create_masks.add(key)

        return _MigrationPlan(
            create_masks=tuple(
                sorted(create_masks)
            ),
            preserve_masks=tuple(
                sorted(preserve_masks)
            ),
            migrated_maps=tuple(
                sorted(migrated_maps)
            ),
            diagnostics=tuple(
                diagnostics
            ),
        )

    def _analyze_map(
        self,
        document: MapDocument,
        placements: dict[
            TileCollisionKey,
            list[tuple[str, int, int, int, bool]],
        ],
        legacy_sources: set[TileCollisionKey],
        migrated_maps: set[str],
        diagnostics: list[Diagnostic],
    ) -> None:
        cell_count = (
            document.width *
            document.height
        )

        collision = document.data.get(
            "collision",
            [],
        )

        bindings = document.data.get(
            "collisionBindings",
            [],
        )

        if not isinstance(collision, list):
            diagnostics.append(
                Diagnostic(
                    "error",
                    "legacy collision must be an array",
                    "collision",
                    "legacy_collision_invalid",
                    source_path=document.path,
                    map_id=document.map_id,
                )
            )
            return

        if len(collision) != cell_count:
            diagnostics.append(
                Diagnostic(
                    "error",
                    (
                        "legacy collision size does not match "
                        "map dimensions"
                    ),
                    "collision",
                    "legacy_collision_invalid_size",
                    source_path=document.path,
                    map_id=document.map_id,
                )
            )
            return

        if not isinstance(bindings, list):
            diagnostics.append(
                Diagnostic(
                    "error",
                    "collisionBindings must be an array",
                    "collisionBindings",
                    "legacy_collision_invalid_bindings",
                    source_path=document.path,
                    map_id=document.map_id,
                )
            )
            return

        solid_indices = {
            index
            for index, value in enumerate(
                collision
            )
            if bool(value)
        }

        if solid_indices or bindings:
            migrated_maps.add(
                document.map_id
            )

        valid_locations: set[
            tuple[int, int, int]
        ] = set()

        bound_cells: set[
            tuple[int, int]
        ] = set()

        references = document.data.get(
            "tileReferences",
            [],
        )

        if not isinstance(references, list):
            diagnostics.append(
                Diagnostic(
                    "error",
                    "tileReferences must be an array",
                    "tileReferences",
                    "legacy_collision_invalid_references",
                    source_path=document.path,
                    map_id=document.map_id,
                )
            )
            return

        for binding_index, binding in enumerate(
            bindings
        ):
            path = (
                f"collisionBindings[{binding_index}]"
            )

            if not isinstance(binding, dict):
                diagnostics.append(
                    Diagnostic(
                        "error",
                        "legacy collision binding must be an object",
                        path,
                        "legacy_collision_invalid_binding",
                        source_path=document.path,
                        map_id=document.map_id,
                    )
                )
                continue

            layer = binding.get("layer")
            x = binding.get("x")
            y = binding.get("y")
            tileset_id = binding.get(
                "tilesetId"
            )
            source_index = binding.get(
                "sourceIndex"
            )
            flags = binding.get(
                "flags",
                0,
            )

            if (
                not isinstance(layer, int)
                or isinstance(layer, bool)
                or not isinstance(x, int)
                or isinstance(x, bool)
                or not isinstance(y, int)
                or isinstance(y, bool)
                or not isinstance(source_index, int)
                or isinstance(source_index, bool)
                or not isinstance(flags, int)
                or isinstance(flags, bool)
                or not isinstance(tileset_id, str)
                or not tileset_id
                or layer < 0
                or layer >= len(document.layers)
                or x < 0
                or x >= document.width
                or y < 0
                or y >= document.height
            ):
                diagnostics.append(
                    Diagnostic(
                        "error",
                        "legacy collision binding is invalid",
                        path,
                        "legacy_collision_invalid_binding",
                        source_path=document.path,
                        map_id=document.map_id,
                    )
                )
                continue

            location = (
                layer,
                x,
                y,
            )

            if location in valid_locations:
                diagnostics.append(
                    Diagnostic(
                        "error",
                        "duplicate legacy collision binding",
                        path,
                        "legacy_collision_duplicate_binding",
                        source_path=document.path,
                        map_id=document.map_id,
                    )
                )
                continue

            cell_index = (
                y * document.width +
                x
            )

            if cell_index not in solid_indices:
                diagnostics.append(
                    Diagnostic(
                        "error",
                        "collision binding points to a non-solid legacy cell",
                        path,
                        "legacy_collision_binding_without_cell",
                        source_path=document.path,
                        map_id=document.map_id,
                    )
                )
                continue

            layer_data = document.layers[
                layer
            ]

            cells = layer_data.get(
                "cells",
                [],
            )

            if (
                not isinstance(cells, list)
                or cell_index >= len(cells)
            ):
                diagnostics.append(
                    Diagnostic(
                        "error",
                        "legacy collision binding layer cells are invalid",
                        path,
                        "legacy_collision_binding_mismatch",
                        source_path=document.path,
                        map_id=document.map_id,
                    )
                )
                continue

            reference_index = cells[
                cell_index
            ]

            if (
                not isinstance(reference_index, int)
                or isinstance(reference_index, bool)
                or reference_index < 0
                or reference_index >= len(references)
            ):
                diagnostics.append(
                    Diagnostic(
                        "error",
                        "legacy collision binding has no matching tile placement",
                        path,
                        "legacy_collision_binding_mismatch",
                        source_path=document.path,
                        map_id=document.map_id,
                    )
                )
                continue

            reference = references[
                reference_index
            ]

            if not isinstance(reference, dict):
                diagnostics.append(
                    Diagnostic(
                        "error",
                        "legacy collision binding references an invalid tile",
                        path,
                        "legacy_collision_binding_mismatch",
                        source_path=document.path,
                        map_id=document.map_id,
                    )
                )
                continue

            actual = (
                str(
                    reference.get(
                        "tilesetId",
                        "",
                    )
                ),
                int(
                    reference.get(
                        "sourceIndex",
                        -1,
                    )
                ),
                int(
                    reference.get(
                        "flags",
                        0,
                    )
                ),
            )

            expected = (
                tileset_id,
                source_index,
                flags,
            )

            if actual != expected:
                diagnostics.append(
                    Diagnostic(
                        "error",
                        "legacy collision binding no longer matches its tile placement",
                        path,
                        "legacy_collision_binding_mismatch",
                        source_path=document.path,
                        map_id=document.map_id,
                    )
                )
                continue

            valid_locations.add(
                location
            )

            bound_cells.add(
                (x, y)
            )

            legacy_sources.add(
                (
                    tileset_id,
                    source_index,
                )
            )

        for index in sorted(
            solid_indices
        ):
            x = index % document.width
            y = index // document.width

            if (x, y) not in bound_cells:
                diagnostics.append(
                    Diagnostic(
                        "error",
                        (
                            "solid legacy collision cell has no "
                            "tile provenance and cannot be migrated safely"
                        ),
                        f"collision[{index}]",
                        "legacy_collision_orphan",
                        source_path=document.path,
                        map_id=document.map_id,
                    )
                )

        for layer_index, layer in enumerate(
            document.layers
        ):
            cells = layer.get(
                "cells",
                [],
            )

            if not isinstance(cells, list):
                continue

            for cell_index, reference_index in enumerate(
                cells
            ):
                if (
                    not isinstance(reference_index, int)
                    or isinstance(reference_index, bool)
                    or reference_index < 0
                    or reference_index >= len(references)
                ):
                    continue

                reference = references[
                    reference_index
                ]

                if not isinstance(reference, dict):
                    continue

                tileset_id = reference.get(
                    "tilesetId"
                )

                source_index = reference.get(
                    "sourceIndex"
                )

                if (
                    not isinstance(tileset_id, str)
                    or not tileset_id
                    or not isinstance(source_index, int)
                    or isinstance(source_index, bool)
                ):
                    continue

                x = (
                    cell_index %
                    document.width
                )

                y = (
                    cell_index //
                    document.width
                )

                key = (
                    tileset_id,
                    source_index,
                )

                placements.setdefault(
                    key,
                    [],
                ).append(
                    (
                        document.map_id,
                        layer_index,
                        x,
                        cell_index,
                        (
                            layer_index,
                            x,
                            y,
                        ) in valid_locations,
                    )
                )
