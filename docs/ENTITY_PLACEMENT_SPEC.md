# Entity placement specification

Phase 9 authors the existing DMAP v1 placements. Phase 10A extends DMAP with the
versioned optional `NPCS` chunk (minor 1); it does not introduce a generic object model.

| placement | stable identity | required authored data | constraints |
|---|---|---|---|
| Player spawn | unique `SpawnId` | position, facing | link targets resolve this ID |
| Enemy | non-zero `PersistentInstanceId` | registered enemy `DefinitionId`, position, facing | IDs share the map placement namespace |
| NPC | non-zero `PersistentInstanceId` | registered NPC `DefinitionId`, position, facing | DMAP 1.1 `NPCS`; runtime handle is separate |
| Object | non-zero `PersistentInstanceId` | registered object `DefinitionId`, position, initial contents | Chest approach is advisory semantic validation |
| Pickup | non-zero `PersistentInstanceId` | registered pickup/visual IDs, position, collection bounds, payload | IDs share the map placement namespace |
| Map link | unique string ID | positive trigger AABB, target `MapId`, target `SpawnId` | target resolution is validated by `MapCatalog` |

Coordinates are world pixels. Entity positions are not inferred from sprite dimensions;
definitions own their runtime collision, interaction, and visual policy. Map Maker may
hold experimental regions and property overrides in its document, but they are not
persisted by DMAP until a separate versioned contract is approved.
