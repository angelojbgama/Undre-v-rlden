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
FASE 9 — Map Maker                                     IN PROGRESS
FASE 10 — NPC + diálogo
FASE 11 — Quests
FASE 12 — RPG + XP + equipment + loot
FASE 13 — Headless + replay + auditoria multiplayer
FASE 14 — Networking
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
Y-sort, resize/letterbox, perda de foco e `WM_CLOSE`. A Fase 8 permanece não
iniciada e é a próxima etapa.

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

Banco funcional, `.dmap`, save, loot/XP, equipment stats e drops de inimigos não
foram implementados.

---

# Fase 8 — `.dmap`, transições e save

## Status

**Concluída.** DMAP 1.0 alimenta o `game.exe`; duas salas são resolvidas por
`MapId`, construídas por `RuntimeWorldBuilder` e trocadas por `MapSession` em boundary
de tick. `SessionWorldState` registra deltas de Chest, Crate e Pickup para A→B→A e é
a mesma estrutura serializada por DSAV 1.0. F5/F9 são edges lógicos; save usa
temporário + backup e load prepara o novo world antes do swap.

Baseline de fechamento: MSVC 19.44 x64, C++20, `/W4`, 0 warnings, 403 checks,
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

**IN PROGRESS — Block 1.** O editor e runtime compartilham `TilesetCatalog` no
`GameContentRegistry`. Multi-tileset authoring está implementado: `MapTileReference`
persiste `DefinitionId`, o runtime resolve `world::TilesetId` local e o Map Maker oferece
selector, palette dinâmica, painting/rectangle/fill e eyedropper por pack. A validação
rejeita tileset desconhecido, source index fora do atlas e tile size incompatível. DMAP e
DSAV permanecem v1.0. O smoke interativo geral ainda é gate separado para declarar Block 1
como concluído.

O checkpoint semântico adiciona `AuthoringSemanticRegistry` para as 72 células visíveis
do atlas Dungeon, oito stamps visuais, paleta/inspector semânticos e validação advisory
separada da validação estrutural. `PlaceStampCommand` preserva undo/redo atômico e rejeita
layer bloqueada ou placement fora dos limites antes de escrever. A aceitação final ainda
requer rebuild MSVC `/W4`, testes e smokes Windows; não avançar formatos nem os itens
deferred antes desse gate.

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

---

# Fase 12 — RPG, XP, equipment e loot

## Gate

Somente depois de existir vertical slice estável e jogável.

## Capacidades

- stats derivados;
- XP;
- level curve;
- equipment slots;
- modifiers;
- loot tables;
- inventário expandido;
- UI associada.

`EnemyDefinition` pode fornecer reward metadata; Creature não incrementa diretamente XP do Player.

Loot tables resolvem drops; não espalhar RNG em cada classe de inimigo.

## Aceite

Novo item/monstro entra principalmente por definitions e dados, sem alterar engine base a cada conteúdo.

---

# Fase 13 — headless, replay e auditoria multiplayer

## Gate

Networking real não começa aqui.

## Objetivo

Provar que gameplay/simulation consegue existir sem janela/render e que command streams podem reproduzir cenários.

## Capacidades

- auditar autoridade/state ownership;
- separar dependências restantes de apresentação;
- execução headless;
- gravar/reproduzir command streams;
- numerar snapshots;
- definir identidade replicável/network IDs;
- medir nondeterminismo relevante.

## Aceite

Simulação headless executa cenário gravado e alcança estado esperado.

---

# Fase 14 — Networking

Somente depois da Fase 13.

## Capacidades iniciais

- adapter Winsock;
- framing;
- handshake;
- protocol versioning;
- conexão;
- command upload;
- servidor autoritativo;
- snapshots/deltas;
- interpolation;
- timeout/rate limits;
- latency/loss testing.

Prediction/reconciliation entram somente quando medições/experiência de jogo justificarem.

## Primeiro marco

Dois clientes em LAN veem movimento autoritativo; combate/networking é expandido em incrementos próprios depois.

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

não iniciar networking
antes de simulação headless + replay/command stream

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
e a mesma fronteira também ajuda testes/replay/multiplayer futuro.
```

Exemplo atual aceitável:

```text
EntityHandle mínimo na fundação de combate,
porque sword target, projectile target e creatures precisarão da mesma identidade runtime.
```

Exemplo não aceitável:

```text
criar ECS completo, network snapshots ou scripting engine
porque talvez sejam úteis no futuro.
```

---

# Ordem recomendada a partir do estado atual

```text
validar/checkpoint Fase 4 local
        ↓
5A runtime identity + combat primitives
        ↓
5B sword + Training Puppet
        ↓
5C projectile + arrow + VFX
        ↓
6A EnemyDefinition/runtime/factory
        ↓
6B BehaviorProfile/FSM
        ↓
6C attack selection
        ↓
6D death/events
        ↓
6E segundo perfil/inimigo
        ↓
7 objetos/pickup/HUD/inventário
        ↓
8 .dmap/transições/save
        ↓
9 Map Maker
```

Essa ordem prepara bases reutilizáveis imediatamente antes de seus consumidores reais, evitando tanto duplicação quanto overengineering.

---

# Regra final do roadmap

O roadmap é uma ordem de dependências, não uma obrigação de construir toda abstração desenhada antecipadamente.

Se o código real mostrar que uma pequena fundação simplifica várias etapas seguintes, ela pode ser antecipada conforme os critérios acima.

Se uma abstração ainda não possui consumidores reais, preservar apenas a fronteira arquitetural e continuar construindo o próximo comportamento jogável.

# Phase 9 current content checkpoint

Phase 9 Block 1 now uses three small authored gameplay maps (`map.dungeon.01` through
`map.dungeon.03`) instead of generated demo rooms or the editor playground. The maps
are linked in both directions, remain DMAP 1.0, and exercise the current enemy,
object, pickup, spawn, collision, semantic-tile, and stamp authoring contracts.

`MapComposer` remains a small composition foundation for deterministic room geometry;
procedural generation, MapLogic, and LLM blueprint production remain deferred.
