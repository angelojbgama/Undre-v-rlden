# DSAV V1 — formato de save implementado

`DSAV` é separado de `DMAP`: guarda Player e deltas mutáveis, enquanto tiles,
collision e placements originais continuam vindo dos mapas.

Todos os inteiros são little-endian. O header é `DSAV:char[4]`, major `u16=1`, minor
`u16=1`, flags `u16=0`, headerSize `u16=20` e declaredFileSize `u64`. Cada chunk usa
`tag:char[4] + payloadSize:u64 + payload`. `STRS`, `PLYR` e `DELT` são obrigatórios e
singleton; `FLGS` é singleton opcional em DSAV 1.1; chunks desconhecidos size-bounded são ignorados. Major incompatível,
minor futuro, flags desconhecidas, duplicatas, truncamento e tamanhos inválidos são
rejeitados.

## `STRS`

Tabela ordenada/deduplicada: `count:u32`, seguida de `byteLength:u32 + bytes` por
entrada. As referências seguintes são `stringIndex:u32`.

## `PLYR`

```text
currentMapId stringIndex
positionX i32, positionY i32
facing u8
health i32
gold u64

inventorySlotCount u32        # exatamente 30
repeat slot 0..29:
    present u8
    if present: itemDefinitionId stringIndex, quantity u32

quickSlotCount u32            # exatamente 4
repeat slot 0..3:
    present u8
    if present: itemDefinitionId stringIndex
```

A ordem exata dos slots é preservada. Health deve estar entre zero e maximum,
quantidades devem respeitar `stackLimit`, e bindings podem continuar sem estoque.

## `DELT`

Chave persistente: `PersistentEntityKey = MapId:stringIndex +
PersistentInstanceId:u64`. Nunca contém `EntityHandle`.

```text
objectDeltaCount u32
repeat:
    mapId stringIndex, persistentInstanceId u64
    opened u8, destroyed u8
    remainingStackCount u32
    repeat: itemDefinitionId stringIndex, quantity u32

pickupDeltaCount u32
repeat:
    mapId stringIndex, persistentInstanceId u64
    collected u8, hasRemainingQuantity u8
    if hasRemainingQuantity: remainingQuantity u64
```

## `FLGS` (DSAV 1.1, opcional)

O chunk contém as flags de diálogo persistentes como IDs estáveis, sem estado de
renderer ou referências a `EntityHandle`:

```text
dialogueFlagCount u32
repeat:
    flagId stringIndex
```

Os IDs são únicos e gravados em ordem lexicográfica para manter serialização
determinística. O reader aceita DSAV 1.0 sem `FLGS`; um arquivo 1.0 que contenha esse
chunk é rejeitado, assim como contagens, índices ou payloads fora dos limites.

Chest usa `opened` e conteúdo restante; Crate removida usa `destroyed`; pickup total
usa `collected` e parcial usa `remainingQuantity`. Enemy death não persiste na v1.
Chaves são únicas por tipo, IDs zero/references inexistentes e definitions/stacks
inválidas são rejeitados.

## Sessão, load e escrita atômica

Mutações atualizam `SessionWorldState`. Na saída de uma sala, runtime é comparado ao
`MapData` original e só diferenças são guardadas. Retorno reconstrói o DMAP e aplica
os mesmos deltas. F5 serializa essa estrutura; F9 prepara/valida mapa+deltas antes do
swap e restaura posição/facing exatos e os 30 slots do Player.

Não são salvos `RuntimeMap`, handle, animator, projectile, VFX, `AttackInstance` ou
timers de IA.

Para `savegame.sav`, o writer grava e faz flush em `.sav.tmp`, remove o backup antigo,
renomeia o save anterior para `.sav.bak` e então promove o temporário. Falha tenta
restaurar o backup e remove o temporário. O jogo usa o diretório do executável;
testes usam diretório temporário. Esses arquivos são ignorados pelo Git.
