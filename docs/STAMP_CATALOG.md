# Dungeon stamp catalogue

Status: visual authoring definitions for Phase 9 Block 1. A stamp is one undoable editor
command and expands to persistent `MapTileReference` cells; it adds no new DMAP data.
Every listed stamp is atomic in the editor and may be placed only completely inside an
unlocked tile layer.

| ID | dimensions | members from atlas | confidence | interpretation |
|---|---:|---|---|---|
| `stamp.dungeon.masonry_frame_3x3` | 3x3 | (2..4,2), (2,3), (4,3), (2..4,4) | CONFIRMED visual | frame; center is intentionally empty |
| `stamp.dungeon.inset_2x2` | 2x2 | (9..10,3..4) | CONFIRMED visual | inset only; not a transition or trigger |
| `stamp.dungeon.vertical_strip_left_1x3` | 1x3 | (1,6..8) | CONFIRMED visual | continuous left strip |
| `stamp.dungeon.vertical_strip_right_1x4` | 1x4 | (7,5..8) | CONFIRMED visual | continuous right strip |
| `stamp.dungeon.small_masonry_2x2` | 2x2 | (4..5,6..7) | CONFIRMED visual | compact masonry unit |
| `stamp.dungeon.horizontal_ledge_3x1` | 3x1 | (3..5,9) | CONFIRMED visual | ledge art; no hazard inferred |
| `stamp.dungeon.horizontal_toothed_3x1` | 3x1 | (3..5,11) | CONFIRMED visual | toothed art; no hazard inferred |
| `stamp.dungeon.top_cap_3x1` | 3x1 | (4..6,0) | CONFIRMED visual | top-cap art; no spike behavior inferred |

The semantic validator reports `broken_atomic_stamp` only when a complete in-bounds
stamp candidate has exactly one missing member. It does not prohibit an author from
painting individual component tiles, does not repair maps, and never changes structural
`MapData` validity.
