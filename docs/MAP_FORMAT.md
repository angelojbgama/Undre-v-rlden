# UMAP v1 / DMAP 1.2 — formatos de mapa implementados

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
  "version": 1
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

`DMAP` é a serialização compilada/runtime de um mapa, não o documento authored e não
é savegame. O reader produz `MapData`, valida o documento inteiro e somente então
`RuntimeWorldBuilder` cria handles e estado runtime.

Todos os inteiros são little-endian. Strings são bytes com comprimento `u32`, sem
NUL. Nenhuma estrutura C++, ponteiro, `EntityHandle`, animator ou estado transitório
é persistido.

## Header e chunks

| Offset | Tipo | Campo |
|---:|---|---|
| 0 | `char[4]` | magic `DMAP` |
| 4 | `u16` | major = 1 |
| 6 | `u16` | minor = 2 |
| 8 | `u16` | flags = 0 |
| 10 | `u16` | header size = 20 |
| 12 | `u64` | tamanho total declarado |

Major diferente de 1 e minor maior que 2 são rejeitados. DMAP 1.0 continua legível;
DMAP 1.1 adiciona o chunk opcional `NPCS` para placements authored de NPC. DMAP 1.2
adiciona os chunks opcionais `REGN`, `WRLD` e `ENCT` para regiões, ordered world
rules and encounter definitions. Para DMAP 1.0/1.1 esses dados são vazios. A extensão
é necessária porque NPC não pode ser representado corretamente como objeto nem ficar
apenas em memória. Extensões de header podem ser puladas por `header size`; flags
desconhecidas são rejeitadas. Cada chunk usa `tag:char[4] + payloadSize:u64 + payload`.
Os oito chunks v1 originais são obrigatórios e singleton; `NPCS`, `REGN`, `WRLD` e
`ENCT` são singleton opcionais.
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

### `REGN`, `WRLD` e `ENCT` (DMAP 1.2)

`REGN` contém regiões authored em ordem estável:

```text
count u32
repeat count: regionId stringIndex, bounds (x i32, y i32, width i32, height i32)
```

`WRLD` contém regras ordered. Cada regra grava seu ID, trigger, `once`, condições e
ações na ordem authored. Targets de definição usam `stringIndex`; targets de
placement usam `PersistentInstanceId` e são distinguidos por um tag de target:

```text
target tag u8: 0 = none, 1 = DefinitionId/stringIndex, 2 = PersistentInstanceId/u64
```

Kinds e estados são enums bounded e são rejeitados quando saem do vocabulário atual.
`doorState` usa `locked`, `closed` e `open`.

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

`DSAV 1.7` é o formato de estado separado do mapa. Seu reader continua aceitando
`DSAV 1.6`; saves antigos defaultam regras como não disparadas, encounters como
inativos e portas no estado inicial authored. O save novo persiste apenas estado
mutável: `mapId + ruleId` para regras `once`, `mapId + encounterId` para state/reward
claim e deltas de portas por `mapId + PersistentInstanceId`. O mapa compilado continua
sendo carregado do DMAP, nunca copiado para o save.

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
MapData -> DMAP v1.1 -> MapCatalog -> deserialize/validate
         -> RuntimeWorldBuilder -> RuntimeMap + factories + WorldPickup/NPC
```

Handles são sempre novos. Map Maker e conteúdo authored versionado produzem o mesmo
`MapData` e chamam o mesmo writer sem dependência de UI no formato.
