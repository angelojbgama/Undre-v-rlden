# Arquitetura do Dungeon Underworld

Este documento descreve as **invariantes arquiteturais** e a direção de evolução do Dungeon Underworld / Undre-v-rlden.

Ele não deve funcionar como fotografia exata do último commit. O estado real de uma tarefa é determinado primeiro pelo working tree local, pelos testes e pelo comportamento executável. A arquitetura abaixo define **como os sistemas devem se encaixar** à medida que novas capacidades forem introduzidas.

As decisões detalhadas de ordem e escopo pertencem ao `ROADMAP.md`; regras operacionais para agentes pertencem ao `AGENTS.md`.

---

## 1. Princípios centrais

### 1.0 Baseline implementada

O commit de código validado da Fase 6 é
`4fa4474a770f5c8e195d51161cb69c38277c4e99`, com MSVC 2022 x64,
C++20, `/W4`, 0 warnings e 296 checks.

Além das bases das Fases 0–4, o código já contém:

```text
ActionEdgeBuffer
PlayerCommand com ActionIntent
EntityHandle(index, generation) + EntityHandlePool
Health / Faction
CollisionBody / Hurtbox / Hitbox / InteractionArea
AttackInstanceId + CombatSystem
EventBuffer
EntityDamaged / EntityDefeated / ProjectileImpact
ProjectileSystem / EffectSystem
animation markers
actor Y-sort
DefinitionId + catálogos imutáveis
AttackKey(owner, localAttackInstance)
CombatantState + CombatTargetRef/CombatResolution
AttackDefinition + ProjectileDefinition
EnemyDefinition + EnemyInstance + EnemyFactory
BehaviorProfile + FSM Idle/Wander/Chase/Attack/Dead
EnemyVisualSet + EnemyVisualInstance
Evil Soldier melee + Skull ranged
```

<!-- Registro anterior ao fechamento: a Fase 7 foi implementada nos commits `30b413d`, `ccbaf4a`, `7873e32` e `0721b12`,
com 340 checks portáveis, mas ainda aguarda build MSVC `/W4` e smoke Win32 para ser
declarada concluída. `.dmap`, editor, save, loot/XP, NPCs e quests
permanecem deferidos. -->

A Fase 7 está concluída. O código-base está em
`7873e32222b1e8d72e996771a88a6890b0eb9220` e o fechamento Windows/fix em
`7cc9da495d314de52ab097f890594dd7deb2d0a4`. A baseline validada usa Windows 11
x64, MSVC 19.44.35219 (toolset da linha Visual Studio 2022), Windows SDK
10.0.26100.0, C++20, `/W4`, 0 warnings e 347 checks, com `git diff --check` e
smoke visual/interativo passando.

A Fase 8 também está concluída: DMAP 1.4/DSAV 1.8, `MapData`, persistent IDs,
`RuntimeWorldBuilder`, `MapCatalog`, `MapSession`, transições e deltas de sessão/save
alimentam o slice jogável com os mapas authored atuais. A Fase 9 (Map Maker) foi fechada
no checkpoint Linux atual: o editor possui validação revision-cached, culling de tiles,
playtest por snapshot via `RuntimeWorldBuilder` e sidecar `.autosave.dmap` sem substituir
o arquivo authored. A Fase 10 está concluída; NPC foundation e dialogue data model
estão concluídos, assim como sessão/UI, conditions, actions e flags persistentes. A
Fase 11 possui definições, estado runtime, progressão por eventos e persistência de
quests; loot/XP e rewards também estão implementados nas fases seguintes. Networking
e multiplayer estão fora de escopo.

### 1.1 C++ nativo e dependências controladas

- C++20;
- Standard Library;
- Windows SDK;
- Win32/WIC/GDI apenas nas bordas de plataforma quando necessário;
- nenhuma biblioteca de terceiros sem decisão explícita.

O projeto deve continuar implementando suas estruturas principais em código próprio.

### 1.2 Renderer próprio em software

Contrato visual canônico:

```text
logical framebuffer = 272 × 224
internal pixel format = RGBA8
presentation = Win32
scaling = integer
filtering = nearest-neighbor
unused client area = letterbox
```

O renderer não conhece regras de gameplay.

### 1.3 Fixed timestep

Gameplay evolui em ticks fixos de 60 Hz.

Renderização, velocidade da janela e frequência de eventos físicos não determinam a velocidade da simulação.

### 1.4 Sistemas pequenos antes de frameworks grandes

Preferir:

- estado runtime explícito;
- definições imutáveis quando houver conteúdo repetível;
- IDs estáveis;
- composição;
- sistemas pequenos;
- extrações feitas após casos reais demonstrarem a necessidade.

Evitar:

- `GameObject` universal;
- classes Deus;
- hierarquias profundas;
- ECS sofisticado criado antecipadamente;
- interfaces vazias para um futuro hipotético;
- managers globais que conhecem tudo;
- subsistemas paralelos que duplicam capacidades existentes.

---

## 2. Níveis de decisão arquitetural

Para não transformar ideias futuras em obrigações prematuras, este documento diferencia três níveis.

### Invariante

Regra que não deve ser quebrada sem decisão arquitetural explícita.

Exemplos:

```text
Win32 não entra em gameplay.
Renderer não modifica gameplay.
Gameplay usa fixed timestep.
Collision de mapa não é duplicada por Player/Enemy.
Map/save não persistem ponteiros ou handles runtime.
```

### Direção arquitetural aprovada

Estrutura considerada correta para uma capacidade futura, mas cujo detalhe deve ser implementado apenas quando existir uso concreto.

Exemplos:

```text
CreatureDefinition + runtime state + BehaviorProfile
EntityHandle + PersistentInstanceId + DefinitionId
Map Maker compartilhando runtime/serialization
scene/game-state layer mínima quando houver múltiplos estados reais
```

### Planejado / deferido

Capacidade desejada cuja forma ainda pode amadurecer.

Exemplos:

```text
scripting
pathfinding avançado
hot reload
ferramentas adicionais de diagnóstico determinístico
multithreading do renderer
formato final de diálogo/quest
```

---

## 3. Fluxo principal

Fluxo conceitual atual e futuro:

```text
physical events
    ↓
platform InputState / edge events
    ↓
CommandBuilder
    ↓
PlayerCommand / action commands
    ↓
fixed simulation tick
    ↓
gameplay state + world state
    ↓
domain events / read-only render state
    ↓
visual composition + Camera2D
    ↓
Renderer2D
    ↓
Framebuffer RGBA8
    ↓
Win32 presentation
```

`Simulation` neste documento é uma **fronteira conceitual**, não a obrigação de existir uma classe monolítica chamada `Simulation`.

O jogo pode continuar com objetos e sistemas explícitos enquanto isso for suficiente. A abstração só deve crescer quando o número de consumidores reais justificar.

Input físico não chama diretamente uma função de movimento do Player. O renderer não aplica dano, não decide IA, não cria loot e não altera estado persistente.

### 3.1 Composição transitória do runtime

O executável ativo usa `GameRuntime` como composition root da aplicação. Ele coordena
bootstrap, input, filesystem, auditoria e apresentação, mas delega o estado
autoritativo de gameplay para `GameSession` e a composição visual para
`GamePresentation`.

`GamePresentation` possui a câmera e os passes visuais do mundo, atores, projéteis,
efeitos, HUD, diálogo, inventário e debug. Ele recebe uma view somente-leitura do
estado necessário para desenhar; não possui acesso mutável irrestrito ao runtime nem
autoridade sobre gameplay.

Historicamente esta foi uma etapa intermediária intencional:

```text
GameRuntime
    ├── gameplay/runtime state ainda interno
    └── GamePresentation
```

O fechamento descrito abaixo concluiu a extração da simulação autoritativa para
`GameSession` dirigida por `PlayerCommand`. Isso não conclui headless/replay.

### GameSession — fronteira autoritativa concluída

`GameSession` já fornece uma fronteira de simulação sem dependências de renderer,
plataforma, decoder ou assets gráficos. Ela possui o Player, o `EventBuffer` e a
sessão lógica do mapa (`MapSession`, `RuntimeWorld` e `SessionWorldState`). Seu
`tick` recebe somente `PlayerCommand`, resolve internamente a colisão e o tile size
do mapa ativo, emite `MapEntered` após transições e agora também coordena a autoridade
de combate, criaturas, projéteis, objetos, pickups e inventário em fixed ticks.
Isso inclui Wallet, Quick Slots, interação lógica de objetos e o timer de destruição
de objetos; a conclusão de destruição nunca depende de `Animator::finished()`.
`GamePresentation` apenas observa esses estados para desenhar. A Session também
possui `DialogueSession`, flags, `QuestStateStore` e `QuestSystem`: diálogo é roteado
antes da simulação normal, escolhas produzem ações concretas e a progressão consome
o `EventBuffer` uma vez por tick. `GameRuntime` continua compondo filesystem e
apresentação, mas não possui aliases mutáveis para o estado da Session. `GameSession`
é dona do `EntityHandlePool`, aloca a identidade runtime das entidades e expõe apenas
views constantes para presentation/audit. Captura e restauração lógica de save são
operações específicas da Session; filesystem e escrita atômica continuam sendo
responsabilidade da aplicação. Restauração faz rollback internamente, fecha diálogo
e limpa transientes antes de devolver o controle ao Runtime. Nenhum renderer, asset
ou dependência de plataforma pode entrar em `GameSession`.

### RPG — Rewards and Loot (12B)

Criaturas nunca concedem recompensas diretamente. `EnemyDefinition` referencia um
`RewardProfileDefinition`; após `EntityDefeated`, `GameSession` resolve o perfil com
`RewardResolver`, aplica o XP e cria pickups de loot. O resolver é lógica pura,
determinística e usa o mapa e a instância persistente como contexto. Ouro continua
sendo um pickup físico antes de entrar na Wallet. Drops de inimigos são transitórios,
não entram em DSAV/DMAP e são descartados em transições; `QuestSystem` observa o mesmo
evento de derrota independentemente.

### RPG — Progression Foundation (12A)

Progressão do Player é estado autoritativo da `GameSession`. Os base stats usados
atualmente originam-se de conteúdo authored compilado; `Player` não possui mais uma
constante concreta de maximum health. XP é armazenado como total cumulativo em
`uint64_t`, com acumulação saturada, e o level é derivado deterministicamente da
curva compilada. Criaturas não concedem XP diretamente: `EntityDefeated` continuará
sendo consumido por um futuro sistema de rewards, que permanece separado desta
fundação. O save persiste o ID da definição de progressão e o XP total, não um level
independente; a curva builtin atual é provisória para desenvolvimento.

---

## 4. Módulos e dependências

A direção estrutural é:

```text
core
  ↑
platform contracts / render primitives / simulation primitives
  ↑
world
  ↑
game gameplay
  ↑
game composition
```

Win32 permanece na borda.

### `engine/core`

Responsável por:

- tipos básicos;
- geometria;
- coordenadas;
- métricas;
- IDs genéricos quando necessários;
- utilidades pequenas;
- tipos de erro/result quando úteis.

Não conhece:

- Win32;
- gameplay concreto;
- renderer concreto.

### `engine/platform`

Responsável por contratos de:

- janela;
- message pump;
- relógio;
- input físico;
- arquivos quando necessário;
- decoder de imagem;
- apresentação.

Headers públicos evitam `windows.h` sempre que possível.

### `engine/platform/win32`

Implementa a borda Windows:

- `HWND`;
- eventos;
- QPC;
- WIC;
- DIB/GDI;
- DPI;
- mapeamento de teclas físicas para estado neutro.

Gameplay nunca recebe `VK_*`, `HWND` ou WIC.

### `engine/render`

Responsável por:

- framebuffer;
- imagens;
- clipping;
- alpha blending;
- sprites;
- bitmap font;
- animação visual;
- câmera;
- composição/desenho.

O renderer consome estado visual. Ele não possui autoridade de gameplay.

### `engine/assets`

Responsável por:

- `AssetId`;
- ownership/cache de imagens;
- catálogo e metadados visuais conforme a necessidade crescer;
- resolução de referências de assets.

Assets são carregados uma vez por ownership/cache, não a cada frame.

### `engine/world`

Responsável por:

- `RuntimeMap`;
- tile layers;
- collision grid;
- consultas espaciais;
- colisão contra o mapa;
- futuramente regiões, links e transições.

Player, criatura e Scene não criam outro tile collision system.

### `engine/simulation`

Contém somente primitivas genéricas realmente necessárias ao tick/simulação.

Exemplos atuais ou próximos:

```text
Tick
PlayerId
CommandSequence
MovementIntent
PlayerCommand
EntityHandle          # quando combate/múltiplas entidades exigirem
DomainEvent primitives
```

Não deve se transformar antecipadamente em uma classe que possui Player, mapa, câmera, renderer, assets e janela.

### `game/gameplay`

Contém regras concretas do jogo:

- Player;
- combate;
- criaturas;
- objetos;
- pickups;
- inventário;
- NPCs;
- quests;
- progressão.

Gameplay pode depender de primitivas da engine e do world, mas não de detalhes Win32/WIC.

### `game`

Faz composição de dependências e coordenação do executável.

`game.cpp` não deve virar depósito de todas as regras.

### `engine/serialization` — quando a fase exigir

Responsável por:

- readers/writers bounds-checked;
- DTOs de disco;
- `.dmap`;
- save;
- migrações;
- validação estrutural.

Nunca serializa dump cru de structs runtime.

### `editor` — somente na fase do Map Maker

Compartilha:

- renderer;
- assets;
- world/map;
- definitions;
- serialização.

Mantém separado:

- `EditorDocument`;
- seleção;
- ferramentas;
- undo/redo;
- dirty state;
- UI do editor.

O runtime nunca depende do editor.

---

## 5. Loop, tempo e comandos

O relógio Win32 usa alta resolução. O loop acumula tempo real e executa ticks inteiros de 1/60 s.

Conceitualmente:

```text
pollEvents()
frameDelta = clamp(now - previous)
accumulator += frameDelta

while accumulator >= fixedDt and catchUpBudgetAvailable:
    command = commandBuilder.build(nextTick, input)
    tickGameplayAndWorld(command)
    accumulator -= fixedDt
    ++nextTick

composeVisualState()
render()
present()
```

`Sleep` pode ser usado apenas para economia de CPU; nunca dirige a simulação.

Um limite de frame delta e catch-up evita spiral of death.

### Input

`InputState` representa intenção física neutra, por exemplo:

```text
moveUp
moveDown
moveLeft
moveRight
```

Ao perder foco, estado held deve ser limpo para impedir movimento preso.

Ações com edge (`attack pressed`, `interact pressed`, etc.) devem ser preservadas até o tick que as consumir, sem depender de o render frame coincidir com o tick.

### PlayerCommand

A fronteira mantém informação suficiente para testes, replay futuro e eventual autoridade remota:

```text
tick
player identity
sequence/order
movement intent
action intent quando introduzido
```

Movimento contraditório é resolvido deterministicamente:

```text
Left + Right = 0
Up + Down    = 0
```

Não se promete determinismo bit a bit entre máquinas. Inteiros/subpixels são preferidos para estabilidade do gameplay atual; replay e state hashing podem ser usados para testes e auditoria.

---

## 6. Coordenadas e pixel-art

Fronteiras conceituais:

```text
WindowPx
LogicalPx
WorldPx / WorldSubpixel
TileCoord
ViewportPx
```

`GameMetrics` centraliza valores como:

```text
logicalWidth  = 272
logicalHeight = 224
tileSize      = 16
tickRate      = 60
```

Conversões são explícitas. Não espalhar magic numbers.

A posição conceitual de personagens é o ponto dos pés.

Anchor visual, draw offset, collision body, hurtbox e hitbox são conceitos diferentes.

Para o Player idle/walk, a auditoria de assets fornece como referência inicial:

```text
frame  = 32 × 32
anchor ≈ (16, 31)
```

Valores inferidos de asset continuam sujeitos a validação visual.

---

## 7. Renderer e assets

### Framebuffer

`Framebuffer` usa RGBA8 canônico.

Operações devem manter clipping e validação de source/destination.

O apresentador Win32 converte para o formato necessário da DIB somente na borda.

### Render order

A composição deve evoluir para passes explícitos, sem misturar regra física:

```text
ground/background
decoration low
entities/objects
foreground/occluders
VFX
HUD/UI
```

Quando Y-sort for realmente necessário, ordenar por baseline/pés com desempate estável.

### Animation

Estrutura conceitual:

```text
SpriteSheet
AnimationClip
AnimationFrame
Animator
```

Frames podem carregar metadados/markers.

Markers como:

```text
attack_on
attack_off
spawn_projectile
```

são metadados de apresentação. Eles podem alimentar VFX ou áudio futuros, mas não
governam dano, hitboxes, projéteis ou o término de ataques.

O timing semântico de ataques pertence às definições de gameplay e avança em ticks
fixos por `AttackExecution`. A execução emite eventos de timeline diretamente para
o combate/projéteis, enquanto `PlayerVisual` e `EnemyVisualInstance` apenas
observam o estado da ação e reproduzem a animação correspondente.

Não resetar clip todo tick nem reconstruir definição de animação a cada frame.

---

## 8. World e Map Engine

A base de mapa é única e evolutiva:

```text
RuntimeMap
TileLayer
CollisionGrid
Camera2D
tile culling
AABB collision
```

Novas capacidades estendem essa base.

Não criar:

```text
Map2
LevelManager paralelo
collision grid exclusivo de Player
collision grid exclusivo de Enemy
```

Evolução prevista:

```text
múltiplas layers
spawns
map entities
triggers
regions
doors
links
transições
persistent IDs
```

Arte e colisão permanecem separadas.

`Camera2D` recebe um target/posição calculada; Player não possui Camera.

---

## 9. Gameplay runtime e evolução de entidades

### 9.1 Estado atual de complexidade

Enquanto Player é o único ator real de gameplay, uma classe pequena e explícita é preferível a um ECS antecipado.

O Player pode concentrar somente estado próprio, por exemplo:

```text
identity
world/subpixel position
facing
motion state
movement configuration
collision body information
```

Ele não possui janela, teclado físico, renderer, imagem, WIC ou câmera.

### 9.2 Quando introduzir identidade runtime genérica

Combate cria o primeiro caso concreto de múltiplos participantes e relações source/target.

Nesse momento é aceitável introduzir a menor identidade runtime compartilhada necessária, preferencialmente:

```text
EntityHandle(index, generation)
```

Objetivos imediatos:

- identificar atacante e alvo;
- invalidar referências após destruição/reuso;
- suportar deduplicação de hits;
- preparar criaturas e objetos sem exigir registry de componentes completo.

Introduzir `EntityHandle` **não implica** criar imediatamente:

```text
ComponentArray
Archetype
SparseSet
SystemManager genérico
ECS completo
```

### 9.3 Três identidades futuras

Não confundir:

```text
EntityHandle(index, generation)       # runtime/sessão
PersistentInstanceId(mapId, localId) # mapa/save
DefinitionId                         # tipo/conteúdo
```

`PersistentInstanceId` e `DefinitionId` entram quando mapas, spawns, definitions e save precisarem deles.

### 9.4 Componentização incremental

Quando Player + criaturas + objetos repetirem dados e operações reais, podem surgir componentes simples como:

```text
Transform
Facing
Visual / Animator
CollisionBody
HurtboxSet
Health
Faction
AIController
Pickup
Projectile
Persistent
```

A extração deve seguir uso comprovado.

Componentes são dados; sistemas operam sobre capacidades necessárias.

Não transformar isso em um framework ECS por vaidade técnica.

---

## 10. Colisão e combate

Manter semanticamente separados:

```text
CollisionBody    # bloqueia movimento
Hurtbox          # recebe dano
Hitbox           # causa dano
InteractionArea  # uso/fala/baú
Trigger          # overlap sem resposta física
```

Sprite bounds não substituem nenhuma dessas estruturas.

### Fluxo de ataque

```text
PlayerCommand / ActionCommand
    ↓
attack state
    ↓
AttackDefinition / AttackExecution timeline
    ↓
AttackSystem / gameplay
    ↓
activate hitbox ou spawn projectile
    ↓
CombatSystem
    ↓
damage resolution
    ↓
domain event
```

Cada execução de ataque recebe `AttackInstanceId` ou equivalente.

Um alvo não pode receber múltiplos hits do mesmo swing por permanecer dentro da hitbox.

### AttackDefinition

Quando houver mais de um ataque real, dados reutilizáveis podem assumir forma semelhante a:

```text
AttackDefinition
    type
    range
    damage
    cooldown
    startup/recovery
    hitbox or projectile definition
    animation clip / markers
    knockback
    optional selection conditions
```

A definição não aplica dano sozinha.

### Projectile e VFX

Projéteis possuem estado runtime leve e usam gameplay/world collision.

VFX são transitórios e não são entidades persistentes por obrigação.

---

## 11. Engine de criaturas — implementada na Fase 6

O objetivo é tornar a adição de monstros majoritariamente uma operação de conteúdo.

Forma atual para adicionar criaturas:

```text
register assets/clips
+ EnemyDefinition
+ BehaviorProfile
+ AttackDefinitions
+ spawn/map placement
```

### Definition x runtime

Definição imutável pode conter:

```text
EnemyDefinition
    DefinitionId
    visual / animation set
    body/hurtbox metadata
    faction
    stats
    movement parameters
    attacks
    BehaviorProfileId
```

Estado runtime contém somente mutações:

```text
EntityHandle
position
facing
health
behavior state
target
timers
cooldowns
active attack
runtime flags
```

Não copiar catálogos inteiros para cada instância.

### BehaviorProfile + FSM

Comportamentos reutilizáveis formam uma máquina de estados explícita e observável.

Estados implementados:

```text
Idle
Wander
Chase
Attack
Dead
```

Sleep, Wake, Retreat e Stunned permanecem deferidos até existir mecânica concreta.

Transições dependem de condições claras:

```text
distância
target válido
attack range
cooldown
timer
health esgotada
```

Evitar `if (enemy == ...)` espalhado pela engine.

O wander segue sequência determinística baseada no handle; chase usa distância inteira,
histerese detection/disengage e colisão existente do mapa. Não há pathfinding ou LOS.

### Dano, morte e recompensas

Dano é resolvido pelo mesmo CombatSystem usado pelo Player.

Fluxo de morte desejado:

```text
health <= 0
    ↓
Dead
    ↓
desabilitar ataque/hurtbox conforme regra
    ↓
death animation / marker
    ↓
EntityDefeated event
    ↓
loot/reward observers quando existirem
    ↓
despawn ou world delta persistente
```

Creature não atualiza diretamente HUD, quest, save ou XP do Player.

Loot/XP pertencem às fases próprias e nenhum metadata especulativo foi adicionado.

---

## 12. Objetos e capacidades

Objetos devem evoluir por definição + capacidades, não por uma subclasse profunda para cada sprite.

Capacidades possíveis:

```text
Pickup
Container
Destructible
Interactable
Door
Trigger
```

Um objeto pode combinar capacidades.

Adicionar um novo objeto não deve exigir um novo sistema se as capacidades existentes já o descrevem.

### Fundação implementada na Fase 7

```text
ItemCatalog owns ItemDefinition
ItemContainer owns optional ItemStack slots
PlayerInventory wraps ItemContainer(30)
Wallet owns uint64 gold separately
QuickSlotBindings owns 4 ItemDefinitionId bindings
WorldPickup owns runtime payload + EntityHandle
WorldObjectDefinition composes Interactable/Container/Destructible
WorldObjectInstance owns runtime container/combat state
GameViewModel copies read-only UI data
```

`ItemContainer` recebe capacidade na construção; o mesmo tipo atende inventário e
Chest e poderá atender um BankStorage de 50 slots depois que save/persistência existir.
Stack limits são resolvidos pelo `ItemCatalog`: life potion usa 66 e equipment usa 1.
Gold nunca é convertido em `ItemStack`.

`Faction::environment` permite dano Player -> Environment e bloqueia inicialmente
Enemy -> Environment e Environment -> qualquer alvo. Crate usa o mesmo CombatSystem,
EntityDefeated e lifecycle de handles das criaturas.

### Breakable environmental props

Crate, vase, stone block variants and fire block are ordinary authored
`WorldObjectDefinition` values. They reuse the existing destructible capability and
`WorldObjectVisualDefinition`; the optional generic `destroyedAnimationId` is used
for final stone/fire residue, while crate/vase disappear after their breaking clips.
No prop-specific gameplay system or `DefinitionId` branch is introduced. The fire
block's active/inactive visuals use the existing activation capability only; visual
fire does not imply hazard damage.

Completion still destroys the live `EntityHandle`, preserving generation and save
semantics. When a visual residue is authored, `RuntimeWorld` keeps only a persistent
ID, visual-set ID and position. Presentation renders that passive record without a
handle, and `ObjectDelta.destroyed` reconstructs it on save/load and map return.
This keeps runtime object lifetime, collision ownership and DSAV 1.8 unchanged.

---

## 13. Eventos, HUD, quests e observers

À medida que sistemas diferentes precisarem reagir à mesma ocorrência, usar eventos de domínio explícitos.

Exemplos futuros:

```text
EntityDamaged
EntityDefeated
ItemPickedUp
ChestOpened
RegionEntered
DialogueCompleted
```

Eventos desacoplam produtores de consumidores.

HUD observa estado somente leitura, preferencialmente via `GameViewModel` ou snapshot.

Quests consomem eventos; não fazem polling invasivo de internals todo tick.

Morte de criatura não executa diretamente UI, quest e save.

---

## 14. `.dmap`, IDs persistentes e save

Formato de mapa e save são separados.

`.dmap` deve ser:

- versionado;
- bounds-checked;
- explicitamente codificado;
- baseado em IDs estáveis;
- validado antes de construir o mundo;
- independente do ABI/layout de structs C++.

Fluxo:

```text
bytes
  ↓
structural validation
  ↓
Disk DTO
  ↓
migration
  ↓
semantic validation
  ↓
RuntimeMap / EditorDocument
```

Save guarda estado do Player/progressão necessária e **deltas persistentes** do mundo, por exemplo:

```text
chest opened
switch state
destructible removed
boss defeated
quest/world flags
```

Não copiar o mapa inteiro para o save.

Não persistir `EntityHandle`.

---

## 15. Scene / Game-State layer

Uma camada de game states será útil quando houver pelo menos dois estados reais que precisem coordenar lifecycle/input/tick/render.

Casos prováveis:

```text
menu
gameplay
pause
transition
game over
editor playtest
```

Quando surgir a necessidade, introduzir a menor interface que resolva:

```text
enter
exit
update/tick
render
input/command routing
```

Não criar agora Scene Graph universal.

Scene/GameState coordena sistemas; não substitui `RuntimeMap`, entidades, renderer ou câmera.

---

## 16. Map Maker

Recomendação:

```text
game.exe
map_editor.exe
```

Executáveis separados compartilhando engine/map/serialization.

O editor possui modelo próprio:

```text
EditorDocument
selection
dirty state
tools
EditorCommand apply/revert
undo/redo
property editing
playtest session
```

Playtest deve criar runtime a partir de uma cópia/DTO validado sem transformar o estado global do game em editor state.

Copy/paste gera novos IDs persistentes e reescreve referências internas do grupo copiado.

### Tilesets compartilhados

`GameContentRegistry` possui o catálogo puro `TilesetCatalog`. Cada
`TilesetDefinition` é metadata imutável: `DefinitionId`, display name, path relativo ao
asset root, tile size, colunas e linhas. `MapTileReference`/DMAP v1 persistem somente o
`DefinitionId`, source index e flags; `world::TilesetId` é criado deterministicamente por
`RuntimeTilesetCatalog` durante a composição e nunca é serializado.

`TilesetVisualCatalog` é a fronteira de assets carregados: resolve o mesmo runtime ID para
`Image + TileAtlasLayout`. Assim editor e game escolhem a imagem acima de `Renderer2D`,
sem colocar imagens/WIC no registry de conteúdo. O editor usa o catálogo para selector,
palette dinâmica e eyedropper; mapas podem misturar packs numa mesma `TileLayer`.
Definição desconhecida, source index inválido e tile size incompatível são erros semânticos
de `MapData`; imagem local ausente é um diagnóstico visual, nunca fallback para Dungeon.

### Semântica de autoria (Block 1)

`AuthoringSemanticRegistry` é conteúdo imutável compartilhado pelo Map Maker. Ele associa
as referências persistidas do Dungeon atlas a IDs estáveis, família visual, papel,
topologia, perfis de borda e níveis de confiança; `StampDefinition` agrupa estruturas
visuais que devem entrar pelo mesmo comando de undo/redo. A semântica não muda DMAP v1.

`MapSemanticValidator` é uma segunda passagem, posterior à validação estrutural de
`MapData`. Seus diagnósticos são somente informativos/avisos e nunca constroem collision,
transitions ou gameplay a partir de pixels. Os contratos de detalhe estão em
`TILE_CATALOG.md`, `STAMP_CATALOG.md`, `MAP_COMPOSITION_SPEC.md` e
`ENTITY_PLACEMENT_SPEC.md`.

### Composição semântica inicial

O primeiro slice da composição de mapas mantém a intenção separada dos dados concretos:

```text
MapBlueprint / RoomBlueprint
        ↓
RoomCompositionGrid
        ↓
MapComposer + AuthoringSemanticRegistry
        ↓
MapData
```

`RoomBlueprint` descreve uma sala retangular, até quatro openings (`north`, `east`,
`south`, `west`) e um `PlayerSpawn` opcional em coordenadas de tile. O compositor gera
boundary, área walkable e collision a partir de `RoomCellKind`; não deduz collision de
PNG e não transforma opening em `MapLink`. O modelo é in-memory e o resultado segue o
writer DMAP v1 existente.

`MapSemanticValidator` continua sendo advisory. `ReachabilityValidator` é uma passagem
independente de playability: um BFS no `MapData.collision` valida que o spawn alcance os
openings. Assim, as três responsabilidades permanecem separadas:

```text
validateMapData       # structural/runtime/serialization
MapSemanticValidator  # semantic visual authoring
ReachabilityValidator # playability/connectivity
```

O catálogo atual não comprova um semantic ID de piso. A composição inicial usa somente
uma definição semântica de parede comprovada/provável para a boundary e deixa o interior
sem tile visual até que a auditoria forneça evidência de piso; nenhum source index é
hardcoded e nenhum comportamento futuro de LLM é implementado.

---

## 17. Networking e multiplayer fora de escopo

Dungeon Underworld não terá networking ou multiplayer. A arquitetura não deve
introduzir replication, network authority, rollback-netcode, network identity ou
protocolos de rede. PlayerCommand, execução headless, replay e state hashing são
fronteiras úteis somente para testes determinísticos, reprodução de bugs e auditoria.

---

## 18. Ownership

Direção preferida:

```text
AssetManager
    owns Images

Animation definitions
    reference assets

Player / Creature / Object runtime
    own mutable gameplay state necessário

Animator / visual state
    own playback state

RuntimeMap
    owns map layers/collision/runtime map data

Camera2D
    owns camera state
```

Não criar ownership circular.

Definições compartilhadas são referenciadas por ID/handle estável de catálogo, não copiadas para cada runtime instance.

---

## 19. Estrutura de diretórios evolutiva

Preservar a estrutura existente e criar novas pastas apenas quando houver código real para colocá-las.

Direção:

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
    serialization/     # quando a fase iniciar
  game/
    gameplay/
    ...                 # composição/visuals/states conforme necessidade
  editor/               # somente na fase do Map Maker
tests/
build.bat
```

Não mover arquivos apenas para satisfazer um desenho teórico se a mudança não trouxer ganho arquitetural concreto.

Criar `engine.lib` somente quando game/editor realmente compartilharem uma biblioteca e isso simplificar o build.

CMake não é requisito sem decisão explícita.

---

## 20. Qualidade e performance

Cada incremento deve terminar compilável, testável e observável.

Regras:

- `/W4`;
- objetivo de 0 warnings;
- testes para regras novas relevantes;
- fixtures sintéticas próprias;
- `git diff --check`;
- smoke tests para comportamento visual/interativo;
- erros externos não dependem de `assert`.

Não otimizar prematuramente, mas também não introduzir ineficiências óbvias:

```text
não carregar PNG todo frame
não reconstruir clips todo frame
não copiar spritesheet todo frame
não alocar pequenos comandos no heap sem motivo
não testar colisão contra o mundo inteiro
não duplicar queries que o world já oferece
```

Broad-phase, pooling e estruturas densas entram quando medições ou volume real justificarem.

---

## 21. Persistência implementada na Fase 8

As fronteiras permanentes são:

```text
authoring MapData -> DMAP -> parse/validation -> RuntimeWorldBuilder -> MapSession
gameplay mutation -> SessionWorldState -> DSAV
```

`MapId` é string estável e não é path. `PersistentInstanceId` é `u64` local ao mapa,
com zero reservado. `PersistentEntityKey` combina os dois. `EntityHandle` continua
identidade de uma execução e nunca é persistido.

`MapData` contém tiles, collision, spawns, placements e links. `RuntimeWorldBuilder`
reutiliza `EnemyFactory`, `WorldObjectFactory` e `WorldPickup`, criando novos handles.
`MapCatalog` resolve MapId para DMAP. `MapSession` mantém o world ativo e só destrói o
anterior depois que o destino foi lido, validado, construído e recebeu seus deltas.
O Player fica fora do world local e preserva Health, Inventory, Wallet e QuickSlots.

Transitions são requests pendentes consumidas na boundary do fixed tick. O swap
invalida handles locais, recria visuals, limpa projectiles/VFX/hit records, reposiciona
o Player no `SpawnId` e recentraliza/clampa a câmera. Um latch e spawns fora das AABBs
evitam retorno imediato.

`SessionWorldState` captura somente diferenças contra o `MapData`: Chest aberto e
conteúdo restante, Crate destruída, pickup coletado ou quantidade parcial. A mesma
estrutura mantém A→B→A e é gravada pelo DSAV. Load valida e prepara o target antes de
substituir world/player; inventário restaura os 30 slots exatos.

| Dados | DMAP | DSAV |
|---|:---:|:---:|
| Tile layout / collision | sim | não |
| Player spawn | sim | não |
| Enemy placement | sim | não (morte não persiste na v1) |
| Object placement | sim | delta apenas |
| Pickup placement | sim | delta apenas |
| Player Health / posição / facing | não | sim |
| Inventory / Wallet / QuickSlots | não | sim |
| Current MapId | não | sim |
| EntityHandle / animator | não | não |
| Projectile / VFX / AttackInstance | não | não |

Os mapas demo são `MapData` temporários de autoria, serializados deterministicamente
para `build/bin/data` e recarregados pelo caminho real. O futuro editor produzirá o
mesmo DTO e chamará o mesmo writer, sem dependência de UI no serializer.

## 22. Anti-padrões arquiteturais

Evitar explicitamente:

```text
Win32 -> Player diretamente
Renderer -> modifica gameplay
Renderer/animation -> aplica dano diretamente
Enemy subclass por monstro sem necessidade
Map duplicado por sistema
Sprite AABB = collision = hurtbox = hitbox
Quest polling de internals
Save de ponteiros/handles runtime
Dump binário cru de structs
ECS genérico antecipado
Scene graph universal
Manager que conhece tudo
Singleton global para facilitar dependências
biblioteca externa substituindo engine existente sem decisão
```

---

## 23. Regra de evolução

Quando existir um único caso real, implemente-o de forma pequena e deixe uma fronteira limpa.

Quando dois ou três casos reais demonstrarem repetição, extraia o sistema comum mínimo.

O objetivo final não é possuir o maior número possível de “engines”. É chegar a uma base em que:

```text
novo monstro
novo objeto
novo mapa
novo NPC
nova quest
```

sejam principalmente operações de conteúdo e configuração, sem reescrever sistemas centrais.

# Current authored map set

Production gameplay maps are discovered recursively from `maps/gameplay/`. The DMAP
metadata, not the filename, owns each `MapId`. Discovery sorts paths deterministically,
rejects duplicate IDs, loads every map into the `MapCatalog`, and validates all links
before a session starts. With no explicit `--map`, one map starts automatically; with
multiple maps, the unique map containing `entry.start` is selected. Ambiguous or
missing entry spawns require `--map`, while `--spawn` remains an explicit spawn
override.

The runtime path is:

```text
GameLaunchOptions
    -> gameplay map discovery and startup selection
    -> MapCatalog
    -> MapSession
    -> RuntimeWorldBuilder
    -> RuntimeWorld
    -> synchronized runtime visuals
```

Map Maker authored content follows the same boundary:

```text
EditorDocument -> MapData -> DMAP v1.1 -> readDmap/validateMapData
                -> MapCatalog/MapSession -> RuntimeWorld
```

The three reference maps cover the current 72 semantic Dungeon atlas cells and the
8 registered stamps. Coverage is tested from `AuthoringSemanticRegistry`; the maps do
not add a second atlas catalog or infer collision from artwork.

## Fase 10A — NPC foundation

`NpcDefinition` is immutable catalog content and `NpcInstance` owns only runtime
position, facing and `EntityHandle`. Authored `NpcPlacement` is carried by `MapData`
and the optional DMAP 1.1 `NPCS` chunk. `RuntimeWorldBuilder` creates NPC instances
through `NpcFactory`; `NpcCatalog` validates definition IDs before construction.
NPC interaction reuses the shared `InteractionArea` and a deterministic nearest-target
query. Until the asset audit provides approved NPC sprite sheets, the runtime visual
catalog uses explicit non-asset marker colors so authored NPCs remain visible without
inventing PNG semantics.

## Fase 10B — Dialogue data model

`DialogueDefinition`, `DialogueNode`, `DialogueChoice` and `DialogueCatalog` provide a
renderer-independent dialogue graph. Nodes own one or more ordered text pages and may
either continue to one next node or expose choices targeting nodes in the same
definition. Catalog insertion validates IDs, non-empty pages, the entry node and every
transition target. Guard and Scholar use separate catalogued definitions through their
NPC `defaultDialogueId`.

## Fase 10C — Dialogue runtime and UI

`DialogueSession` consumes the existing `PlayerCommand` boundary and owns only the
current definition/node, page and choice selection. While open it consumes the whole
command, so movement, combat and inventory do not advance in parallel. `interact` or
the primary logical action advances pages and confirms choices; movement selects a
choice and the secondary logical action closes the session. The game opens the
session from the interacted NPC's `defaultDialogueId` and renders a small overlay from
session read-only state using the existing bitmap font and software renderer. Input
mapping remains at the Win32 boundary; conditions, actions and persistence remain
deferred to Phase 10D.

## Fase 10D — Persistent dialogue flags

`DialogueFlagSet` stores sorted stable flag IDs and exposes set, clear and query
operations for dialogue conditions/actions. Choices can require a flag to be set or
unset and can set or clear a flag when selected. The game carries this state in
`SaveData`; DSAV 1.1 adds the optional bounds-checked `FLGS` chunk while DSAV 1.0 saves
without flags remain readable. Conditions/actions are intentionally limited to these
two flag operations; quest state is described in the following Phase 11 sections and
a broader scripting model remains deferred.

## Fase 11A — Quest definitions

Quest content begins with immutable, renderer-independent `QuestDefinition` values
containing ordered `QuestObjectiveDefinition` values. Each objective has a stable
`DefinitionId`, one of the declared objective kinds (`talk`, `kill`, `pickup`,
`enter`, `open`, `deliver`), a target definition ID, a required count and authoring
text. `QuestCatalog` validates complete definitions, duplicate quest IDs, duplicate
objective IDs and provides stable lookup for runtime systems that will be added in
later blocks.

The shared `GameContentRegistry` registers the current Scholar quest definition so
the model is exercised by real content registration. Runtime progress is defined by
the following Fase 11B block; event consumption and save data remain outside these
two foundations. DMAP and DSAV formats remain unchanged.

## Fase 11B — Quest state

`QuestStateStore` owns runtime-only `QuestProgress` records and never stores mutable
state in `QuestDefinition`. An absent record is `inactive`; `start` creates ordered
zeroed `QuestObjectiveProgress` entries and marks the quest `active`. Objective
progress can be advanced or explicitly set, is clamped to the definition's required
count, and changes the quest to `completed` only after all definition objectives are
complete. `reset` removes the record and returns the quest to its inactive state.

This block does not consume domain events or serialize state itself; those concerns are
provided by the following 11C and 11D blocks.

## Fase 12A — Player progression foundation

`PlayerProgressionDefinition` is compiled from authored content and supplies the
typed base stats currently used by the Player, including maximum health. The
`GameSession` owns `PlayerProgressionState`; it stores cumulative `uint64_t` XP and
derives level, next threshold and current-level progress from the compiled curve.
Accumulation saturates at the integer maximum. The current builtin curve is
provisional development content, not final balance. Creatures do not grant XP
directly; defeat rewards and guaranteed quest grants remain separate domain systems.

## Fase 11C — Event-driven quest progression

`QuestSystem` consumes the existing `SimulationEvent` variant and advances only active
quest records in `QuestStateStore`. `EntityDefeated`, `PickupCollected`, `NpcTalked`,
`MapEntered`, `ObjectOpened` and `ItemDelivered` map to the corresponding objective
kinds by stable IDs and event amounts. Unrelated events are ignored, and no system
polls enemy lists, inventory or world objects. Defeat and pickup producers attach the
concrete runtime definition ID needed by quest matching.

## Fase 11D — Persistent quest state

`SaveData` owns a copyable `QuestStateStore` alongside player and world deltas. DSAV
minor 2 adds the optional bounds-checked `QSTS` chunk; it serializes only quest and
objective `DefinitionId`s, status and counters, while definitions remain in the
runtime `QuestCatalog`. The reader validates quest existence, objective order,
counter limits and active/completed consistency before replacing state. DSAV 1.0 and
1.1 saves without `QSTS` remain readable, and older versions reject the future chunk.
The game save/load path passes the quest catalog and carries the state through F5/F9;
DMAP is unchanged. DSAV minor 3 adds the `PROG` chunk containing the progression
definition ID and cumulative XP; older saves without it default to level 1 of the
default progression.

## Audit foundation (Block A)

The project now has a small platform-neutral observability boundary. `GameRuntime` can
expose `auditSnapshot()` as a value-only `GameAuditSnapshot`; the snapshot contains
stable map/content/persistent IDs and diagnostic state, but no runtime handles,
pointers or renderer ownership. `AuditSession` consumes snapshots and structured
events and writes a versioned metadata document, JSON Lines event/state streams and a
human-readable summary. Gameplay remains responsible for gameplay state and does not
perform file I/O.

The session is mutation/event driven: state is not written every tick by default;
the first state and periodic checkpoints (60 ticks by default) are recorded, with an
explicit force option for important transitions. Audit output is development-only in
`audit/<session-id>/` and is ignored by Git. The logical framebuffer and screenshot
writer, headless platform, scripted playtest runner and Linux build are subsequent
blocks; DMAP/DSAV remain unchanged at their current runtime versions.

The next audit increment adds a small BMP adapter at the same boundary. It accepts
the renderer's read-only `PixelBufferView` in RGBA8 and writes a 32-bit bottom-up BMP
without capturing a desktop window. Screenshot names are confined to the audit
session's `screenshots` directory. This adapter is still passive; automatic capture,
F12 wiring and headless presentation belong to later blocks.

`HeadlessAuditPlatform` implements the same `Platform` contract without creating a
window or depending on Win32/X11. It receives an injected `ImageDecoder`, advances a
controlled monotonic clock only when requested, schedules platform-neutral
`InputState`/`DebugInputState` values by simulation tick, stores operational logs and
copies the real `PixelBufferView` passed to `present`. It is a platform adapter, not a
second gameplay implementation; scenario orchestration remains the responsibility of
the later playtest runner.

## Audit/playtest portability track — Block D

`playtest_runner` is a separate harness, not a second game. It composes the real
`GameRuntime` with `HeadlessAuditPlatform`, injects logical `InputState` values on
controlled ticks, calls the real fixed-tick/update boundary and presents the real
272x224 framebuffer. Assertions read the value-only `GameAuditSnapshot`; failures
write an audit event, forced state checkpoint and framebuffer screenshot before the
session closes with a non-zero process result.

The runner supports independent scenario sessions, `--all`, `--scenario`, `--seed`,
`--ticks`, `--audit-root` and explicit `--asset-root`/`UNDERWORLD_ASSET_ROOT`
selection. It uses a small injected synthetic decoder for portable runs without
local licensed assets, while preserving the production `ImageDecoder` boundary.
Quest scenario names remain startup smoke entries until the game exposes a real
player command for quest start; no quest state is fabricated by the harness.

## Audit/playtest portability track — Block E

The Win32 game loop now accepts `--audit` through `GameLaunchOptions`. When enabled,
the loop observes the value-only `GameAuditSnapshot` after presenting the real
logical framebuffer. `ManualAuditObserver` records startup, map transitions,
health/death, enemy/object/pickup changes, dialogue and save/load observations in
the same `AuditSession`; important changes force a state checkpoint and a BMP.
Win32 maps F12 to a platform-neutral `captureAuditSnapshotPressed` edge, so manual
captures use the same tick for the event, snapshot and framebuffer image. Gameplay
systems remain unaware of file I/O.

The repository also contains Docker build helpers. The Linux container compiles
the portable tests, headless playtest runner and Linux `game` without Win32 sources;
it is not a replacement for the existing MSVC `build.bat`. The Windows Docker recipe
requires a Windows host with Docker Desktop in Windows-container mode. Interactive
Linux execution requires X11/WSLg and local licensed assets.

## Linux runtime platform — Block G

The Linux executable uses the same `game::run`, `GameRuntime`, fixed timestep,
`Renderer2D` and 272x224 `Framebuffer` as the Win32 executable. `LinuxPlatform`
is an X11 adapter that owns only the window, event mapping, monotonic clock and
nearest-neighbor presentation; gameplay receives only `InputState` and
`DebugInputState`. `LinuxImageDecoder` is the Linux-side libpng adapter and returns
the same owned RGBA8 `ImageData` contract as WIC on Windows. The portable Docker
build emits `build/linux/game`; interactive execution requires X11/WSLg and local
licensed assets.

## Content Definition Boundary

Conteúdo authored não popula catálogos de runtime diretamente. Cada categoria
principal possui um DTO authored distinto da definição runtime correspondente. O conteúdo oficial
temporariamente construído em C++ passa pela fronteira `AuthoredContentPack` ->
`ContentValidator` -> `ContentCompiler` -> `GameContentRegistry`. Os DTOs são
tipados, em memória e independentes de runtime state, renderer e assets carregados.
Validação produz diagnósticos estruturados, estáveis e determinísticos; IDs visuais
são metadata e sua disponibilidade pertence ao bootstrap/presentation. O registry
publicado não oferece mutação aos consumidores e não semeia conteúdo no construtor.

## External Content Workspace

O `ContentWorkspace` recebe uma lista explícita ou um diretório de arquivos JSON v1.
Cada arquivo passa pelo decoder estrito existente; depois os documentos são ordenados
por caminho lexical normalizado, mesclados por categoria e validados/compilados uma
única vez:

```text
Authoring Sources
       ↓
Builtin ou workspace directory discovery
       ↓
ContentWorkspaceLoader
       ↓
Per-file strict decode + source provenance
       ↓
Deterministic merge (duplicates are errors)
       ↓
AuthoredContentPack
       ↓
ContentCompiler
       ↓
GameContentRegistry
```

O resultado preserva o `AuthoredContentPack`, o `GameContentRegistry` e o mapa de
origens. A seleção é compartilhada por Game, Map Maker e `content_check`; builtin é a
fonte transicional default e `--content` é uma substituição explícita, sem overrides.
Não há manifest, hot reload, mutação de registry ou autoria visual genérica.

## Authored World Source Boundary

`UMAP v3` is the authored map source and `DMAP 1.4` is the compiled/runtime map
serialization. The production boundary is:

```text
Human / Map Maker / future LLM
       ↓
AuthoredMapSource (.umap)
       ↓
MapCompiler
       ↓
MapData
       ↓
DMAP 1.4
       ↓
RuntimeWorld
```

`AuthoredMapSource` is a real authored representation, distinct from `MapData`, and
preserves manual geometry, placements, links, regions, world rules, encounters and
typed placement metadata. The compiler validates authored references, constructs a
fresh `MapData`, and then performs compiled-map validation. `MapComposer` remains an
initial geometry generator, not a regeneration step for an existing `.umap`.

Runtime world logic reuses the existing event stream:

```text
SimulationEvent / EventBuffer
          ├── QuestSystem
          └── WorldLogicSystem
                    ├── RegionTracker
                    ├── EncounterSystem
                    └── stateful object doors
```

Regions are spatial data, rules are authored ordered event reactions, doors are an
optional `WorldObjectDefinition` capability, and encounters monitor authored enemy
placement IDs. The Phase 14 implementation deliberately did not add a scripting VM,
Puzzle Engine, encounter waves, Content Studio or LLM integration. The visual
boundary is documented in the Phase 17 section below; it remains separate from
authoritative world logic.

## Presentation Feedback Foundation — Phase 15

Presentation feedback is derived from authoritative simulation state and never writes
gameplay state back into the simulation:

```text
SimulationEvent
      ├── QuestSystem
      └── WorldLogicSystem
                ↓
       PresentationEffectRequested
                ↓
PresentationFeedbackController
                ↓
PresentationEffectSystem
                ↓
PresentationEffectFrame
                ↓
GamePresentation → Framebuffer
```

`PresentationEffectSystem` owns only fixed-tick presentation state. Transient effects
are keyed by effect ID and retrigger by restarting their lifetime; persistent effects
are keyed by effect ID plus structured region sources. Camera shake is deterministic,
linear-decay and applied as an offset to an effective render camera. The base camera,
player position, collision, AI, regions, world rules and saves are not changed.

The renderer applies world-layer effects after world-space VFX and before debug/HUD,
then applies final-layer fades/overlays after HUD. Vision masks are CPU passes over the
small logical framebuffer and are centered on the player's logical position, not on the
viewport center. Deterministic priority/ID ordering avoids dependence on container
iteration order.

The existing `EffectSystem` remains the world-space animated VFX system. It is not
merged with `PresentationEffectSystem`; an impact may consume both systems in the
future. Content JSON v3 introduced presentation effects, v4 added object activation
definitions and v5 added gameplay visual definitions. Authored UMAP v2/v3 and DMAP 1.3/1.4 regions may bind a
persistent environment effect and world rules may emit a transient presentation cue.
DSAV 1.8 remains an authoritative gameplay format and does not persist presentation
state; presentation state is cleared/rebuilt on map activation and load.

## Interactive World Components — Phase 16

Interactive objects are capabilities on the existing authored/compiled world-object
definition. They are not a separate puzzle engine or a second event bus:

```text
WorldObjectDefinition
        ↓ optional activation capability
WorldObjectInstance
        ↓ concrete transition
ObjectActivationChanged(MapId, PersistentInstanceId, active)
        ↓
WorldLogicSystem
        ↓
Door / Flag / Encounter / Presentation
```

`interactToggle` is the shared capability for lever/switch-like objects. Its state is
authoritative and persistent. `playerPressure` is a distinct capability evaluated by
`GameSession` using only the Player feet position; its bounds are local to the object
placement, events are emitted in authored object order, and its state is derived rather
than saved. The current pressure source is Player-only; blocks, weights, enemies and
multi-actor occupancy are future work.

The existing interaction selection remains the single input path. A logical interact
command selects the nearest eligible object deterministically, toggles its capability,
and appends `ObjectActivationChanged` to the shared `simulation::EventBuffer`. The
fixed-tick pressure pass does the equivalent only on real inside/outside transitions.
World Logic consumes the appendable event sequence with a bounded cursor, so an
activation can drive a door rule in the same logical cycle without recursive event
loops. Object activation is intentionally independent from `DoorState` and from
`WorldObjectState` (`idle`, `opened`, `destroying`, `destroyed`).

Authored maps continue to cross the compiled boundary as:

```text
Human / Map Maker / future author
        ↓
AuthoredMapSource (.umap v3)
        ↓
MapCompiler
        ↓
MapData
        ↓
DMAP 1.4
        ↓
RuntimeWorld / GameSession
```

Content JSON v4 carries the activation capability while readers remain compatible
with v1–v3. UMAP v3 carries object activation triggers and conditions while readers
remain compatible with v1–v2. DMAP 1.4 retains readers for 1.0–1.3. DSAV 1.8 stores
only persistent toggle activation deltas; pressure activation is reconstructed from
the restored Player position. Existing `EffectSystem` and the Phase 15
`PresentationEffectSystem` remain separate from these authoritative gameplay
components. No PuzzleEngine, expression language, push-block system or visual
authoring boundary is introduced here.

## World Object Persistence

World-object persistence is authored per map placement, not per
`WorldObjectDefinition`. The same definition can therefore be persistent in one
room and resettable in another. The effective runtime state is modeled as:

```text
Authored state
      +
Persistence policy
      +
Runtime delta
      =
Effective runtime state
```

`ObjectPersistencePolicy::persistent` keeps the existing behavior: runtime changes
survive map unload/reload and are represented by the existing `ObjectDelta` values
in `SessionWorldState`, including save/load. `resetOnMapEnter` keeps changes only in
the currently loaded `RuntimeWorld`; when that world is rebuilt, the authored
placement and initial contents are used again. Missing policy fields in legacy map
sources and DMAPs default to `persistent`.

This policy is currently limited to `WorldObject` placements. Derived capability
state is a separate concept and is not governed by this policy: for example,
`ObjectActivationMode::playerPressure` is recalculated from the current Player
position and is never persisted as an object delta.

## Map-authored scene serialization

Scenes are authored in `AuthoredMapSource::scenes`, carried unchanged into `MapData`,
and emitted in DMAP 1.5's optional `SCNE` chunk. A scene ID is local to its map;
`WorldRule` activates it through `WorldActionKind::startScene`, rather than duplicating
trigger/condition data in the scene. The map validator owns cross-reference checks:
NPC/enemy bindings resolve concrete placement `PersistentInstanceId`s, clip targets
stay within map bounds, and scene world actions use the same target/capability rules
as ordinary `WorldRule` actions. `SCNE` cannot start another scene.

Legacy UMAP/DMAP files contain no scene state and decode as `scenes = []`; DSAV does
not serialize an in-flight timeline.

## Equipment and derived player stats

Equipment is Player-owned gameplay state. Equipment items remain normal
`ItemDefinition` values with typed armor/accessory metadata. In this slice,
modifiers affect only maximum health and Player attack damage. Derived stats are
calculated from authored base stats plus equipped items and are not persisted.
`AttackDefinition` remains immutable; Player damage bonuses are applied when an
effective `DamageSpec` is produced. Inventory/equipment transfers are transactional.
`InventoryOverlayState` owns only inventory/equipment navigation state. `GameViewModel`
exposes copied equipment and derived-stat read models; `GamePresentation` renders them
without mutating gameplay. Inventory routing reports equipment changes explicitly so
`GameSession` refreshes derived stats.

## Bank storage

`PlayerBank` is global Player-owned gameplay state, composed into `PlayerItems` and
backed by the existing `ItemContainer` stacking rules. It has a fixed capacity of 50
slots (5 by 10) and a gold balance distinct from the carried `Wallet`. Bank contents
survive map transitions and are persisted by the `BANK` chunk in DSAV 1.5; they are
not part of `RuntimeWorld`, `SessionWorldState`, DMAP, or transient loot. Bank access
and its UI are implemented as a separate access slice. `WorldObjectDefinition::bankAccess`
is an access capability, not storage: the object contains no bank contents and does not
open as a container or emit `ObjectOpened`. `BankOverlayState` contains only transient
navigation state. `GameSession` owns modal lifetime and routes selected-slot transfers
and Deposit All/Withdraw All operations; the ViewModel copies Bank data for presentation,
which never mutates it. Official placement and final art remain content/level-design work.
## Guaranteed rewards and quest delivery

`RewardProfileDefinition` remains probabilistic defeat reward data. `RewardGrantDefinition`
is guaranteed, atomic reward data referenced by `QuestDefinition`. Quest completion and
reward delivery are separate persistent states: claims are exactly once and legacy
completed quests are migrated as already claimed. Item grants target Inventory first
and Bank second; if storage cannot fit the complete grant, nothing is applied and the
reward remains pending for a later tick. Equipment rewards are normal non-stackable
items and are never auto-equipped. Gold targets Wallet then Bank, XP uses
`PlayerProgressionState`, and grant items do not emit `PickupCollected`.

The QSTS claim flag is persisted in DSAV 1.6. Shops are immutable authored content
and are exposed by `DialogueAction::openShop`; NPCs do not own prices or stock.
`ShopOverlayState` is transient navigation/feedback state owned by `GameSession`.
Shop transactions use carried Wallet Gold and Inventory only, never the Bank, and
the dialogue command that opens a shop cannot also buy in the same tick. The
ViewModel copies offers and transaction feedback for presentation; no shop runtime
stock or persistence exists.

## Shops

`ShopDefinition` is immutable authored content. Each offer independently enables
Player purchase and/or sale through optional prices, including valid zero prices.
Shop transactions use carried `Wallet` Gold only: purchases require Inventory space
and sales remove exactly one item from a selected Inventory slot and credit Wallet.
The Bank is never accessed automatically. Failures are atomic, offers have unlimited
authored availability, and there is no runtime stock, buyback, Shop state, persistence
or transaction event in 12E2. Shop access and presentation are deferred to 12E3.

## Visual Content Boundary — Phase 17

Gameplay definitions carry only stable visual IDs. They do not own images, sprite
sheets, animation clips, animators or filesystem paths:

```text
Authored Visual Definitions (Content JSON v5)
        ↓
ContentWorkspace / ContentValidator / ContentCompiler
        ↓
GameContentRegistry
        ↓
VisualContentLoader + explicit asset roots
        ↓
RuntimeVisualContent
        ↓
GamePresentation / per-instance Animator
```

The boundary contains `VisualImageDefinition`, `StaticSpriteDefinition`,
`AnimationDefinition`, flexible `EnemyVisualDefinition` and
`WorldObjectVisualDefinition`. `NpcVisualSet` can use directional idle animations
or its authored marker-color fallback. Images are resolved once from either the
licensed game asset root or an explicitly selected external content workspace; a
workspace path is relative, contained and cannot use symlink path components to
escape that workspace. Content validation checks authored structure, while the
loader checks real file resolution, image decoding and frame bounds before rendering.

Creature visual profiles require only `idle`. A profile can bind a single
non-directional animation, any subset of down/up/side directions, optional
move/hurt/death/dead states, and zero or more arbitrary visual action IDs such as
`attack.sword`, `attack.bow`, `defend.shield` or `sleep`. Resolution is exact
direction, authored default, then the first available direction in deterministic
down/up/side order. Missing optional states/actions fall back to idle and never
disable the authoritative gameplay actor. Right-facing side art uses the existing
horizontal flip policy.

`VisualContentLoader` builds shared immutable runtime `AnimationClip` and static
sprite catalogs; actors own only mutable playback state. Projectile, pickup and item
presentation use the common static-sprite catalog with authored source rectangles and
anchors. Enemy and object runtime visual catalogs are built from definitions, so
`GameRuntime` contains no Soldier/Skull/chest/crate/pickup/item/projectile visual
registration. Player animation sheets, HUD/font assets, tileset loading and generic
arrow-impact VFX remain fixed game presentation assets by explicit Phase 17 scope.

The gameplay/presentation distinction remains:

```text
authoritative gameplay state/events
        ↓ visual IDs / readonly state
VisualContentLoader + visual instances
        ↓
GamePresentation
```

This phase does not add a status-effect system, asset importer, resource packer, hot
reload, audio engine, shader framework, Content Studio or LLM integration. `EffectSystem`
continues to own world-space animated VFX, independently from presentation screen
effects and authored gameplay visuals.

## Content Studio Foundation — Phase 18A

The existing Map Maker shell is the first Content Studio shell. It has a MAP mode for
the authored map document and a CONTENT mode for an editable external workspace:

```text
Content Studio
├── Map Document
│     └ AuthoredMapSource (.umap) -> MapCompiler -> MapData / DMAP
└── Content Workspace Document
      ├ source file A (.json)
      ├ source file B (.json)
      ├ source file C (.json)
      └ merge + provenance -> ContentValidator -> ContentCompiler
                                             -> GameContentRegistry
```

`ContentFileDocument` entries are the editable source of truth. The merged authored
pack, source index, diagnostics and compiled registry are derived and rebuilt after
typed mutations. Files retain their definition ownership; duplicates remain merge
errors. Save All writes only dirty files, with canonical Content JSON v5 and atomic
replacement. Structural JSON is produced by the shared strict encoder, while semantic
reference errors are visible and saveable but disable compilation and playtest.

The browser uses a fixed order for all twenty-five content categories and lexical
`DefinitionId` order within a category. Selection is a typed
`ContentDefinitionKey`, not a pointer into a vector. 18A has concrete typed editors
for `visualImages`, `staticSprites`, `animations` (including frame and marker
operations) and flexible `enemyVisuals`; other categories are browsable read-only.
Builtin content is read-only, while `--content <workspace>` supplies the editable
workspace root. The same document and compiler APIs are suitable for future human or
LLM authored DTOs; no reflection, generic editor framework or raw JSON editor is
introduced.

Explicit workspace validation has two layers. Content diagnostics come from the
shared workspace validator/compiler. When that derived registry is valid, the Studio
invokes `VisualContentLoader` with the editor's game-asset root and (for external
workspaces) the document root. Its diagnostics are cached by the document's tooling
revision, so rendering does not repeatedly decode images; any authored mutation
invalidates the cache. Invalid semantic content skips asset loading and is shown as
such. Static sprite source rectangles, animation frame fields and opaque markers are
edited through typed document operations; uncommitted buffers are scoped to the
selected definition/frame/marker and are cancelled on selection changes or Escape.

## Visual authoring preview — Phase 18B

The Studio preview is a presentation/tooling service, not a second renderer:

```text
Authored VisualImage / Animation
             ↓
shared secure VisualAssetResolver
             ↓
ImageDecoder (lazy, cached)
             ↓
EditorVisualPreview
   ├ source-rectangle canvas
   ├ grid/pan/nearest-neighbor zoom
   └ shared AnimationClip + Animator playback
```

`VisualContentLoader` and the Studio share path containment, workspace/game-assets
root selection, image decoding and animation-clip construction. The Studio may
preview a locally resolvable selected definition while unrelated workspace errors
remain editable; full asset validation still runs through `VisualContentLoader` on
the explicit Validate command. Preview ticks come from the editor timer, not paint
frequency. Pan, zoom, grid, selected frame, playback and facing are editor-only and
never enter Content JSON or gameplay/save state.

The spritesheet canvas authoring operations are deliberately bounded: mouse drags
are clamped to decoded image bounds, grid multi-cell additions use authored row-major
order, and no automatic slicing, asset import, timeline/curve system or image
preview database is introduced. Flexible character profiles continue to require only
`idle`; optional move/hurt/death/dead states and arbitrary visual action IDs use the
same exact/default/deterministic-direction fallback as runtime.

## Content Studio gameplay authoring

The single MAP/CONTENT shell now exposes typed gameplay authoring without a
reflection or property-generation framework:

```text
typed inspector
      ↓
ContentWorkspaceDocument mutation API
      ↓
individual AuthoredContentPack source file
      ↓
workspace merge + provenance
      ↓
ContentValidator
      ↓
ContentCompiler
      ↓
GameContentRegistry
      ↓
existing runtime factories and systems
```

Reference pickers consume the authored `ContentDefinitionIndex` in lexical ID
order and remain usable while a workspace is semantically invalid. Missing
references are displayed and preserved so the user can repair them incrementally.
The typed inspectors cover projectiles, attacks and fixed-tick timelines, behavior
profiles, enemies, items, pickups, world-object capabilities, NPCs, structured
dialogue, quests, rewards, shops, progression and presentation effects. Dialogue
and quest authoring use ordered lists rather than a node graph. Phase 18D completes
typed inspectors and document mutations for tilesets, authoring descriptors, tile
semantics and stamps, so all twenty-five Content JSON categories have an authored
editing path.

## Unified MAP/CONTENT workflow — Phase 18D

The Content Studio remains one immediate-mode application shell with two document
perspectives. Source JSON files and the UMAP document are authored truth; merged
content, provenance, compiled registry and DMAP are derived artifacts:

```text
Content Studio
├ MAP
│  ├ tile palette / semantic palette / stamps / entities
│  ├ layers, tools, placements, regions, world rules, encounters
│  └ authored UMAP
└ CONTENT
   ├ gameplay and visual definitions
   ├ tilesets, authoring descriptors, tile semantics, stamps
   └ source Content JSON files

Authored Content JSON + UMAP
             ↓
      validation / compiler
             ↓
    GameContentRegistry + DMAP
             ↓
           runtime
```

The MAP palette uses the existing tileset/semantic/stamp registries and ordinary
`MapTileReference` storage. A rectangular tile brush is editor-only and repeats its
pattern periodically inside exactly the requested inclusive rectangle in deterministic
row-major order through a compound command, so undo/redo remains one coherent edit.
Layer add/remove/rename/reorder operations are commands; layer lock,
editor visibility, active palette, brush, viewport and tool are not serialized.

Map placements retain `PersistentInstanceId` while their definitions retain
`DefinitionId`. MAP can open a placement's definition in CONTENT, and CONTENT can
activate place/find-in-map operations for placeable enemy, NPC, object and pickup
definitions. `FIND IN MAP` shares one authored-order usage traversal and centers the
viewport for every placeable kind, wrapping after the last occurrence. Valid builtin
definitions remain read-only for JSON mutation but can still be placed/found in an
editable map. Definitions without authoring descriptors use their IDs as palette
fallback labels. Tilesets, semantic tiles and stamps use the same navigation in the
opposite direction. Regions expose presentation-effect binding, while world rules
and encounters remain structured list editors with compatible placement pickers.
Current external workspace content replaces builtin content; an invalid
workspace invalidates content-dependent map validation, compile/export and playtest
instead of silently using a stale registry.

Map validation is cached against both the map document revision and the current
content-derived registry. Save All writes only dirty Content JSON files and the dirty
UMAP; DMAP is never written implicitly. The current in-memory authored documents can
be used for editor validation and playtest without a second map editor or a parallel
renderer.

Human authoring converges at the existing authored boundary:

```text
Content Studio → Authored DTOs → validators → compiler → runtime
```

Phase 18D does not add advanced autotiling, a procedural rule solver, asset import,
hot reload, a dialogue/quest graph, scripting, audio, networking or generic
authoring automation.

## Content Studio world projects

O Content Studio agora possui uma camada de projeto acima do documento de mapa:

```text
Content Studio
        ↓
WorldProjectDocument
        ├── EditorDocument map A
        ├── EditorDocument map B
        └── ... na ordem authored
        ↓
AuthoredWorldSource (UWORLD v1)
        ↓ validação + compiler
MapData por MapId
```

`EditorDocument` continua representando um único mapa e mantém seu viewport,
seleção, layer ativa e `CommandHistory`. `WorldProjectDocument` é a autoridade de
persistência quando os mapas pertencem a um `.uworld`; UMAP continua disponível como
fluxo standalone e pode ser importado para o projeto. `MapId` é estável depois da
criação: links são referências estruturadas e remoção é bloqueada quando quebraria a
entrada do projeto ou links existentes.

`UWORLD v1` contém somente dados authored dos mapas e `entryMapId`. Preferências,
layout e estado temporário do editor permanecem fora do arquivo. O writer é
determinístico, estrito e atômico. Compile/export produz artefatos `DMAP 1.4`
separados, um por mapa; DMAP não foi transformado em um container de mundo.

### Multi-map playtest

O playtest usa a fonte authored atual do `WorldProjectDocument`, não a última versão
salva. Todos os mapas são compilados em memória e inseridos em um provider/catalog
runtime. A sessão inicia no mapa ativo para iteração rápida (ou no `entryMapId` quando
solicitado) e reutiliza `MapSession`, `MapLink` e `PendingMapTransition` para carregar
o alvo. Há somente um `RuntimeWorld` ativo por vez; mapas não são simulados
simultaneamente e o editor não mantém um sistema de transição paralelo.

## Content Studio deep authoring tools

O Content Studio mantém a autoria tipada no `ContentWorkspaceDocument`, mas a
interface agora trata referências e coleções como estruturas de produção, não
como campos isolados. Inspectors usam seleção independente para cada coleção
aninhada; remover uma entrada remove a linha selecionada, e não implicitamente a
última entrada. Essas regras deixam workspaces incompletos editáveis sem permitir
acesso fora dos limites.

As referências conhecidas passam por um `ContentReferencePicker`, que pesquisa o
índice determinístico de definições (builtin e authored), mostra o resumo quando
disponível e escreve somente o `DefinitionId` authored. `Quick Inspect` é uma
visão somente leitura e pode abrir a definição real; uma pilha curta de
`ContentDefinitionKey` fornece `BACK` sem perder a definição de origem. Esse
fluxo é editor-only: não adiciona um grafo genérico nem altera o formato dos
conteúdos.

Reward grants, reward profiles, shops, progressões, ataques de inimigos,
objetivos/tags de quests e as listas aninhadas de diálogos usam o mesmo padrão
de coleção: linha selecionável, edição contextual, remoção segura e operações
de ordem quando a ordem authored tem significado. O picker Quest → Reward
Grant também pode criar uma nova definição authored e ligar o ID em uma única
operação de workflow.

O browser de assets mantém um catálogo de paths relativos sob as raízes
permitidas e deixa a decodificação de imagens para o preview/cache existente.
Assim, escolher uma imagem não persiste paths absolutos nem faz descoberta de
assets a cada frame. Estado temporário de busca, seleção, quick inspect e drafts
continua fora de Content JSON, UMAP, UWORLD e DMAP.

Máscaras opcionais de ataque são dados de gameplay authored separados dos dados
de apresentação. O compilador transforma runs determinísticos da máscara em
regiões retangulares compactas; o runtime não compara pixels nem consulta o
frame atual do `Animator`. Ataques antigos continuam usando
`DirectionalBoxes`, enquanto a nova anotação é opcional.

O contrato de estabilidade é deliberado: seleção de uma definição inválida ou
de uma coleção vazia produz diagnóstico e estado editável. Validação e compile
podem rejeitar o workspace, mas um inspector não deve encerrar o Content Studio.

## Content Studio localization and preferences

The editor language is a tooling preference, not authored game content. The shell owns
an `EditorPreferences` value and an `EditorLocalization` catalog. `EditorTextId` is the
typed boundary for normal menus, labels, commands, status text and enum display names;
`pt-BR` is the first-run default and `en-US` is the second supported language. The
Win32 shell persists the selection in a user-writable Content Studio settings file and
rebuilds its native menu when the language changes. Tests inject a filesystem path, so
repository documents never receive personal settings.

UTF-8 is decoded into Unicode codepoints before bitmap-font lookup and text-field edits.
The existing 7x9 font remains the renderer and gains a small Latin accent treatment for
Portuguese; no installed system font or third-party typography dependency is required.
`DefinitionId`, map/save/content formats, authored `displayName`, dialogue text and all
runtime serialization remain language-independent. Editor language is therefore not
game content localization and does not dirty an `EditorDocument` or
`ContentWorkspaceDocument`.
