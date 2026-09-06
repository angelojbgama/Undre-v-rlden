# External Authored Content — JSON schema v1

This is the external representation of `AuthoredContentPack`. It is strict UTF-8
JSON, identified by `"format": "dungeon-underworld-content"` and
`"version": 1`. Runtime definitions, C++ and DMAP are not authoring formats.

The canonical top-level field order is:

`format`, `version`, `tilesets`, `projectiles`, `attacks`, `behaviors`, `enemies`,
`items`, `objects`, `pickups`, `npcVisuals`, `npcs`, `dialogues`, `quests`,
`playerProgressions`, `rewardProfiles`, `rewardGrants`, `shops`,
`authoringDescriptors`, `tileSemantics`, `stamps`.

Each category is an array; an omitted category decodes as empty. In 13A1, the
decoder foundation covers tilesets, behaviors, items, NPC visuals, progressions,
reward profiles, reward grants, shops and authoring descriptors; the remaining
authored categories are staged for 13A2/13A3. Unknown fields,
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
LLM or author JSON -> decoder -> AuthoredContentPack -> ContentValidator
                   -> ContentCompiler -> GameContentRegistry
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

`ContentJsonEncoder`/`ContentJsonDecoder` are reusable by the future Content Studio.
The builtin C++ pack remains the runtime source in 13A; this document defines the
codec boundary, not runtime external-content selection. Workspace loading and
multi-file merge are deferred to 13B.
This slice does not scan workspaces, merge files, replace builtin runtime content or
implement Studio/LLM tooling; those belong to later phases.
