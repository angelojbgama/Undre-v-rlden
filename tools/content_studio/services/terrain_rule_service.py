"""Authoring helpers for visual Smart Terrain rule assignment.

The rule editor is intentionally a view over the existing ``tileSemantics``
definitions.  It does not introduce a parallel SmartTile database or a new
content format.
"""

from __future__ import annotations

import re
from collections.abc import Mapping

from ..model.content_workspace import ContentWorkspace
from ..model.types import JsonValue


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

# The 3x3 editor presents candidate positions while the existing resolver
# selects by its authored topology categories.  Repeated topology values are
# therefore variants, chosen deterministically by AutoTileResolver.
RULE_SLOT_TOPOLOGY = {
    "north_west": "outerCorner", "north": "straightHorizontal", "north_east": "outerCorner",
    "west": "straightVertical", "center": "interior", "east": "straightVertical",
    "south_west": "outerCorner", "south": "straightHorizontal", "south_east": "outerCorner",
}


def _slug(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return result or "unnamed"


def rule_semantic_id(tileset_id: str, family: str, role: str, topology: str, source_index: int) -> str:
    return ".".join((
        "semantic", "rule", _slug(family), _slug(role), _slug(tileset_id),
        _slug(topology), str(int(source_index)),
    ))


class TerrainRuleService:
    """Apply a visual 3x3 rule as one ContentWorkspace command."""

    @staticmethod
    def load_assignments(workspace: ContentWorkspace | None, tileset_id: str,
                         family: str, role: str) -> dict[str, int]:
        if workspace is None:
            return {}
        by_topology: dict[str, list[int]] = {}
        for definition in workspace.definitions("tileSemantics"):
            data = definition.data
            if (data.get("tilesetId") != tileset_id or data.get("family") != family
                    or data.get("role") != role):
                continue
            topology = data.get("topology")
            source_index = data.get("sourceIndex")
            if isinstance(topology, str) and isinstance(source_index, int) and not isinstance(source_index, bool):
                by_topology.setdefault(topology, []).append(source_index)
        result: dict[str, int] = {}
        cursors: dict[str, int] = {}
        for slot in RULE_SLOTS:
            topology = RULE_SLOT_TOPOLOGY[slot]
            candidates = by_topology.get(topology, [])
            if not candidates:
                continue
            cursor = cursors.get(topology, 0)
            result[slot] = candidates[cursor % len(candidates)]
            cursors[topology] = cursor + 1
        return result

    def save_rule(self, workspace: ContentWorkspace, tileset_id: str, family: str, role: str,
                  assignments: Mapping[str, int]) -> None:
        family = family.strip()
        role = role.strip()
        if not family:
            raise ValueError("terrain rule family cannot be empty")
        if role not in {"floor", "wall"}:
            raise ValueError(f"unsupported terrain rule role: {role}")
        normalized = {
            slot: int(source_index)
            for slot, source_index in assignments.items()
            if slot in RULE_SLOT_TOPOLOGY
        }
        if not normalized:
            raise ValueError("assign at least one atlas tile to the terrain rule")
        topology_by_source: dict[int, str] = {}
        for slot, source_index in normalized.items():
            topology = RULE_SLOT_TOPOLOGY[slot]
            previous = topology_by_source.setdefault(source_index, topology)
            if previous != topology:
                raise ValueError("the same atlas tile cannot represent two different rule topologies")

        prefix = f"semantic.rule.{_slug(family)}.{_slug(role)}.{_slug(tileset_id)}."

        def operation() -> None:
            content_file = next((value for value in workspace.files if value.origin == "project"), None)
            if content_file is None:
                raise ValueError("no project content file is available")
            values = content_file.data.setdefault("tileSemantics", [])
            if not isinstance(values, list):
                raise ValueError("tileSemantics category must be an array")

            desired_ids: set[str] = set()
            for source_index, topology in sorted(topology_by_source.items(), key=lambda value: (value[1], value[0])):
                definition_id = rule_semantic_id(tileset_id, family, role, topology, source_index)
                desired_ids.add(definition_id)
                data: dict[str, JsonValue] = {
                    "id": definition_id,
                    "tilesetId": tileset_id,
                    "sourceIndex": source_index,
                    "family": family,
                    "role": role,
                    "topology": topology,
                    "north": "unknown", "east": "unknown",
                    "south": "unknown", "west": "unknown",
                    "preferredLayer": "Walls" if role == "wall" else "Ground",
                    "flipXAllowed": False,
                    "visualConfidence": "confirmed",
                    "semanticConfidence": "probable",
                    "gameplayConfidence": "unverified",
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
                                 and value["id"].startswith(prefix)
                                 and value["id"] not in desired_ids)]
            content_file.dirty = True

        workspace.mutate("Configure Smart Terrain Rule", operation)
