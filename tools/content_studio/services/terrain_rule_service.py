"""Authoring helpers for visual Smart Terrain rule assignment.

The rule editor is intentionally a view over the existing ``tileSemantics``
definitions.  It does not introduce a parallel SmartTile database or a new
content format.
"""

from __future__ import annotations

import re
from collections.abc import Mapping
from dataclasses import dataclass
from typing import TYPE_CHECKING

from ..formats.content_json import CONTENT_VERSION
from ..model.content_workspace import ContentWorkspace
from ..model.types import JsonValue
from .autotile_resolver import EAST, NORTH, SOUTH, WEST

if TYPE_CHECKING:
    from ..model.world_project import WorldProject


RULE_SLOTS = (
    "north_west", "north", "north_east",
    "west", "center", "east",
    "south_west", "south", "south_east",
)

RULE_SLOT_LABELS = {
    "north_west": "NW", "north": "N", "north_east": "NE",
    "west": "W", "center": "X", "east": "E",
    "south_west": "SW", "south": "S", "south_east": "SE",
}

# A visual 3x3 rule describes a filled rectangular area.  The mask is the
# set of orthogonal terrain neighbours for the cell represented by a slot.
# For example, the atlas cell at NW is connected to E and S when it is used at
# the top-left corner of a painted room.  These masks are tooling metadata
# encoded through the existing semantic edge fields; no Content v5 field is
# added.
ALL_NEIGHBORS = NORTH | EAST | SOUTH | WEST
RULE_SLOT_MASK = {
    "north_west": EAST | SOUTH,
    "north": EAST | SOUTH | WEST,
    "north_east": SOUTH | WEST,
    "west": NORTH | EAST | SOUTH,
    "center": ALL_NEIGHBORS,
    "east": NORTH | SOUTH | WEST,
    "south_west": NORTH | EAST,
    "south": NORTH | EAST | WEST,
    "south_east": NORTH | WEST,
}

RULE_SLOT_TOPOLOGY = {
    "north_west": "outerCorner", "north": "straightHorizontal", "north_east": "outerCorner",
    "west": "straightVertical", "center": "interior", "east": "straightVertical",
    "south_west": "outerCorner", "south": "straightHorizontal", "south_east": "outerCorner",
}


@dataclass(frozen=True, slots=True)
class TerrainRuleSummary:
    tileset_id: str
    family: str
    role: str
    assigned_slots: int


def _slug(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return result or "unnamed"


def rule_semantic_id(tileset_id: str, family: str, role: str, topology: str,
                     source_index: int, slot: str = "") -> str:
    parts = ["semantic", "rule", _slug(family), _slug(role), _slug(tileset_id), _slug(topology)]
    if slot:
        parts.append(_slug(slot))
    parts.append(str(int(source_index)))
    return ".".join(parts)


def _rule_slot_from_id(definition_id: str) -> str | None:
    parts = definition_id.split(".")
    if len(parts) < 3 or parts[-2] not in RULE_SLOTS:
        return None
    try:
        int(parts[-1])
    except ValueError:
        return None
    return parts[-2]


def _edges_for_mask(mask: int) -> dict[str, str]:
    return {
        "north": "masonry" if mask & NORTH else "voidEdge",
        "east": "masonry" if mask & EAST else "voidEdge",
        "south": "masonry" if mask & SOUTH else "voidEdge",
        "west": "masonry" if mask & WEST else "voidEdge",
    }


def _topology_for_slot(slot: str, role: str) -> str:
    # Floor variants do not have wall topology.  Keeping every selected floor
    # tile as ``interior`` makes the same rule usable by Smart Floor while the
    # 3x3 layout remains a convenient visual picker.
    return "interior" if role == "floor" else RULE_SLOT_TOPOLOGY[slot]


class TerrainRuleService:
    """Apply a visual 3x3 rule as one ContentWorkspace command."""

    @staticmethod
    def list_rules(workspace: ContentWorkspace | None, tileset_id: str = "") -> tuple[TerrainRuleSummary, ...]:
        if workspace is None:
            return ()
        grouped: dict[tuple[str, str, str], set[str]] = {}
        for definition in workspace.definitions("tileSemantics"):
            data = definition.data
            definition_id = data.get("id")
            current_tileset = data.get("tilesetId")
            family = data.get("family")
            role = data.get("role")
            if (not isinstance(definition_id, str) or not definition_id.startswith("semantic.rule.")
                    or not isinstance(current_tileset, str) or (tileset_id and current_tileset != tileset_id)
                    or not isinstance(family, str) or not isinstance(role, str)):
                continue
            slot = _rule_slot_from_id(definition_id)
            grouped.setdefault((current_tileset, family, role), set()).add(slot or definition_id)
        return tuple(
            TerrainRuleSummary(current_tileset, family, role, len(slots))
            for (current_tileset, family, role), slots in sorted(grouped.items())
        )

    @staticmethod
    def load_assignments(workspace: ContentWorkspace | None, tileset_id: str,
                         family: str, role: str) -> dict[str, int]:
        if workspace is None:
            return {}
        by_topology: dict[str, list[int]] = {}
        generated: dict[str, int] = {}
        for definition in workspace.definitions("tileSemantics"):
            data = definition.data
            if (data.get("tilesetId") != tileset_id or data.get("family") != family
                    or data.get("role") != role):
                continue
            topology = data.get("topology")
            source_index = data.get("sourceIndex")
            if isinstance(topology, str) and isinstance(source_index, int) and not isinstance(source_index, bool):
                by_topology.setdefault(topology, []).append(source_index)
                if (isinstance(data.get("id"), str) and data["id"].startswith("semantic.rule.")):
                    slot = _rule_slot_from_id(data["id"])
                    if slot:
                        generated[slot] = source_index
        if generated:
            return {slot: generated[slot] for slot in RULE_SLOTS if slot in generated}
        result: dict[str, int] = {}
        cursors: dict[str, int] = {}
        for slot in RULE_SLOTS:
            topology = _topology_for_slot(slot, role)
            candidates = by_topology.get(topology, [])
            if not candidates:
                continue
            cursor = cursors.get(topology, 0)
            result[slot] = candidates[cursor % len(candidates)]
            cursors[topology] = cursor + 1
        return result

    @staticmethod
    def load_weights(workspace: ContentWorkspace | None, tileset_id: str,
                     family: str, role: str) -> dict[str, int]:
        """Load weights for generated slots; legacy semantics default to one."""
        if workspace is None:
            return {}
        result: dict[str, int] = {}
        for definition in workspace.definitions("tileSemantics"):
            data = definition.data
            definition_id = data.get("id")
            if (data.get("tilesetId") != tileset_id or data.get("family") != family
                    or data.get("role") != role or not isinstance(definition_id, str)
                    or not definition_id.startswith("semantic.rule.")):
                continue
            slot = _rule_slot_from_id(definition_id)
            weight = data.get("variantWeight", 1)
            if slot and isinstance(weight, int) and not isinstance(weight, bool):
                result[slot] = max(1, weight)
        return result

    def save_rule(self, workspace: ContentWorkspace, tileset_id: str, family: str, role: str,
                  assignments: Mapping[str, int], previous_family: str | None = None,
                  previous_role: str | None = None, label: str = "Configure Smart Terrain Rule",
                  weights: Mapping[str, int] | None = None) -> None:
        family = family.strip()
        role = role.strip()
        if not family:
            raise ValueError("terrain rule family cannot be empty")
        if role not in {"floor", "wall"}:
            raise ValueError(f"unsupported terrain rule role: {role}")
        tileset = workspace.find("tilesets", tileset_id)
        if tileset is None:
            raise ValueError(f"terrain rule references missing tileset: {tileset_id}")
        columns = int(tileset.data.get("columns", 0))
        rows = int(tileset.data.get("rows", 0))
        normalized = {
            slot: int(source_index)
            for slot, source_index in assignments.items()
            if slot in RULE_SLOT_TOPOLOGY
        }
        if not normalized:
            raise ValueError("assign at least one atlas tile to the terrain rule")
        if any(source_index < 0 or source_index >= columns * rows for source_index in normalized.values()):
            raise ValueError("terrain rule contains a tile outside the tileset atlas")
        normalized_weights = {slot: int((weights or {}).get(slot, 1)) for slot in normalized}
        if any(weight < 1 or weight > 100 for weight in normalized_weights.values()):
            raise ValueError("terrain variation weights must be between 1 and 100")
        slot_by_source: dict[int, str] = {}
        for slot, source_index in normalized.items():
            previous = slot_by_source.setdefault(source_index, slot)
            if previous != slot:
                raise ValueError("the same atlas tile cannot be assigned to two rule slots")

        prefix = f"semantic.rule.{_slug(family)}.{_slug(role)}.{_slug(tileset_id)}."
        old_prefix = None
        if previous_family is not None and previous_role is not None:
            old_prefix = f"semantic.rule.{_slug(previous_family)}.{_slug(previous_role)}.{_slug(tileset_id)}."
        target_exists = any(value.family == family and value.role == role
                            for value in self.list_rules(workspace, tileset_id))
        if target_exists and (old_prefix is None or old_prefix != prefix):
            raise ValueError("a terrain rule with this family and role already exists")
        target_ids = {
            rule_semantic_id(tileset_id, family, role, _topology_for_slot(slot, role), source_index, slot)
            for slot, source_index in normalized.items()
        }
        for definition in workspace.definitions("tileSemantics"):
            data = definition.data
            if (data.get("tilesetId") != tileset_id or data.get("sourceIndex") not in normalized.values()
                    or data.get("id") in target_ids):
                continue
            definition_id = data.get("id")
            if (isinstance(definition_id, str)
                    and (definition_id.startswith(prefix)
                         or (old_prefix is not None and definition_id.startswith(old_prefix)))):
                continue
            raise ValueError("an atlas tile is already classified by another semantic definition")

        def operation() -> None:
            content_file = next((value for value in workspace.files if value.origin == "project"), None)
            if content_file is None:
                raise ValueError("no project content file is available")
            content_file.data["version"] = CONTENT_VERSION
            values = content_file.data.setdefault("tileSemantics", [])
            if not isinstance(values, list):
                raise ValueError("tileSemantics category must be an array")

            desired_ids: set[str] = set()
            for slot, source_index in normalized.items():
                topology = _topology_for_slot(slot, role)
                definition_id = rule_semantic_id(tileset_id, family, role, topology, source_index, slot)
                desired_ids.add(definition_id)
                mask = RULE_SLOT_MASK[slot]
                data: dict[str, JsonValue] = {
                    "id": definition_id,
                    "tilesetId": tileset_id,
                    "sourceIndex": source_index,
                    "family": family,
                    "role": role,
                    "topology": topology,
                    **(_edges_for_mask(mask) if role == "wall" else {
                        "north": "unknown", "east": "unknown",
                        "south": "unknown", "west": "unknown",
                    }),
                    "preferredLayer": "Walls" if role == "wall" else "Ground",
                    "flipXAllowed": False,
                    "visualConfidence": "confirmed",
                    "semanticConfidence": "probable",
                    "gameplayConfidence": "unverified",
                    "variantWeight": normalized_weights[slot],
                }
                existing = next((value for value in values
                                 if isinstance(value, dict) and value.get("id") == definition_id), None)
                if existing is None:
                    values.append(data)
                else:
                    existing.clear()
                    existing.update(data)

            # Only remove definitions generated by this rule editor.  Manually
            # authored semantics remain untouched.
            values[:] = [value for value in values
                         if not (isinstance(value, dict)
                                 and isinstance(value.get("id"), str)
                                 and (value["id"].startswith(prefix)
                                      or (old_prefix is not None and value["id"].startswith(old_prefix)))
                                 and value["id"] not in desired_ids)]
            content_file.dirty = True

        workspace.mutate(label, operation)

    def delete_rule(self, workspace: ContentWorkspace, tileset_id: str, family: str, role: str,
                    project: "WorldProject | None" = None) -> bool:
        prefix = f"semantic.rule.{_slug(family)}.{_slug(role)}.{_slug(tileset_id)}."
        removed = False
        removed_references: set[tuple[str, int]] = set()

        def operation() -> None:
            nonlocal removed
            content_file = next((value for value in workspace.files if value.origin == "project"), None)
            if content_file is None:
                raise ValueError("no project content file is available")
            values = content_file.data.get("tileSemantics")
            if not isinstance(values, list):
                raise ValueError("tileSemantics category must be an array")
            kept = [value for value in values
                    if not (isinstance(value, dict) and isinstance(value.get("id"), str)
                            and value["id"].startswith(prefix))]
            for value in values:
                if (isinstance(value, dict) and isinstance(value.get("id"), str)
                        and value["id"].startswith(prefix)):
                    try:
                        removed_references.add((str(value.get("tilesetId", tileset_id)), int(value.get("sourceIndex", 0))))
                    except (TypeError, ValueError):
                        continue
            removed = len(kept) != len(values)
            if removed:
                values[:] = kept
                content_file.dirty = True

        workspace.mutate("Delete Smart Terrain Rule", operation)
        if removed and project is not None:
            remaining = {(str(value.data.get("tilesetId", "")), int(value.data.get("sourceIndex", 0)))
                         for value in workspace.definitions("tileSemantics")
                         if value.data.get("tilesetId")}
            stale = removed_references - remaining
            for document in project.maps:
                document.remove_tile_references(stale)
        return removed
