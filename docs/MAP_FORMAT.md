# Formato `.dmap` — proposta conceitual

## Objetivos

Formato binário próprio, little-endian, versionado, validável e extensível sem parser externo. Deve carregar rapidamente, ignorar extensões opcionais desconhecidas e migrar dados antigos. Não será um dump de memória C++: padding, ponteiros, enums do compilador e layout de structs nunca vão ao disco.

Escolha inicial: **container binário por chunks**, com uma futura ferramenta própria `dmap_dump` para inspeção textual. Um formato texto parece simples, mas exigiria especificar escaping, números, Unicode e recuperação de erros; o container TLV dá limites claros e preserva evolução. A implementação só começa quando uma fase pedir mapas.

## Primitivas

- inteiros `u8/u16/u32/u64`, `i32/i64` em little-endian;
- `bool` como `u8` 0/1;
- strings UTF-8 como `u32 byteLength + bytes`, sem NUL;
- `MapId`: 128 bits estáveis;
- `LocalId`: `u64` único e nunca reutilizado dentro do mapa;
- posições como inteiros em subpixels ou pixels/tile+offset, sem float obrigatório;
- arrays sempre têm contagem e limites máximos validados.

IDs compostos `(MapId, LocalId)` identificam persistentemente entidades, triggers, doors e regiões. IDs de definição são strings estáveis (`enemy.slime`, `tile.dungeon_wall`) internadas na string table. Caminhos de arquivo e índices de registry não são identidade persistente.

## Header

```text
offset  size  field
0       4     magic = "DMAP"
4       2     formatMajor
6       2     formatMinor
8       2     headerSize
10      2     flags
12      4     endianMarker = 0x01020304
16      8     declaredFileSize
24      8     contentRevision
32      16    mapId
48      4     headerCrc32 (opcional na v1; zero = ausente)
52      ...   extensão do header, pulada via headerSize
```

`major` incompatível é recusado. `minor` adiciona campos/chunks opcionais que leitores podem ignorar. `contentRevision` muda a cada save do editor e ajuda a detectar links/cache desatualizados; não é ID.

## Chunks

Cada chunk é autocontido:

```text
tag[4] | chunkVersion:u16 | flags:u16 | payloadSize:u64 | payload bytes
```

Tags obrigatórias desconhecidas causam erro; opcionais desconhecidas são puladas por `payloadSize` e preservadas pelo editor ao salvar, quando possível. Ordem canônica facilita comparação e testes. Chunks v1 propostos:

| Tag | Conteúdo |
|---|---|
| `META` | nome, dimensão, tile size, background, bounds e propriedades do mapa |
| `STRS` | tabela de strings UTF-8 deduplicadas |
| `TREF` | referências estáveis a tilesets/tiles usadas no mapa |
| `LAYR` | uma layer por chunk: ID, nome, tipo, draw order, visibilidade e células |
| `COLL` | flags de colisão por célula ou formas adicionais |
| `ENTS` | instâncias colocadas e propriedades/overrides |
| `SPWN` | spawn points nomeados |
| `TRIG` | triggers e ações/referências declarativas |
| `LINK` | doors/portais e destino em outro mapa |
| `REGN` | regiões nomeadas para eventos, câmera ou ambiente |
| `EDTR` | estado opcional do editor; ignorado pelo jogo |

Pode haver múltiplos `LAYR`; outras tags são únicas na v1. O reader rejeita chunks duplicados quando a regra não permite.

## Metadata e dimensões

`META` contém:

```text
nameStringId
widthTiles, heightTiles: u32
tileWidthPx, tileHeightPx: u16   # v1 exige 16×16, sem espalhar o valor
worldOriginPx: i32,i32
backgroundColor: RGBA8
defaultSpawnStringId (optional)
propertyBag
```

Limites defensivos impedem overflow/alocação hostil (`width*height`, contagens, payloads). O runtime aceita mapas maiores que 17×14; essa é apenas a viewport lógica.

## Tile references e layers

Uma célula não salva caminho. `TREF` mapeia um índice local compacto para `tilesetDefinitionId`, `sourceTileIndex` (ou stable tile ID) e flags padrão.

Uma layer possui `LayerId:u64`, nome, tipo semântico, draw pass, Y-sort policy, largura/altura e encoding. Tipos iniciais: `ground`, `walls`, `decoration_low`, `objects_visual`, `decoration_high`; nomes são livres e tipos opcionais desconhecidos podem ser preservados.

```text
TileCell:
  tileRefIndex:u32    # 0 = vazio
  flags:u8            # flipX; flipY/rotation/variant reservados
```

Encoding v1: `RAW` para mapas pequenos e `RLE_ROWS` para runs por linha. RLE é simples, determinístico e implementável internamente. Não adicionar compressão geral até medições justificarem. O decoder exige que cada linha produza exatamente `widthTiles` células.

Colisão é separada do tile visual. `COLL` começa como grid de flags (`solid`, direções bloqueadas, hazard e reservados) também em RLE. Assim a arte pode mudar sem mudar física e o editor pode desenhar overlay. AABBs adicionais podem ser acrescentadas numa versão do chunk.

## Entidades

Cada entrada de `ENTS`:

```text
localId:u64
definitionId:stringRef          # ex.: enemy.slime, object.chest
kindHint:u16                    # ajuda editor; definition é autoridade
positionSubpixel:i32,i32
facing:u8
layerId:u64
flags:u32                       # persistent, initiallyDisabled, etc.
propertyBag
```

O mapa guarda referência à definição e apenas overrides. Sprite, stats, loot, diálogo e IA padrão vivem em catálogos externos. Player spawn não é uma entidade Player serializada. Entidades temporárias (flechas, VFX, loot não persistente) nunca entram no `.dmap`.

`propertyBag` é lista ordenada de `(propertyKeyStringId, typeTag, valueLength, value)`. Tipos v1: bool, i64, u64, fixed/int, string, Vec2i, ColorRGBA8, referência local, referência map+local e blob opcional. Chaves desconhecidas são preservadas. O comprimento permite skip. Propriedades essenciais devem ganhar campos estruturados em vez de virar um saco indefinido.

## Spawn, triggers, doors e regiões

```text
SpawnPoint: localId, stableName, position, facing, tags[], propertyBag

MapLink:
  localId
  sourceArea (tile/AABB or entity localId)
  destinationMapId
  destinationSpawnName
  transitionKind
  conditions[]
  propertyBag

Trigger:
  localId, shape, activationPolicy, filters,
  conditions[], actions[], propertyBag
```

Referenciar spawn nomeado evita quebrar portas quando o destino muda de posição. O editor valida links disponíveis; o jogo relata destino ausente sem corromper estado.

Na primeira versão, condições/ações usam um conjunto pequeno de opcodes versionados e declarativos (`require_flag`, `set_flag`, `emit_event`, `transition_map`). Não embutir ponteiro/função C++. Opcode desconhecido torna o trigger indisponível com diagnóstico, sem executar dados arbitrários.

Region é ID/nome + retângulo/conjunto de tiles + tags. v1 pode limitar formas a AABB; polígonos ficam para versão posterior.

## IDs, save e edição

- O editor mantém `nextLocalId:u64` e nunca recicla IDs apagados.
- Copy/paste cria novos IDs e reescreve referências internas do grupo copiado.
- Mover, renomear ou mudar definição preserva o ID da instância.
- Duplicidade, zero reservado e referências pendentes são erros de validação.
- O save game endereça deltas por `(MapId, LocalId)`, não por posição ou ordem.
- “Save As” distingue nova identidade (novo `MapId`) de nova revisão do mesmo mapa.

Geração de `MapId` vem de um serviço de IDs injetado no editor; o core só conhece os 128 bits. Uma implementação Win32 futura pode usar RNG do sistema.

## Versionamento e migração

```text
bytes -> bounds/CRC validation -> DiskDto(version N)
      -> migrate N→N+1→...→current
      -> semantic validation
      -> RuntimeMap / EditorDocument
```

Cada migração é pura, testada com fixtures próprias e nunca sobrescreve automaticamente o original. O editor salva na versão corrente apenas após load/validação, por temporário + replace, mantendo backup. O writer usa ordem canônica.

Política:

- mudança aditiva compatível: minor/chunkVersion;
- mudança de significado/remoção: major + migrador;
- definições renomeadas: aliases/migração explícitos, nunca hash acidental;
- fixtures de todas as versões suportadas e relatório legível;
- chunks/propriedades opcionais desconhecidos preservados para round-trip.

## Validação e erros

Antes de criar o mundo, verificar magic/versão, tamanhos e overflow, UTF-8, dimensões, índices de string/tile, unicidade de IDs, layers/collision compatíveis, referências locais, source tile válido e links. Erros retornam offset, tag e contexto; não usar `assert` para arquivo externo.

Testes futuros mínimos:

- round-trip canônico de mapa vazio e pequeno;
- truncamento em cada campo e payload oversized;
- RLE que produz células a menos/a mais;
- chunk opcional desconhecido preservado;
- IDs duplicados e referências inválidas;
- migração de cada versão suportada;
- bytes golden próprios, sem assets licenciados;
- save delta ainda encontra a instância após mover/reordenar no editor.

## Exemplo conceitual (não é sintaxe de arquivo)

```text
DMAP 1.0 mapId=8f... width=64 height=48 tile=16
LAYR ground:             RLE tile refs
LAYR walls:              RLE tile refs
COLL:                    RLE collision flags
ENTS:
  #104 enemy.slime       pos=(640,384) facing=down
  #205 object.chest      pos=(704,384) persistent loot=loot.small
SPWN:
  #10 "entrance"        pos=(128,192) facing=down
LINK:
  #300 -> mapId=91... spawn="entrance"
TRIG:
  #400 area=(20,12,2,1) on_enter emit_event="region.dungeon_02"
```

Offsets finais, limites e opcodes só serão congelados junto com parser, writer e testes.
