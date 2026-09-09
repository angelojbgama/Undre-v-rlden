# UMAP v4 / DMAP 1.5 — formatos de mapa implementados

`UMAP` is the strict UTF-8 JSON authored source. `DMAP` is the bounded compiled
runtime format; it is not the editor document and it is not a savegame. The authored
pipeline is:

```text
.umap → AuthoredMapSource → MapCompiler → MapData → .dmap → RuntimeWorld
```

The UMAP root is:

```json
{
  "format": "dungeon-underworld-map-source",
  "version": 4
}
```

The strict UMAP codec rejects duplicate keys, unknown fields, wrong types, invalid
enums, missing required fields and destination-range overflows. Its canonical writer
preserves geometry, layers, collision, spawns, enemies, NPCs, objects and contents,
pickups and payloads, links, regions, ordered world rules, encounters including
`rewardGrantId`, and typed placement overrides. `writeAuthoredMapFile` uses a
temporary file followed by replacement so a failed write does not truncate the
previous authored document. Legacy `.dmap` files remain importable by the Map Maker;
the resulting document is saved as `.umap`, while DMAP output is an explicit compile/
export operation.

`UMAP v1`, `UMAP v2` e `UMAP v3` continuam legíveis; o writer atual emite UMAP v4. UMAP v2
adiciona o binding opcional `regions[].environmentEffectId` e a action
`playPresentationEffect`; UMAP v3 adiciona os triggers/conditions de activation de
objects; UMAP v4 adiciona a política de persistência authored por colocação de
WorldObject, cenas map-local (`scenes[]`) e a action `startScene`. A ausência do campo
`persistence` em um objeto legado significa `persistent`. UMAP v1 não pode conter campos
de environment/presentation, UMAP v1/v2 não podem conter activation rules, e UMAP v1/v2/v3
não podem conter `scenes`, `persistence` ou `startScene`. `DMAP` é a serialização compilada/runtime de um mapa,
não o documento authored e não é savegame. O reader produz `MapData`, valida o documento inteiro e somente então
`RuntimeWorldBuilder` cria handles e estado runtime.

Todos os inteiros são little-endian. Strings são bytes com comprimento `u32`, sem
NUL. Nenhuma estrutura C++, ponteiro, `EntityHandle`, animator ou estado transitório
é persistido.

## Header e chunks

| Offset | Tipo | Campo |
|---:|---|---|
| 0 | `char[4]` | magic `DMAP` |
| 4 | `u16` | major = 1 |
| 6 | `u16` | minor = 5 |
| 8 | `u16` | flags = 0 |
| 10 | `u16` | header size = 20 |
| 12 | `u64` | tamanho total declarado |

Major diferente de 1 e minor maior que 5 são rejeitados. DMAP 1.0 continua legível;
DMAP 1.1 adiciona o chunk opcional `NPCS` para placements authored de NPC. DMAP 1.2
adiciona os chunks opcionais `REGN`, `WRLD` e `ENCT` para regiões, ordered world
rules and encounter definitions. Para DMAP 1.0/1.1 esses dados são vazios. A extensão
é necessária porque NPC não pode ser representado corretamente como objeto nem ficar
apenas em memória. Extensões de header podem ser puladas por `header size`; flags
desconhecidas são rejeitadas. Cada chunk usa `tag:char[4] + payloadSize:u64 + payload`.
Os oito chunks v1 originais são obrigatórios e singleton; `NPCS`, `REGN`, `WRLD`,
`ENCT` e `SCNE` são singleton opcionais. `SCNE` requer DMAP 1.5; sua ausência em
qualquer versão legada representa uma lista vazia de cenas.
Singleton conhecido duplicado é erro. Chunk desconhecido é ignorado se seu tamanho for
válido e estiver contido no arquivo.

`stringIndex` é `u32` e aponta para `STRS`; não é ID runtime. O writer reúne as
strings referenciadas, ordena e deduplica, tornando o output determinístico.

## Layout dos payloads

### `META`

```text
mapId stringIndex
widthTiles u32
heightTiles u32
tileSizePixels u16
```

### `STRS`

```text
count u32
repeat count: byteLength u32 + bytes u8[byteLength]
```

Entradas vazias e índices fora da tabela são inválidos.

### `TREF`

```text
count u32
repeat count:
    tilesetDefinitionId stringIndex
    sourceIndex u32
    flags u8                    # bit 0 = flipX
```

DMAP v1.0 já suporta múltiplos tilesets: cada reference usa um `tilesetDefinitionId`
estável. O Map Maker e o game resolvem esse ID pelo catálogo compartilhado; o
`world::TilesetId` numérico é runtime-only e não faz parte do layout binário.

### `LAYR`

```text
layerCount u32
repeat layerCount:
    name stringIndex
    visible u8                 # 0 ou 1
    cellCount u32              # width * height
    cells u32[cellCount]       # TREF index; UINT32_MAX = vazio
```

Nomes são não vazios/únicos e toda referência não vazia deve existir.

### `COLL`

```text
cellCount u32                  # width * height
solid u8[cellCount]            # somente 0 ou 1
```

Collision é independente dos tiles visuais.

### `SPWN`

```text
count u32
repeat count:
    spawnId stringIndex
    x i32
    y i32
    facing u8                  # Down, Up, Left, Right = 0..3
```

`SpawnId` é não vazio e único. Transitions resolvem por ID, sem fallback `(0,0)`.

### `ENTS`

```text
enemyCount u32
repeat enemyCount:
    persistentInstanceId u64
    enemyDefinitionId stringIndex
    x i32, y i32, facing u8

objectCount u32
repeat objectCount:
    persistentInstanceId u64
    objectDefinitionId stringIndex
    x i32, y i32
    persistence u8                 # 0 = persistent, 1 = resetOnMapEnter
    initialStackCount u32
    repeat initialStackCount: itemDefinitionId stringIndex, quantity u32

pickupCount u32
repeat pickupCount:
    persistentInstanceId u64
    pickupDefinitionId stringIndex
    visualDefinitionId stringIndex
    x i32, y i32
    collectionX i32, collectionY i32, collectionWidth i32, collectionHeight i32
    payloadKind u8
    payload:
        0 Health: amount i32
        1 Currency: amount u64
        2 Item: itemDefinitionId stringIndex, quantity u32
```

ID zero é reservado. Enemy, object e pickup compartilham um namespace de IDs por
mapa. Referências a definitions/itens são validadas antes da construção runtime.

### `LINK`

```text
count u32
repeat count:
    linkId stringIndex
    triggerX i32, triggerY i32, triggerWidth i32, triggerHeight i32
    targetMapId stringIndex
    targetSpawnId stringIndex
```

IDs de link são únicos, AABBs têm dimensão positiva e `MapCatalog` valida mapa e
spawn de destino. Link guarda `MapId`, nunca path.

### `NPCS` (DMAP 1.1)

```text
count u32
repeat count:
    persistentInstanceId u64
    npcDefinitionId stringIndex
    x i32, y i32, facing u8
```

O reader aceita arquivos DMAP 1.0 sem `NPCS` como mapas sem NPCs. NPCs usam a mesma
namespace de `PersistentInstanceId` dos demais placements e são validados pelo
`NpcCatalog` antes de `RuntimeWorldBuilder`.

### `REGN`, `WRLD` e `ENCT` (DMAP 1.2/1.3/1.4)

`REGN` contém regiões authored em ordem estável:

```text
count u32
repeat count:
    regionId stringIndex
    bounds (x i32, y i32, width i32, height i32)
    [DMAP 1.3] hasEnvironmentEffect u8
    [DMAP 1.3, when non-zero] environmentEffectId stringIndex
```

`WRLD` contém regras ordered. Cada regra grava seu ID, trigger, `once`, condições e
ações na ordem authored. DMAP 1.4 inclui os triggers `objectActivated`/
`objectDeactivated` e conditions `objectActive`/`objectInactive`; seus targets usam
`PersistentInstanceId`. Targets de definição usam `stringIndex`; targets de
placement usam `PersistentInstanceId` e são distinguidos por um tag de target:

```text
target tag u8: 0 = none, 1 = DefinitionId/stringIndex, 2 = PersistentInstanceId/u64
```

DMAP 1.3 adiciona a action `playPresentationEffect`, cujo target é um
`PresentationEffectDefinition` transient. Kinds e estados são enums bounded e são
rejeitados quando saem do vocabulário atual. DMAP 1.4 mantém esses chunks e adiciona
somente o vocabulário de activation; DMAP 1.5 adiciona a política de persistência
em cada placement de objeto, a action `startScene` e o chunk `SCNE`. O layout de
campos dos chunks permanece versionado e bounded.
`doorState` usa `locked`, `closed` e `open`.

### `SCNE` (DMAP 1.5)

`SCNE` contém a timeline map-local. Cada cena tem um `DefinitionId` único dentro do
mapa, duração em ticks, actor bindings por alias, tracks tipadas e markers. Bindings
de NPC/enemy usam `PersistentInstanceId` da placement concreta; player não possui
instance ID authored. Clips de mundo usam o mesmo `WorldAction` serializado por
`WRLD`, mas `startScene` é rejeitado em `SCNE` para evitar cenas aninhadas.

```text
sceneCount u32
repeat sceneCount:
    sceneId stringIndex
    durationTicks u32
    actorCount u32
    actors: slotId stringIndex, kind u8, hasInstance u8, [instanceId u64]
    trackCount u32
    tracks: kind u8, optional actorSlot, clipCount u32, clips[]
    markerCount u32
    markers: name stringIndex, tick u32
```

The binary clip layout stores all fields needed by the fixed tick evaluator
(kind, actor slot, timing, position/facing/emote/hop data, optional dialogue/effect
IDs, and `WorldAction`). Reader-side bounds checks and `validateMapData` enforce
track/clip compatibility, actor identities, map bounds, world-action targets and
non-overlapping moves before a `RuntimeWorld` is constructed.

`ENCT` contém encounters e o reward opcional:

```text
count u32
repeat count:
    encounterId stringIndex
    participantCount u32
    participant PersistentInstanceId[u32]
    hasReward u8
    rewardGrantId stringIndex when hasReward != 0
```

O compilador valida referências de regiões, regras, portas, participants, rewards e
duplicatas antes de o mapa chegar ao runtime. `RuntimeWorld` mantém a colisão base das
células controladas por uma porta: estados `locked`/`closed` tornam-nas sólidas e
`open` restaura exatamente os valores authored. Portas sobrepostas são inválidas nesta
versão.

### Save relacionado

`DSAV 1.8` é o formato de estado separado do mapa. Seu reader continua aceitando
`DSAV 1.6` e `DSAV 1.7`; saves antigos defaultam regras como não disparadas,
encounters como inativos, portas no estado inicial authored e pressure plates como
estado derivado. O save novo persiste apenas estado
mutável: `mapId + ruleId` para regras `once`, `mapId + encounterId` para state/reward
claim, deltas de portas e activation toggles por `mapId + PersistentInstanceId`.
Pressure activation nunca é persistida. O mapa compilado continua
sendo carregado do DMAP, nunca copiado para o save. Deltas de WorldObject com
`resetOnMapEnter` não entram no `SessionWorldState`; portanto não são restaurados
por transições nem por save/load. O mapa authored continua sendo a autoridade para
recriar seu estado inicial.

O estado de encounter `active` é monitorado sobre placements authored já existentes;
esta fundação não adiciona waves, spawn tables, snapshots de HP ou fases de boss.

## Limites e validação

```text
dimensão por eixo: 4096 tiles     layers: 64
tile references: 65.536           placements: 100.000
strings: 100.000                  string: 4.096 bytes
chunk: 64 MiB                     arquivo: 256 MiB
```

`width * height`, offsets e tamanhos são verificados antes de leitura/alocação. O
reader rejeita truncamento, tamanho declarado incorreto, chunks ausentes, índices
inválidos, dimensões incompatíveis, payloads inválidos e references sem definition.

## Cadeia runtime

```text
MapData -> DMAP 1.5 -> MapCatalog -> deserialize/validate
         -> RuntimeWorldBuilder -> RuntimeMap + factories + WorldPickup/NPC
```

Handles são sempre novos. Map Maker e conteúdo authored versionado produzem o mesmo
`MapData` e chamam o mesmo writer sem dependência de UI no formato.

## Content Studio MAP workflow — Phase 18D

O Content Studio usa a mesma fonte authored UMAP do Map Maker. A perspectiva MAP possui
paletas editor-only para `TILES`, `SEMANTICS`, `STAMPS` e `ENTITIES`, além das
ferramentas de seleção, pencil, erase, rectangle, fill, eyedropper, collision,
region, entity e stamp. A paleta de tiles pode formar uma seleção retangular de várias
células; o brush é expandido em ordem row-major para referências ordinárias de tile e
uma edição composta preserva undo/redo. No Tile Rectangle, um brush multi-tile é
repetido periodicamente somente dentro do retângulo inclusivo solicitado; nenhuma
célula fora dele é alterada e um brush 1x1 preserva o comportamento simples. Flood
fill continua usando a referência de um único tile.

Layers authored podem ser adicionadas, removidas (mantendo ao menos uma), renomeadas e
reordenadas por commands. Visibilidade e lock são estado do editor: visibilidade não
altera semantics/runtime e uma layer bloqueada não aceita pintura. Nenhum desses
estados de ferramenta, palette, brush, lock ou viewport entra no UMAP.

As quatro categorias restantes do Content JSON v5 — `tilesets`,
`authoringDescriptors`, `tileSemantics` e `stamps` — possuem inspectors e mutations
typed. A navegação MAP ↔ CONTENT mantém `DefinitionId` para definições e
`PersistentInstanceId` para placements; abrir uma definição, colocar conteúdo e
encontrar usages são ações de tooling. Tilesets, semantic tiles e stamps podem ser
selecionados de volta na paleta. O painel MAP também expõe a edição estruturada de
regiões/efeitos, world rules e encounters usando os pickers do mapa e do Content
Workspace. Definitions sem `AuthoringDescriptor` continuam aparecendo pelo
`DefinitionId`. UMAP v4 e DMAP 1.5 carregam a política de persistência das colocações
de WorldObject e cenas map-local; Content JSON v5 e DSAV 1.8 permanecem inalterados.
Compile/export/playtest usam o registry atual do workspace quando ele é válido; um
workspace inválido torna a dependência de conteúdo indisponível em vez de usar builtin
ou cache stale silenciosamente. Conteúdo builtin válido é read-only para edição de
JSON, mas pode ser colocado e localizado em um UMAP editável. `FIND IN MAP` percorre
as ocorrências da definição no mapa atual em ordem authored e retorna ao primeiro uso
após o último.

## UWORLD v1 — authored world project

`UWORLD v1` é o formato authored de um projeto de mundo inteiro. É um JSON UTF-8
estrito e canônico com a raiz:

```json
{
  "format": "dungeon-underworld-world-project",
  "version": 1,
  "entryMapId": "map.village",
  "maps": [ /* AuthoredMapSource completos */ ]
}
```

Cada item de `maps` é semanticamente um `AuthoredMapSource` completo. O codec de
mapa existente é reutilizado para codificar/decodificar esses objetos, portanto
`UWORLD` não cria uma segunda representação da geometria, das layers, placements,
regions, world rules, encounters, scenes ou overrides authored. A ordem do array é estável e
é a ordem usada pelo Content Studio, pela validação e pela busca de usos. `entryMapId`
deve referenciar exatamente um dos mapas e os `MapId` devem ser únicos e não vazios.

O writer valida toda a estrutura antes de escrever, rejeita campos desconhecidos e
faz a substituição por arquivo temporário e backup. Estado de editor — viewport,
zoom, seleção, ferramenta, palette, scroll, locks e mapa atualmente ativo — não
pertence ao `UWORLD`.

Os formatos têm responsabilidades distintas:

```text
UWORLD v1  authored project containing multiple AuthoredMapSource values
UMAP v4    standalone authored map source
DMAP 1.5   compiled runtime artifact for exactly one map
DSAV 1.8   mutable session/save state and world deltas
```

A validação global verifica o `entryMapId`, IDs duplicados e cada `MapLink`: o mapa
alvo deve existir e o `targetSpawnId` deve existir no mapa alvo. A compilação de um
projeto só produz `MapData` quando todos esses diagnósticos e a validação individual
dos mapas passam. Exportar um projeto escreve DMAPs individuais, em ordem authored,
com nomes determinísticos derivados do `MapId`; nunca agrupa mapas em um DMAP.

O playtest do Content Studio compila o `UWORLD` atual em memória, inclusive alterações
não salvas. Um `MapCatalog` em memória fornece os `MapData` compilados a um único
`MapSession`, que mantém somente um `RuntimeWorld` ativo e reutiliza a mesma transição
de `MapLink` do jogo.
