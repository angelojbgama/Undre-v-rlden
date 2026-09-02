# Arquitetura proposta

## Decisões centrais

- C++20 inicialmente; Standard Library + Windows SDK, sem dependências de terceiros.
- Simulação 2D em timestep fixo de 60 Hz, independente de input, render e apresentação.
- Framebuffer lógico canônico de 272×224 RGBA8; Win32 só cria a janela, mede tempo, recebe eventos, decodifica imagens e apresenta pixels.
- Renderer inteiramente em software. A janela usa integer scaling, nearest-neighbor e letterbox.
- Composição simples orientada a dados: handles de runtime + componentes pequenos + sistemas explícitos. Sem hierarquia profunda, ECS externo ou `GameObject` universal.
- Definições imutáveis (`PlayerDefinition`, `EnemyDefinition`, `ObjectDefinition`, clips) separadas de instâncias mutáveis.
- Mapas e saves usam IDs estáveis; handles/índices de memória nunca são persistidos.
- O editor compartilha renderer, assets, mapa e serialização, mas seu modelo de documento/undo não entra no runtime.
- Não criar `net/` vazio. A fronteira `PlayerCommand → Simulation` e ticks numerados são preparados agora; rede só aparece quando houver um caso real.

## Fluxo principal

```text
Win32 events -> InputState -> CommandBuilder -> PlayerCommand(tick, player)
                                               |
                                               v
                                    fixed-step Simulation
                                    World + event stream
                                               |
                        +----------------------+------------------+
                        v                                         v
                 RenderSnapshot                         Save/Quest observers
                        |
              camera + render queue
                        |
        software Renderer2D -> RGBA framebuffer
                        |
             Win32 presenter -> client area
```

Input não chama `Player::move`. Renderer não muda o mundo. Animação pode emitir marcadores de frame, mas sistemas de gameplay interpretam esses marcadores; o renderer apenas desenha o frame selecionado.

## Módulos e dependências

| Módulo | Responsabilidade | Pode depender de | Não pode depender de |
|---|---|---|---|
| `core` | tipos inteiros, vetores/retângulos, IDs, resultado/erro, log, utilidades | STL | Win32, gameplay, renderer |
| `platform` | contratos de janela, eventos, relógio, arquivos, imagem e apresentação | `core` | gameplay/world |
| `platform/win32` | `HWND`, message pump, QPC, WIC, DIB/GDI, DPI | `platform`, Windows SDK | gameplay |
| `render` | framebuffer, blend, recorte, imagens, sprites, fonte, câmera, fila de desenho | `core` | Win32, regras de combate |
| `assets` | IDs, catálogo, cache, definições e validação de metadados | `core`, interfaces de I/O, `render` data | HWND/WIC concretos, estado do mundo |
| `simulation` | ticks, comandos, entity registry, componentes, eventos | `core` | janela, input físico, apresentação |
| `world` | mapa runtime, tile layers, queries espaciais, colisão, transições | `core`, `simulation` | Win32, editor |
| `gameplay` | player, inimigos, combate, itens, inventário e regras | `simulation`, `world`, asset IDs | APIs de plataforma/render direto |
| `serialization` | readers/writers, `.dmap`, saves, migrações | `core`, DTOs de mapa | structs ABI/runtime crus |
| `game` | composição das dependências e estados/telas do jogo | módulos anteriores | detalhes Win32 fora do bootstrap |
| `editor` | documento, ferramentas, UI própria, undo/redo, preview | core/render/assets/world/serialization | internals Win32, estado global do jogo |

Regra estrutural: dependências apontam para dentro (`core`) e para contratos, nunca do engine para `game` ou `editor`. Headers públicos de plataforma não incluem `windows.h`; os tipos nativos ficam no `.cpp` ou em implementação privada.

## Loop e tempo

O relógio Win32 usa `QueryPerformanceCounter`/`QueryPerformanceFrequency`. O jogo acumula tempo real em segundos de alta precisão:

```text
pollEvents()
frameDelta = clamp(clock.now - previous, 0, maxFrameDelta)
accumulator += frameDelta
sample physical input and append edge events

ticksThisFrame = 0
while accumulator >= fixedDt and ticksThisFrame < maxCatchUpTicks:
    commands = commandBuilder.commandsFor(nextTick)
    simulation.tick(commands)             // fixedDt = 1/60
    accumulator -= fixedDt
    ++nextTick

alpha = accumulator / fixedDt
snapshot = simulation.renderSnapshot(alpha)
render(snapshot, framebuffer)
present(framebuffer)
```

`Sleep` não dirige a simulação. Pode ser acrescentado apenas como economia de CPU, combinado com medição final/spin curto, sem alterar o acumulador. Um limite de delta e de catch-up evita “spiral of death”; overflow deve ser logado. Pausa de gameplay interrompe ticks do mundo, não o message pump.

Para futuro servidor autoritativo, todo comando carrega `tick`, `playerId`, sequência e payload (`MoveIntent`, `AttackPressed`, `InteractPressed`, etc.). O single player usa exatamente o mesmo caminho. Não se promete determinismo bit a bit entre máquinas: velocidades/posições podem usar inteiros em subpixels para estabilidade, mas multiplayer autoritativo poderá enviar snapshots e correções.

## Coordenadas e configuração

Tipos distintos ou wrappers impedem conversões acidentais:

- `WindowPx`: área cliente física e mouse do SO;
- `LogicalPx`: framebuffer 272×224;
- `WorldPx`/`WorldSubpixel`: posição no mundo;
- `TileCoord`: índice inteiro de tile;
- `ViewportPx`: área lógica usada pelo editor.

`GameMetrics` centraliza `tileSize=16`, `logicalWidth=272`, `logicalHeight=224`, `tickRate=60` e conversões. Não espalhar constantes. A câmera guarda posição em mundo; o raster arredonda uma única vez ao converter world→logical para evitar shimmer. No editor:

```text
WindowPx -> remove letterbox / divide integer scale -> ViewportPx
ViewportPx + camera origin -> WorldPx -> floorDiv(tileSize) -> TileCoord
```

## Renderer em software

`Framebuffer` possui largura, altura, stride e bytes RGBA8 straight-alpha. Operações iniciais: `clear`, `setPixel`, `fillRect`, `drawImage`, `drawImageRegion`, `drawImageRegionFlipX`, `drawTile`, `drawSprite`; depois linhas e outlines. Toda operação recorta no destino e valida source rectangles.

Blend straight-alpha por canal, com aritmética inteira e arredondamento definido. Mesmo que os assets atuais tenham alpha binário, o contrato suporta 0–255. Imagem fonte, framebuffer e packing devem ter especificação única; testes com vermelho/azul detectam troca RGBA/BGRA.

O apresentador Win32 converte o pequeno buffer RGBA canônico para o layout esperado por uma DIB BGRA/BGRX, se necessário, e chama `StretchDIBits` em um retângulo inteiro centralizado. Para client size `W×H`, `scale=max(1,min(W/272,H/224))`; a janela terá tamanho mínimo lógico. Margens são limpas (letterbox), e `COLORONCOLOR`/cópia de pixels evita interpolação. DPI awareness e client size, não tamanho externo da janela, guiam o cálculo. A documentação oficial do Windows confirma tanto a apresentação de DIBs por [`StretchDIBits`](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-stretchdibits) quanto o uso de [`QueryPerformanceCounter`](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps) para timestamps de alta resolução.

Render order vira uma fila de comandos com chave `(pass, layer, sortY, tieBreaker)`:

1. ground/background;
2. decoration low;
3. entidades e objetos com Y-sort pelo baseline/anchor dos pés;
4. foreground/occluders;
5. VFX conforme o pass configurado;
6. HUD e UI em screen space.

O `tieBreaker` estável evita flicker quando dois objetos têm o mesmo Y.

## PNG e assets

Primeira versão: WIC somente em `platform/win32`. A implementação cria decoder, lê o primeiro frame, converte explicitamente para `GUID_WICPixelFormat32bppRGBA` e devolve apenas:

```text
ImageData { width, height, stride, pixelsRGBA }
```

Nenhum header de WIC cruza o contrato. A sequência de factory/decoder/frame é documentada pela [Windows Imaging Component](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-decoder-howto-createusingfilename). Mais tarde um decoder PNG próprio pode implementar o mesmo `IImageDecoder` sem tocar renderer/gameplay. Decodificar PNG diretamente em `StretchDIBits` não serve: misturaria carregamento, apresentação e suporte dependente do driver.

`AssetManager` resolve um `AssetId` estável por catálogo, carrega sob demanda, mantém ownership e oferece handles que detectam geração inválida. Caminhos são dados do catálogo, nunca IDs persistentes. Metadados descrevem sheets, clips, anchors, draw offsets e frame events; inferência automática feita nesta auditoria não será lógica de runtime.

```text
SpriteSheet: imageId
AnimationClip: [AnimationFrame], loop mode, default duration
AnimationFrame: sourceRect, durationTicks, anchor, drawOffset, flip flags, markers[]
Animator: clipId, frameIndex, elapsedTicks, playback state
```

Cada clip pode ter dimensões diferentes. A posição lógica é o ponto dos pés; `anchor` é o ponto equivalente na célula, e `drawOffset` ajusta arte sem mover collision/hurtbox. Marcadores como `attack_on`, `attack_off`, `spawn_projectile` entram numa fila de eventos da animação; sistemas de combate os consomem uma vez por tick.

## Mundo, entidades e composição

Três identidades não devem ser confundidas:

- `EntityHandle(index,generation)`: rápido e válido apenas na sessão;
- `PersistentInstanceId(mapId, localId)`: estável em mapa/save;
- `DefinitionId`: string/ID estável para `enemy.slime`, `npc.blacksmith`, etc.

O registry mantém stores simples por tipo. Uma entidade é um handle associado apenas aos componentes necessários:

```text
Transform, Velocity, Facing
Visual/Animator
CollisionBody, HurtboxSet, HitboxSet, InteractionArea
Health, Faction, DamageSource
PlayerControlled, AIController
Inventory, Pickup, Projectile, Door, Trigger, Persistent
```

Componentes são dados; sistemas iteram conjuntos explícitos (`MovementSystem`, `CollisionSystem`, `CombatSystem`, `AnimationSystem`, `AISystem`). Componentes raros podem usar mapas; componentes quentes usam vetores densos. Não é necessário um ECS genérico sofisticado na primeira versão.

`EnemyDefinition` referencia visual, stats, caixas, ataques, loot e um `BehaviorProfileId`. `EnemyInstance` é só entidade composta + estado runtime. Comportamentos reutilizáveis (`idle`, `wander`, `chase`, `attack`, `retreat`, `sleep`) formam uma máquina de estados parametrizada; 50 inimigos combinam dados/perfis sem 50 subsistemas. Exceções reais podem ter um sistema especializado pequeno, sem contaminar a base.

## Colisão e combate

- AABB em coordenadas de mundo; movimento top-down por eixo, resolvendo tile sólido e depois corpos opcionais.
- Broad phase inicial por células/chunks do mapa; não testar todo o mundo.
- `CollisionBody`: bloqueia movimento, normalmente concentrado nos pés.
- `Hurtbox`: região que aceita dano.
- `Hitbox`: ataque temporário com owner, faction, damage, knockback e attack instance ID.
- `InteractionArea`: consulta de uso/fala/baú, sem bloquear.
- `Trigger`: overlap sem resposta física.

Hitboxes não são entidades visuais por obrigação. São ativadas por gameplay ao receber frame marker e deduplicam acertos por `(attackInstance,target)`. Projéteis são entidades leves. Explosões/poeira são `EffectInstance` transitórios em um pool/sistema VFX, não inimigos nem objetos persistentes.

## Eventos, HUD, NPC, quests e save

A simulação publica eventos de domínio (`EntityDamaged`, `EntityDefeated`, `ItemPickedUp`, `ChestOpened`, `RegionEntered`, `DialogueCompleted`). HUD observa um `GameViewModel`/snapshot somente leitura; não acessa campos privados do Player. Quests e achievements futuros consomem eventos e avaliam condições declarativas, sem acoplamento aos sistemas emissores.

NPC/map instance guarda referência a `NpcDefinition` e overrides mínimos (posição, direção, propriedades). Diálogo, rotina e quest são definições externas versionadas futuramente. O save armazena estado do jogador e deltas do mundo indexados por IDs persistentes: baú aberto, switch, destrutível, boss, flags. Mapas não são copiados inteiros para o save.

## Editor integrado

Recomendação: `game.exe` e `map_editor.exe` separados, ambos ligados às mesmas bibliotecas. Isso mantém ciclo de build e falhas do editor isolados, sem duplicar engine. Um `--editor` pode ser adicionado depois como conveniência.

O editor possui:

- `EditorDocument`: mapa de autoria, seleção, dirty flag e propriedades;
- ferramentas (`paint`, `erase`, rectangle, stamp, collision, entity, trigger, door, pan`);
- `EditorCommand` com `apply/revert` e transações para strokes; undo/redo tem orçamento de memória;
- UI própria desenhada por `Renderer2D`, com hit testing/input no editor;
- palettes alimentadas pelo catálogo de assets/definitions;
- `PlaytestSession` que converte uma cópia do documento em mapa runtime, sem salvar nem mutar o documento;
- salvamento atômico em temporário no mesmo diretório seguido de replace, mantendo backup recuperável.

O modelo runtime não depende de editor. A serialização opera em DTOs compartilhados e valida tudo antes de construir o mundo.

## Estrutura de diretórios evolutiva

Criar diretórios somente quando sua fase começar:

```text
docs/
src/
  engine/
    core/
    platform/
      win32/
    render/
    assets/
    simulation/
    world/
    serialization/
  gameplay/
  game/
  editor/             # apenas na fase do editor
tests/
  unit/
  fixtures/           # fixtures próprias, nunca assets licenciados
build.bat
```

`build.bat` localiza/pressupõe um Developer Command Prompt, compila com `cl.exe`, `/std:c++20`, warnings altos e gera objetos em `build/obj` e binários em `build/bin`. Engine pode começar como conjunto de `.obj`; criar `engine.lib` quando game/editor realmente compartilharem o código. CMake permanece opcional, não requisito.

## Riscos e controles

| Risco | Controle proposto |
|---|---|
| RGBA interno versus BGRA/DIB | formato documentado, conversão na borda e teste de canais |
| Alpha/recorte com pixels fora da tela | testes unitários golden com imagens sintéticas próprias |
| Pixel shimmer e escala fracionária | arredondar só em world→logical; integer scale + letterbox |
| Frame layouts/anchors inferidos incorretamente | catálogo declarativo + visualizador antes de gameplay |
| Gate/spikes têm fundo opaco | validar uso; não aplicar color key implícito |
| Catch-up infinito após breakpoint/resize | clamp, limite de ticks e telemetria |
| Input perde edges entre frames/ticks | fila de edges com consumo por tick + estado held separado |
| IDs quebrados pelo editor | IDs imutáveis, duplicação gera novo ID, validação de unicidade |
| Formato vira dump de structs | encoding explícito, chunks versionados, migração em DTO |
| Entidade vira novo `GameObject` monolítico | componentes pequenos e systems orientados a capacidades |
| ECS vira framework próprio excessivo | implementar apenas stores/queries exigidos pelo slice |
| Editor contamina runtime | document model separado e dependência unidirecional |
| “Preparar multiplayer” paralisa o jogo | apenas comandos/ticks/IDs/eventos agora; rede depois |
| Assets licenciados vazam no Git/build | ignore futuro, distribuição separada e fixtures autorais |
| WIC/COM vaza para o engine | adapter Win32 com retorno `ImageData` puro |

## Decisões adiadas deliberadamente

Áudio, decoder PNG próprio, scripting, formato de diálogo/quest, pathfinding avançado, hot reload, multithreading do renderer, rede e protocolo não pertencem ao primeiro vertical slice. Suas extensões são preservadas por contratos e IDs, não por pastas vazias ou abstrações sem consumidor.
