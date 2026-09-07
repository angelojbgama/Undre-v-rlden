# External Authored Content — JSON schema v4

This is the external representation of `AuthoredContentPack`. It is strict UTF-8
JSON, identified by `"format": "dungeon-underworld-content"` and
`"version": 4`. The decoder remains compatible with schema versions 1, 2 and 3;
the encoder emits v4. Runtime definitions, C++ and DMAP are not authoring formats.

The canonical top-level field order is:

`format`, `version`, `tilesets`, `projectiles`, `attacks`, `behaviors`, `enemies`,
`items`, `objects`, `pickups`, `npcVisuals`, `npcs`, `dialogues`, `quests`,
`playerProgressions`, `rewardProfiles`, `rewardGrants`, `shops`,
`authoringDescriptors`, `tileSemantics`, `stamps`, `presentationEffects`.

Each category is an array; an omitted category decodes as empty. The twenty
categories are merged by the workspace loader after per-file strict decoding.
Schema v2 added door capabilities, v3 added presentation effects and schema v4
added object activation capabilities. Unknown fields,
unknown enum strings, duplicate object keys, comments, trailing commas and future
versions are errors. Definition IDs are strings. Optional fields may be omitted or
`null`. Variants use an explicit `kind` string. Integer fields are parsed from their
lexemes with exact range checks; fractional values are not accepted where an integer
is required.

The codec preserves UTF-8 and supports JSON escapes, including valid Unicode
surrogate pairs. Source diagnostics include line, column and logical path. The
writer uses two-space indentation, a final newline and stable field order while
preserving authored vector order.

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
an overlay. Presentation effects and object activation are data-driven capabilities,
not authoritative gameplay state. Content Studio and LLM tooling remain future work.
