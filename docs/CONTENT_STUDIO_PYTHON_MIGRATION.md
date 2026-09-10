# Migração do Content Studio para Python

O Content Studio oficial de autoria é `Python 3.11+` com `PySide6`. O jogo,
runtime, validação rigorosa e compilação continuam em C++20. O executável do
jogo não importa Python nem PySide6.

```text
Python Content Studio
        ↓ content JSON v5 / UMAP v4 / UWORLD v1
C++ content_check / map_compile / world_compile
        ↓ DMAP 1.5
C++ game/runtime
```

## Matriz de paridade

Esta matriz registra a implementação efetiva, não um plano futuro. Os modelos
Python preservam os DTOs authored existentes e os serviços C++ continuam sendo
a autoridade para operações executáveis.

| Funcionalidade | Código C++ substituído/compartilhado | Implementação Python | Cobertura | Estado |
|---|---|---|---|---|
| Documento de mapa, dirty state e histórico | `EditorDocument` | `model/map_document.py` | `test_formats_and_documents.py` | MIGRATED |
| Workspace, ownership e índice authored | `ContentWorkspaceDocument` | `model/content_workspace.py` | index/origin/CRUD tests | MIGRATED |
| Content JSON v5 | `content_json*` | `formats/content_json.py`, `formats/json_io.py` | round-trip + C++ `content_check` | MIGRATED |
| UMAP v4 | `authored_map*` | `formats/umap.py`, `model/map_document.py` | round-trip + C++ `map_compile` | MIGRATED |
| UWORLD v1 multimapa | `authored_world*`, `WorldProjectDocument` | `formats/uworld.py`, `model/world_project.py` | order/link/save tests | MIGRATED |
| Descoberta de entidades | `AuthoredEntityIndex` | `model/authored_entity_index.py` | unrelated-error/local-validation tests | MIGRATED |
| Browser de conteúdo e referências | `content_collection`, `content_reference_tools` | `ui/widgets.py` (`ContentBrowser`, `StructuredInspector`) | Qt smoke + model tests | MIGRATED |
| Browsers de mapas e layers | `EditorApp` | `ui/widgets.py` (`MapBrowser`, `LayersPanel`) | document tests + Qt smoke | MIGRATED |
| Tiles, seleção, brush, rectangle, fill e stamps | `editor_commands`, authoring semantics | `ui/map_canvas.py`, `model/map_document.py`, `ui/widgets.py` | map model tests | MIGRATED |
| Colisão | `SetCollisionCommand` | `ui/map_canvas.py`, `model/map_document.py` | map command coverage | MIGRATED |
| Enemy/NPC/Object/Pickup | `PlaceEntityCommand` | `model/map_document.py`, `ui/map_canvas.py` | category/ID/position tests | MIGRATED |
| Seleção, mover, remover e duplicar | `editor_commands` | `ui/map_canvas.py`, document history | map tests + Qt smoke | MIGRATED |
| Spawn, links e regiões | placement commands | `model/map_document.py`, `ui/map_canvas.py`, `CollectionPanel` | structural/round-trip tests | MIGRATED |
| Rules e encounters | `WorldRuleDefinition`, `EncounterDefinition` | `CollectionPanel`, `StructuredInspector` | map round-trip/inspector path tests | MIGRATED |
| Scenes, actors, tracks, clips, markers e preview | `scene_timeline`, scene UI | `model/scene_timeline.py`, `ui/scene_editor.py` | timeline + Qt smoke | MIGRATED |
| Assets e preview nearest-neighbor | `asset_browser`, `visual_preview` | `services/assets.py`, `ui/preview.py`, `AssetBrowser` | safe-path/search tests | MIGRATED |
| Undo/redo | `EditorCommand`, `CommandHistory` | `model/commands.py` + document histories | edit/undo/redo tests | MIGRATED |
| Autosave, preferências e localização | editor services | `services/autosave.py`, `preferences.py`, `localization.py` | autosave/preferences model coverage | MIGRATED |
| Validate workspace | `ContentValidator`, `content_check` | `services/toolchain.py` | C++ integration tests | MIGRATED |
| Export DMAP | `map_compile`, world compiler | `services/toolchain.py`, `src/tools/world_compile.cpp` | map/world integration tests | MIGRATED |
| Playtest, inclusive alterações não salvas | `EditorPlaytestSession` | `services/toolchain.py` (`PlaytestService`) | compile/launch service path | MIGRATED |
| Shell/UI nativo do autor | `EditorApp`, `EditorUiContext`, `editor_launch` | `__main__.py`, `ui/main_window.py` e widgets Qt | offscreen smoke | RETIRED |

## Estrutura

```text
tools/content_studio/
├── app.py, __main__.py
├── formats/              # codecs versionados e round-trip
├── model/                # documentos, índices e comandos
├── services/             # assets, preferências, autosave e C++ toolchain
├── ui/                   # MainWindow, canvas, browsers, inspectors, timeline
└── tests/                # unittest stdlib e smoke Qt offscreen
```

O `ContentWorkspace` mantém a origem de cada arquivo (`project` ou `builtin`)
e o `AuthoredEntityIndex` apenas referencia essas definições; não há uma cópia
paralela de conteúdo compilado. Conteúdo builtin só pode ser fornecido
explicitamente por código de desenvolvimento/teste (`from_builtin_json`). Um
projeto novo inicia vazio, sem inimigo selecionado implicitamente e sem spawn
oculto.

## Instalação e execução

```bash
python3 -m venv .venv
. .venv/bin/activate                 # Linux/macOS
.venv\\Scripts\\activate             # Windows
python -m pip install -e .
python -m tools.content_studio
python -m tools.content_studio --project-root .
```

Na raiz também existem `content_studio.sh` e `content_studio.bat`. O entrypoint
instalável é `du-content-studio`. Caminhos são resolvidos com `pathlib`; o jogo
não recebe a dependência Python. A dependência gráfica é PySide6 `>= 6.7, < 7`,
com as opções de licença LGPLv3/GPLv3 ou comercial de Qt for Python; os binários
não são versionados neste repositório.

## Fronteira C++

O Python grava somente artefatos authored e invoca processos C++ por arquivos,
exit code e stdout/stderr. `content_check` valida o workspace, `map_compile`
compila um UMAP e `world_compile` valida/compila todos os mapas de um UWORLD.
`PlaytestService` copia o estado em memória para uma pasta temporária, compila
os DMAPs e inicia o binário C++ sem alterar os arquivos originais. Falhas de
validação ficam no painel de diagnósticos e bloqueiam export/playtest.

Os arquivos `src/editor/*.cpp` restantes não formam mais um produto/editor
oficial: são suporte de regressão nativa e do `playtest_runner` existente. O
target `map_editor.exe` e o shell Win32 exclusivo foram retirados dos builds;
`game`, `content_check`, `map_compile` e `world_compile` não linkam a UI C++.
