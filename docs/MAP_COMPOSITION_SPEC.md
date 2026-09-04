# Map composition specification

This is the Phase 9 Block 1 composition contract for `MapData` and Map Maker.

1. Build visual ground/walls with `MapTileLayer` cells. Each cell persists a
   `MapTileReference { DefinitionId tilesetId, sourceIndex, flags }`; runtime numeric
   tileset IDs are never authored or serialized.
2. Use semantic families and approved stamps before RAW atlas coordinates. A visual role
   is a placement hint only: collision remains the separate `collision` grid.
3. Place collision deliberately after art. The validator may warn about a player spawn
   on a solid cell or a chest without a non-solid cardinal approach, but warnings do not
   make a structurally valid map unloadable.
4. Use layers for draw order only. The current semantic registry suggests `walls` for
   non-floor art, but neither DMAP v1 nor runtime gives that label gameplay meaning.
5. Validate in two passes: structural `validateMapData` protects the DMAP/runtime
   contract; `MapSemanticValidator` provides advisory authoring diagnostics. Do not
   substitute either pass for the other.

Deferred by design: automatic tiling, automatic collision, region persistence,
property-override persistence, MapLogic, DMAP/DSAV 1.1, and LLM map generation.
DMAP and DSAV remain v1.0.
