# Content Studio

O editor oficial de mapas e conteúdo é Python/PySide6 e funciona em Windows e
Linux. O jogo continua sendo C++ e não precisa de Python instalado.

## Preparar

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e .
```

No Windows, use `.venv\\Scripts\\activate` (ou execute diretamente
`.venv\\Scripts\\python.exe`). A dependência PySide6 está restrita
ao pacote de ferramentas de autoria. A faixa suportada é `PySide6 >= 6.7, < 7`;
PySide6/Qt for Python é distribuído sob as opções de licença LGPLv3/GPLv3 ou
comercial do Qt, conforme a instalação escolhida. O repositório não incorpora a
dependência nem seus binários; consulte a licença da distribuição usada.

## Executar

```bash
python -m tools.content_studio
python -m tools.content_studio --project-root . --cpp-root .
```

Os wrappers `content_studio.sh` e `content_studio.bat` abrem a raiz do
repositório como projeto do Studio. O conteúdo authored fica sempre em
`content/definitions`, o projeto de mundo padrão em `content/world.uworld` e as
imagens em `assets/`. Se ainda não houver conteúdo, o Studio cria
`content/definitions/content.json`; não existe uma etapa separada para ativar,
criar ou abrir um workspace de conteúdo.

`content_studio.bat` usa automaticamente `.venv` quando ela existe e informa o
comando de preparação quando PySide6 ainda não foi instalado. O primeiro setup
no Windows pode ser feito com:

```bat
py -3.11 -m venv .venv
.venv\\Scripts\\python.exe -m pip install -e .
content_studio.bat
```

## Fluxo de produção

`New Project` cria um mapa blank, sem inimigo builtin selecionado e sem Player
Spawn implícito. Crie explicitamente um spawn com a ferramenta `Player Spawn`
quando o mapa precisar iniciar playtest. O browser de entidades lê diretamente
o conteúdo authored, separa Enemy/NPC/Object/Pickup, suporta busca e mantém
definições inválidas visíveis para correção.

`Save`, `Save As`, `Save All`, autosave, undo/redo, layers, tiles, collision,
regions, links, rules, encounters e scenes escrevem apenas JSON authored.
`New Map` abre uma única janela com Map ID, largura, altura, tamanho do tile e
Player Spawn inicial. Ela também permite escolher ou digitar uma pasta de
organização, formando grupos como `Floresta > mapa1, mapa2`. O clique direito
no mapa visível (com a ferramenta Selecionar) ou em um mapa da árvore abre a mesma janela
para edição; redimensionar preserva a interseção superior esquerda das layers e
da colisão, e renomear atualiza `entryMapId` e links entre mapas. As pastas são
metadados locais do Content Studio e não alteram UWORLD, DMAP ou os Map IDs.
Ao excluir o mapa de entrada de um projeto multimapa, o mapa vizinho passa a ser
a nova entrada automaticamente; o último mapa do projeto não pode ser removido.

`Validate Workspace` executa `content_check`. `Export DMAP` usa o writer DMAP 1.5
Python do próprio Studio para o projeto UWORLD. `Playtest` usa o mesmo writer para
criar cópias temporárias do estado atual e inicia o runtime;
conteúdo inválido ou mapa sem Player Spawn é recusado antes de iniciar.

A interface usa ações contextuais: controles que exigem uma seleção ficam
desabilitados quando não podem operar, e a toolbar de mapa aparece somente no
modo MAP. Definições possuem Create/Duplicate/Rename/Delete; placements e
elementos selecionados podem ser editados no inspetor e excluídos pelo botão
visível ou pela tecla Delete. A ferramenta `Apagar`, ao lado de Playtest,
permanece ativa: clique em um canto e arraste até o canto oposto para selecionar
um retângulo de tiles. A área é apagada ao soltar o botão em uma única operação.
Entradas de arrays authored podem ser removidas individualmente e todas essas
mutações continuam passando pelo undo/redo.
Com a ferramenta `Selecionar`, o `+` do Player Start pode ser clicado e arrastado;
ele acompanha o cursor com o snap atual e a posição é gravada ao soltar em uma
única operação de undo. `Selecionar` inicia realmente ativo ao abrir o Studio e
funciona como alternância ON/OFF; desligado, o canvas permanece em modo neutro.

Painéis principais e internos usam divisores redimensionáveis. O atlas conserva
exatamente as linhas e colunas da imagem importada e usa rolagem quando o painel
não comporta toda a largura. Cada tile conserva seu `sourceIndex` authored;
redimensionar a interface nunca altera a composição visual, o mapa ou as regras
de Smart Terrain.

## Tilesets, semantics e Smart Terrain

Um `Tileset` é somente a fonte visual/atlas. `Terrain` é a intenção de autoria:
uma família como `dungeon.stone` pode reunir floor, paredes e detalhes de vários
tilesets. O mapa não possui um tileset único: cada entrada de `tileReferences`
continua carregando seu próprio `tilesetId`, `sourceIndex` e `flags`, inclusive
quando uma mesma layer mistura packs.

A aba Tiles usa a Tileset Library. Ela lista, pesquisa e mostra o atlas
visualmente; `Add Files`, `Add Folder` e drop de múltiplos arquivos passam pelo
mesmo batch importer. Cada candidato recebe um ID editável, por exemplo
`dungeon_floor.png` → `tileset.dungeon.floor`, e conflitos exigem Skip,
Reimport ou Change ID. Reimport preserva o ID e é bloqueado quando o novo atlas
invalidaria índices usados. Excluir um tileset também consulta usos em mapas,
semantics e stamps.

Imagens escolhidas fora do projeto são copiadas para `assets/tilesets` com nome
derivado do `TilesetId`; o arquivo de origem é preservado. Tilesets continuam
sem um campo `root`: `relativeAssetPath` aponta para a cópia gerenciada sob
`assets/`, que permanece ignorada pelo Git por conter arte licenciada/local.
Tilesets cujo `tileSize` não coincide com o `tileSize` do mapa ficam
incompatíveis para pintura.

O Semantic Editor classifica a célula visual selecionada com a estrutura
authored `tileSemantics`: `family`, `role`, `topology`, edges e `preferredLayer`.
O catálogo deriva famílias e indexa por família/papel/topologia e por
tileset/sourceIndex, produzindo diagnósticos para referências inválidas,
índices fora do atlas e duplicidades.

Para configurar uma regra visual sem editar cada definição manualmente, clique
com o botão direito em um tileset na Tileset Library e escolha `Configurar
lógica de Smart Terrain`. A janela apresenta nove slots em uma grade 3 × 3:
`NW`, `N`, `NE`, `W`, `X`, `E`, `SW`, `S`, `SE`. Selecione um slot e clique em
um tile do atlas para associá-lo. A janela permite criar, listar, editar e
excluir regras; a lista de regras é uma visão derivada das definições
`tileSemantics`, não uma segunda base de dados. Cada slot também grava a
vizinhança ortogonal correspondente usando os campos de borda já existentes,
para que NW/NE/SW/SE não sejam confundidos durante a pintura. A configuração
da família apresenta diretamente `Ativar colisão` ou `Desativar colisão`; os
papéis internos `wall` e `floor` permanecem apenas por compatibilidade com o
formato Content v5 e com o resolver.

Quando o papel é `Floor`, os nove espaços funcionam como variações visuais.
Cada uma possui peso relativo (`variantWeight`): piso liso com peso 8 e rachado
com peso 2 resulta em aproximadamente 80%/20%. A janela mostra a porcentagem,
permite remover uma variação individual e usa o seed para manter a pintura
reproduzível. Os slots usam células compactas do mesmo tamanho visual dos tiles
do atlas; peso, porcentagem e `sourceIndex` ficam disponíveis no tooltip. No
Gerenciador de Smart Terrain, as configurações ficam à esquerda e o atlas do
tileset permanece à direita em um divisor redimensionável.

O atlas visual preserva a grade original da imagem: o índice continua sendo
`sourceIndex = linha * colunas + coluna`. Redimensionar o painel não reordena
os tiles nem altera a referência escolhida pela regra.

Um tile ou brush selecionado no atlas permanece ativo depois de pintar. Clique
e arraste sobre o mapa para continuar pintando sem voltar ao atlas; o pincel só
muda ao selecionar outro tile, brush ou ferramenta.

O modo Raw Tiles continua pintando exatamente o tile escolhido. Na aba Smart
Terrain, as famílias aparecem como cartões quadrados. Passar o mouse sobre um
cartão abre a composição visual 3 × 3 e clicar mantém aquela família ativa para
pintura. A colisão também não é escolhida durante a pintura: famílias somente
`floor` pintam sem colisão, famílias somente `wall` pintam com colisão e famílias
mistas usam `floor` na pintura comum. O papel continua no formato de conteúdo
para compatibilidade com o resolver, mas não é mais uma decisão repetida:

* Smart Floor escolhe deterministicamente entre os candidates `floor`, respeitando
  seus pesos relativos;
* Smart Wall usa a vizinhança ortogonal N/E/S/W para escolher
  `straightHorizontal`, `straightVertical`, `outerCorner`, `cap`, `junction`
  ou fallbacks `interior`/`unknown`. Em um traçado aberto, E/W não contém
  informação suficiente para distinguir a borda de cima da de baixo (e o
  mesmo vale para N/S); nesse caso o resolver escolhe um lado canônico fixo,
  evitando alternância visual. Em áreas fechadas, as máscaras com o vizinho
  interno selecionam N/S/E/W e os quatro cantos individualmente;
* Room / Area Brush pinta o perímetro como Wall e o interior como Floor usando
  os mesmos serviços de floor/wall. A ocupação completa da sala orienta o
  contorno, distinguindo corretamente os slots superiores dos inferiores.

O `AutoTileResolver` não conhece Qt. `TerrainPaintingService` calcula somente
as células tocadas e os vizinhos necessários, e cada gesto (incluindo updates
de vizinhos) é uma única operação de undo no `MapDocument`. A escolha de
variantes usa `mapId`, coordenadas, família, papel e seed estáveis; reabrir o
mesmo mapa não troca tiles aleatoriamente. A engine não assume que uma família
pertence a um único tileset, o que permite floor em A, parede em B e corner em
C sem alterar UMAP.

A colisão derivada da família é salva na grade do UMAP no mesmo gesto. Quando uma
célula sólida possui tile, o UMAP também registra `collisionBindings` com a camada
e a referência authored daquele tile. Assim, substituir/apagar o tile ou excluir a
camada remove automaticamente a colisão vinculada. Uma célula vazia não cria uma
colisão órfã: primeiro deve existir um tile na camada ativa. No Room / Area Brush,
o interior usa `floor` sem colisão e o contorno usa `wall` com colisão
automaticamente, sem um controle adicional no painel.

Smart Terrain funciona como seleção retangular inclusiva: clique pinta uma célula;
arrastar do ponto inicial ao final pinta todo o quadrante, inclusive as bordas, e o
resolver calcula cada tile usando a ocupação final e seus vizinhos.

## Build/testes

```bash
make -f docker/linux_build.mk all
python -m unittest discover -s tools/content_studio/tests -v
QT_QPA_PLATFORM=offscreen python -m unittest discover -s tools/content_studio/tests -v
```

O build C++ de produto contém `game`, `content_check`, `map_compile` e
`world_compile`. Os objetos `src/editor` que ainda aparecem nos targets
`tests`/`playtest_runner` existem somente para preservar regressões nativas; não
há um executável C++ oficial de Content Studio.
