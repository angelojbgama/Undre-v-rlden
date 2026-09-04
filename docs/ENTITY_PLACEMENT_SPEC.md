# Entity placement specification

Phase 9 Block 1 authors the existing DMAP v1 placements; it does not introduce a new
entity format or a generic object model.

| placement | stable identity | required authored data | constraints |
|---|---|---|---|
| Player spawn | unique `SpawnId` | position, facing | link targets resolve this ID |
| Enemy | non-zero `PersistentInstanceId` | registered enemy `DefinitionId`, position, facing | IDs share the map placement namespace |
| Object | non-zero `PersistentInstanceId` | registered object `DefinitionId`, position, initial contents | Chest approach is advisory semantic validation |
| Pickup | non-zero `PersistentInstanceId` | registered pickup/visual IDs, position, collection bounds, payload | IDs share the map placement namespace |
| Map link | unique string ID | positive trigger AABB, target `MapId`, target `SpawnId` | target resolution is validated by `MapCatalog` |

Coordinates are world pixels. Entity positions are not inferred from sprite dimensions;
definitions own their runtime collision, interaction, and visual policy. Map Maker may
hold experimental regions and property overrides in its document, but they are not in
DMAP v1 and must not be represented as persistent map content until a versioned format
change is explicitly approved.
