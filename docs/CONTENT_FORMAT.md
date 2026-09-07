# External Authored Content — JSON schema v5

This is the external representation of `AuthoredContentPack`. It is strict UTF-8
JSON, identified by `"format": "dungeon-underworld-content"` and
`"version": 5`. The decoder remains compatible with schema versions 1 through 4;
the encoder emits v5. Runtime definitions, C++ and DMAP are not authoring formats.

The canonical top-level field order is:

`format`, `version`, `tilesets`, `projectiles`, `attacks`, `behaviors`, `enemies`,
`items`, `objects`, `pickups`, `npcVisuals`, `npcs`, `dialogues`, `quests`,
`playerProgressions`, `rewardProfiles`, `rewardGrants`, `shops`,
`authoringDescriptors`, `tileSemantics`, `stamps`, `presentationEffects`,
`visualImages`, `staticSprites`, `animations`, `enemyVisuals`, `objectVisuals`.

Each category is an array; an omitted category decodes as empty. The twenty-five
categories are merged by the workspace loader after per-file strict decoding.
Schema v2 added door capabilities, v3 added presentation effects, schema v4 added
object activation capabilities and schema v5 added visual definitions. Unknown fields,
unknown enum strings, duplicate object keys, comments, trailing commas and future
versions are errors. Definition IDs are strings. Optional fields may be omitted or
`null`. Variants use an explicit `kind` string. Integer fields are parsed from their
lexemes with exact range checks; fractional values are not accepted where an integer
is required.

The codec preserves UTF-8 and supports JSON escapes, including valid Unicode
surrogate pairs. Source diagnostics include line, column and logical path. The
writer uses two-space indentation, a final newline and stable field order while
preserving authored vector order.

Visual definitions are flexible. A visible creature requires only one `idle`
directional binding; every binding may be non-directional (`default`), partial
directional (`down`, `up`, `side`) or fully directional. `move`, `hurt`, `death`,
`dead` and zero-or-more arbitrary `actions` mappings are optional. At runtime a
requested direction resolves to its exact binding, then the authored default, then
the first available binding in deterministic down/up/side order. Missing optional
states or actions fall back to idle and never disable gameplay. NPCs may continue to
use their explicit marker-color fallback when no idle sprite binding is authored.

Visual image paths are normalized relative paths rooted explicitly at either
`gameAssets` or `contentWorkspace`; they cannot traverse outside that root. Content
validation checks path syntax and authored geometry, while actual file resolution,
image decoding and frame bounds are checked by the presentation `VisualContentLoader`.

The pipeline is:

```text
author JSON -> strict decoder -> workspace merge -> AuthoredContentPack
            -> ContentValidator -> ContentCompiler -> GameContentRegistry
```

Examples:

```json
{"id":"item.training_armor","category":"equipment","stackLimit":1,
 "equipment":{"slot":"armor","modifiers":{"maximumHealthBonus":2,
 "playerAttackDamageBonus":0}}}
```

```json
{"id":"dialogue.merchant.greeting","choices":[{"label":"Trade",
 "targetNodeId":"merchant.trade","actions":[{"kind":"openShop",
 "targetId":"shop.development.general"}]}]}
```

```json
{"id":"reward.quest.scholar.path","experience":40,"gold":25,
 "items":[{"itemId":"item.life_potion","quantity":2}]}
```

```json
{"id":"shop.development.general","offers":[{"itemId":"item.life_potion",
 "playerBuyPrice":25,"playerSellPrice":10}]}
```

`ContentJsonEncoder`/`ContentJsonDecoder` are shared by the builtin-equivalence
tests, workspace loader, Game, Map Maker and `content_check`. The builtin C++ pack
remains the transitional default source; an explicit workspace replaces it without
an overlay. Presentation effects, object activation and visual definitions are
data-driven capabilities, not authoritative gameplay state. Player visuals, HUD/font
assets, tileset loading and generic impact VFX remain fixed presentation concerns in
this phase. Content Studio 18A provides the typed workspace-document shell and 18B
adds editor-only spritesheet selection, grid/pan/zoom and AnimationClip/Animator
playback through the same secure asset resolver used by runtime. Preview state is not
serialized. Asset import, automatic slicing, complex timelines and gameplay
definition inspectors remain future work.

## Content Studio source ownership

The Content Studio foundation keeps each discovered JSON file as an editable
`ContentFileDocument`. These source files are the authored truth, including their
individual dirty state and ownership of definitions. The merged `AuthoredContentPack`,
provenance index and `GameContentRegistry` are derived from all files through the same
workspace merge, validation and compilation pipeline used by runtime tools. Semantic
cross-reference errors leave the structured document editable and saveable, but make
the compiled registry unavailable for playtest/launch.

Save writes only dirty files, emits canonical v5 JSON and uses an atomic temporary-file
replacement. Opening a mixed v1-v5 workspace does not rewrite untouched legacy files;
only a modified file is upgraded by the encoder. Builtin content is exposed as a
read-only document. Content Studio 18A provides typed authoring for visual images,
static sprites (including optional source rectangles and anchors), animations
(including complete frame/marker data) and flexible enemy visual profiles. Phase 18B
adds editor-only spritesheet selection, grid/pan/zoom and playback; these settings
are not serialized. The preview uses the same secure asset resolver and immutable
animation-clip construction as runtime, while asset import, automatic slicing,
complex timelines and gameplay definition inspectors remain future work. Explicit
workspace validation continues to run `VisualContentLoader` against the selected
asset roots without changing the authored JSON format.
