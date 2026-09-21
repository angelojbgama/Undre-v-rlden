# External Authored Content — JSON schema v7

This is the external representation of `AuthoredContentPack`. It is strict UTF-8
JSON, identified by `"format": "dungeon-underworld-content"` and
`"version": 7`. The decoder remains compatible with schema versions 1 through 6;
the encoder emits v7. Runtime definitions, C++ and DMAP are not authoring formats.

The canonical top-level field order is:

`format`, `version`, `tilesets`, `projectiles`, `attacks`, `behaviors`, `enemies`,
`items`, `objects`, `pickups`, `npcVisuals`, `npcs`, `dialogues`, `quests`,
`playerProgressions`, `rewardProfiles`, `rewardGrants`, `shops`, `craftingRecipes`,
`authoringDescriptors`, `tileSemantics`, `stamps`, `presentationEffects`,
`visualImages`, `staticSprites`, `animations`, `enemyVisuals`, `objectVisuals`,
`players`, `playerVisuals`, `uiScreens`.

Each category is an array; an omitted category decodes as empty. The categories are
merged by the workspace loader after per-file strict decoding.
Schema v2 added door capabilities, v3 added presentation effects, schema v4 added
object activation capabilities, schema v5 added visual definitions, schema v6
added `craftingRecipes` and schema v7 added `uiScreens` (the UI Engine screen
definitions of `docs/UI_ENGINE.md`; the decoder resolves binding/action ids
against the runtime registries). `craftingRecipes` present in a file whose
version is below 6 is an error; `uiScreens` present in a file whose version is
below 7 is an error. Unknown fields, unknown enum strings, duplicate object keys,
comments, trailing commas and future versions are errors. Definition IDs are
strings. Optional fields may be omitted or `null`. Variants use an explicit `kind`
string. Integer fields are parsed from their lexemes with exact range checks;
fractional values are not accepted where an integer is required.

The codec preserves UTF-8 and supports JSON escapes, including valid Unicode
surrogate pairs. Source diagnostics include line, column and logical path. The
writer uses two-space indentation, a final newline and stable field order while
preserving authored vector order.

`tileSemantics` aceita o campo opcional positivo `variantWeight` (padrão `1`).
Ele define a frequência relativa de uma variação visual no Smart Terrain;
pesos 8 e 2 produzem aproximadamente 80% de piso liso e 20% rachado. A escolha
é determinística por mapa, coordenada, família, papel e seed, sem estado de RNG
persistente, e o campo é compatível com o schema v5. `variantWeight` é escolha
relativa entre candidates equivalentes — ele não é densidade nem chance de
spawn; controles futuros de densidade/scatter usarão campos próprios.

Convenção de identidade das variações 1 × 1 do Smart Terrain (tooling): o
Content Studio grava variantes de piso como
`semantic.variant.{family}.floor.{tileset}.{sourceIndex}` — identidade estável
derivada de família, papel, tileset e `sourceIndex`, sem depender de slots
3 × 3 nem de posição em lista. Definições `semantic.rule.*` geradas por
versões anteriores continuam válidas e carregam normalmente; ao serem
re-salvas pelo editor atual, migram para os novos IDs preservando tile,
pesos e topologia. O formato em si não muda: IDs são strings opacas para o
decoder/encoder e para o validador C++, que continuam exigindo apenas
referência de tileset válida e `variantWeight > 0`. Padrões multi-tile do
Smart Terrain não introduzem categoria nova: são derivados dos `stamps`
existentes (todas as células apontando para semantics da mesma família).

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

```json
{"id":"recipe.life_potion",
 "inputs":[{"itemId":"item.red_herb","quantity":2},
           {"itemId":"item.empty_bottle","quantity":1}],
 "outputs":[{"itemId":"item.life_potion","quantity":1}]}
```

```json
{"id":"recipe.royal_sword",
 "inputs":[{"itemId":"item.iron_sword","quantity":1},
           {"itemId":"item.rune_stone","quantity":3}],
 "outputs":[{"itemId":"item.royal_sword","quantity":1}],
 "unlockQuestId":"quest.smith.trial"}
```

## Crafting recipes (schema v6)

`craftingRecipes` são authored content first-class e ficam inteiramente fora de
`ItemDefinition`: uma receita apenas referencia itens existentes do `ItemCatalog`.
Regras estruturais validadas pelo `ContentValidator`:

- `id` obrigatório e único (o Content Studio usa o namespace `recipe.*`);
- `inputs`: entre 2 e 4 tipos distintos de item, `quantity > 0`, sem repetir
  `itemId`;
- `outputs`: entre 1 e 4 tipos distintos de item, `quantity > 0`, sem repetir
  `itemId`;
- todo `itemId` precisa existir no conteúdo authored/builtin merged;
- `unlockQuestId` é opcional e, quando presente, precisa referenciar uma quest
  existente: a receita só fica conhecida no runtime quando essa quest alcança o
  status completed — até lá o caderno mostra apenas a silhueta do resultado;
- não há probabilidades nem timers de produção nesta etapa.

O `ContentCompiler` converte cada receita em
`gameplay::CraftingRecipeDefinition` e registra tudo em `CraftingCatalog`
(`GameContentRegistry::craftingRecipes()`). O `CraftingService` executa a
operação de forma transacional: toda a troca é simulada em um contêiner
destacado e o inventário real só é substituído se todos os inputs existirem nas
quantidades pedidas e todos os outputs couberem depois da remoção — consumir
ingredientes pode liberar os slots que recebem os outputs. Falhas são tipadas
(`missingIngredients`, `inventoryFull`, `invalidRecipe`, `recipeLocked`) e nunca
alteram o inventário. Receitas fabricadas ganham um contador no histórico do
jogador, persistido no chunk `CRFT` do DSAV 1.10; o desbloqueio por quest é
derivado do estado persistido de quests e não é salvo separadamente.

```json
{"id":"recipe.iron_sword",
 "inputs":[{"itemId":"item.iron_ore","quantity":2},
           {"itemId":"item.coal","quantity":1},
           {"itemId":"item.wood","quantity":1}],
 "outputs":[{"itemId":"item.iron_sword","quantity":1},
            {"itemId":"item.slag","quantity":2}]}
```

Object visual definitions may optionally set `destroyedAnimationId`. This field is
compatible with schema v5 because it is optional and omitted by older authored
files. It binds a generic destroyed visual state and does not change runtime or
save format versions.

An object with an ID and an `objectVisual` but no gameplay capability is valid
scenery. `interactable`, `container`, `destructible`, `bankAccess`, `door` and
`activation` remain optional capabilities added only when the authored object needs
the corresponding gameplay behavior.

`ContentJsonEncoder`/`ContentJsonDecoder` are shared by the builtin-equivalence
tests, workspace loader, Game, Map Maker and `content_check`. The builtin C++ pack
supplies the playable baseline. At game startup, an authored workspace overlays that
baseline by category and stable ID: a matching authored ID replaces the baseline
definition and a new ID extends it. Presentation effects, object activation and visual
definitions are data-driven capabilities, not authoritative gameplay state. Player
visuals, HUD/font assets, tileset loading and generic impact VFX remain fixed
presentation concerns in this phase. Content Studio 18A provides the typed
workspace-document shell and 18B
adds editor-only spritesheet selection, grid/pan/zoom and AnimationClip/Animator
playback through the same secure asset resolver used by runtime. 18C adds typed
gameplay inspectors and 18D completes typed authoring paths for tilesets, authoring
descriptors, tile semantics and stamps. All twenty-five categories therefore have a
typed authoring route while MAP-local authored data remains in UMAP. Preview state,
palette tabs, layer visibility/lock and multi-tile brush state are not serialized.
Asset import, automatic slicing and complex timeline tooling remain future work.

## Content Studio source ownership

The Content Studio foundation keeps each discovered JSON file as an editable
`ContentFileDocument`. These source files are the authored truth, including their
individual dirty state and ownership of definitions. The merged `AuthoredContentPack`,
provenance index and `GameContentRegistry` are derived from all files through the same
workspace merge, validation and compilation pipeline used by runtime tools. Semantic
cross-reference errors leave the structured document editable and saveable, but make
the compiled registry unavailable for playtest/launch.

Save writes only dirty files, emits canonical v6 JSON and uses an atomic temporary-file
replacement. Opening a mixed v1-v6 workspace does not rewrite untouched legacy files;
only a modified file is upgraded by the encoder. Creating a recipe in an older project
promotes that file to v6 on save. Builtin content is exposed as a
read-only document. Content Studio 18A provides typed authoring for visual images,
static sprites (including optional source rectangles and anchors), animations
(including complete frame/marker data) and flexible enemy visual profiles. Phase 18B
adds editor-only spritesheet selection, grid/pan/zoom and playback; these settings
are not serialized. The preview uses the same secure asset resolver and immutable
animation-clip construction as runtime, while asset import, automatic slicing and
complex timeline tooling remain future work. Explicit
workspace validation continues to run `VisualContentLoader` against the selected
asset roots without changing the authored JSON format.

## Content Studio gameplay authoring

Content Studio 18C adds typed inspectors and document mutations for projectiles,
attacks, behaviors, enemies, items, pickups, objects, NPCs, dialogues, quests,
player progressions, reward profiles, reward grants, shops and presentation
effects. Reference pickers use the authored workspace index in lexical ID order;
broken references remain editable and are never silently replaced. Definition IDs
are stable during editing, so a rename is expressed as create/update references/
delete.

Attack timelines remain fixed-tick gameplay data, dialogue remains a structured
node/page/choice list rather than a graph, reward profiles remain probabilistic,
reward grants remain guaranteed, and shops deliberately have no stock system.
Tilesets, authoring descriptors, tile semantics and stamps are edited through the same
workspace ownership, merge, validation and compiler pipeline. Semantic-invalid
documents remain structurally saveable while compilation is unavailable; map compile
and playtest use only the current valid registry and never silently fall back to a
builtin or stale registry.

## Editor language boundary

Content Studio localization is outside this format. The `pt-BR`/`en-US` preference is
stored in a user settings file; it is never written into Content JSON, definition IDs,
authored display names or dialogue text. Content JSON remains v6 and language-independent.
