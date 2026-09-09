# Roadmap incremental — Dungeon Underworld

Este roadmap define **ordem de desenvolvimento, dependências, critérios de aceite e gates de escopo**.

O working tree local, os testes e o comportamento executável prevalecem sobre marcadores de status deste documento. Um recurso pode estar concluído localmente antes de existir um checkpoint/commit remoto.

A arquitetura detalhada pertence ao `ENGINE_ARCHITECTURE.md`; regras para agentes pertencem ao `AGENTS.md`.

---

## 1. Regras de execução

Cada incremento deve terminar em algo:

- compilável;
- executável ou observável quando aplicável;
- testável;
- pequeno o suficiente para identificar regressões;
- reutilizável sem criar abstração especulativa.

Regras permanentes:

- não avançar com regressões conhecidas;
- assets licenciados não viram fixtures públicas;
- testes de pixels usam fixtures sintéticas próprias;
- decisões adiadas não criam pastas/interfaces vazias;
- preferir estender sistemas existentes em vez de criar versões paralelas;
- introduzir uma abstração antes da fase prevista somente quando houver uso concreto imediato e redução clara de retrabalho.

### Fundação de separação gameplay/presentation — concluída

O timing semântico dos ataques agora é definido por `AttackDefinition.timeline` e
avança através de `AttackExecution` em ticks fixos. Hitbox activation/deactivation,
projectile spawn e attack completion não dependem de `Animator`, sprites ou markers
visuais. Animation markers permanecem disponíveis apenas como metadados de
apresentação para usos futuros, como VFX ou áudio.

Esta fundação prepara a futura separação `GameSession` / `GamePresentation`; ela não
antecipa nem conclui a Fase 13 de headless/replay.

### Composição `Phase7Demo` → `GameRuntime` — concluída

O runtime ativo foi renomeado para `GameRuntime` e a responsabilidade de desenho foi
extraída para `GamePresentation`. A apresentação agora concentra câmera, composição
de layers/atores, projéteis, efeitos, HUD, diálogo, inventário e debug, consultando
uma view somente-leitura do runtime. `GameRuntime` continua sendo uma composição
transitória que contém a simulação; a extração de `GameSession` permanece como o
próximo incremento e não deve ser considerada concluída por esta mudança.

### GameSession — cortes históricos

O segundo corte de `GameSession` moveu para essa unidade o `MapSession`, `RuntimeWorld`
e `SessionWorldState`. `GameSession::tick` recebe somente `PlayerCommand`, resolve a
colisão/tile size do mapa ativo e emite `MapEntered` para transições. A migração dos
demais sistemas autoritativos de gameplay ainda não foi concluída. Não marcar a
separação `GameSession`/Presentation como pronta até que mapa, combate, criaturas,
objetos, pickups, inventário, diálogo e quests fossem coordenados pela Session.

O corte seguinte moveu `CombatSystem`, `ProjectileSystem`, `EnemyBehaviorSystem`,
`AttackExecution`, contato, dano, knockback e derrota para a Session. Objetos,
pickups, inventário, Wallet e Quick Slots agora também pertencem à Session; coleta,
interação e ciclo lógico de destruição avançam por ticks fixos. NPC/diálogo e quests
ainda permanecem como bridges transitórias no `GameRuntime`, assim como algumas
operações de persistência.

O corte seguinte concluiu os blocos `Objects`, `Pickups`, `Inventory / Wallet` e
`Quick Slots`; a closure abaixo conclui ownership, bridges e persistência.

O corte narrativo moveu `DialogueFlagSet`, `DialogueSession`, `QuestStateStore` e
`QuestSystem` para a Session. O diálogo roteia comandos modais, choices emitem ações
concretas (incluindo ativação de quest) e quests consomem eventos de domínio uma vez
por tick. O fechamento de ownership e persistência da Session está registrado abaixo.

### GameSession — extração autoritativa concluída

O fechamento removeu as bridges `*ForRuntime()`, transferiu o `EntityHandlePool` para
a Session e concentrou capture/restore lógico de save em operações específicas da
Session. `GameRuntime` permanece como application shell para input, filesystem,
assets e presentation; ele observa estado constante e não muta internals de gameplay.
Os cortes concluídos abrangem Player, mapa, combate, criaturas, projéteis, objetos,
pickups, inventário, NPC/diálogo/quests e ownership/persistence closure. Replay e
state hashing permanecem como ferramentas opcionais de testes e auditoria;
networking e multiplayer estão fora do escopo do projeto.

---

## 2. Estado macro

```text
FASE 0 — Auditoria + arquitetura                         DONE
FASE 1 — Plataforma + janela + framebuffer               DONE
FASE 2 — Renderer + PNG + sprites + animação             DONE
FASE 3 — Tilemap + câmera + colisão                      DONE
FASE 4 — Player + InputState + PlayerCommand             DONE
         + movimento + animação + camera follow
FASE 5 — Combat Foundation + vertical slice de combate   DONE
FASE 6 — Creature Engine reutilizável                    DONE
FASE 7 — Objetos + pickup + HUD + inventário            DONE
FASE 8 — .dmap + transições + save                     DONE
FASE 9 — Map Maker                                     DONE
FASE 10 — NPC + diálogo                               DONE
FASE 11 — Quests                                      DONE
FASE 12 — RPG (12A–12E3 DONE)                         COMPLETE
FASE 13 — External Authored Content                   DONE
13B — External Content Workspace                      DONE
13B1 — Multi-file Workspace Core + Deterministic Merge DONE
13B2 — Workspace Directory Discovery                  DONE
13B3 — Application/Tooling Integration                DONE
FASE 14 — Authored World Foundation                   DONE
14A — Authored Map Source                             DONE
14B — Runtime Regions                                 DONE
14C — World Logic Foundation                          DONE
14D — Stateful Doors                                  DONE
14E — Encounter Foundation                            DONE
FASE 15 — Presentation Feedback Foundation             DONE
15A — Presentation Effect Runtime                     DONE
15B — Authored Presentation Effects                   DONE
15C — Presentation Cue Integration                    DONE
15D — Environment / World Integration                  DONE
FASE 16 — Interactive World Components                 DONE
16A — Stateful Object Activation                      DONE
16B — World Logic Integration                         DONE
16C — Persistence + Authored Content                  DONE
16D — Puzzle Vertical Slice                           DONE
FASE 17 — Visual Content Boundary                     DONE
17A — Visual Asset Definitions                         DONE
17B — Authored Animation + Visual Sets                DONE
17C — Runtime Visual Content Loader                   DONE
17D — External Visual Content Vertical Slice          DONE
FASE 18 — Content Studio                              DONE
18A — Content Studio Foundation                       DONE
18B — Visual Asset + Animation Authoring              DONE
18C — Gameplay Content Editors                        DONE
18D — Unified Content/Map Workflow                    DONE
```

Baseline validada da Fase 6:

```text
code commit: 4fa4474a770f5c8e195d51161cb69c38277c4e99
MSVC 2022 x64 / C++20 / W4
warnings: 0
tests: PASS — 296 checks
git diff --check: PASS
```

---

# Fase 0 — auditoria e decisões

## Objetivo

Conhecer assets, restrições de licença e direção arquitetural antes de construir sistemas.

## Concluído

- inventário recursivo dos assets;
- dimensões, alpha e duplicatas;
- inspeção de spritesheets;
- registro de licença e restrição de redistribuição;
- arquitetura inicial;
- proposta de `.dmap`;
- roadmap incremental.

## Marco

Base documental suficiente para iniciar engine sem depender de inferências de asset durante gameplay.

---

# Fase 1 — plataforma, janela e primeiro pixel

## Objetivo

Criar a borda Win32 e o loop temporal sem acoplar gameplay à plataforma.

## Capacidades

1. `build.bat` x64 / C++20.
2. Contratos mínimos de plataforma sem `windows.h` em headers públicos quando evitável.
3. Janela Win32 DPI-aware e message pump.
4. Relógio de alta resolução.
5. `Framebuffer(272,224)` RGBA8.
6. Apresentação por DIB.
7. Integer scaling + nearest-neighbor + letterbox.
8. Fixed timestep 60 Hz com clamp/catch-up budget.

## Marco

Janela apresenta pixels produzidos pela CPU no framebuffer lógico, escalados corretamente e com simulação temporal independente do render.

---

# Fase 2 — renderer, PNG, sprites e animação

## Objetivo

Construir pipeline visual próprio reutilizável.

## Capacidades

1. `setPixel`, `fillRect`, clipping.
2. `drawImage` / `drawImageRegion`.
3. Alpha blend RGBA straight.
4. `IImageDecoder` / `ImageData` neutros.
5. WIC somente no adapter Win32.
6. Ownership/cache mínimo por `AssetId`.
7. Flip horizontal no blit.
8. `SpriteSheet`, `AnimationClip`, frames, anchor/draw offset.
9. Animator em ticks.
10. Bitmap font.

## Marco

Sprites, animações e texto pixel-art são carregados por adapter de plataforma e desenhados pelo renderer próprio sem lógica de gameplay.

---

# Fase 3 — Map Engine runtime, câmera e colisão

## Objetivo

Criar um mundo maior que a viewport e uma base única de mapa/física.

## Capacidades

1. `GameMetrics` e coordenadas explícitas.
2. `RuntimeMap` em memória.
3. `TileLayer` e tile refs.
4. Tile culling.
5. `Camera2D` world→logical e clamp.
6. Múltiplos passes/layers suficientes para ground/low/foreground.
7. `CollisionGrid` separado da arte.
8. AABB versus tiles e resolução por eixo.
9. Queries/broad-phase apenas na medida necessária pelo runtime.

## Marco

Mapa grande pode ser renderizado, percorrido por câmera e consultado por colisão sem duplicar mapa ou física por entidade.

---

# Fase 4 — Player controlável

## Status

**Concluída e publicada.**

A versão antiga do roadmap previa `EntityHandle`, stores de componentes e `MovementSystem` já nesta fase. Essa antecipação foi removida: com apenas um ator de gameplay real, o Player permanece uma estrutura/classe pequena e explícita.

## Objetivo observável

> abrir `game.exe`, controlar o Player pela dungeon, colidir com o mapa, alternar idle/walk por direção e ter a câmera seguindo o personagem.

## Capacidades da fase

### Input neutro

- `InputState` independente de Win32;
- WASD/arrow keys mapeados somente na borda;
- estado held para movimento;
- perda de foco limpa teclas pressionadas;
- `DebugInputState` continua restrito a ferramentas/debug.

### Command pipeline

```text
InputState
    ↓
CommandBuilder
    ↓
PlayerCommand
```

`PlayerCommand` preserva:

```text
tick
player identity
sequence/order
movement intent
```

Entradas contraditórias:

```text
Left + Right = 0
Up + Down    = 0
```

### Player runtime

Estado pequeno e explícito:

```text
world/subpixel position
facing
motion state
movement configuration
collision body information
```

Sem ownership de janela, teclado físico, renderer, WIC ou câmera.

### Movimento

- fixed tick;
- subpixel/fixed-point pequeno;
- diagonal normalizada aproximadamente;
- sem `sqrt()` todo tick;
- reutiliza AABB/tile collision existente;
- collision body concentrado nos pés.

### Facing/animação

- Down / Up / Left / Right;
- última direção significativa permanece ao parar;
- política diagonal determinística;
- somente Idle/Walk nesta fase;
- side-left + flipX para Right;
- clip não é resetado todo tick.

### Camera follow

- câmera recebe o Player como target;
- follow direto;
- clamp no mapa;
- sem smoothing/dead-zone/shake nesta fase.

## Não incluído

```text
EntityRegistry genérico
component stores
ECS
combate
hurtbox/hitbox
IA
inventário
```

## Validação do checkpoint

- cardinal;
- diagonal;
- direções opostas;
- wall/slide/corner/corridor;
- camera follow;
- camera clamp;
- foreground occlusion existente;
- integer scaling/resize;
- perda de foco com tecla mantida;
- build `/W4` sem warnings;
- testes existentes + novos checks;
- `git diff --check`.

---

# Fase 5 — Combat Foundation

## Status

**Concluída e publicada no commit `4a2f440bf82e653e51a925a505443101103d735d`.**

Capacidades existentes:

```text
ActionEdgeBuffer
PlayerCommand com ActionIntent
EntityHandle(index, generation) / EntityHandlePool
Health / Faction
CollisionBody / Hurtbox / Hitbox / InteractionArea
AttackInstanceId / CombatSystem
EventBuffer
EntityDamaged / EntityDefeated / ProjectileImpact
ProjectileSystem / EffectSystem
animation markers
actor Y-sort
```

A Fase 5 é a primeira em que múltiplos participantes e relações source/target justificam uma pequena generalização compartilhada.

A implementação deve continuar incremental. Não criar ECS completo.

## Fase 5A — identidade runtime e primitivas de combate

### Objetivo

Criar a fundação reutilizável que espada, Training Puppet, projéteis e criaturas usarão.

### Implementar somente o necessário

#### Identidade runtime mínima

Introduzir identidade genérica quando source/target exigir:

```text
EntityHandle(index, generation)
```

Ela serve para:

- atacante/alvo;
- referências runtime seguras;
- invalidação após destruição/reuso;
- deduplicação de hits;
- preparação para criaturas.

Não criar junto, por obrigação:

```text
Archetype
SparseSet
ComponentArray universal
SystemManager genérico
```

#### Primitivas semânticas

Separar:

```text
CollisionBody
Hurtbox
Hitbox
InteractionArea
Trigger
```

Adicionar conforme necessário:

```text
Health
Faction
AttackInstanceId
Damage/knockback data
```

#### Eventos de domínio mínimos

Quando um segundo consumidor real justificar, preparar/introduzir eventos como:

```text
EntityDamaged
EntityDefeated
```

Não criar event bus genérico além da necessidade da fase.

### Aceite

- handle destruído/reutilizado não resolve silenciosamente para alvo antigo;
- boxes têm semânticas separadas;
- combate não depende de renderer;
- uma execução de ataque pode identificar source e target de forma estável durante o tick/runtime.

---

## Fase 5B — espada + Training Puppet

### Input de ação

Estender command pipeline para ação de ataque sem levar tecla física para gameplay.

Edges devem sobreviver até o tick consumidor.

### AttackDefinition mínima

Introduzir definição reutilizável somente com campos usados pelo slice, por exemplo:

```text
damage
startup/recovery ou marker timing
hitbox
knockback
animation reference
```

Não implementar seletor de IA nesta subfase.

### Player attack state

Fluxo:

```text
PlayerCommand
    ↓
attack state
    ↓
animation marker
    ↓
activate hitbox
    ↓
CombatSystem
    ↓
damage
```

### Regras

- renderer nunca aplica dano;
- marker é emitido uma vez;
- cada swing recebe `AttackInstanceId`;
- `(attackInstance, target)` impede hits repetidos do mesmo swing;
- invulnerabilidade curta;
- knockback simples;
- Training Puppet fornece alvo observável e testável.

### Marco

Player acerta o Training Puppet com espada, dano e timing são verificáveis e nenhum estado de combate depende do desenho do sprite.

---

## Fase 5C — projétil e VFX

### Objetivo

Provar que o mesmo combate suporta ataque não-melee.

### Capacidades

- marker de spawn;
- estado runtime leve de projétil;
- owner/faction/attack identity;
- movimento em fixed tick;
- collision/impacto;
- expiração/lifetime;
- flecha;
- VFX de impacto transitório.

Projectile não cria novo collision system.

VFX não precisa ser entidade persistente.

### Marco da Fase 5

Espada e flecha usam a mesma fundação de identidade/dano/eventos, com Training Puppet como alvo verificável.

---

# Fase 6 — Creature Engine reutilizável

## Status

**Concluída e publicada no commit `4fa4474a770f5c8e195d51161cb69c38277c4e99`.**

## Objetivo

Fazer com que adicionar um novo monstro deixe de exigir nova arquitetura.

O resultado desejado é aproximadamente:

```text
assets/clips
+ EnemyDefinition
+ BehaviorProfile
+ AttackDefinitions
+ spawn em memória
```

## Capacidades implementadas

```text
DefinitionId
AttackKey(owner, localAttackInstance)
CombatantState + CombatTargetRef temporária
AttackDefinition + DirectionalBoxes
ProjectileDefinition + orientação canônica
EnemyDefinition + EnemyInstance
EnemyFactory
BehaviorProfile
EnemyVisualSet + EnemyVisualInstance
```

Definições são imutáveis e compartilhadas; instâncias possuem handles, posição
fixed-point, health, estado, timers, cooldowns e ataque ativo independentes.

### FSM implementada

```text
Idle
Wander
Chase
Attack
Dead
```

Estados ainda deferidos:

```text
Sleep
Wake
Retreat
Stunned / Flee / Guard
```

Transições atuais usam:

- distância;
- target válido;
- attack range;
- cooldown;
- timer;
- health esgotada.

O HUD de debug exibe o estado atual de cada inimigo; histórico detalhado de transições
permanece opcional para uma ferramenta futura.

---

### Conteúdo que comprovou reuso

```text
enemy.evil_soldier
    behavior.soldier.melee
    attack.soldier.sword

enemy.skull
    behavior.skull.ranged
    attack.skull.arrow
    projectile.skull.arrow (sprite canônico Right)
```

Soldier e Skull usam o mesmo seletor de ataques, FSM, `CombatSystem`,
`ProjectileSystem`, event stream e lifecycle de handles. Uma terceira definição sintética
nos testes reutiliza o profile melee apenas alterando dados.

### Dano, morte e eventos

Fluxo:

```text
Hitbox/Projectile
    ↓
CombatSystem
    ↓
Health
    ↓
Dead
    ↓
death animation
    ↓
EntityDefeated
    ↓
despawn + invalidação do EntityHandle
```

Creature não modifica diretamente HUD, quest, save ou XP. Entity-vs-entity body
blocking, line of sight e pathfinding continuam conscientemente deferidos.

## Marco

Evil Soldier melee e Skull ranged coexistem com Player e Training Puppet no mapa,
usando a mesma fundação sem sistemas de combate/projéteis específicos por criatura.

---

# Fase 7 — objetos, pickups, HUD e inventário pequeno

## Status

**DONE.** Implementada nos commits `30b413d`, `ccbaf4a`, `7873e32` e `0721b12`;
fechada no Windows pelo commit `7cc9da4`.

O ambiente do checkpoint compilou todos os módulos portáveis e a composição do demo
portátil original, que executou 340 checks. O fechamento Windows validou Windows 11 x64,
MSVC 19.44.35219 (toolset da linha Visual Studio 2022), Windows SDK 10.0.26100.0,
C++20, `/W4`, 0 warnings, 347 checks, `git diff --check` e smoke visual/interativo
passando. O smoke cobriu
regressões das Fases 0–6, pickups, inventário, quick slots, HUD, chest, crate,
Y-sort, resize/letterbox, perda de foco e `WM_CLOSE`. As fases posteriores de mapa,
save, conteúdo authored e Content Studio estão concluídas; o foco atual é tooling
de produção multimapa.

## Objetivo

Expandir composição para entidades não-hostis e estado do jogador.

### Objetos por capacidades

Evoluir para definição + capacidades:

```text
Pickup
Container
Destructible
Interactable
Door
Trigger
```

Não criar árvore profunda de subclasses.

### Capacidades da fase

1. pickup de coração/dinheiro;
2. eventos de pickup;
3. inventário mínimo por stable item IDs;
4. `GameViewModel`/snapshot somente leitura para HUD;
5. HUD de vida/dinheiro/item equipado;
6. baú/interação;
7. destrutível com estado/animação.

### Generalização de entidades

Esta fase, junto com criaturas, é o ponto em que Player + enemies + objects podem justificar stores/componentes compartilhados adicionais.

Extrair somente dados/operações comprovadamente repetidos.

## Marco

Pickup, baú, destrutível e HUD funcionam sem acessar internals uns dos outros e sem criar subsistemas exclusivos por sprite.

## Decisões consolidadas implementadas

```text
PlayerInventory = 30 slots
layout visual = 10 colunas x 3 linhas
QuickSlots = 4 bindings por ItemDefinitionId
stackLimit pertence a ItemDefinition
item.life_potion = stackLimit 66, RestoreHealth 2
equipment usa stackLimit 1
Gold pertence a Wallet e não ocupa slot
ItemContainer aceita capacidade arbitrária
BankStorage futuro = 50 slots, somente após persistência/save
```

As fases posteriores adicionaram banco, `.dmap`, save, loot/XP, equipment stats e
drops de inimigos; os detalhes históricos desta fase permanecem acima apenas como
registro do escopo original.

---

# Fase 8 — `.dmap`, transições e save

## Status

**Concluída.** DMAP 1.4 alimenta o `game.exe`; duas salas são resolvidas por
`MapId`, construídas por `RuntimeWorldBuilder` e trocadas por `MapSession` em boundary
de tick. `SessionWorldState` registra deltas de Chest, Crate e Pickup para A→B→A e é
a mesma estrutura serializada por DSAV 1.8. F5/F9 são edges lógicos; save usa
temporário + backup e load prepara o novo world antes do swap.

Baseline histórica de fechamento: MSVC 19.44 x64, C++20, `/W4`, 0 warnings.
A validação portátil atual registra 2774 checks; a validação anterior incluiu
`git diff --check` PASS e smoke Win32 incluindo DMAP, A→B→A, save, restart/load,
resize/focus e `WM_CLOSE`.

## Gate

Só congelar `.dmap` v1 depois que runtime de entidades/spawns/objetos tiver necessidades suficientemente concretas.

O `MAP_FORMAT.md` permanece proposta conceitual até esta fase.

## Capacidades

1. revisar e congelar contratos v1 realmente necessários;
2. byte reader/writer bounds-checked;
3. header/chunks/string table;
4. tile layers/collision com encoding simples;
5. entities/spawns/links com IDs estáveis;
6. DTO validado → `RuntimeMap`;
7. portas/transições entre mapas;
8. formato de save separado;
9. player state + world deltas;
10. escrita atômica/backup.

## IDs

Distinguir:

```text
EntityHandle          # runtime, nunca persistido
PersistentInstanceId  # mapa/save
DefinitionId          # tipo de conteúdo
```

## Aceite

- arquivo inválido não cria mundo parcial;
- links inválidos geram diagnóstico;
- ida/volta entre duas salas funciona;
- estado persistente relevante sobrevive reload;
- save não copia mapa inteiro.

## Marco

Vertical slice com duas salas, transição e pelo menos um delta persistente.

---

# Fase 9 — Map Maker

## Status

**DONE — blocks 1–9F.** O editor e runtime compartilham `TilesetCatalog` no
`GameContentRegistry`. Multi-tileset authoring está implementado: `MapTileReference`
persiste `DefinitionId`, o runtime resolve `world::TilesetId` local e o Map Maker oferece
selector, palette dinâmica, painting/rectangle/fill e eyedropper por pack. A validação
rejeita tileset desconhecido, source index fora do atlas e tile size incompatível. DMAP
1.4 e DSAV 1.8 permanecem os formatos runtime estáveis. O smoke interativo geral é uma
validação de host separada e não altera o formato authored/runtime.

O fechamento 9F adiciona uma sessão mínima de playtest em memória: ela copia o `MapData`,
resolve o spawn pela política oficial e passa o snapshot pelo `RuntimeWorldBuilder` e
pelas factories de conteúdo; parar o playtest libera os handles temporários e deixa o
`EditorDocument` intacto. Após o documento ter um caminho authored, o shell Win32 agenda
um backup periódico em `<arquivo>.autosave.dmap`; esse sidecar é validado e nunca altera
o caminho, a revisão ou o estado dirty do documento. A validação continua orientada por
mutação/revisão, e o desenho de tiles continua limitado à faixa visível.

O checkpoint semântico adiciona `AuthoringSemanticRegistry` para as 72 células visíveis
do atlas Dungeon, oito stamps visuais, paleta/inspector semânticos e validação advisory
separada da validação estrutural. `PlaceStampCommand` preserva undo/redo atômico e rejeita
layer bloqueada ou placement fora dos limites antes de escrever. O workflow atual mantém
essa base e adiciona o projeto authored multimapa descrito no checkpoint de produção.

O primeiro slice de Map Composition também está implementado sem alterar DMAP/DSAV:
`MapBlueprint`/`RoomBlueprint` descrevem uma sala retangular in-memory, até quatro
openings semânticos e um spawn opcional; `MapComposer` produz uma `RoomCompositionGrid`
e `MapData` determinísticos usando `AuthoringSemanticRegistry`. Boundary vira collision
solid, openings permanecem passáveis e `ReachabilityValidator` valida a conectividade do
spawn aos openings por BFS sobre o grid de collision. `validateMapData`, `MapSemanticValidator`
e a validação de playability permanecem passagens independentes. O catálogo ainda não
comprova um semantic ID de floor, portanto o compositor inicial produz apenas a visual
de boundary comprovada e não inventa atlas/floor data.

## Gate

Não iniciar antes de runtime map + `.dmap` + transições estarem utilizáveis.

## Executável

```text
map_editor.exe
```

separado de `game.exe`.

## Compartilhar

- renderer;
- assets;
- map/runtime contracts;
- definitions;
- serialization.

## Modelo próprio

```text
EditorDocument
selection
dirty state
EditorCommand apply/revert
undo/redo
property model
playtest session
```

## Capacidades

1. new/open/save;
2. validação;
3. pan/zoom;
4. tile palette;
5. paint/erase;
6. rectangle/fill;
7. layers;
8. collision painting;
9. colocar/mover entidades;
10. property editor;
11. spawns/triggers/regions/doors;
12. link validation;
13. undo/redo;
14. copy/paste com novos IDs;
15. playtest in-memory;
16. autosave/backup.

## Marco

Criar um mapa do zero, salvar, reabrir semanticamente equivalente, editar com undo/redo e playtestar sem contaminar o documento de autoria.

---

# Trilha transversal — Scene / Game-State

Não criar uma fase artificial apenas para possuir “Scene Engine”.

Introduzir a menor abstração quando dois ou mais estados reais exigirem lifecycle e routing compartilhados.

Casos possíveis:

```text
gameplay + pause
gameplay + transition
gameplay + menu
editor + playtest
```

Interface mínima possível:

```text
enter
exit
update/tick
render
input/command routing
```

Se essa necessidade surgir antes da Fase 9 e suas dependências forem concretas, a pequena abstração pode ser antecipada.

Não criar Scene Graph universal nem usar Scene como `GameObject` global.

---

# Fase 10 — NPC e diálogo

## Block 10A — NPC Foundation (DONE)

Implemented in this increment: reusable `NpcDefinition`/`NpcCatalog`, runtime
`NpcInstance`/`NpcFactory`, shared `InteractionArea` detection, editor placement
commands, and authored persistence through DMAP 1.1's optional `NPCS` chunk. Two
content definitions (`npc.guard` and `npc.scholar`) exercise the same runtime system.
Dialogue sessions, choices, conditions and actions are deliberately deferred to the
following blocks.

## Block 10B — Dialogue Data Model (DONE)

Implemented in this increment: renderer-independent `DialogueDefinition`,
`DialogueNode`, `DialogueChoice` and `DialogueCatalog`. Nodes support ordered text
pages, a linear next-node transition, or choices targeting nodes in the same dialogue.
Catalog validation rejects empty pages, duplicate node IDs, unknown entry/transition
targets and ambiguous next-node-plus-choice graphs. Guard and Scholar are connected to
separate dialogue definitions through their existing `defaultDialogueId` field.

Dialogue sessions/UI, command routing, conditions, actions and persistence remain
deferred to Blocks 10C–10D.

## Block 10C — Dialogue Runtime + UI (DONE)

Implemented in this increment: renderer-independent `DialogueSession`, command-based
pagination and choice navigation, deterministic selection, close handling and
integration with NPC `defaultDialogueId`. The game consumes commands while a dialogue
is open and presents speaker, page, text and choices in an overlay rendered with the
existing bitmap font. Dialogue conditions, actions and persistent flags remain
deferred to Block 10D.

## Block 10D — Persistent Dialogue Flags (DONE)

Implemented in this increment: sorted `DialogueFlagSet`, `flagSet`/`flagNotSet`
conditions and `setFlag`/`clearFlag` choice actions. The game persists the state through
the optional `FLGS` chunk in DSAV 1.1 and restores it on load. DSAV 1.0 files without
flags remain backward compatible; malformed or misplaced flag chunks are rejected.
Quest state and general scripting remain outside this block.

## Dependências

- entidades/IDs estáveis;
- interação;
- save/world flags;
- editor capaz de colocar NPCs ou mecanismo equivalente de autoria.

## Capacidades

- `NpcDefinition`;
- posição/facing + overrides pequenos no mapa;
- InteractionArea compartilhada;
- caixa de diálogo;
- paginação;
- choices;
- conditions;
- actions pequenas;
- diálogo orientado a dados.

## Aceite

Dois NPCs diferentes reutilizam o mesmo sistema e pelo menos uma condição/flag permanece correta após reload.

---

# Fase 11 — Quests

## Block 11A — Quest definitions (DONE)

`QuestDefinition`, `QuestObjectiveDefinition` e `QuestCatalog` formam a primeira
fundação de quests independente de renderer. As definições usam `DefinitionId`
estável, seis tipos de objetivo (`talk`, `kill`, `pickup`, `enter`, `open`,
`deliver`) e IDs explícitos para os alvos. O `GameContentRegistry` registra uma
quest multiobjetivo pequena para exercitar o modelo com conteúdo real.

Avanço orientado a eventos e persistência permanecem fora deste bloco.

## Block 11B — Quest state (DONE)

`QuestStateStore` mantém `QuestProgress` separado de `QuestDefinition`, com status
`inactive`, `active` e `completed`, contadores por objetivo e reset explícito.
Iniciar uma quest copia somente os IDs dos objetivos e os contadores; os limites
continuam pertencendo às definições imutáveis. O estado aceita avanço incremental ou
atribuição clamped ao limite requerido e conclui automaticamente quando todos os
objetivos terminam.

Avanço dirigido por eventos e persistência de save permanecem deferidos para os
blocos seguintes.

## Block 11C — Event-driven quest progression (DONE)

`QuestSystem` consome o `SimulationEvent` stream existente e atualiza somente
quests ativas. Derrotas, coletas, conversas com NPCs, entrada em mapas, abertura de
objetos e entregas de itens são convertidas em avanço dos objetivos correspondentes
por `DefinitionId`, sem polling de entidades, inventário ou objetos. Os eventos de
derrota e coleta carregam seus IDs de definição concretos; a persistência é fornecida
pelo Block 11D.

## Gate

Não iniciar sem event stream suficiente e IDs persistentes comprovados.

## Capacidades

Eventos consumidos, por exemplo:

```text
EntityDefeated
ItemPickedUp
ChestOpened
RegionEntered
DialogueCompleted
```

Objetivos declarativos:

```text
talk
kill
pickup
enter
open
deliver
```

Quest state é salvo separadamente de definição.

## Aceite

Quest multiobjetivo progride por eventos, não por polling acoplado, e sobrevive save/load.

## Block 11D — Quest persistence (DONE)

`SaveData` agora carrega o `QuestStateStore` separadamente das definições. DSAV minor
2 adiciona o chunk opcional `QSTS`, com IDs estáveis, status e contadores de objetivos;
o reader valida tudo contra o `QuestCatalog` antes de aceitar o save. Saves DSAV 1.0
e 1.1 sem esse chunk continuam compatíveis, enquanto um `QSTS` em versão anterior é
rejeitado explicitamente. DMAP permanece inalterado.

## Block 12A — Player progression foundation (DONE)

Progressão authored passa pelo `ContentCompiler` e `PlayerProgressionCatalog`.
`GameSession` possui XP cumulativo e deriva o level da curva compilada; DSAV 1.3
persiste o profile e o XP total, mantendo saves anteriores compatíveis. Rewards,
loot, equipment, modifiers e bank permanecem nos cortes futuros de RPG.

---

# Fase 12 — RPG

## Gate

Somente depois de existir vertical slice estável e jogável.

## 12A — Progression Foundation — DONE

Base stats authored, XP cumulativo, level derivado pela curva compilada e
persistência DSAV estão concluídos. A curva builtin `{0, 100, 250}` é provisória
para desenvolvimento e não representa balanceamento final.

## 12B — Rewards + Loot — DONE

`EntityDefeated` alimenta resolução determinística de rewards na `GameSession`. XP e
loot usam conteúdo compilado; drops de inimigos são transient no mundo ativo e não
alteram DSAV/DMAP. Os valores builtin são provisórios.

## 12C — Equipment + Modifiers + Derived Stats — FUTURO

## 12D — Bank / persistence / UI — FUTURO

## Capacidades futuras

- stats derivados;
- XP;
- level curve;
- equipment slots;
- modifiers;
- loot tables;
- inventário expandido;
- UI associada.

`EnemyDefinition` fornece referência a reward metadata; Creature não incrementa
diretamente XP do Player. Rewards e loot estão concluídos em 12B; equipment,
modifiers, stats derivados e bank permanecem futuros.

Loot tables resolvem drops; não espalhar RNG em cada classe de inimigo.

## Aceite

Novo item/monstro entra principalmente por definitions e dados, sem alterar engine base a cada conteúdo.

---

# Tooling opcional — headless, replay e auditoria determinística

## Status

Networking e multiplayer estão fora do escopo. A fronteira headless já é usada pelos
testes/playtests; replay e state hashing são tooling opcional para testes, auditoria
e reprodução de bugs, sem gate obrigatório para o próximo foco.

## Objetivo

Provar que gameplay/simulation consegue existir sem janela/render e que command streams podem reproduzir cenários.

## Capacidades

- auditar autoridade/state ownership;
- separar dependências restantes de apresentação;
- execução headless;
- gravar/reproduzir command streams;
- numerar snapshots;
- registrar identificadores estáveis apenas para testes e auditoria;
- medir nondeterminismo relevante.

## Aceite

Simulação headless executa cenário gravado e alcança estado esperado.

---

---

# Portões de escopo

```text
não iniciar Map Maker
antes de runtime map + serialização + formato de mapa utilizáveis

não congelar .dmap
antes de entities/spawns/objetos reais definirem suas necessidades

não iniciar quests
antes de event stream + IDs persistentes

não iniciar RPG completo
antes de vertical slice estável e jogável

replay e state hashing são ferramentas de teste opcionais, sem relação com rede

não criar ECS genérico
antes de múltiplas entidades reais justificarem

não criar scripting
enquanto definitions + sistemas C++ cobrirem os casos reais
```

---

# Critérios para antecipar uma fundação

Uma etapa futura pode ser antecipada somente quando:

1. suas dependências necessárias já existem;
2. existe uso concreto imediato;
3. desbloqueia pelo menos duas tarefas próximas ou evita duplicação já iminente;
4. não viola gate de escopo;
5. cabe em incremento pequeno e testável;
6. não exige abstrações para conteúdo inexistente;
7. reduz acoplamento/retrabalho mensurável.

Exemplo já adotado:

```text
PlayerCommand foi criado cedo porque Player já precisava dele,
e a mesma fronteira também ajuda testes e replay determinístico.
```

Exemplo atual aceitável:

```text
EntityHandle mínimo na fundação de combate,
porque sword target, projectile target e creatures precisarão da mesma identidade runtime.
```

Exemplo não aceitável:

```text
criar ECS completo, snapshots de estado ou scripting engine
porque talvez sejam úteis no futuro.
```

---

# Ordem de evolução a partir do estado atual

```text
preservar UMAP standalone
        ↓
WorldProjectDocument + UWORLD v1 multimapa
        ↓
validação cross-map + compile em memória
        ↓
export DMAP individual + playtest via MapSession
        ↓
tooling de produção guiado por necessidades concretas do jogo
```

Essa ordem prepara bases reutilizáveis imediatamente antes de seus consumidores reais, evitando tanto duplicação quanto overengineering.

---

# Regra final do roadmap

O roadmap é uma ordem de dependências, não uma obrigação de construir toda abstração desenhada antecipadamente.

Se o código real mostrar que uma pequena fundação simplifica várias etapas seguintes, ela pode ser antecipada conforme os critérios acima.

Se uma abstração ainda não possui consumidores reais, preservar apenas a fronteira arquitetural e continuar construindo o próximo comportamento jogável.

# Current authored content and tooling checkpoint

Phase 9 Block 1 uses authored gameplay maps discovered recursively from
`maps/gameplay/`, rather than a runtime manifest or generated demo rooms. The DMAP
internal ID is authoritative; discovery is deterministic, rejects duplicate IDs,
loads the complete `MapCatalog`, and validates cross-map links before startup. UMAP v3
remains the standalone authored format, while UWORLD v1 embeds ordered
AuthoredMapSource values for the multi-map Content Studio workflow; DMAP 1.4 remains
one runtime file per map.

`MapComposer` remains a small composition foundation for deterministic room geometry.
Procedural generation and LLM blueprint production are outside the current product
direction.

## Audit/playtest portability track — Block A

The first, deliberately partial, anticipation of the later headless/replay work is
complete: `AuditSession` writes structured audit metadata/events/state checkpoints,
and `GameRuntime::auditSnapshot()` exposes a value-only diagnostic view. Output is
development-only and ignored by Git; replay and hashing remain optional tooling.

Still deferred, in order, are logical-framebuffer BMP capture, the real headless
platform, deterministic scripted playtest runner, Windows manual/F12 integration,
portable Linux build/runtime, and seeded stress playtesting. Formal command replay,
state hashing remains optional deterministic-test tooling; network identity and multiplayer authority are out of scope.

The logical framebuffer screenshot increment is also complete: `writeBmp32` preserves
the framebuffer dimensions and RGBA8 pixels in a development-only 32-bit BMP, and
`AuditSession` can place named captures inside its session directory. Desktop capture,
automatic checkpoints, headless presentation and the playtest runner remain deferred.

The headless platform increment is now complete. `HeadlessAuditPlatform` exercises
the existing `Platform` boundary with deterministic caller-controlled time, scripted
logical input, debug input, injected image decoding, operational logs and real
framebuffer reception. It intentionally does not run a game loop or define scenario
assertions; those belong to Blocks D and later.

## Audit/playtest portability track — Block D

`playtest_runner` now runs the real `GameRuntime` through
`HeadlessAuditPlatform`, injects `InputState` by tick, renders the real logical
framebuffer and checks gameplay through `GameAuditSnapshot`. It creates independent
audit sessions, checkpoints and failure screenshots, and returns non-zero when an
assertion fails. The initial scenarios cover startup, movement, collision, combat,
pickups, inventory, objects, map transitions, save/load and NPC dialogue. Quest
names remain startup smoke entries until a real player-facing quest-start command is
available. Asset-root selection is supported; a synthetic decoder is used only for
portable runs without local licensed assets.

## Audit/playtest portability track — Block E and Docker preparation

Block E is implemented in the Win32 game loop: `--audit` enables a manual
`AuditSession`, and F12 requests a logical-framebuffer capture together with a
read-only `GameAuditSnapshot`. Important gameplay changes observed at that
boundary produce structured events, forced state checkpoints and screenshots.
Windows execution remains a host-dependent validation gate and was not run on the
current Linux/WSL host.

`docker/build_linux.sh` now provides a reproducible Debian container for the
Linux targets (`game`, `tests` and `playtest_runner`). `docker/build_windows.ps1`
and `docker/Dockerfile.windows` provide the corresponding Windows-container recipe
for the existing MSVC `build.bat`; it can only run on a Windows Docker daemon.
The Linux graphical runtime implementation is now present in the working tree;
formal replay and state hashing remain optional audit tooling; networking and multiplayer are out of scope.

## Linux graphical runtime — Block G

The Linux runtime boundary is implemented with X11 presentation, platform-neutral
input mapping, monotonic time and libpng-based RGBA8 image loading. The Docker
Linux image builds `build/linux/game` using the same gameplay and software renderer
as the other targets. No Linux-specific gameplay or renderer path was created. A
graphical smoke requires an X11/WSLg display; formal replay, seeded stress
playtesting remains iterative; networking is out of scope.

### Content Definition Boundary — DONE

O conteúdo authored é representado por DTOs tipados dentro de `AuthoredContentPack`, validado com
diagnósticos estruturados e compilado em um `GameContentRegistry` imutável. O builtin
em C++ é a fonte authored default temporária; o workspace JSON externo é opt-in.
Content Studio e a autoria visual necessária ao jogo agora estão concluídos; a
evolução seguinte é orientada por necessidades concretas de produção.

### FASE 13 — External Authored Content

13A — Strict JSON Codec + Authored Content Schema v1 — DONE.
13A1 — Decoder Foundation + Core DTOs — DONE. 13A2 — Combat + World DTOs — DONE.
13A3 — Narrative + Semantic DTOs + Full Roundtrip — DONE. The strict UTF-8 JSON
codec remains separate from runtime source.

13B — External Content Workspace — DONE
13B1 — Multi-file Workspace Core + Deterministic Merge — DONE.
13B2 — Workspace Directory Discovery — DONE.
13B3 — Application/Tooling Integration — DONE.

FASE 14 — Authored World Foundation — DONE. It supersedes the earlier Content Studio
placeholder: authored maps, runtime regions, world rules, stateful doors and encounter
foundations are implemented before any Studio work. Replay/state hashing are optional
testing/debug tooling; networking and multiplayer remain permanently out of scope.

The explicit loader sorts caller-provided source paths, rejects duplicate definitions
without overrides, preserves source provenance, and validates the merged pack through
the existing compiler. 13B2 discovers regular `.json` files recursively; 13B3 shares
the source bootstrap with Game, Map Maker and the headless `content_check` tool. No
manifest is required at this stage.

### Authored world source boundary

`UMAP v3` is the authored map source. It is decoded strictly into an
`AuthoredMapSource`, compiled into a fresh `MapData`, and serialized as `DMAP 1.4`.
`REGN`, `WRLD` and `ENCT` are compiled runtime chunks. `DSAV 1.8` persists the
session-side door, world-rule, encounter and persistent-toggle state while remaining
compatible with DSAV 1.6 and 1.7. The Map Composer remains an initial-map generator;
it does not regenerate manual authored geometry after a `.umap` is opened.

### Phase 14 — Authored World Foundation — DONE

14A — Authored Map Source — DONE.
14B — Runtime Regions — DONE.
14C — World Logic Foundation — DONE.
14D — Stateful Doors — DONE.
14E — Encounter Foundation — DONE.

The full Content Studio, puzzle engine, encounter waves, boss phases and networking
remain outside this increment. Content Studio 18A through 18D are closed; the single
MAP/CONTENT shell now provides the unified authored workflow.
The Phase 14 closure suite and
authored arena vertical slice are verified; no later phase is started here.

### Phase 15 — Presentation Feedback Foundation — DONE

Phase 15 is the presentation-only feedback boundary. It does not create gameplay
status effects and it does not make presentation state authoritative:

```text
authoritative gameplay/events
        ↓
PresentationEffectRequested / feedback controller
        ↓
PresentationEffectSystem
        ↓
GamePresentation / framebuffer
```

15A — Presentation Effect Runtime — DONE. The fixed-tick runtime supports keyed
transient effects, source-tracked persistent effects, deterministic camera shake,
world/final overlays, player-relative vision masks and linear fades.

15B — Authored Presentation Effects — DONE. Presentation effects were the twentieth
authored content category. Content JSON v3 introduced them; Phase 16 evolved the
emitted schema to v4 for object activation, and Phase 17 evolved it to v5 for visual
definitions while readers remain compatible with v1–v4. Builtin is still the
transitional default source and workspaces may mix all readable schema versions.

15C — Presentation Cue Integration — DONE. Player damage and authored world-rule
presentation cues reach the presentation layer through `SimulationEvent`; the world
`EffectSystem` remains responsible for world-space animated VFX.

15D — Environment / World Integration — DONE. UMAP v2 introduced optional region
environment effects and presentation actions; the current writer emits UMAP v3 and
DMAP 1.4, while readers retain UMAP v1/v2 and DMAP 1.0–1.3 compatibility. DSAV 1.8
does not persist transient or derived presentation state.

The next architectural decisions after Phase 15 were deliberately delivered in this
order:

```text
Phase 17 — Visual Content Boundary (now complete)
Phase 18 — Content Studio
```

Status effects, poison/blindness gameplay, audio, scripting, GPU post-processing,
networking and multiplayer remain out of scope.

### Phase 16 — Interactive World Components — DONE

Phase 16 extends the authored world foundation with reusable object activation
capabilities. It does not create a Puzzle Engine: `WorldObjectDefinition` provides
the capability, `GameSession` evaluates the concrete interaction/occupancy, and the
existing `EventBuffer` and `WorldLogicSystem` compose the result into doors, flags,
encounters and presentation cues.

```text
WorldObjectDefinition
        ↓ activation capability
WorldObjectInstance
        ↓
ObjectActivationChanged
        ↓
WorldLogicSystem
        ↓
door / flag / encounter / presentation
```

16A — Stateful Object Activation — DONE. `interactToggle` is shared by lever and
switch-like objects, while `playerPressure` uses only the Player feet position in
authored object order. Toggle state is persistent; pressure state is derived and is
not saved. No pushable blocks, weights or other actors activate plates yet.

16B — World Logic Integration — DONE. `objectActivated`/`objectDeactivated` triggers
and `objectActive`/`objectInactive` conditions target persistent object instances and
are processed in the same bounded event cycle as the existing world rules.

16C — Persistence + Authored Content — DONE. Content JSON v4, UMAP v3, DMAP 1.4 and
DSAV 1.8 carry activation definitions, authored rules and persistent toggle deltas;
readers retain the preceding compatible versions. Pressure activation remains a
runtime derivation from the restored Player position.

16D — Puzzle Vertical Slice — DONE. The headless interactive-world scenario proves a
two-switch authored AND composition, a pressure-controlled door, deterministic
activation events, collision changes and toggle save/load without map-specific C++.

The next architectural decisions remain deliberately separate:

```text
Phase 17 — Visual Content Boundary (now complete)
Phase 18 — Content Studio
```

Status effects, complex puzzle components, encounter waves, boss phases, audio,
scripting and networking remain outside the current product direction.

### Phase 17 — Visual Content Boundary — DONE

Phase 17 moves gameplay-owned visual references across the existing authored content
boundary. Content JSON v5 adds `visualImages`, `staticSprites`, `animations`,
`enemyVisuals` and `objectVisuals`; readers remain compatible with v1–v4. The
definitions contain IDs and authored metadata only. `VisualContentLoader` resolves
explicit `gameAssets` or `contentWorkspace` roots, decodes each image once, validates
source rectangles and constructs immutable runtime clips/catalogs before rendering.
External content can therefore supply new enemy, object, NPC, projectile, pickup and
item visuals without branches in `GameRuntime`.

Creature visual profiles are deliberately flexible. `idle` is the only required
binding; optional move/hurt/death/dead states and arbitrary visual action IDs support
single-frame, partial-directional and directional profiles. Exact direction, explicit
default and deterministic available-direction fallback are applied by the loader;
missing optional visuals fall back to idle and do not disable gameplay. Runtime
animators remain per-instance mutable state while clips/images are shared.

`EffectSystem` remains the world-space animated VFX system. The presentation feedback
system remains responsible for camera/screen effects, and this visual-content loader
is a separate content-to-runtime asset boundary. Player visuals, HUD/font assets,
tileset loading and generic impact VFX remain fixed game presentation assets by
explicit Phase 17 scope; automatic asset importing, hot reload, status effects and
audio remain outside the current product direction.

The current format status is:

```text
Content JSON v5  (reader v1–v5)
UMAP v3          (unchanged)
DMAP 1.4         (unchanged)
DSAV 1.8         (unchanged)
```

### Phase 18 — Content Studio — DONE

18A — Content Studio Foundation — DONE. The existing Map Maker shell now exposes MAP
and CONTENT modes. `ContentWorkspaceDocument` preserves individual source JSON files,
definition ownership, independent dirty state and derived merge/validation/compile
results. It provides typed authoring operations for visual images, static sprites,
animations and flexible enemy visual profiles, including frame/marker editing and
Slime-like idle-plus-death profiles. Save All writes only dirty files as canonical v5
JSON using atomic replacement; semantic errors remain editable and saveable but block
the derived registry and playtest. Static sprite source rectangles and all animation
frame fields are editable through the typed inspector; explicit validation also
reports VisualContentLoader asset/decode/bounds diagnostics without changing Content
JSON v5.

The browser lists all twenty-five categories in deterministic order; all categories
have typed authoring paths after 18D. Builtin content is read-only and an explicit
`--content` workspace is editable. The unified shell preserves the existing MAP mode
and keeps both map and content documents alive when switching modes.

18B — Visual Asset + Animation Authoring — DONE. The CONTENT mode now uses a shared
secure visual asset resolver and a lazy preview cache. Visual images can be viewed
with nearest-neighbor FIT/1x/2x/4x/8x zoom, pan, grid snapping and bounded mouse
rectangle selection. Static sprites and animation frames expose source overlays;
animations reuse the runtime `AnimationClip`/`Animator` path for play/pause/restart,
frame stepping, markers and authored anchor/drawOffset playback. Flexible enemy,
object and NPC visual profiles have typed binding/state inspectors, including
optional states, arbitrary visual actions, directional fallback and NPC marker
fallback. Preview ticks come from a dedicated editor timer and are never driven by
paint frequency. Grid/viewport state is editor-only and is not serialized.
18C — Gameplay Content Editors — DONE. Typed gameplay inspectors cover the
runtime-content categories.
18D — Unified Content/Map Workflow — DONE. The MAP perspective now exposes persistent
tileset/semantic/stamp/entity palettes, deterministic multi-tile brushes, layer
commands with undo/redo, and navigation between map placements and CONTENT
definitions. Tilesets, authoring descriptors, tile semantics and stamps have typed
document mutations and inspectors. Current content revisions invalidate map validation;
compile/export/playtest never fall back silently to builtin or stale registries.

After Phase 18, the focus is Content Studio production tooling and concrete game
content production. The immediate direction is a multi-map authored world project,
better daily authoring UX and in-memory project playtest. Asset import, automatic
slicing, complex timeline editing, hot reload, node graphs, generic scripting, audio,
procedural generation, semantic solvers, LLM authoring and networking are not part of
this direction.

The Content Studio shell also has an editor-only localization boundary. `EditorPreferences`
stores the selected `EditorLanguage` outside authored JSON/UMAP/DMAP/DSAV, defaults to
`pt-BR` and persists `pt-BR`/`en-US` between runs. `EditorLocalization` owns the typed
`EditorTextId` catalog and the Win32 Settings > Language menu; switching language rebuilds
only the native menu and presentation state, never authored documents. Portuguese UI text
uses UTF-8-safe editor input and the bitmap font's small Latin accent extension. This is
tool localization only: game content, IDs, dialogue and runtime language remain unchanged.

### Próximo foco — Content Studio production tooling

Não há uma sequência obrigatória posterior de fases sem necessidade concreta. O foco
após a Fase 18 é tornar o Content Studio a ferramenta de produção do jogo: um projeto
`.uworld` mantém vários mapas authored na ordem estável, `WorldProjectDocument` mantém
um `EditorDocument` vivo por mapa, e links entre mapas são validados antes de compile
e playtest. O editor preserva UMAP standalone e exporta DMAP individual por mapa.

O playtest compila o projeto atual em memória, inicia no mapa ativo e reutiliza
`MapSession` para atravessar links. Preferências de tooling, layout redimensionável,
browser de mapas, navegação de usos e diagnóstico contextual são incrementos de
produção; novas features de gameplay entram somente quando houver necessidade real.
O primeiro incremento de conteúdo concreto após esse foco adiciona props ambientais
quebráveis (crate, vase, stone block variants e fire block) por definitions,
destructible/activation capabilities e estados visuais genéricos, sem criar uma nova
fase ou sistema específico por prop.

O incremento atual aprofunda esse mesmo tooling: coleções authored têm seleção
individual e remoção segura, referências conhecidas usam picker/quick inspect e o
workflow Quest → Reward Grant pode criar e ligar uma recompensa. Isso é uma melhoria
contínua do Content Studio production tooling, não uma nova fase obrigatória.

O workflow de entidades recebeu a mesma separação de autoria: `AuthoredEntityIndex`
descobre Enemy/NPC/Object/Pickup diretamente do workspace, inclusive quando outra
definição contém erro sem relação. A palette MAP → Entities agora tem categorias,
scroll, busca por nome/ID, origem Project/Engine, validação local, placement repetido
e ghost/placeholder; `PlaceEntityCommand` continua cuidando da mutação e do
undo/redo. `GameContentRegistry` permanece reservado para compile/export/playtest e
runtime, que continuam recusando conteúdo inválido. O editor também não escolhe mais
um inimigo builtin implicitamente: mapas novos são blank por padrão e Player Spawn é
uma opção explícita do diálogo, não um conteúdo de gameplay oculto.

### 12C1 — Equipment domain + derived stats — DONE

Armor and accessory equipment, typed modifiers, derived health/attack stats and
DSAV persistence are implemented. Equipment UI and weapon replacement remain future work.

### 12C2 — Equipment UI / inspection / unequip — DONE

Inventory and equipment focus, slot inspection, equip/unequip commands and derived-stat
presentation are implemented. Bank storage is covered by 12D1; access and UI remain future work.

### 12D1 — Bank storage + persistence — DONE

`PlayerBank` provides 50 reusable `ItemContainer` slots and separate persistent gold.
Item/gold transfers are bounded and lossless, bank state survives map transitions, and
DSAV 1.5 persists it through the `BANK` chunk.

### 12D2 — Bank access + UI — DONE

Bank access is exposed through an authored/compiled world-object capability and opens
a dedicated inventory/storage/gold overlay backed by the global `PlayerBank`. Transfers
operate on selected slots, gold uses Deposit All/Withdraw All, and the modal pauses
gameplay. Official placement and final art remain level-design/content tasks.

### 12E1 — Guaranteed reward grants + quest rewards — DONE

Guaranteed `RewardGrantDefinition` data is separate from probabilistic defeat rewards.
Quest completion persists an exactly-once reward claim state; item rewards use Inventory
then Bank atomically, while XP and Gold are applied directly. Values are provisional
development balance. Shop definitions/transactions (12E2) and shop access/UI (12E3)
remain future work.

### Phase 12 — RPG / XP / Equipment / Loot / Bank — EXTENDED / IN PROGRESS

The previous RPG scope was extended with 12E1 before external authored content work.
No shops are implemented yet; no Bank placement or new art is added to the official
DMAP in this increment.

### 12E2 — Shop definitions + transactions — DONE

Shops are immutable authored content with independently optional Player buy/sell
prices. Transactions are headless, one item at a time, atomic, use carried Wallet
Gold only, and place purchases in Inventory only. There is no stock, buyback or shop
persistence; access and UI are provided by 12E3.

### 12E3 — Shop access + UI — DONE

Dialogue choices can open a shared authored ShopDefinition through GameSession.
The transient BUY/SELL overlay routes one-item transactions and displays carried
Gold, prices and typed feedback. Official Merchant placement and final art remain
content tasks.

### Phase 12 — RPG / XP / Equipment / Loot / Bank — COMPLETE

The planned RPG block through quest rewards, bank access and shop access is complete.
The authoritative GameSession boundary is already implemented; replay and state
hashing remain optional deterministic-testing/debug tooling, not a required phase
gate. The next development focus is External Authored Content Format:
content files -> loader -> AuthoredContentPack -> validator -> compiler -> registry,
followed later by Content Studio foundation and document editors. Phase 12E2 and
12E3 are DONE; networking remains out of scope.

### World Object Persistence — production tooling increment

WorldObject placements now author an `ObjectPersistencePolicy` in the MAP workflow.
The default is `persistent`, preserving existing maps and save behavior; an explicit
`resetOnMapEnter` placement is rebuilt from its authored state after the corresponding
`RuntimeWorld` is unloaded. This is intentionally scoped to WorldObject instances,
keeps `playerPressure` derived, and does not start Phase 19 or generalize persistence
to enemies, NPCs, or entities.

### Map-authored scenes — production tooling increment

Map-local scenes are serialized through `AuthoredMapSource`/`MapData` and DMAP 1.5's
optional `SCNE` chunk. `WorldLogic` starts them only through the existing
`WorldActionKind::startScene` action. Legacy UMAP v1–v3 and DMAP v1.0–1.4 remain
readable with no scenes and persistent object placements by default; DSAV 1.8 does
not persist an active scene timeline.
