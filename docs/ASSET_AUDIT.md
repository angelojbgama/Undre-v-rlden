# Auditoria dos assets — Dungeon Underworld

## Escopo e método

Auditoria realizada sobre `Dungeon Underworld/`, sem alterar os arquivos. A árvore contém **168 PNGs**, mas existe uma cópia integral em `Dungeon Underworld/Dungeon Underworld/`: os 84 caminhos relativos correspondentes têm SHA-256 idêntico. Portanto há **84 imagens únicas** e 84 duplicatas byte a byte. As tabelas abaixo usam os caminhos do conjunto externo; o conjunto interno repete exatamente os mesmos dados.

As dimensões e o alpha foram lidos programaticamente, e cada imagem foi inspecionada visualmente em escala nearest-neighbor, com grades candidatas sobre as spritesheets. Todos os PNGs estão em RGBA e usam apenas alpha 0 ou 255; não há pixels com alpha parcial. Cinco imagens são totalmente opacas: `game_background.png`, `menu_background.png`, `controls.png`, `Tileset/gate.png` e `Tileset/tile_spickes.png`.

Convenções:

- `C×R` significa colunas × linhas; `single` não é spritesheet.
- `down/up/side` é a ordem visual das linhas direcionais. A linha `side` olha para a esquerda e deve ser espelhada para a direita.
- Âncora provável de personagens em células 32×32: pés em torno de `(16, 31)`. Em ataques 48×48: pés em torno de `(24, 31)`, e não no fundo da célula. Esses valores são ponto de partida e **needs verification** em uma sobreposição animada.
- “Frames” descreve células visuais, não estados de gameplay. Duração, ordem e eventos não podem ser deduzidos de PNG estático.

## Player (10)

| Asset | Size | Frame size | Grid / frames | Category | Directions | Notes |
|---|---:|---:|---:|---|---|---|
| `Characters/Player/attacking/arrow.png` | 16×16 | 16×16 | single | projectile | vertical | Seta apontada para cima; demais orientações/rotação **needs verification**. |
| `Characters/Player/attacking/player_attacking.png` | 192×144 | 48×48 | 4×3 / 12 | sword animation | down/up/side | Espada excede o corpo; side requer flip para right; âncora provável `(24,31)`. |
| `Characters/Player/attacking/player_attacking_bow.png` | 64×96 | 32×32 | 2×3 / 6 | bow animation | down/up/side | Dois quadros por direção; evento de disparo e timing **needs verification**. |
| `Characters/Player/death/player_dead.png` | 32×96 | 32×32 | 1×3 / 3 | terminal pose | down/up/side | Uma pose final por direção. |
| `Characters/Player/death/player_death.png` | 64×96 | 32×32 | 2×3 / 6 | damage/death | down/up/side | Sequência exata (dano ou morte) **needs verification**. |
| `Characters/Player/idle/player_idle.png` | 64×96 | 32×32 | 2×3 / 6 | idle animation | down/up/side | Dois quadros por direção. |
| `Characters/Player/sheld/player_sheld.png` | 64×96 | 32×32 | 2×3 / 6 | shield animation | down/up/side | Nome original preservado; shield/hurtbox não deve vir dos pixels. |
| `Characters/Player/slepping/player_sleeping.png` | 96×32 | 32×32 | 3×1 / 3 | sleep animation | down only | Três estágios com `Z`; não há variantes up/side. |
| `Characters/Player/wake_up/player_wake_up.png` | 128×32 | 32×32 | 4×1 / 4 | wake animation | down only | Quatro estágios; provável sequência inversa relacionada ao sono, **needs verification**. |
| `Characters/Player/walking/player_walking.png` | 128×96 | 32×32 | 4×3 / 12 | walk animation | down/up/side | Side olha à esquerda; flip horizontal para direita. |

## Enemies e alvo de treino (17)

| Asset | Size | Frame size | Grid / frames | Category | Directions | Notes |
|---|---:|---:|---:|---|---|---|
| `Characters/Enemies/Evil_soldier/attacking/evil_soldier_attacking.png` | 192×144 | 48×48 | 4×3 / 12 | sword animation | down/up/side | Mesmo layout amplo do player; provável âncora `(24,31)`. |
| `Characters/Enemies/Evil_soldier/death/evil_soldier_death.png` | 64×96 | 32×32 | 2×3 / 6 | damage/death | down/up/side | Ordem semântica **needs verification**. |
| `Characters/Enemies/Evil_soldier/idle/evil_soldier_idle.png` | 64×96 | 32×32 | 2×3 / 6 | idle animation | down/up/side | Flip da linha side para right. |
| `Characters/Enemies/Evil_soldier/sleep/evil_soldier_sleep.png` | 96×64 | 32×32 | 3×2 / 6 | sleep animation | down/up | Sem side; partículas `Z` crescem. |
| `Characters/Enemies/Evil_soldier/wake_up/evil_soldier_wake_up.png` | 128×64 | 32×32 | 4×2 / 8 | wake animation | down/up | Sem side. |
| `Characters/Enemies/Evil_soldier/walking/evil_soldier_walking.png` | 128×96 | 32×32 | 4×3 / 12 | walk animation | down/up/side | Side + flip. |
| `Characters/Enemies/Skull/attacking/arrow.png` | 16×16 | 16×16 | single | projectile | right | Seta horizontal apontada para a direita; outras direções **needs verification**. |
| `Characters/Enemies/Skull/attacking/skull_attacking.png` | 64×96 | 32×32 | 2×3 / 6 | ranged attack | down/up/side | Visual de arco; frame-event deve criar projétil fora do renderer. |
| `Characters/Enemies/Skull/death/skull_death.png` | 64×96 | 32×32 | 2×3 / 6 | damage/death | down/up/side | Ordem semântica **needs verification**. |
| `Characters/Enemies/Skull/idle/skull_idle.png` | 64×96 | 32×32 | 2×3 / 6 | idle animation | down/up/side | Side + flip. |
| `Characters/Enemies/Skull/sleeping/skull_sleeping.png` | 96×64 | 32×32 | 3×2 / 6 | sleep animation | down/up | Sem side. |
| `Characters/Enemies/Skull/wake up/skull_wake_up.png` | 128×64 | 32×32 | 4×2 / 8 | wake animation | down/up | Pasta com espaço preservada. |
| `Characters/Enemies/Skull/walking/skull_walking.png` | 128×96 | 32×32 | 4×3 / 12 | walk animation | down/up/side | Side + flip. |
| `Characters/Enemies/Slime/slime_death.png` | 64×32 | 32×32 | 2×1 / 2 | death/damage | nondirectional | Dois quadros largos. |
| `Characters/Enemies/Slime/slime_idle.png` | 128×16 | 32×16 | 4×1 / 4 | idle animation | nondirectional | Confirmado visualmente: são 4 slimes, não 8 células 16×16. Âncora provável `(16,16)`. |
| `Training_puppet/training_puppet.png` | 32×32 | 32×32 | single | target/prop | front | Base em torno de `(16,32)`. |
| `Training_puppet/training_puppet_death and demange.png` | 64×32 | 32×32 | 2×1 / 2 | damage animation | front | Normal/dano ou dano/morte **needs verification**; nome original preservado. |

## Tiles, arquitetura e interativos (22)

| Asset | Size | Frame size | Grid / frames | Category | Directions | Notes |
|---|---:|---:|---:|---|---|---|
| `Tileset/Walls_trap_arrows.png` | 12×11 | 12×11 | single | trap detail | vertical | Pequeno emissor/indicador; função e encaixe **needs verification**. |
| `Tileset/block.png` | 16×32 | 16×32 | single | tall object | front | Ocupa visualmente até 2 tiles; colisão provável apenas na base. |
| `Tileset/block_2.png` | 16×32 | 16×32 | single | tall object | front | Variante visual de block. |
| `Tileset/block_destroied.png` | 16×32 | 16×32 | single | destroyed state | front | Estado separado, não animação. |
| `Tileset/breaking_crate.png` | 224×32 | 32×32 | 7×1 / 7 | destruction animation | front | Intacta, rachada e detritos; footprint lógico menor que debris visual. |
| `Tileset/breaking_vase.png` | 192×32 | 32×32 | 6×1 / 6 | destruction animation | front | Seis estágios. |
| `Tileset/chest.png` | 80×32 | 16×32 | 5×1 / 5 | chest animation | front | Cinco estados de abertura; base provável `(8,31)`. |
| `Tileset/commands_sing.png` | 16×16 | 16×16 | single | sign | front | Nome original preservado; interação é metadado, não propriedade do sprite. |
| `Tileset/crate.png` | 16×32 | 16×32 | single | tall object | front | Arte concentrada na metade inferior; footprint provável 1 tile. |
| `Tileset/door_back.png` | 192×16 | 48×16 | 4×1 / 4 | door animation | back | Quatro estados confirmados por separação de 48 px; sprite visível ~24 px central. |
| `Tileset/door_front.png` | 192×16 | 48×16 | 4×1 / 4 | door animation | front | Quatro estados; não confundir com 6 células de 32 ou 3 de 64. |
| `Tileset/door_side.png` | 64×32 | 16×32 | 4×1 / 4 | door animation | side | Quatro estados; flip pode produzir lado oposto, **needs verification** no mapa. |
| `Tileset/fire_block.png` | 16×32 | 16×32 | single | brazier/block | front | Sem fogo. |
| `Tileset/fire_block_destroied.png` | 16×32 | 16×32 | single | destroyed state | front | Estado quebrado. |
| `Tileset/fire_block_with_fire.png` | 64×32 | 16×32 | 4×1 / 4 | fire animation | front | Quatro quadros de chama. |
| `Tileset/gate.png` | 288×48 | 48×48 | 6×1 / 6 | gate animation | front | Totalmente opaco e contém fundo cor `(54,30,38)`; reutilização em outro piso **needs verification**. |
| `Tileset/iron_door.png` | 144×16 | 48×16 | 3×1 / 3 | destruction/opening | front | Porta íntegra e dois estágios de fragmentos. |
| `Tileset/shop_block.png` | 32×32 | 32×32 | single | shop counter/sign | front | Objeto de 2×2 tiles em bounding box, embora parte seja transparente. |
| `Tileset/statue.png` | 16×32 | 16×32 | single | tall decoration | front | Base/oclusão devem ser metadados. |
| `Tileset/tile_spickes.png` | 48×16 | 16×16 | 3×1 / 3 | trap animation | nondirectional | Totalmente opaco; três estados de espinhos; nome original preservado. |
| `Tileset/tileset.png` | 304×192 | 16×16 | 19×12 / 228 cells | tile atlas | n/a | 73 células não transparentes e 155 vazias. Paredes, pisos, bordas, cantos, portas e detalhes; estruturas multi-cell devem permanecer como seleções de tiles, não “frames”. |
| `Tileset/vase.png` | 16×32 | 16×32 | single | tall object | front | Footprint provável 1 tile inferior. |

O atlas principal está alinhado exatamente a 16×16. Células preenchidas por linha `(x:y, origem 0)`:

```text
y0:  x4-x6                         y6:  x1,x4,x5,x7,x14
y1:  x14                           y7:  x1,x2,x4-x7
y2:  x0-x8,x10-x14                 y8:  x1,x7
y3:  x0-x2,x4-x10,x12-x14          y9:  x3-x5
y4:  x0-x4,x7-x18                  y10: vazio
y5:  x7,x11-x14                    y11: x3-x5
```

## Objects / items (10)

| Asset | Size | Frame size | Grid / frames | Category | Directions | Notes |
|---|---:|---:|---:|---|---|---|
| `Objects/big_money.png` | 16×16 | 16×16 | single | pickup | n/a | Moeda/barra grande. |
| `Objects/bow.png` | 16×16 | 16×16 | single | equipment pickup | n/a | Visual separado do ícone de HUD. |
| `Objects/extra_heart.png` | 16×16 | 16×16 | single | upgrade pickup | n/a | Coração laranja com `+`. |
| `Objects/gold_block.png` | 16×16 | 16×16 | single | pickup/object | n/a | Identidade lógica **needs verification**. |
| `Objects/heart.png` | 16×16 | 16×16 | single | health pickup | n/a | Coração rosa com `+`. |
| `Objects/key.png` | 16×16 | 16×16 | single | key pickup | horizontal | Chave; tipo/cor não codificados no nome. |
| `Objects/life_potion.png` | 16×16 | 16×16 | single | consumable pickup | n/a | Frasco. |
| `Objects/meat.png` | 16×16 | 16×16 | single | consumable pickup | n/a | Carne. |
| `Objects/money.png` | 16×16 | 16×16 | single | currency pickup | n/a | Moeda pequena. |
| `Objects/tnt.png` | 32×16 | 16×16 | 2×1 / 2 | explosive animation | n/a | Dois estados, provável pavio/animação; semântica **needs verification** em movimento. |

## Effects (2)

| Asset | Size | Frame size | Grid / frames | Category | Directions | Notes |
|---|---:|---:|---:|---|---|---|
| `Explosion/arrow_hits_dust.png` | 48×16 | 16×16 | 3×1 / 3 | transient VFX | n/a | Três quadros de poeira/impacto. |
| `Explosion/explosion.png` | 480×48 | 48×48 | 10×1 / 10 | transient VFX | n/a | Dez quadros confirmados; centro visual varia, portanto usar âncora configurável. |

## HUD (16)

Todos são imagens únicas, transparentes e sem animação embutida.

| Asset | Size | Frame size | Frames | Category | Notes |
|---|---:|---:|---:|---|---|
| `Icons/arrows_counter.png` | 9×13 | 9×13 | 1 | HUD counter | Seta vertical. |
| `Icons/bow.png` | 20×22 | 20×22 | 1 | HUD equipment | Inclui badge rosa `Z`. |
| `Icons/extra_heart.png` | 11×10 | 11×10 | 1 | HUD health | Variante laranja cheia. |
| `Icons/extra_heart_half.png` | 11×10 | 11×10 | 1 | HUD health | Variante laranja meia. |
| `Icons/heart_complete.png` | 11×10 | 11×10 | 1 | HUD health | Cheio. |
| `Icons/heart_half.png` | 11×10 | 11×10 | 1 | HUD health | Meio. |
| `Icons/heart_void.png` | 11×10 | 11×10 | 1 | HUD health | Vazio. |
| `Icons/money.png` | 8×9 | 8×9 | 1 | HUD counter | Moeda. |
| `Icons/pause.png` | 11×11 | 11×11 | 1 | menu button | Pausa. |
| `Icons/potion.png` | 13×16 | 13×16 | 1 | HUD item | Poção. |
| `Icons/reprend_the_game.png` | 11×11 | 11×11 | 1 | menu button | Play; nome original preservado. |
| `Icons/restart.png` | 11×11 | 11×11 | 1 | menu button | Reiniciar. |
| `Icons/shield.png` | 20×21 | 20×21 | 1 | HUD equipment | Inclui badge rosa `X`. |
| `Icons/sword.png` | 17×21 | 17×21 | 1 | HUD equipment | Inclui badge rosa `Z`. |
| `Icons/tnt.png` | 17×21 | 17×21 | 1 | HUD item | Inclui badge rosa `C`. |
| `Icons/tnt_conter.png` | 12×11 | 12×11 | 1 | HUD counter | TNT pequeno; nome original preservado. |

## Menus, backgrounds e ícone (6)

| Asset | Size | Frame size | Frames | Category | Notes |
|---|---:|---:|---:|---|---|
| `Sword_arrow_for_menu_options.png` | 32×16 | 16×16 | 2 | menu cursor animation | Dois quadros de ponteiro/espada; ordem **needs verification**. |
| `Title.png` | 99×35 | 99×35 | 1 | title/logo | Transparente; não duplicar em documentação. |
| `controls.png` | 115×69 | 115×69 | 1 | menu panel | Totalmente opaco; texto inglês rasterizado. |
| `game_background.png` | 272×224 | 272×224 | 1 | logical background | Cor sólida opaca `(54,30,38)`; confirma 17×14 tiles. |
| `icon.png` | 22×24 | 22×24 | 1 | application/menu icon | Quase todo opaco; dimensões não são as usuais de `.ico`. |
| `menu_background.png` | 272×224 | 272×224 | 1 | logical background | Preto sólido opaco; confirma framebuffer lógico. |

## Bitmap font (1)

| Asset | Size | Cell size | Grid / glyphs | Category | Notes |
|---|---:|---:|---:|---|---|
| `fonts_index.png` | 182×27 | 7×9 | 26×3 / 68 used | bitmap font | Linha 0 `A-Z`, linha 1 `a-z`, linha 2 `0-9` + 6 símbolos; 10 células finais vazias. |

A grade 7×9 é exata. O desenho ocupa normalmente 5×7 pixels, com exceções de até 7 px e descendentes até a linha 9. A largura de tinta é variável (por exemplo `I` é estreito e `W` ocupa 7 px), embora as células tenham avanço fixo de 7 px. É possível recortar métricas pelo alpha, mas o primeiro renderer deve preservar célula/avanço fixos para reproduzir a intenção do pacote. Na terceira linha, `0-9`, ponto, vírgula, exclamação, interrogação e underscore são reconhecíveis; o sexto símbolo é um disco/botão estilizado e **needs verification**. Não inferir uma tabela ASCII apenas pela posição: declarar explicitamente o mapa glyph→célula.

## Ambiguidades e validações futuras

1. Confirmar duração, repetição, ping-pong e ordem de todos os clips em um visualizador de animações.
2. Afinar âncoras por clip sobrepondo idle, walk e attack com o mesmo ponto de pés.
3. Definir se as duas setas serão rotacionadas, combinadas por direção ou tratadas como projéteis distintos.
4. Confirmar o significado do último glyph, de `Walls_trap_arrows`, `gold_block` e dos dois frames de TNT/puppet.
5. Validar se o fundo opaco de gate/spikes é intencional. Não aplicar color key automaticamente: a cor pode também existir na arte.
6. Catalogar semanticamente os 73 tiles não vazios no futuro editor; a auditoria visual identifica geometria, mas não prova colisão, autotile ou layer.
7. Não usar detecção automática de transparência como definição de hitbox, hurtbox, collision ou interação.

## Licença e higiene do repositório

`Personal Licence.txt` credita Albi Lico e permite uso pessoal, comercial e modificação. Proíbe redistribuir os arquivos, independentemente do grau de modificação. Consequências:

- não publicar os PNGs, o ZIP, cópias modificadas, contact sheets ou assets embutidos;
- manter código/formatos independentes do pacote e documentar como o usuário fornece assets localmente;
- quando houver Git, ignorar no mínimo `/Dungeon Underworld/`, `/Dungeon Underworld.zip` e quaisquer diretórios locais de assets licenciados; não foi criado `.gitignore` nesta fase;
- evitar inclusive a duplicata aninhada em qualquer distribuição. Ela foi apenas identificada, não removida;
- registrar licença/proveniência fora de manifests públicos sem reproduzir os arquivos.

Isto é uma leitura operacional do texto fornecido, não aconselhamento jurídico.
