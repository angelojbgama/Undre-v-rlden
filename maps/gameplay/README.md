# Official authored gameplay maps

This is the current three-map playable set. The files are DMAP 1.0 resources written
with the official `writeDmap` boundary; the game only reads them at startup.

| MapId | File | Size | Entry spawns | Links | Gameplay |
| --- | --- | --- | --- | --- | --- |
| `map.dungeon.01` | `dungeon_01_entry.dmap` | 24x18 | `entry.start`, `entry.from_02` | east -> Map 02 | Evil Soldier, Chest, Money |
| `map.dungeon.02` | `dungeon_02_gallery.dmap` | 24x18 | `entry.from_01`, `entry.from_03` | west -> Map 01; east -> Map 03 | Skull, Crate, Heart |
| `map.dungeon.03` | `dungeon_03_depths.dmap` | 24x18 | `entry.from_02` | west -> Map 02 | Evil Soldier, Skull, Chest, Crate, Life Potion |

The transition graph is deliberately bidirectional:

```text
map.dungeon.01 <-> map.dungeon.02 <-> map.dungeon.03
```

Destination spawns are placed several tiles inside the destination room, away from
the return trigger. Collision is the explicit DMAP grid; it is not inferred from the
tileset artwork.

The three maps together cover all 72 current `tile.dungeon.*` semantic definitions
and all 8 registered Dungeon stamps. The visual distribution is intentional: Map 01
introduces masonry frames/caps, Map 02 concentrates inset and vertical architectural
strips, and Map 03 presents ledge and toothed structures. Remaining semantic tiles
are arranged as grouped architectural reference niches rather than a raw atlas grid.
The per-map semantic-reference counts are Map 01: 30, Map 02: 30, and Map 03: 22
(the boundary reference is intentionally shared); the aggregate unique coverage is
72/72. Stamp distribution is Map 01: masonry frame, top cap, and small masonry; Map
02: inset and both vertical strips; Map 03: horizontal ledge and horizontal toothed.
The coverage test derives its expected set from `AuthoringSemanticRegistry`, so a
future catalogue change requires an explicit authored-map update.

Current semantic uncertainty remains unchanged: detail/opening families are visual
authoring references with `UNVERIFIED` gameplay confidence. They do not imply doors,
hazards, triggers, stairs, or collision. No PNG-only asset was promoted to gameplay
content by this map set.
