# Dungeon tile catalogue

Status: Phase 9 Block 1 semantic-authoring foundation. This is an authoring aid, not
an implicit collision, autotile, or gameplay specification. `tileset.dungeon` is the
only current map tileset; its 19-column source index is `y * 19 + x`.

The atlas inspection and the registry both contain **72 visible cells**. `CONFIRMED`
means the cell and its visual grouping are visible; a role or topology marked
`PROBABLE` is deliberately not a gameplay assertion. All unspecified edges are
`unknown`; only equal known edge profiles are currently compatible.

| Coordinates | Semantic IDs / family | role / topology | confidence |
|---|---|---|---|
| (4..6,0) | `top_cap.left`, `.center`, `.right` / masonry | wall; cap, horizontal, cap | PROBABLE |
| (14,1) | `detail.01` / detail | detail / architectural detail | UNVERIFIED |
| (0..1,2), (5..8,2), (10..14,2) | `masonry.00..14` (with unused source gaps preserved) / masonry | wall; outer corner or horizontal | PROBABLE |
| (2..4,2), (2,3), (4,3), (2..4,4) | `frame.nw,n,ne,w,e,sw,s,se` / masonry | corner or wall / frame topology | PROBABLE; use the 3x3 stamp |
| (0..1,3), (5..8,3), (12..14,3), (0..1,4), (7..8,4), (11..18,4), (11..14,5) | remaining `masonry.*` / masonry | wall / interior, horizontal, vertical, or corner | PROBABLE |
| (9..10,3), (9..10,4) | `inset.nw,ne,sw,se` / architectural_detail | opening / outer corner | UNVERIFIED; use the 2x2 stamp |
| (7,5..8), (1,6..8) | `strip_right.*`, `strip_left.*` / architectural_detail | wall / cap or vertical | PROBABLE; use matching strip stamps |
| (4..5,6..7) | `small_frame.nw,ne,sw,se` / masonry | corner / outer corner | PROBABLE; use 2x2 stamp |
| (14,6), (2,7), (6,7) | `detail.02..04` / detail | detail / architectural detail | UNVERIFIED |
| (3..5,9) | `ledge.left,center,right` / ledge | ledge / horizontal | PROBABLE; use 3x1 stamp |
| (3..5,11) | `toothed.left,center,right` / ledge | ledge / horizontal | PROBABLE; use 3x1 stamp |

The compact ranges above map one-to-one to the 72 `TileSemanticDefinition` entries in
`AuthoringSemanticRegistry`; source gaps are transparent and are intentionally absent.
The semantic palette exposes only catalogued tiles by family. RAW mode remains available
for deliberate investigation and receives an informational `unclassified_tile` result.

## Exact source manifest

Each `x:name` below is a visible source cell. This is the complete 72-cell manifest;
the prefix of every name is `tile.dungeon.`.

```text
y0:  4:top_cap.left 5:top_cap.center 6:top_cap.right
y1:  14:detail.01
y2:  0:masonry.00 1:masonry.01 2:frame.nw 3:frame.n 4:frame.ne 5:masonry.05 6:masonry.06 7:masonry.07 8:masonry.08 10:masonry.10 11:masonry.11 12:masonry.12 13:masonry.13 14:masonry.14
y3:  0:masonry.15 1:masonry.16 2:frame.w 4:frame.e 5:masonry.20 6:masonry.21 7:masonry.22 8:masonry.23 9:inset.nw 10:inset.ne 12:masonry.26 13:masonry.27 14:masonry.28
y4:  0:masonry.29 1:masonry.30 2:frame.sw 3:frame.s 4:frame.se 7:masonry.35 8:masonry.36 9:inset.sw 10:inset.se 11:masonry.39 12:masonry.40 13:masonry.41 14:masonry.42 15:masonry.43 16:masonry.44 17:masonry.45 18:masonry.46
y5:  7:strip_right.top 11:masonry.48 12:masonry.49 13:masonry.50 14:masonry.51
y6:  1:strip_left.top 4:small_frame.nw 5:small_frame.ne 7:strip_right.mid_a 14:detail.02
y7:  1:strip_left.mid 2:detail.03 4:small_frame.sw 5:small_frame.se 6:detail.04 7:strip_right.mid_b
y8:  1:strip_left.bottom 7:strip_right.bottom
y9:  3:ledge.left 4:ledge.center 5:ledge.right
y10: empty
y11: 3:toothed.left 4:toothed.center 5:toothed.right
```

Authoring rules:

- Do not infer solid collision from a wall-looking cell; paint collision separately.
- Do not FlipX unless the individual definition explicitly approves it. No current
  Dungeon semantic tile approves it.
- Prefer an approved stamp for a visually continuous structure. A stamp warning is
  advisory and is emitted only for an otherwise in-bounds pattern with one missing cell.
- `unknown` edges never create a warning. The edge-profile foundation is intentionally
  conservative until tile-to-tile evidence is validated in the editor.
