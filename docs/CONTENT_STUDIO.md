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
