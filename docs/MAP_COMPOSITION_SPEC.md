# Map composition specification

Status: Phase 9 Block 1 — first deterministic room composition foundation implemented.

This document defines the small boundary between authoring intent and concrete `MapData`.
It does not change DMAP/DSAV and it is not a procedural dungeon generator.

## Composition pipeline

```text
manual/procedural/LLM intent (future producers)
        ↓
MapBlueprint / RoomBlueprint (in memory)
        ↓
MapComposer + AuthoringSemanticRegistry
        ↓
MapData
        ↓
validateMapData                 # structural/runtime contract
MapSemanticValidator             # advisory visual authoring diagnostics
ReachabilityValidator             # playability/connectivity diagnostics
        ↓
DMAP v1 / RuntimeWorldBuilder
```

`MapBlueprint` is an intermediate model and is intentionally not persisted in DMAP v1.
The concrete `MapData` remains the authority for the runtime and the existing DMAP
reader/writer remains the only persistence path.

## Implemented first slice

`RoomBlueprint` currently contains:

```text
width, height                 # tile dimensions
openings[]                    # side, offset, width
optional playerSpawn           # SpawnId, TileCoord, facing
```

The supported sides are the explicit semantic values `north`, `east`, `south` and
`west`. An opening offset is measured along its side from the map origin. Both corner
cells are reserved for the boundary, so a legal opening satisfies:

```text
1 <= offset
offset + width <= sideLength - 1
width > 0
```

At most four openings are accepted. Overlapping opening cells are rejected with the
structured diagnostic `opening_overlap`; invalid bounds use `invalid_opening`.

The in-memory `RoomCompositionGrid` contains only authoring intent:

```text
Void | Walkable | Boundary | Opening | Reserved
```

The current rectangular composer marks the perimeter as `Boundary`, replaces declared
perimeter cells with `Opening`, and leaves the interior as `Walkable`. Boundary cells
become solid collision; walkable and opening cells remain non-solid. This relationship
comes from the structural intent, never from PNG transparency or sprite appearance.

## Semantic visual resolution

`MapComposer` consumes `AuthoringSemanticRegistry` and creates `MapTileReference` through
the existing `tileReferenceFor` helper. It never embeds an atlas source index in the
layout algorithm. The default profile chooses deterministically from the first masonry
wall segment with a straight-horizontal topology. A caller may provide an explicit
wall semantic ID, which must resolve to a tile with the `wall` role.

The current catalogue deliberately has no confirmed floor semantic ID. Consequently the
first slice emits a `walls` layer for the proven boundary visual and leaves interior
visual cells empty while still representing the walkable area in the composition grid
and collision data. Adding floor art requires catalog evidence and a later small content
update; the compositor does not invent `floor.*` IDs or raw atlas coordinates.

The official authored maps add a separate, explicit visual-surface pass after the
rectangular composition. It uses the catalogued `tile.dungeon.masonry.39` reference
as a conservative masonry surface because that cell is visibly suitable for
continuous painting and was already used by the runtime's authored predecessor.
This is still visual authoring data, not a new floor role: walkability continues to
come only from the DMAP collision grid. The editor's authored New Map action uses the
same content-aware path, so a new document opens with a visible dataset canvas and
can immediately be painted with the semantic palette or approved stamps.

An optional blueprint spawn is converted to the existing `PlayerSpawn` contract at the
center of its authored tile. It must have a stable `SpawnId` and be in a walkable or
opening cell. Invalid spawns produce `spawn_not_walkable` or `invalid_player_spawn`.

## Reachability/playability

`ReachabilityValidator` is independent of renderer and assets. Its iterative four-way
BFS works only on `MapData.collision` and can return all reachable `TileCoord` cells.
`validateRoomOpenings` maps a `RoomBlueprint`'s openings to target cells and reports
`unreachable_opening` when a valid spawn cannot reach one of them. This is a playability
diagnostic, not structural DMAP validation and not AI pathfinding.

Diagnostics are structured records containing a stable code, human-readable message,
optional tile and optional room side. They are suitable for editor display and future
content producers, including an eventual LLM adapter, without adding that adapter now.

## Deliberately deferred

The first foundation does not implement:

```text
automatic tiling or floor inference
MapLink creation from openings
procedural dungeon generation, BSP, WFC or maze generation
entity/encounter/loot/NPC placement
MapLogic, scripting or regions persistence
Blueprint persistence, DMAP 1.1 or DSAV 1.1
LLM integration
```

Approved stamps remain an editor authoring feature. The composition slice deliberately
does not reinterpret an opening as a transition or infer gameplay from an asset role.

# Official authored reference maps

The first production content set produced from the authoring/composition direction is
stored as three ordinary DMAP 1.0 files under `maps/gameplay/`. The runtime does not
serialize or regenerate a blueprint at startup: it reads the authored DMAP, validates
it, registers all three resources in `MapCatalog`, and builds the normal `RuntimeWorld`.

The maps are also a semantic reference set. Their aggregate coverage is checked
against the live `AuthoringSemanticRegistry` (72 Dungeon tile definitions and 8
stamps), while collision remains an explicit authored grid. Visual `PROBABLE` or
`UNVERIFIED` meanings do not become gameplay mechanics.
