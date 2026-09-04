# DMAP V1 — formato de mapa implementado

`DMAP` descreve os dados originais de autoria de um mapa. Ele não é savegame. O
reader produz `MapData`, valida o documento inteiro e somente então
`RuntimeWorldBuilder` cria handles e estado runtime.

Todos os inteiros são little-endian. Strings são bytes com comprimento `u32`, sem
NUL. Nenhuma estrutura C++, ponteiro, `EntityHandle`, animator ou estado transitório
é persistido.

## Header e chunks

| Offset | Tipo | Campo |
|---:|---|---|
| 0 | `char[4]` | magic `DMAP` |
| 4 | `u16` | major = 1 |
| 6 | `u16` | minor = 0 |
| 8 | `u16` | flags = 0 |
| 10 | `u16` | header size = 20 |
| 12 | `u64` | tamanho total declarado |

Major diferente de 1 e minor maior que 0 são rejeitados. Extensões de header podem
ser puladas por `header size`; flags desconhecidas são rejeitadas. Cada chunk usa
`tag:char[4] + payloadSize:u64 + payload`. Os oito chunks v1 são obrigatórios e
singleton, nesta ordem canônica no writer: `META`, `STRS`, `TREF`, `LAYR`, `COLL`,
`SPWN`, `ENTS`, `LINK`. Singleton conhecido duplicado é erro. Chunk desconhecido é
ignorado se seu tamanho for válido e estiver contido no arquivo.

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
MapData demo -> DMAP em build/bin/data -> MapCatalog -> deserialize/validate
             -> RuntimeWorldBuilder -> RuntimeMap + factories + WorldPickup
```

Handles são sempre novos. Os DMAPs demo são regeneráveis e não versionados. O futuro
Map Maker poderá produzir o mesmo `MapData` e chamar o mesmo writer sem dependência
de UI no formato.
