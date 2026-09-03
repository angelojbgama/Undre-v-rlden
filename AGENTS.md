# AGENTS.md — Dungeon Underworld

Este arquivo define como agentes de implementação, especialmente o Codex, devem trabalhar neste repositório.

Ele consolida as decisões arquiteturais, a ordem de desenvolvimento e as restrições estabelecidas para o projeto **Dungeon Underworld / Undre-v-rlden**.

> Regra principal: este projeto não deve crescer pela criação apressada de features isoladas.
> Cada nova capacidade deve entrar na ordem correta, reutilizar a base existente e deixar o jogo mais fácil — e não mais difícil — de expandir depois.

---

# 1. Papel do agente

O agente é responsável por **implementar, validar e evoluir o projeto dentro da arquitetura aprovada**.

O agente NÃO é autorizado a:

- redesenhar a arquitetura inteira porque encontrou outra abordagem interessante;
- implementar toda ideia futura imediatamente;
- criar frameworks hipotéticos sem uso presente;
- introduzir dependências de terceiros por conveniência;
- misturar plataforma, renderização e gameplay;
- criar sistemas paralelos quando já existe uma engine ou abstração adequada;
- transformar uma fase pequena em uma refatoração geral;
- antecipar multiplayer, scripting, ECS, editor ou RPG antes de suas dependências reais.

Ao receber uma nova ideia ou tarefa, primeiro determine:

1. qual problema concreto ela resolve;
2. de quais sistemas ela depende;
3. quais sistemas futuros dependem dela;
4. em qual fase do roadmap ela pertence;
5. se antecipá-la agora reduz retrabalho real ou apenas adiciona abstração;
6. se existe um sistema atual que deve ser estendido em vez de criar outro.

Se a ideia for válida, mas ainda estiver fora de ordem, **não implemente silenciosamente**. Preserve-a como decisão/backlog e mantenha o escopo atual.

---

# 2. Visão do projeto

Dungeon Underworld é um jogo 2D top-down em C++ inspirado estruturalmente em jogos de aventura/dungeon como Zelda.

O objetivo não é somente terminar uma pequena demo. O objetivo é construir uma base própria que permita, progressivamente:

- mapas maiores e múltiplas salas;
- novos monstros;
- múltiplos comportamentos de inimigos;
- espada, arco, projéteis e outros ataques;
- dano, morte, drops e loot;
- experiência e níveis;
- itens e inventário;
- objetos interativos;
- NPCs;
- diálogos;
- quests;
- save;
- transições de mapas;
- Map Maker próprio;
- criação de conteúdo orientada a dados;
- eventualmente uma simulação headless;
- posteriormente multiplayer com servidor autoritativo.

A expansão futura deve ser consequência da arquitetura construída aos poucos, não de uma mega-engine criada antecipadamente.

---

# 3. Filosofia técnica

## 3.1 C++ nativo

Usar inicialmente:

- C++20;
- Standard Library;
- Windows SDK;
- Win32/WIC/GDI apenas nas bordas de plataforma quando necessário.

Não adicionar bibliotecas de terceiros sem autorização explícita.

O projeto deve continuar sendo uma implementação própria das principais estruturas do jogo.

## 3.2 Software renderer

A renderização principal é própria e em software.

Contrato atual:

```text
logical framebuffer = 272 × 224
internal pixel format = RGBA8
presentation = Win32
scaling = integer
filtering = nearest-neighbor
unused client area = letterbox
```

Não substituir isso por SDL, SFML, GLFW, DirectX engine, OpenGL framework ou outra biblioteca.

## 3.3 Fixed timestep

Gameplay roda em timestep fixo de 60 Hz.

Renderização não determina velocidade de gameplay.

A cadeia conceitual deve continuar:

```text
physical events
    ↓
InputState
    ↓
CommandBuilder
    ↓
PlayerCommand / commands
    ↓
fixed simulation tick
    ↓
gameplay/world state
    ↓
render state
    ↓
Renderer2D
    ↓
Framebuffer
    ↓
platform presentation
```

Nunca implementar gameplay diretamente em eventos Win32 ou no renderer.

## 3.4 Código orientado a dados, sem overengineering

Preferir:

- definições imutáveis;
- estado runtime separado;
- componentes pequenos quando realmente necessários;
- sistemas explícitos;
- IDs estáveis;
- composição.

Evitar:

- hierarquias profundas;
- `GameObject` universal;
- classes Deus;
- ECS externo;
- framework genérico antes de existir mais de um caso real;
- interfaces vazias “para o futuro”.

---

# 4. Fontes de verdade e precedência

Ao iniciar qualquer tarefa, use esta ordem:

1. instrução explícita da tarefa atual;
2. estado REAL do working tree local;
3. testes existentes e comportamento executável;
4. este `AGENTS.md`;
5. `docs/ENGINE_ARCHITECTURE.md`;
6. `docs/ROADMAP.md`;
7. `docs/MAP_FORMAT.md`;
8. `docs/ASSET_AUDIT.md`;
9. comentários históricos e demos antigas.

Documentação pode ficar desatualizada em relação ao working tree.

Nunca assuma que o GitHub remoto contém as alterações locais mais recentes.

Se houver conflito entre uma instrução nova e uma decisão arquitetural anterior, não quebre silenciosamente a arquitetura. Identifique o conflito e execute somente o menor override explicitamente necessário.

---

# 5. Procedimento obrigatório antes de editar

Antes de modificar código:

```bash
git status
git diff --check
```

Depois:

1. identifique alterações não commitadas;
2. não sobrescreva trabalho local;
3. leia os documentos relevantes;
4. inspecione integralmente os módulos que serão afetados;
5. execute o build/testes existentes quando possível;
6. confirme a baseline antes de introduzir regressões.

Documentos que devem ser considerados leitura obrigatória para mudanças estruturais:

```text
docs/ENGINE_ARCHITECTURE.md
docs/ROADMAP.md
docs/MAP_FORMAT.md
docs/ASSET_AUDIT.md
```

Não redesenhar sistemas aprovados sem necessidade concreta.

---

# 6. Estado conhecido do desenvolvimento

A baseline tecnicamente validada até a Fase 5 é:

```text
FASE 0 — arquitetura + auditoria
CONCLUÍDA

FASE 1 — Win32 + framebuffer + fixed timestep
CONCLUÍDA

FASE 2 — Renderer2D + WIC + sprites + animação + bitmap font
CONCLUÍDA

FASE 3 — RuntimeMap + TileLayer + Camera2D + culling
         + CollisionGrid + AABB
CONCLUÍDA

FASE 4 — Player + InputState + PlayerCommand + movimento
         + animação + camera follow
CONCLUÍDA

FASE 5 — fundação reutilizável de combate
CONCLUÍDA

FASE 6 — Creature Engine reutilizável
PRÓXIMA / EM ANDAMENTO
```

Baseline registrada:

```text
Compiler: MSVC 2022 x64
Language: C++20
Warnings: 0
Tests: PASS — 243 checks
git diff --check: PASS
Phase 5 commit: 4a2f440bf82e653e51a925a505443101103d735d
```

A baseline inclui:

```text
ActionEdgeBuffer
PlayerCommand com ActionIntent
EntityHandle(index, generation)
EntityHandlePool
Health / Faction
CollisionBody / Hurtbox / Hitbox / InteractionArea
AttackInstanceId
CombatSystem
EventBuffer
EntityDamaged / EntityDefeated / ProjectileImpact
ProjectileSystem / EffectSystem
animation markers
actor Y-sort
```

Ainda não existem `.dmap`, editor, save, ECS completo, IA ou Creature Engine. Nenhuma
dependência externa foi adicionada e os assets licenciados continuam fora do Git.

## Estado local prevalece

O usuário pode estar trabalhando com mudanças não commitadas.

Portanto, antes de assumir que a Fase 4 ainda está intocada, verifique o working tree.

Se a fase anterior estiver completa mas sem checkpoint e a tarefa atual autorizar um checkpoint:

1. rode build/testes;
2. confirme `git diff --check`;
3. crie o checkpoint local;
4. não reescreva histórico;
5. nunca faça force push.

Não fazer push automaticamente sem solicitação explícita.

---

# 7. Estrutura arquitetural

A direção desejada é aproximadamente:

```text
core
  ↑
simulation primitives / commands

world
  ↑
gameplay

render
  ↑
game visual composition

platform
  ↓
InputState
  ↓
CommandBuilder
  ↓
gameplay commands
```

Win32 permanece na borda.

## 7.1 `core`

Pode conter:

- tipos básicos;
- coordenadas;
- IDs;
- geometria;
- métricas;
- utilidades pequenas;
- tipos de erro/result quando necessários.

Não pode conhecer:

- Win32;
- gameplay concreto;
- renderer concreto.

## 7.2 `platform`

Responsável por:

- janela;
- message pump;
- relógio;
- input físico;
- arquivos;
- decoder de imagem;
- apresentação.

Headers públicos não devem expor `windows.h` quando isso puder ser evitado.

## 7.3 `render`

Responsável por:

- framebuffer;
- imagens;
- sprites;
- clipping;
- alpha blending;
- bitmap font;
- animação visual;
- câmera;
- desenho.

Renderer não aplica dano, não move entidades, não decide IA e não cria loot.

## 7.4 `assets`

Responsável por:

- ownership/cache;
- `AssetId`;
- carregamento;
- catálogo;
- metadados visuais;
- definições necessárias.

Não carregar o mesmo PNG todo frame.

## 7.5 `world`

Responsável por:

- `RuntimeMap`;
- tile layers;
- collision grid;
- consultas espaciais;
- colisão;
- futuramente transições.

Não duplicar collision dentro de Player, Enemy ou Scene.

## 7.6 `simulation`

Quando existir, deve conter somente primitivas genéricas necessárias naquele momento.

Exemplos aceitáveis:

```text
Tick
PlayerId
CommandSequence
PlayerCommand
event stream primitives
```

Não criar prematuramente uma classe gigantesca `Simulation` que possua:

```text
Player
Map
Camera
Renderer
Assets
Window
```

## 7.7 `gameplay`

Quando a necessidade surgir, gameplay contém regras do jogo:

- player;
- criaturas;
- combate;
- itens;
- inventário;
- NPCs;
- quests;
- progressão.

Gameplay não conhece WIC, HWND ou VK codes.

## 7.8 `game`

Faz composição das dependências e controla o executável do jogo.

Não transformar `game.cpp` em depósito de todas as regras.

## 7.9 `editor`

Só criar quando a fase de Map Maker realmente começar.

O editor compartilhará engine/mapa/assets/serialização, mas terá modelo de documento próprio.

---

# 8. Regras de ownership

Direção conceitual preferida:

```text
AssetManager
    owns Images

AnimationClip
    references SpriteSheet/Image

Player / Creature runtime state
    owns gameplay state

Visual state / Animator
    owns animation playback state

RuntimeMap
    owns map layers/collision

Camera
    owns camera state
```

Não criar ownership circular.

Não persistir ponteiros, handles de memória ou índices transitórios.

---

# 9. Coordenadas e pixel-art

Usar fronteiras claras entre:

```text
WindowPx
LogicalPx
WorldPx / WorldSubpixel
TileCoord
ViewportPx
```

Não misturar screen coordinates com world coordinates.

`GameMetrics` centraliza valores como:

```text
logicalWidth  = 272
logicalHeight = 224
tileSize      = 16
tickRate      = 60
```

Não espalhar magic numbers.

Personagens usam como posição conceitual o ponto dos pés.

Para Player idle/walk, a auditoria validou como referência:

```text
frame  = 32 × 32
anchor = aproximadamente (16, 31)
```

Anchor visual e collision body são conceitos diferentes.

---

# 10. Assets

Os assets locais originais são licenciados.

Regras:

- podem ser usados localmente para desenvolvimento;
- não adicionar PNGs licenciados ao Git;
- testes automatizados de pixels devem usar fixtures sintéticas próprias;
- não inferir regra de gameplay de um PNG estático;
- timings de animação inferidos visualmente são provisórios;
- usar os metadados auditados como ponto de partida, não como verdade absoluta onde houver `needs verification`.

A auditoria identificou, entre outros:

- Player;
- Evil Soldier;
- Skull;
- Slime;
- Training Puppet;
- espada;
- arco;
- flecha;
- TNT;
- pickups;
- tiles;
- portas;
- baús;
- crates;
- vasos;
- traps;
- UI/HUD.

Não implementar uma mecânica somente porque existe um sprite correspondente.

---

# 11. Regra para novas engines e subsistemas

Antes de criar uma nova “engine”, pergunte:

> existem pelo menos dois ou três casos concretos que justificam uma abstração compartilhada?

Se não, implemente o caso atual de forma limpa e deixe a fronteira adequada.

Sistemas especialmente importantes para expansão futura são descritos abaixo, mas devem ser introduzidos somente em suas fases.

---

# 12. Engine de Player — Fase 4

A próxima fase autorizada após a baseline da Fase 3 é o Player controlável.

Objetivo observável:

> abrir `game.exe`, controlar o Player pela dungeon, colidir com o mapa, alternar idle/walk por direção e ter a câmera seguindo o personagem.

Não implementar combate nesta fase.

## 12.1 Input

Criar `InputState` neutro.

Win32 pode ler:

```text
W A S D
Arrow Up / Down / Left / Right
```

Mas gameplay recebe intenção lógica.

Nunca enviar:

```text
VK_W
VK_LEFT
```

para gameplay.

Movimento usa estado held.

Ao perder foco da janela, limpar teclas pressionadas para evitar movimento preso.

Tratar `WM_KILLFOCUS` ou mecanismo equivalente.

`DebugInputState` da Fase 3 não pode virar input de Player.

## 12.2 PlayerCommand

A fronteira desejada é:

```text
InputState
    ↓
CommandBuilder
    ↓
PlayerCommand
```

`PlayerCommand` deve carregar pelo menos, conceitualmente:

```text
tick
player identity
sequence/order
movement intent
```

Movimento lógico:

```text
moveX ∈ {-1, 0, +1}
moveY ∈ {-1, 0, +1}
```

Entradas contraditórias devem ser determinísticas:

```text
Left + Right = 0
Up + Down    = 0
```

Comandos são produzidos/consumidos por fixed tick.

## 12.3 Player

O estado de gameplay deve conter somente o necessário, como:

```text
world position
facing
movement state
collision body information
```

Player não possui:

```text
HWND
keyboard state
Renderer2D
Image
WIC
Camera
```

## 12.4 Movimento

Preferir representação inteira/fixed-point pequena quando subpixel for necessário.

Não criar biblioteca matemática geral.

Diagonal deve ser normalizada aproximadamente para a mesma velocidade total da direção cardinal.

Não calcular `sqrt()` todo tick.

Reutilizar o sistema de colisão já existente.

Não criar um segundo sistema de AABB dentro de Player.

## 12.5 Collision body

Sprite 32×32 não significa collision body 32×32.

Collision body deve representar região próxima aos pés.

Tamanho/offset devem ser definidos conscientemente e validados visualmente.

## 12.6 Facing

Direções:

```text
Down
Up
Left
Right
```

Ao parar, conservar a última direção significativa.

Em diagonal, usar política determinística documentada.

## 12.7 Animação

Na Fase 4 somente:

```text
Idle
Walk
```

Não implementar ainda:

```text
Attack
Hurt
Dead
Shield
Sleep
WakeUp
```

Mapping auditado:

```text
row 0 = down
row 1 = up
row 2 = side-left

Right = side-left + flipX
```

Não duplicar sprite para direita.

Não resetar clip todo tick.

## 12.8 Camera follow

A câmera recebe o Player como target.

Player não possui a câmera.

Primeiro implementar follow direto + clamp.

Não antecipar:

- smoothing;
- dead zone;
- camera shake;
- cinematic camera.

## 12.9 Não criar ECS na Fase 4

Enquanto só existe Player de gameplay, NÃO criar:

```text
EntityRegistry
ComponentArray
SystemManager
Archetype
SparseSet
```

A arquitetura de entidades entra quando criaturas/objetos justificarem o custo.

---

# 13. Combate — Fase 5

Só iniciar depois do Player estar validado.

Separar claramente:

```text
CollisionBody
Hurtbox
Hitbox
InteractionArea
Trigger
```

Não usar uma única box para tudo.

Fluxo de ataque:

```text
PlayerCommand / ActionCommand
    ↓
attack state
    ↓
animation marker
    ↓
gameplay system
    ↓
activate hitbox / spawn projectile
    ↓
combat resolution
```

Renderer nunca aplica dano.

Frame markers devem ser emitidos uma vez por tick/evento.

Cada ataque deve ter um `attackInstanceId` ou equivalente para impedir que um único swing acerte o mesmo alvo repetidamente.

Fase 5 inclui inicialmente:

- espada;
- dano;
- invulnerabilidade curta;
- knockback simples;
- projétil/flecha;
- VFX transitório;
- training puppet como alvo verificável.

---

# 14. Engine de criaturas — Fase 6 e evolução posterior

O sistema de criaturas deve ser construído para que novos monstros sejam baratos de adicionar.

O objetivo final é que criar uma nova criatura normalmente exija:

```text
1. registrar assets/clips;
2. criar Creature/EnemyDefinition;
3. escolher ou configurar BehaviorProfile;
4. configurar stats;
5. configurar ataques;
6. configurar loot/drop;
7. configurar XP/recompensa quando a progressão existir;
8. colocar spawn no mapa;
```

e NÃO:

```text
criar um framework novo
+
criar um collision system novo
+
criar um combat system novo
+
criar uma arquitetura exclusiva para aquele monstro
```

## 14.1 Definition x runtime

Separar definição imutável de estado runtime.

Uma definição futura pode conter conceitualmente:

```text
EnemyDefinition
    DefinitionId
    visual / animation set
    body dimensions
    hurtboxes
    faction
    base stats
    movement parameters
    attack definitions
    behavior profile
    loot table id
    xp reward
    tags / capabilities
```

A instância runtime contém somente o estado mutável necessário:

```text
entity/persistent identity
position
facing
health
current behavior state
target
timers
cooldowns
active attack
runtime flags
```

Não copiar todas as definições para cada instância.

## 14.2 Máquina de estados de comportamento

Comportamentos reutilizáveis devem formar uma FSM explícita e observável.

Estados candidatos:

```text
Idle
Wander
Sleep
Wake
Chase
Attack
Retreat
Stunned        # somente quando houver necessidade real
Dead
```

Nem toda criatura precisa de todos.

Transições devem depender de condições claras, por exemplo:

```text
distance to target
line/query of perception
health threshold
attack range
cooldown
timer
received damage
target validity
```

Evitar IA escondida em uma cadeia gigantesca de `if` específica por sprite.

## 14.3 BehaviorProfile

Criaturas diferentes podem reutilizar a mesma infraestrutura com parâmetros diferentes.

Exemplos futuros:

```text
behavior.slime.basic
behavior.soldier.melee
behavior.skull.ranged
```

O perfil pode escolher:

- estados habilitados;
- distâncias;
- timers;
- agressividade;
- velocidade;
- preferência de ataque;
- regras simples de transição.

Não criar scripting genérico enquanto profiles + estados C++ cobrirem o conteúdo real.

## 14.4 Escolha de ataque

Ataques devem ser definições reutilizáveis.

Conceitualmente:

```text
AttackDefinition
    type
    range
    damage
    cooldown
    startup/recovery
    hitbox or projectile definition
    animation clip
    animation markers
    knockback
    optional selection weight/condition
```

A IA escolhe entre ataques válidos.

Não codificar:

```cpp
if (enemy == SKULL) ...
if (enemy == SOLDIER) ...
```

espalhado pelo engine.

Um comportamento especializado pequeno é aceitável quando um inimigo realmente possuir mecânica única.

## 14.5 Dano recebido

O fluxo deve ser centralizado:

```text
Hitbox/Projectile
    ↓
CombatSystem
    ↓
damage resolution
    ↓
Health/state
    ↓
EntityDamaged event
```

A criatura não deve inventar um sistema particular de dano.

## 14.6 Morte

Fluxo desejado:

```text
health <= 0
    ↓
enter Dead
    ↓
desabilitar capacidade de atacar
    ↓
ajustar collision/hurtbox conforme regra
    ↓
death animation / marker
    ↓
EntityDefeated event
    ↓
loot resolution
    ↓
XP/reward observer
    ↓
despawn ou persistent world delta
```

A morte não deve executar diretamente UI, quest e save.

Esses sistemas observam eventos.

## 14.7 Loot e drops

Drops pertencem a dados/loot tables.

Conceitualmente:

```text
LootTable
    entries
        item id
        chance / weight
        quantity
        conditions
```

Não espalhar RNG e criação de pickup em cada classe de inimigo.

Quando loot ainda não fizer parte da fase atual, apenas preserve a fronteira necessária; não implemente o RPG antecipadamente.

## 14.8 XP

Experiência é uma recompensa de progressão, não responsabilidade da IA.

Quando Fase 12 chegar:

```text
EntityDefeated
    ↓
reward/XP system
    ↓
player progression
```

`EnemyDefinition` pode fornecer `xpReward`, mas o monstro não incrementa diretamente o nível do Player.

## 14.9 Spawn

Spawn deve evoluir de constantes de demo para dados de mapa.

Conceitualmente:

```text
SpawnDefinition / map entity
    PersistentInstanceId
    DefinitionId
    world position
    facing
    properties/overrides
```

Spawner/factory resolve a definição e monta a entidade.

Depois da engine de criaturas estar pronta, adicionar um monstro ao mapa deve ser majoritariamente uma operação de conteúdo.

---

# 15. Entity architecture — quando realmente entrar

A arquitetura futura prevista usa:

```text
EntityHandle(index, generation)
PersistentInstanceId
DefinitionId
```

Não confundir:

- handle runtime;
- identidade persistente de mapa/save;
- identidade da definição.

Quando a quantidade de Player + inimigos + objetos justificar composição genérica, poderão surgir componentes como:

```text
Transform
Velocity
Facing
Visual
Animator
CollisionBody
HurtboxSet
HitboxSet
InteractionArea
Health
Faction
DamageSource
PlayerControlled
AIController
Inventory
Pickup
Projectile
Door
Trigger
Persistent
```

Componentes são dados.

Sistemas operam sobre conjuntos necessários.

Não transformar isso em um ECS sofisticado por vaidade técnica.

---

# 16. Map engine

A base atual já possui:

```text
RuntimeMap
TileLayer
CollisionGrid
Camera2D
tile culling
AABB collision
```

Qualquer nova funcionalidade de mapa deve estender essa base.

Não criar um `Map2`, `LevelManager` paralelo ou novo tile collision independente.

A evolução prevista inclui:

- múltiplas layers;
- ground;
- low decorations;
- foreground;
- collision separada da arte;
- spawns;
- entities;
- triggers;
- regions;
- doors;
- links;
- transições;
- persistent IDs.

---

# 17. `.dmap` e serialização — Fase 8

Não usar dump cru de structs C++.

Formato precisa ser:

- explicitamente codificado;
- versionado;
- bounds-checked;
- validado antes de construir o mundo;
- baseado em IDs estáveis.

A serialização opera em DTOs.

Erro de carregamento não pode deixar mundo parcialmente construído.

Save é formato separado do mapa.

Save deve guardar:

- Player;
- progressão necessária;
- flags;
- deltas persistentes do mundo.

Não copiar mapa inteiro para o save.

Exemplos de deltas:

```text
chest opened
switch state
destructible removed
boss defeated
quest/world flags
```

---

# 18. Map Maker — Fase 9

O editor será uma ferramenta central do projeto, mas somente depois de runtime + `.dmap` + transições estarem utilizáveis.

Executável recomendado:

```text
map_editor.exe
```

separado de:

```text
game.exe
```

Compartilhar:

- renderer;
- assets;
- mapa;
- definitions;
- serialização.

Não compartilhar o estado global do jogo.

Capacidades previstas:

- new/open/save;
- dirty state;
- validação;
- pan/zoom;
- tile palette;
- paint/erase;
- rectangle/fill;
- layers;
- collision painting;
- colocar/mover entidades;
- property editor;
- spawns;
- triggers;
- regions;
- doors;
- link validation;
- undo/redo;
- copy/paste gerando novos IDs;
- playtest in-memory;
- autosave;
- backup.

O objetivo é que desenvolvimento futuro de conteúdo dependa cada vez menos de recompilar C++.

---

# 19. Scene / game-state engine

Uma camada para cenas/estados pode ser útil para:

```text
menu
gameplay
pause
transition
game over
editor playtest
```

Mas NÃO criar agora um framework complexo de Scene Graph.

Quando dois ou mais estados reais exigirem coordenação, introduzir a menor abstração que resolva:

```text
enter
exit
update/tick
render
input/command routing
```

A scene/game-state layer coordena sistemas.

Ela não deve substituir:

- RuntimeMap;
- gameplay entities;
- Renderer2D;
- Camera2D.

Não usar “Scene” como novo `GameObject` universal.

---

# 20. Objetos, pickups, HUD e inventário — Fase 7

Objetos devem evoluir para definição + capacidades.

Capacidades possíveis:

```text
Pickup
Container
Destructible
Interactable
Door
Trigger
```

Não criar uma subclasse profunda por objeto.

HUD observa estado somente leitura, preferencialmente via `GameViewModel` ou snapshot.

HUD não modifica Player diretamente.

---

# 21. NPC e diálogo — Fase 10

NPCs devem ser orientados por definições.

Mapa guarda:

```text
NpcDefinitionId
position
facing
small overrides
```

Sistema compartilhado cuida de:

- interação;
- facing;
- diálogo;
- paginação;
- choices;
- conditions;
- pequenas actions.

Dois NPCs diferentes devem reutilizar o mesmo sistema.

---

# 22. Quests — Fase 11

Quests devem consumir eventos de domínio.

Não implementar quest por polling acoplado do tipo:

```text
todo tick:
    procurar se slime morreu
    procurar se baú abriu
```

Usar event stream, por exemplo:

```text
EntityDefeated
ItemPickedUp
ChestOpened
RegionEntered
DialogueCompleted
```

Objetivos declarativos previstos:

```text
talk
kill
pickup
enter
open
deliver
```

Quest state deve sobreviver save/load.

---

# 23. RPG, XP e equipment — Fase 12

Somente depois de existir vertical slice estável.

Inclui:

- stats derivados;
- XP;
- level curve;
- equipment slots;
- modifiers;
- loot tables;
- inventário expandido;
- UI associada.

Conteúdo deve entrar por definição.

Não alterar engine base toda vez que surgir um novo item ou monstro.

---

# 24. Multiplayer — Fases 13 e 14

Não implementar rede cedo.

A preparação correta já começa pelas fronteiras:

```text
PlayerCommand
tick
player identity
sequence
simulation state
stable IDs
```

Mas NÃO criar agora:

```text
Winsock layer
lobby
prediction
reconciliation
rollback
network ECS
```

## Fase 13 primeiro

Antes da rede real:

- auditar autoridade;
- separar state ownership;
- permitir simulação sem janela/render;
- gravar/reproduzir command streams;
- numerar snapshots;
- definir network entity IDs;
- medir nondeterminismo relevante.

Aceite:

> simulação headless executa cenário gravado e alcança estado esperado.

## Fase 14 depois

Somente então:

- adapter Winsock;
- framing;
- handshake;
- protocol versioning;
- server authoritative;
- command upload;
- snapshots/deltas;
- prediction/reconciliation quando necessário;
- interpolation;
- timeout;
- rate limits;
- testes com latency/loss.

---

# 25. Portões de escopo

Estes gates são obrigatórios:

```text
não iniciar Map Maker
antes de runtime map + serialização + formato de mapa utilizáveis

não iniciar quests
antes de event stream + IDs persistentes

não iniciar RPG completo
antes do vertical slice estar estável e jogável

não iniciar networking
antes de simulação headless + replay/command stream

não criar ECS genérico
antes de múltiplas entidades reais justificarem

não criar scripting
enquanto definições + sistemas C++ resolverem os casos reais
```

---

# 26. Como decidir se uma etapa pode ser antecipada

Às vezes uma engine planejada para depois pode reduzir muito o retrabalho.

É permitido PROPOR antecipação somente quando todos os critérios abaixo forem verdadeiros:

1. as dependências dela já existem;
2. existe uso concreto imediato;
3. ela desbloqueia pelo menos duas tarefas próximas;
4. não viola um gate de escopo;
5. pode ser entregue testável em um incremento pequeno;
6. não exige inventar abstrações para conteúdo inexistente;
7. reduz acoplamento ou retrabalho mensurável.

Exemplo aceitável:

> criar uma pequena fronteira de `PlayerCommand` antes de multiplayer porque Player já precisa dela para input, testes e replay futuro.

Exemplo não aceitável:

> criar protocolo de rede, serializer de snapshots e prediction porque multiplayer será desejado algum dia.

---

# 27. Priorização macro atual

Ordem de desenvolvimento de referência:

```text
0  Auditoria + arquitetura                 DONE
1  Plataforma + janela + framebuffer       DONE
2  Renderer + PNG + sprites + animation    DONE
3  Tilemap + câmera + colisão              DONE

4  Player + InputState + PlayerCommand       DONE
5  Combate mínimo                            DONE
6  Criaturas / inimigo reutilizável          NEXT / IN PROGRESS
7  Objetos + pickup + HUD + inventário
8  .dmap + transições + save
9  Map Maker
10 NPC + diálogo
11 Quests
12 RPG + XP + equipment + loot
13 Auditoria multiplayer/headless/replay
14 Networking
```

A ordem pode ser refinada, mas não deve ser ignorada sem análise de dependências.

---

# 28. Qualidade e testes

Cada incremento deve terminar em algo:

- compilável;
- executável;
- observável;
- testável.

Não avançar com regressões conhecidas.

## Regras

- manter `/W4`;
- objetivo de 0 warnings;
- preservar testes existentes;
- adicionar testes para toda regra nova relevante;
- preferir fixtures sintéticas próprias;
- executar `git diff --check`;
- validar erros e limites;
- não depender apenas de smoke test visual.

## Smoke tests

Quando existir comportamento visual/interativo, também validar manualmente.

Para Player Fase 4, exemplos obrigatórios:

- cardinal;
- diagonal;
- direções opostas;
- collision wall;
- slide;
- corner;
- corridor;
- camera follow;
- camera clamp;
- foreground occlusion;
- integer scaling;
- resize;
- perda de foco com tecla mantida.

Teste de foco:

```text
1. segurar W;
2. retirar foco da janela;
3. soltar W fora da janela;
4. voltar ao jogo;
5. Player NÃO pode continuar andando.
```

---

# 29. Performance

Não otimizar prematuramente.

Mas também não introduzir ineficiências obviamente erradas.

Não:

- carregar PNG todo frame;
- reconstruir clips todo frame;
- copiar spritesheet todo frame;
- alocar comando pequeno no heap sem motivo;
- iterar mapa inteiro quando culling/query espacial já existe;
- testar colisão contra todas as entidades do mundo;
- fazer lógica de gameplay no renderer.

Otimização deve seguir profiling ou problemas concretos.

---

# 30. Regras para alteração de código existente

Preferir a menor mudança coerente.

Antes de criar uma nova função, procure se já existe:

- conversão de coordenadas;
- movimento contra tiles;
- asset cache;
- clip/animator;
- camera clamp;
- tile culling;
- draw function.

Não duplicar implementação.

Se uma função existente precisar atender um segundo caso, faça a menor generalização necessária e preserve testes anteriores.

---

# 31. Build

Build principal permanece via:

```text
build.bat
```

Configuração esperada:

```text
MSVC x64
C++20
/W4
```

Atualizar o build apenas para incluir novos `.cpp` e necessidades reais.

Não introduzir CMake como requisito sem autorização.

---

# 32. Git

Nunca:

- force push;
- apagar histórico;
- resetar trabalho do usuário;
- descartar arquivos não reconhecidos;
- commitar assets licenciados;
- commitar `build/`, `.exe`, `.obj`, `.pdb`, `.ilk`.

Antes de operações destrutivas, pare e preserve o estado.

Commit/push não fazem parte automaticamente de toda tarefa.

Se uma tarefa explicitamente pedir checkpoint de fase, um commit local de checkpoint é aceitável depois de build/testes passarem.

Push exige solicitação explícita.

---

# 33. Relatório esperado ao terminar uma tarefa

Ao final de uma implementação, informar de forma objetiva:

```text
Resumo
- o que foi implementado

Arquitetura
- decisões relevantes
- sistemas reutilizados
- qualquer pequena generalização feita

Arquivos
- arquivos criados/modificados/removidos

Validação
- build
- warnings
- quantidade de checks/testes
- smoke tests realizados
- git diff --check

Escopo
- o que foi deliberadamente NÃO implementado
- dependências futuras preservadas

Git
- status final
- commit criado ou não
- push realizado ou não
```

Não declarar uma fase concluída somente porque compilou.

Conclusão exige critérios de aceite.

---

# 34. Regra de expansão de conteúdo

Este é um dos objetivos mais importantes do projeto.

A arquitetura deve evoluir até permitir:

## Novo monstro

Idealmente:

```text
registrar assets
+ EnemyDefinition
+ BehaviorProfile existente ou pequeno profile novo
+ AttackDefinitions
+ loot/xp
+ spawn no mapa
```

## Novo mapa

Idealmente:

```text
abrir Map Maker
+ pintar tiles/layers
+ collision
+ colocar entities/spawns/triggers/doors
+ salvar .dmap
```

## Novo NPC

Idealmente:

```text
NpcDefinition
+ diálogo/conditions/actions
+ colocar no mapa
```

## Nova quest

Idealmente:

```text
quest definition
+ objectives declarativos
+ rewards
```

Se adicionar conteúdo novo exigir modificar muitos sistemas centrais, pare e verifique se a abstração existente está incompleta.

---

# 35. Anti-padrões proibidos

Evitar explicitamente:

```text
Win32 → Player diretamente
Renderer → modifica gameplay
Animation frame → aplica dano diretamente no renderer
Enemy subclass por cada monstro sem necessidade
Map duplicado para cada sistema
Sprite AABB usado como collision/hurtbox/hitbox ao mesmo tempo
Quest polling de sistemas internos
Save de ponteiros/handles
Dump binário cru de structs
ECS genérico antecipado
Scene graph universal
Manager que conhece tudo
Singleton global para facilitar acesso
biblioteca externa para substituir engine existente
```

---

# 36. Princípio final

Ao escolher entre:

```text
A) terminar uma capacidade pequena, verificável e reutilizável
```

e

```text
B) construir antecipadamente uma arquitetura enorme para features futuras
```

escolha **A**.

Mas ao escolher entre:

```text
A) duplicar uma solução específica pela terceira vez
```

e

```text
B) extrair a engine mínima comum que os casos reais já demonstraram
```

escolha **B**.

O objetivo é construir Dungeon Underworld como uma sequência de sistemas pequenos que se encaixam, até que adicionar mapas, criaturas, NPCs, quests e conteúdo deixe de exigir reescrever o jogo.
