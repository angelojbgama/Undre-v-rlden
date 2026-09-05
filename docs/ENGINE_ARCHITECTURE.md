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
declarada concluída. `.dmap`, editor, save, loot/XP, NPCs, quests e networking
permanecem deferidos. -->

A Fase 7 está concluída. O código-base está em
`7873e32222b1e8d72e996771a88a6890b0eb9220` e o fechamento Windows/fix em
`7cc9da495d314de52ab097f890594dd7deb2d0a4`. A baseline validada usa Windows 11
x64, MSVC 19.44.35219 (toolset da linha Visual Studio 2022), Windows SDK
10.0.26100.0, C++20, `/W4`, 0 warnings e 347 checks, com `git diff --check` e
smoke visual/interativo passando.

A Fase 8 também está concluída: DMAP/DSAV v1, `MapData`, persistent IDs,
`RuntimeWorldBuilder`, `MapCatalog`, `MapSession`, transições e deltas de sessão/save
alimentam o slice jogável com os mapas authored atuais. A Fase 9 (Map Maker) foi fechada
no checkpoint Linux atual: o editor possui validação revision-cached, culling de tiles,
playtest por snapshot via `RuntimeWorldBuilder` e sidecar `.autosave.dmap` sem substituir
o arquivo authored. A Fase 10 está em andamento; NPC foundation e dialogue data model
estão concluídos, assim como sessão/UI, conditions, actions e flags persistentes. A
Fase 11 possui definições, estado runtime, progressão por eventos e persistência de
quests; loot/XP e networking permanecem deferidos.

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
rollback/prediction
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

Não se promete determinismo bit a bit entre máquinas. Inteiros/subpixels são preferidos para estabilidade do gameplay atual; networking futuro poderá trabalhar com snapshots e correções.

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

são sinais para gameplay. O renderer apenas desenha o frame selecionado.

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
Animator / gameplay marker
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

## 17. Multiplayer futuro

Preparação saudável já existente ou permitida:

```text
PlayerCommand
tick
player identity
sequence
stable IDs
domain events
separação de estado e render
```

Isso não autoriza implementar rede agora.

Antes de Winsock real:

- separar simulação de janela/render;
- permitir execução headless;
- gravar/reproduzir command streams;
- numerar snapshots;
- definir identidade replicável;
- medir nondeterminismo relevante.

Só depois:

```text
handshake
protocol version
server authoritative
command upload
snapshots/deltas
prediction/reconciliation quando necessário
interpolation
timeouts/rate limits
```

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

The current playable content is the three-map DMAP 1.1-capable set in `maps/gameplay/`:
`map.dungeon.01`, `map.dungeon.02`, and `map.dungeon.03`. They are registered together
through the small official map manifest so `MapCatalog::validateLinks()` resolves the
bidirectional 01 <-> 02 <-> 03 graph before a session starts. Startup selects Map 01
and `entry.start`; `--map` and `--spawn` remain explicit overrides.

The runtime path is:

```text
GameLaunchOptions
    -> official startup map selection
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
DMAP is unchanged.

## Audit foundation (Block A)

The project now has a small platform-neutral observability boundary. `Phase7Demo` can
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
`Phase7Demo` with `HeadlessAuditPlatform`, injects logical `InputState` values on
controlled ticks, calls the real fixed-tick/update boundary and presents the real
272x224 framebuffer. Assertions read the value-only `GameAuditSnapshot`; failures
write an audit event, forced state checkpoint and framebuffer screenshot before the
session closes with a non-zero process result.

The runner supports independent scenario sessions, `--all`, `--scenario`, `--seed`,
`--ticks`, `--audit-root` and explicit `--asset-root`/`UNDERWORLD_ASSET_ROOT`
selection. It uses a small injected synthetic decoder for portable runs without
local licensed assets, while preserving the production `ImageDecoder` boundary.
Quest scenario names remain startup smoke entries until the game exposes a real
player command for quest start; no quest state is fabricated by the harness. Formal
command replay, state hashing, manual/F12 integration and Linux build remain later
blocks.
