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
python -m tools.content_studio --project content/world.uworld \\
    --content content/definitions --asset-root /path/to/licensed/assets \\
    --cpp-root .
```

Os wrappers `content_studio.sh`, `content_studio.bat` e o entrypoint instalado
`du-content-studio` são equivalentes. `--asset-root` aponta para os assets
licenciados externos; eles não são copiados para o repositório.

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

`Validate Workspace` executa `content_check`. `Export DMAP` executa o
`world_compile` C++ para UWORLD ou `map_compile` no fluxo de mapa. `Playtest`
cria cópias temporárias do estado atual, compila com C++ e inicia o runtime;
conteúdo inválido ou mapa sem Player Spawn é recusado antes de iniciar.

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

Imagens de tileset devem permanecer sob o `--asset-root` configurado. Tilesets
não suportam `root` por definição e o Studio não copia assets externos para o
workspace; isso preserva a política de assets licenciados e o contrato C++.
Tilesets cujo `tileSize` não coincide com o `tileSize` do mapa ficam
incompatíveis para pintura.

O Semantic Editor classifica a célula visual selecionada com a estrutura
authored `tileSemantics`: `family`, `role`, `topology`, edges e `preferredLayer`.
O catálogo deriva famílias e indexa por família/papel/topologia e por
tileset/sourceIndex, produzindo diagnósticos para referências inválidas,
índices fora do atlas e duplicidades.

O modo Raw Tiles continua pintando exatamente o tile escolhido. O modo Smart
Terrain seleciona uma família e um papel:

* Smart Floor escolhe deterministicamente entre os candidates `floor`;
* Smart Wall usa a vizinhança ortogonal N/E/S/W para escolher
  `straightHorizontal`, `straightVertical`, `outerCorner`, `cap`, `junction`
  ou fallbacks `interior`/`unknown`;
* Room / Area Brush pinta o perímetro como Wall e o interior como Floor usando
  os mesmos serviços de floor/wall.

O `AutoTileResolver` não conhece Qt. `TerrainPaintingService` calcula somente
as células tocadas e os vizinhos necessários, e cada gesto (incluindo updates
de vizinhos) é uma única operação de undo no `MapDocument`. A escolha de
variantes usa `mapId`, coordenadas, família, papel e seed estáveis; reabrir o
mesmo mapa não troca tiles aleatoriamente. A engine não assume que uma família
pertence a um único tileset, o que permite floor em A, parede em B e corner em
C sem alterar UMAP.

Collision automática é uma policy opcional do `TerrainPaintingService`; sem
policy, Smart Terrain não modifica a layer de collision. Isso permite que uma
parede authored não seja universalmente tratada como sólida e deixa espaço para
água, ponte, low wall e decoração com regras diferentes.

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
