# ASSET_MAP_SPEC.md — Especificação de uso de assets para criação de mapas

> **Status:** documento evolutivo — Parte 1 concluída: regras globais + tiles/arquitetura/interativos do diretório `Tileset/`.
>
> **Objetivo:** servir como contrato de autoria para humanos, Map Maker e LLMs que criem, editem ou corrijam mapas de **Dungeon Underworld**. Este documento não é apenas um inventário visual: ele descreve **como cada asset pode ou não pode participar da construção do mundo**.

---

# 1. Relação com os demais documentos

Este documento complementa, e não substitui:

- `ASSET_AUDIT.md`: inventário físico/visual dos PNGs;
- `ENGINE_ARCHITECTURE.md`: separação entre render, world, gameplay e assets;
- `MAP_FORMAT.md`: representação futura de layers, collision, entities, triggers e links;
- `ROADMAP.md`: ordem em que runtime, `.dmap` e Map Maker serão implementados.

Regra conceitual:

```text
ASSET_AUDIT.md
    diz o que existe fisicamente

ASSET_MAP_SPEC.md
    diz como o conteúdo pode ser usado semanticamente

MAP_FORMAT.md
    diz como essa intenção será persistida no mapa
```

Uma LLM que editar mapas deve consultar **ASSET_MAP_SPEC.md antes de escolher tiles ou objetos**.

---

# 2. Escopo do catálogo

O pacote auditado contém 84 imagens únicas; a cópia aninhada é duplicata byte a byte e não deve gerar registros semânticos duplicados.

A especificação deve trabalhar sempre com um **caminho canônico** por asset.

Nesta versão estão detalhados:

```text
Tileset/Walls_trap_arrows.png
Tileset/block.png
Tileset/block_2.png
Tileset/block_destroied.png
Tileset/breaking_crate.png
Tileset/breaking_vase.png
Tileset/chest.png
Tileset/commands_sing.png
Tileset/crate.png
Tileset/door_back.png
Tileset/door_front.png
Tileset/door_side.png
Tileset/fire_block.png
Tileset/fire_block_destroied.png
Tileset/fire_block_with_fire.png
Tileset/gate.png
Tileset/iron_door.png
Tileset/shop_block.png
Tileset/statue.png
Tileset/tile_spickes.png
Tileset/tileset.png
Tileset/vase.png
```

Partes futuras devem catalogar, com o mesmo contrato:

```text
Characters/
Training_puppet/
Objects/
Explosion/
Icons/
menus/backgrounds/font
```

---

# 3. Níveis de confiança

Toda interpretação semântica deve indicar confiança.

```text
CONFIRMED
    informação diretamente observável ou já validada no projeto

PROBABLE
    interpretação visual forte e adequada como default de autoria,
    mas que ainda precisa de validação no jogo/editor

UNVERIFIED
    não há evidência suficiente; a LLM não deve inventar comportamento
```

**Regra para LLM:** `UNVERIFIED` nunca deve virar uma mecânica implícita apenas porque “parece fazer sentido”.

---

# 4. Unidade de autoria: tile, stamp, entity ou visual state

Antes de inserir qualquer imagem no mapa, classificar o asset em uma destas categorias:

## 4.1 `TILE`

Célula estática que pode ser usada por uma `TileLayer`.

Exemplo principal:

```text
Tileset/tileset.png :: célula 16×16
```

## 4.2 `STAMP`

Conjunto de múltiplas células cuja coerência visual depende de serem colocadas juntas.

Exemplos:

```text
moldura de parede 3×3
estrutura 2×2
borda horizontal de 3 tiles
```

Uma LLM não deve quebrar um stamp em peças aleatórias apenas porque cada pedaço cabe em uma célula.

## 4.3 `ENTITY`

Objeto posicionado no mundo que possui identidade, footprint, interação, estado, animação, persistência ou comportamento próprio.

Exemplos:

```text
chest
crate
vase
door
gate
spike trap
```

A arte pode ter 16×16, mas isso **não transforma o objeto em tile**.

## 4.4 `VISUAL_STATE`

Imagem ou frame que representa um estado de uma entidade existente e **não deve ser colocado como objeto independente**.

Exemplos:

```text
block_destroied.png
breaking_crate.png
breaking_vase.png
fire_block_destroied.png
```

O mapa deve declarar a entidade e, quando necessário, um estado inicial; não deve declarar “o PNG destruído” como um objeto sem relação com sua definição.

---

# 5. Contrato de metadados para cada asset

O catálogo completo deverá fornecer registros conceitualmente equivalentes a:

```text
assetPath
semanticId              # nome estável sugerido, independente do filename físico
classification          # TILE | STAMP | ENTITY | VISUAL_STATE
visualSizePx
frameSizePx
frameCount
logicalFootprintTiles
anchor / baseline
preferredLayer
collisionPolicy
occlusionPolicy
hostRequirement         # parede, piso, boundary etc.
stateGroup               # quando o asset pertence a outra entidade
allowedTransforms
placementRules[]
forbiddenUses[]
confidence
notes
```

O `semanticId` não precisa repetir erros de digitação dos arquivos físicos.

Exemplo:

```text
Tileset/tile_spickes.png
    semanticId = trap.spikes
```

O catálogo de assets é responsável por mapear o ID estável ao nome real do PNG.

---

# 6. Regras globais de autoria de mapa

## 6.1 Arte e colisão são dados separados

Nunca gerar collision automaticamente pela transparência, formato do sprite ou cor do tile.

```text
visual tile/entity
        ≠
CollisionGrid / CollisionBody
```

A imagem diz como algo parece. O mapa/gameplay diz se aquilo bloqueia, machuca, ativa trigger ou permite interação.

## 6.2 Frame de animação não é conteúdo de mapa

É proibido construir um mapa selecionando frames internos como se fossem objetos diferentes.

Incorreto:

```text
colocar frame 0 de chest como baú fechado
colocar frame 4 de chest como outro objeto "baú aberto"
```

Correto:

```text
entity = object.chest
initialState = closed/opened quando o formato permitir
```

## 6.3 Estado destruído não é um tipo novo de objeto

Arquivos `*_destroied` e spritesheets `breaking_*` pertencem ao ciclo visual da mesma entidade.

## 6.4 Objetos altos usam a base lógica, não o retângulo visual inteiro

Assets 16×32 frequentemente representam um objeto que ocupa aproximadamente uma célula no chão e se estende visualmente para cima.

Conceito:

```text
visual sprite
      ↑
      │ parte que pode ocluir
──────┼──────── baseline / base
 footprint lógico no chão
```

A posição de autoria deve preferir **base/pés**, coerente com a arquitetura do Player.

## 6.5 Oclusão não é colisão

Uma estátua pode desenhar 32 px de altura e bloquear apenas sua base.

Uma parede pode bloquear movimento e também possuir parte visual em `decoration_high` para aparecer na frente do Player.

Essas decisões não devem ser fundidas.

## 6.6 Transformações não são presumidas

Não rotacionar nem espelhar automaticamente um asset para “criar uma variante” sem regra registrada.

O formato prevê `flipX`, mas a possibilidade técnica não significa que todo tile ou objeto foi desenhado para espelhamento.

## 6.7 Célula vazia do atlas não é tile de chão

Uma célula totalmente transparente em `tileset.png` significa **ausência de tile no atlas**, não “piso vazio”.

## 6.8 Cor de fundo não define walkability

A cor escura `(54,30,38)` aparece como fundo lógico e como área negativa em vários assets. Ela não deve ser interpretada automaticamente como chão caminhável.

---

# 7. Layers de autoria

Usar a direção já prevista pelo formato de mapa:

```text
ground
walls
decoration_low
objects_visual
decoration_high
```

Complementadas por dados não visuais:

```text
collision
entities
triggers
regions
links
```

## 7.1 `ground`

Superfície visual inferior.

Não recebe automaticamente collision.

## 7.2 `walls`

Arquitetura fixa que define paredes, bordas e estruturas.

Pode ter células correspondentes marcadas como solid no `CollisionGrid`, mas a relação é explícita.

## 7.3 `decoration_low`

Detalhes abaixo de Player/entidades que não exigem identidade própria.

## 7.4 `objects_visual`

Composição visual de entidades e objetos do mundo.

Objetos stateful não devem existir somente nesta layer; a entidade é autoridade.

## 7.5 `decoration_high`

Partes que devem ocluir personagens e objetos quando visualmente passam à frente deles.

---

# 8. `Tileset/tileset.png`

## 8.1 Dados confirmados

```text
image size   = 304 × 192
cell size    = 16 × 16
grid         = 19 × 12
origin       = top-left
coordinates  = (x,y), zero-based
```

A inspeção direta do PNG atual encontrou **72 células com pixels visíveis**. Todas as demais células são totalmente transparentes.

As 72 células preenchidas são:

```text
y0:  x4-x6
y1:  x14
y2:  x0-x8,x10-x14
y3:  x0-x2,x4-x10,x12-x14
y4:  x0-x4,x7-x18
y5:  x7,x11-x14
y6:  x1,x4,x5,x7,x14
y7:  x1,x2,x4-x7
y8:  x1,x7
y9:  x3-x5
y10: nenhum
y11: x3-x5
```

Observação: o `ASSET_AUDIT.md` atualmente diz “73”, mas a lista de coordenadas registrada nele também soma 72. Para autoria deve prevalecer o PNG efetivamente inspecionado.

## 8.2 Regra fundamental do atlas

As células preenchidas são **células opacas completas**. Mesmo quando um tile parece apenas uma rachadura, borda ou pequeno detalhe, ele inclui o fundo da célula.

Consequência:

> Não tratar as células do atlas como decals transparentes sobre um piso arbitrário.

Uma variante de rachadura é uma **variante completa da célula de piso/fundo**, não um overlay universal.

## 8.3 O atlas não é uma lista de 72 peças independentes

A disposição visual mostra várias estruturas desenhadas através de células adjacentes.

A LLM deve preferir:

```text
semantic tile families
+
approved stamps
+
adjacency rules
```

em vez de escolher coordenadas aleatórias.

## 8.4 Stamps visuais confirmados pelo encaixe

Os nomes abaixo são nomes de autoria provisórios; descrevem a forma observável e não atribuem gameplay.

### `stamp.masonry_frame_3x3`

Moldura de alvenaria 3×3 com centro vazio:

```text
(2,2) (3,2) (4,2)
(2,3) EMPTY (4,3)
(2,4) (3,4) (4,4)
```

Regra:

- usar como conjunto coerente;
- não usar o centro vazio como “tile especial”;
- não inverter/rotacionar até existir regra explícita.

### `stamp.inset_2x2`

Estrutura visual quadrada/rebaixada de 2×2:

```text
(9,3)  (10,3)
(9,4)  (10,4)
```

Interpretação funcional — escada, poço, passagem ou outro — permanece **UNVERIFIED**.

A LLM pode preservar o stamp visual, mas não deve atribuir trigger/transição sem definição semântica adicional.

### `stamp.vertical_strip_left_1x3`

Sequência vertical contínua:

```text
(1,6)
(1,7)
(1,8)
```

O tile central possui detalhe ornamental.

### `stamp.vertical_strip_right_1x4`

Sequência vertical contínua:

```text
(7,5)
(7,6)
(7,7)
(7,8)
```

### `stamp.small_masonry_2x2`

Estrutura compacta visualmente contínua:

```text
(4,6) (5,6)
(4,7) (5,7)
```

O significado funcional permanece **UNVERIFIED**.

### `stamp.horizontal_ledge_3x1`

```text
(3,9) (4,9) (5,9)
```

Borda/ledge horizontal contínuo.

### `stamp.horizontal_toothed_3x1`

```text
(3,11) (4,11) (5,11)
```

Borda horizontal com padrão inferior dentado/irregular.

Não assumir dano/hazard a partir do desenho.

### `stamp.top_cap_3x1`

```text
(4,0) (5,0) (6,0)
```

Conjunto horizontal visualmente contínuo com alvenaria nas extremidades.

O padrão inferior não deve ser interpretado automaticamente como spikes de gameplay.

## 8.5 Famílias visuais provisórias

Estas famílias existem para impedir seleção aleatória. Elas precisam de refinamento futuro em um catálogo tile-a-tile.

### `family.masonry_structure`

Maior concentração:

```text
x0-x8 / y2-y4
x1 / y6-y8
x4-x7 / y6-y8
x3-x5 / y9
x3-x5 / y11
x4-x6 / y0
```

Uso:

- paredes;
- molduras;
- bordas;
- segmentos arquitetônicos.

Não misturar com floor variants sem analisar o encaixe visual.

### `family.surface_variants`

A região direita do atlas contém células visualmente mais planas, rachadas ou ornamentadas, especialmente ao redor de:

```text
x11-x18 / y3-y5
```

Uso provável:

- superfície/piso;
- variantes rachadas;
- detalhes incorporados à própria célula.

Como as células são opacas, usar somente sobre a família de superfície compatível.

### `family.architectural_detail`

Há células com grades, painéis, saliências e padrões integrados às paredes.

Essas células devem ser selecionadas como variante de um segmento estrutural compatível, não como decoração flutuante.

## 8.6 Regra de adjacency

Até existir uma tabela de adjacência validada, a LLM deve seguir a política conservadora:

1. copiar estruturas/stamps já aprovados;
2. prolongar apenas segmentos claramente contínuos;
3. não juntar duas bordas que visualmente terminam em tipos diferentes;
4. não usar uma quina como segmento reto;
5. não deixar metade de uma moldura sem intenção explícita;
6. não preencher o interior de uma estrutura com tiles de parede por conveniência;
7. não deduzir collision a partir da família visual.

Uma versão futura deste documento deve possuir uma tabela do tipo:

```text
tile A.rightCompatibleWith = [B,C,D]
tile A.leftCompatibleWith  = [...]
tile A.topCompatibleWith   = [...]
tile A.bottomCompatibleWith= [...]
```

ou stamps semânticos suficientes para que a LLM raramente precise operar coordenadas brutas.

---

# 9. Objetos arquitetônicos e interativos

## 9.1 `Tileset/block.png`

```text
semanticId         = object.block
classification     = ENTITY
visualSizePx       = 16×32
frames             = 1
logicalFootprint   = PROBABLE 1×1 tile na base
anchor             = PROBABLE bottom-center / base
preferredLayer     = objects_visual
collisionPolicy    = explicit CollisionBody; provável base de 1 tile
occlusionPolicy    = sprite pode se estender acima da base
confidence         = PROBABLE
```

Regras:

- colocar em uma célula de piso, não dentro de uma layer de parede;
- posicionar pela base;
- não considerar os 32 px de altura como footprint de 1×2 automaticamente;
- não deduzir se é empurrável, destrutível ou apenas sólido pelo sprite.

## 9.2 `Tileset/block_2.png`

```text
semanticId         = object.block.variant_02
classification     = ENTITY visual variant
visualSizePx       = 16×32
logicalFootprint   = PROBABLE 1×1
stateGroup         = family object.block
```

Mesmas regras de `block.png`.

Não criar uma engine ou classe separada apenas porque o desenho é diferente.

## 9.3 `Tileset/block_destroied.png`

```text
semanticId         = visual.object.block.destroyed
classification     = VISUAL_STATE
visualSizePx       = 16×32
stateGroup         = object.block
```

Proibido:

- inserir como “objeto destruído independente” por default;
- manter simultaneamente bloco intacto + bloco destruído na mesma instância.

Correto:

```text
entity definition = object.block
runtime/initial state = destroyed
```

quando essa capacidade existir.

---

# 10. Crate

## 10.1 `Tileset/crate.png`

```text
semanticId         = object.crate
classification     = ENTITY
visualSizePx       = 16×32
frames             = 1
logicalFootprint   = PROBABLE 1×1
anchor             = PROBABLE base center
preferredLayer     = objects_visual
collisionPolicy    = explicit; provável base de 1 tile
confidence         = PROBABLE
```

## 10.2 `Tileset/breaking_crate.png`

```text
semanticId         = visual.object.crate.breaking
classification     = VISUAL_STATE / ANIMATION
sheetSizePx        = 224×32
frameSizePx        = 32×32
frames             = 7
stateGroup         = object.crate
```

Regra crítica:

- o mapa coloca **uma crate**;
- os 7 frames não são sete objetos nem sete tiles;
- a animação de destruição possui frame visual maior que o sprite intacto;
- o footprint lógico não cresce quando detritos se espalham visualmente.

Para alinhar intacto e breaking, a instância deve manter o mesmo ponto de base e os clips usam anchors/draw offsets próprios.

---

# 11. Vase

## 11.1 `Tileset/vase.png`

```text
semanticId         = object.vase
classification     = ENTITY
visualSizePx       = 16×32
logicalFootprint   = PROBABLE 1×1
anchor             = PROBABLE base center
preferredLayer     = objects_visual
```

## 11.2 `Tileset/breaking_vase.png`

```text
semanticId         = visual.object.vase.breaking
classification     = VISUAL_STATE / ANIMATION
sheetSizePx        = 192×32
frameSizePx        = 32×32
frames             = 6
stateGroup         = object.vase
```

Mesma regra de footprint da crate: debris visual não expande CollisionBody.

---

# 12. Chest

## `Tileset/chest.png`

```text
semanticId         = object.chest
classification     = ENTITY + animation states
sheetSizePx        = 80×32
frameSizePx        = 16×32
frames             = 5
logicalFootprint   = PROBABLE 1×1
anchor             = PROBABLE (8,31) / base center
preferredLayer     = objects_visual
interaction        = requires explicit InteractionArea
persistence        = expected future persistent state
confidence         = PROBABLE for placement; UNVERIFIED for frame order/timing
```

Regras de geração:

- não colocar chest dentro de wall collision;
- manter pelo menos um ponto de aproximação caminhável quando a intenção for permitir interação;
- não escolher um frame para representar o estado no arquivo de mapa;
- `opened/closed/locked/loot` são dados da entidade, não propriedades visuais inferidas do frame;
- não assumir que os cinco frames correspondem a cinco estados persistentes distintos.

---

# 13. Sign

## `Tileset/commands_sing.png`

```text
semanticId         = object.sign
classification     = ENTITY or static interactable object
visualSizePx       = 16×16
frames             = 1
logicalFootprint   = PROBABLE 1×1
preferredLayer     = objects_visual
interaction        = explicit InteractionArea/dialog/action
confidence         = PROBABLE
```

Regras:

- não tratar como textura de parede por default;
- se for interativo, manter acesso por uma célula caminhável adjacente;
- conteúdo/texto do sign pertence a definition/property data, nunca ao PNG.

---

# 14. Fire block

## 14.1 `Tileset/fire_block.png`

```text
semanticId         = object.fire_block
classification     = ENTITY visual state
visualSizePx       = 16×32
logicalFootprint   = PROBABLE 1×1
```

## 14.2 `Tileset/fire_block_with_fire.png`

```text
semanticId         = visual.object.fire_block.lit
classification     = VISUAL_STATE / ANIMATION
sheetSizePx        = 64×32
frameSizePx        = 16×32
frames             = 4
stateGroup         = object.fire_block
```

## 14.3 `Tileset/fire_block_destroied.png`

```text
semanticId         = visual.object.fire_block.destroyed
classification     = VISUAL_STATE
visualSizePx       = 16×32
stateGroup         = object.fire_block
```

Regras:

- autoria cria uma única entidade `object.fire_block`;
- `unlit`, `lit`, `destroyed` devem ser estados/capacidades, quando implementados;
- a presença de fogo visual não autoriza inferir dano, luz, ignite ou hazard sem definição de gameplay;
- collision deve poder variar por estado somente se o gameplay declarar isso.

---

# 15. Statue

## `Tileset/statue.png`

```text
semanticId         = object.statue
classification     = ENTITY or static world object
visualSizePx       = 16×32
logicalFootprint   = PROBABLE 1×1 base
anchor             = PROBABLE base center
preferredLayer     = objects_visual
occlusionPolicy    = tall visual; Y-sort/baseline when applicable
confidence         = PROBABLE
```

Regras:

- não usar sprite bounds 16×32 como collision automática;
- pode atuar como obstáculo/decorativo conforme definition;
- não colocar a parte superior em `decoration_high` manualmente se o renderer de entidade já desenhar o sprite inteiro por baseline.

---

# 16. Shop block / counter

## `Tileset/shop_block.png`

```text
semanticId         = object.shop_counter
classification     = ENTITY or multi-cell STAMP
visualSizePx       = 32×32
logicalFootprint   = UNVERIFIED
visualBounds       = 2×2 tiles
preferredLayer     = objects_visual
confidence         = UNVERIFIED for collision/interaction footprint
```

A forma visual não ocupa uniformemente todo o quadrado 2×2.

Política conservadora:

- reservar uma região visual 2×2;
- não inferir uma CollisionBody 2×2 completa;
- não auto-colocar NPC atrás do balcão até definir lado de atendimento e footprint;
- não usar como quatro tiles separados.

---

# 17. Wooden doors

Os três arquivos representam a mesma família semântica em orientações/posições arquitetônicas diferentes.

```text
semantic family = door.wood
classification  = ENTITY + directional visual set
stateful         = yes
```

Uma porta pertence à arquitetura da parede; não é decoração solta no centro de uma sala.

## 17.1 `Tileset/door_front.png`

```text
sheetSizePx      = 192×16
frameSizePx      = 48×16
frames           = 4
visualFootprint  = 3×1 tiles
orientationRole  = front-facing horizontal wall variant
confidence       = PROBABLE
```

## 17.2 `Tileset/door_back.png`

```text
sheetSizePx      = 192×16
frameSizePx      = 48×16
frames           = 4
visualFootprint  = 3×1 tiles
orientationRole  = back-facing horizontal wall variant
confidence       = PROBABLE
```

## 17.3 `Tileset/door_side.png`

```text
sheetSizePx      = 64×32
frameSizePx      = 16×32
frames           = 4
visualFootprint  = 1×2 tiles
orientationRole  = vertical/side wall variant
confidence       = PROBABLE
```

## 17.4 Regras obrigatórias de porta

1. Porta deve substituir/ocupar uma **abertura coerente em uma parede**.
2. Não colocar door frame sobre piso aberto sem paredes hospedeiras.
3. Não recortar o sprite 48×16 em três tiles independentes.
4. Os quatro frames pertencem ao estado/animação da mesma porta.
5. Collision de passagem muda por estado lógico, não pelo alpha do frame.
6. Se a porta levar a outro mapa, usar conceito de `LINK` + destination spawn.
7. Se apenas bloquear uma área do mesmo mapa, continua sendo entidade/door, sem obrigar transição.
8. `front/back/side` devem ser escolhidos pela orientação da parede; não são quatro tipos de gameplay.
9. Flip/espelhamento de `door_side` para o lado oposto permanece **UNVERIFIED**.
10. Uma porta transitável deve ter área de aproximação caminhável em ambos os lados relevantes.

---

# 18. Iron door

## `Tileset/iron_door.png`

```text
semanticId         = door.iron
classification     = ENTITY + state animation
sheetSizePx        = 144×16
frameSizePx        = 48×16
frames             = 3
visualFootprint    = 3×1 tiles
hostRequirement    = horizontal wall opening
confidence         = PROBABLE visual placement; UNVERIFIED exact semantics
```

Visualmente há uma porta íntegra seguida por estágios de fragmentação/abertura.

Regras:

- não usar os fragmentos como decoração separada;
- não inferir automaticamente se abre com chave, dano, trigger ou explosão;
- usar a mesma lógica de wall host e approach da porta de madeira.

---

# 19. Gate

## `Tileset/gate.png`

```text
semanticId         = gate.dungeon
classification     = ENTITY + architectural animation
sheetSizePx        = 288×48
frameSizePx        = 48×48
frames             = 6
visualFootprint    = 3×3 tiles
hostRequirement    = architectural boundary/opening
alpha              = fully opaque
backgroundColor    = compatible with #361E26 palette
confidence         = CONFIRMED dimensions / PROBABLE placement
```

Regra crítica:

> Cada frame é um **stamp arquitetônico 3×3 completo**, não nove tiles independentes.

Como o frame é totalmente opaco e contém fundo próprio, o gate não é uma sobreposição universal.

Regras de autoria:

- colocar alinhado a uma abertura/boundary de 3 tiles de largura;
- não posicionar no centro de um piso sem arquitetura compatível;
- não misturar o fundo embutido com outro tema/paleta sem validação visual;
- os seis frames são estados de uma única entidade;
- collision do vão central é lógica e deve acompanhar o estado;
- não inferir qual frame é “aberto” apenas por índice sem AnimationDefinition validada.

---

# 20. Spikes

## `Tileset/tile_spickes.png`

```text
semanticId         = trap.spikes
classification     = ENTITY / animated floor hazard
sheetSizePx        = 48×16
frameSizePx        = 16×16
frames             = 3
visualFootprint    = 1×1 tile
alpha              = fully opaque
preferredHost      = floor
confidence         = CONFIRMED visual / PROBABLE hazard role
```

Regras:

- tratar o conjunto como uma única trap stateful;
- não colocar os três frames lado a lado como três tipos de piso;
- como cada frame é opaco, ele substitui visualmente a célula inferior; validar compatibilidade do fundo;
- não marcar `solid` por default;
- dano, timing, activation policy e safe/active state são metadados de gameplay **UNVERIFIED**;
- o nome físico `spickes` não deve vazar para o ID semântico.

---

# 21. Wall arrow trap detail

## `Tileset/Walls_trap_arrows.png`

```text
semanticId         = trap.arrow_wall.visual
classification     = ENTITY attachment / visual detail
visualSizePx       = 12×11
frames             = 1
hostRequirement    = PROBABLE wall surface
orientation        = visual vertical
confidence         = UNVERIFIED
```

Política atual:

- **não auto-colocar em geração procedural/LLM** até confirmar encaixe e função;
- se usado manualmente, exigir associação a uma parede/trap definition;
- não deixar o sprite flutuando no chão;
- direção de disparo não deve ser inferida exclusivamente pela orientação do desenho.

---

# 22. Regras de composição lógica de salas

Estas regras são de autoria e não pretendem inferir mecânicas específicas dos PNGs.

## 22.1 Parede precisa formar fronteira coerente

Uma parede visual não pode terminar abruptamente em uma peça de quina incorreta.

Preferir stamps e segmentos compatíveis.

## 22.2 Objetos interativos precisam de acesso

Para chest, sign, door e futuros NPCs:

```text
interaction target
    +
pelo menos uma posição caminhável de aproximação
```

Não gerar conteúdo interativo atrás de collision inacessível, salvo se isso for intencional e condicionado por puzzle/progressão explicitamente definidos.

## 22.3 Door precisa conectar espaços

Uma porta deve conectar:

```text
walkable source side
    ↕
wall opening + door entity
    ↕
walkable destination side / map link
```

Uma porta decorativa sem destino deve ser declarada explicitamente como tal.

## 22.4 Objetos altos não podem invalidar circulação por acaso

A LLM deve raciocinar sobre `logicalFootprint`, não visual bounds.

## 22.5 Destrutíveis preservam espaço após destruir

Quando crate/vase/block destruído deixa de colidir, o mapa subjacente ainda precisa ser visualmente válido e caminhável de acordo com as regras da sala.

Não usar destrutível para esconder uma ausência de ground tile.

## 22.6 Traps precisam de espaço de leitura

Uma trap deve ser colocada deliberadamente em uma rota/área do mapa, não como ruído visual aleatório.

O comportamento exato pertence à definition da trap.

---

# 23. Validações que uma LLM deve executar antes de salvar um mapa

Checklist mínimo para esta Parte 1:

```text
[ ] nenhuma célula transparente do atlas foi tratada como tile real
[ ] nenhum frame de animação foi serializado como entidade independente
[ ] nenhum destroyed-state foi duplicado sobre a entidade intacta
[ ] doors estão hospedadas em wall openings
[ ] gates ocupam stamps 3×3 coerentes
[ ] side/front/back door visual corresponde à orientação da parede
[ ] chest/sign/door possuem aproximação possível quando interativos
[ ] tall objects usam footprint/base, não sprite bounds bruto
[ ] collision foi declarada separadamente da arte
[ ] spikes/traps não receberam gameplay inferido apenas pelo PNG
[ ] standalone objects não foram pintados na TileLayer por conveniência
[ ] stamps do atlas não foram quebrados sem intenção explícita
[ ] floor/surface variants opacas não foram usadas como decals universais
[ ] nenhuma transformação/flip não validada foi aplicada automaticamente
```

---

# 24. Regras para edição/correção por LLM

Ao receber um mapa existente, a LLM deve corrigir nesta ordem:

```text
1. integridade estrutural
   - bounds, dimensões, layers, IDs

2. conectividade lógica
   - áreas caminháveis, doors, links, approach

3. coerência arquitetônica
   - wall seams, corners, stamps

4. footprint/collision
   - objetos altos, portas, bloqueios

5. semântica das entidades
   - chest/crate/vase/traps como entities, não frames

6. decoração
   - somente depois que a geometria estiver válida
```

Nunca “embelezar” primeiro um mapa estruturalmente inválido.

---

# 25. Formato de pedido recomendado para uma LLM map author

Uma ferramenta futura pode fornecer à LLM algo próximo de:

```text
Map intent:
  dungeon room / corridor / arena / shop / transition

Allowed semantic tiles/stamps:
  [...]

Allowed entities:
  [...]

Required connections:
  north door -> map X / spawn Y
  west corridor -> region Z

Constraints:
  bounds
  walkable regions
  required collision
  forbidden cells
  persistent entities

Existing map snapshot:
  layers
  collision
  entities
  links
```

A LLM deve responder em **semantic IDs/stamps**, não em palpites de pixels ou nomes de arquivo sempre que o catálogo permitir.

---

# 26. Próximas partes deste documento

## Parte 2 — Characters e spawns

Detalhar:

```text
Player
Evil Soldier
Skull
Slime
Training Puppet
```

Com foco em:

- anchor/base;
- spawn clearance;
- visual bounds versus collision body;
- direcionamento;
- uso de flip;
- quais spritesheets são animation states e nunca conteúdo de mapa;
- regras de colocação de enemy spawns.

## Parte 3 — Objects/items e loot placement

Detalhar:

```text
money
big_money
heart
extra_heart
potion
meat
key
bow
tnt
gold_block
```

Separando:

```text
world pickup
inventory/item definition
HUD icon
loot spawn
persistent placed item
```

## Parte 4 — Effects, HUD, menus, backgrounds e font

Definir o que **não pertence ao mapa** e impedir que uma LLM use UI/VFX como world tiles.

## Parte 5 — Tile semantic catalogue final

Refinar `tileset.png` célula por célula com:

- nome semântico;
- família;
- papel estrutural;
- vizinhos válidos;
- stamp membership;
- collision default sugerida, quando validada pelo design e não pela imagem;
- draw layer;
- exemplos de composição;
- regras de autotiling/editor.

Esta parte deverá transformar coordenadas brutas em um catálogo suficientemente seguro para geração automática.

---

# 27. Princípio final

A LLM não deve montar Dungeon Underworld como um mosaico de PNGs.

Ela deve montar um **modelo semântico de mapa** e usar os assets apenas como representação visual desse modelo:

```text
room / corridor / wall / opening / door / object / trap / spawn
        ↓
semantic definition / stamp
        ↓
visual asset + collision + gameplay metadata
```

Se a LLM precisa “adivinhar” a função de uma imagem para terminar o mapa, a especificação ainda está incompleta e o correto é marcar a decisão como `UNVERIFIED`, não inventá-la.
