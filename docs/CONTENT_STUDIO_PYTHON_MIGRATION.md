# Python Content Studio

## Decisão

O Content Studio oficial é uma aplicação Python/PySide6. O jogo, o runtime e a
validação/compilação final continuam em C++20. O Python nunca é carregado pelo
executável do jogo.

```text
Python Content Studio
        ↓ authored JSON / UMAP / UWORLD
C++ content_check / map_compile / world_compile
        ↓ DMAP
C++ game/runtime
```

Os contratos de dados existentes não foram versionados novamente: content JSON
continua em v5, UMAP em v4 e UWORLD em v1. A aplicação Python escreve somente
artefatos authored; DMAP continua sendo produzido pelo compilador C++.

## Matriz de migração

| Funcionalidade | Origem C++ | Responsabilidade Python | Teste/validação | Estado |
|---|---|---|---|---|
| Modelo authored e dirty state | `EditorDocument`, `ContentWorkspaceDocument` | `model/map_document.py`, `model/content_workspace.py` | `tests/test_models.py` | MIGRATED |
| Content JSON v5 | `content_json.cpp`, `content_json_decoder.cpp` | `formats/content_json.py` | round-trip + `content_check` | MIGRATED |
| UMAP v4 | `authored_map.cpp` | `formats/umap.py`, `model/map_document.py` | round-trip + `map_compile` | MIGRATED |
| UWORLD v1 multimapa | `authored_world.cpp`, `WorldProjectDocument` | `formats/uworld.py`, `model/world_project.py` | ordering/links/round-trip | MIGRATED |
| Índice de conteúdo | `ContentWorkspaceDocument::index`, `AuthoredEntityIndex` | `ContentWorkspace` | invalid unrelated content remains browsable | MIGRATED |
| Categorias e referências | `content_reference_tools` | `model/content_workspace.py`, `ui/inspector.py` | typed reference lookup | MIGRATED |
| Map browser | `EditorApp::drawMapBrowser` | `ui/map_browser.py` | project map operations | MIGRATED |
| Tiles/brush/rectangle/fill | `editor_commands`, `EditorApp` | `ui/map_canvas.py`, `commands.py` | tile command tests | MIGRATED |
| Layers | `editor_commands`, `WorldProjectDocument` | `model/map_document.py`, `ui/layers.py` | ordering/rename/undo | MIGRATED |
| Collision | `SetCollisionCommand` | `ui/map_canvas.py`, `commands.py` | collision tests | MIGRATED |
| Enemy/NPC/Object/Pickup placement | `PlaceEntityCommand` | `ui/map_canvas.py`, `model/map_document.py` | IDs/position/category | MIGRATED |
| Select/move/delete/duplicate | `editor_commands` | `commands.py`, `ui/map_canvas.py` | command history | MIGRATED |
| Regions | `RegionPlacement`, region commands | `ui/map_canvas.py`, `model/map_document.py` | bounds/round-trip | MIGRATED |
| Semantics e stamps | authoring semantics and stamp commands | `ui/map_canvas.py`, structured inspector | UMAP/content round-trip | MIGRATED |
| Rules e encounters | `WorldRuleDefinition`, `EncounterDefinition` | structured collection inspector | round-trip | MIGRATED |
| Scenes/timeline/preview | `scene_timeline`, scene inspectors | `ui/scene_editor.py`, recursive typed form | scene round-trip | MIGRATED |
| Visual asset browser | `AssetBrowserCatalog` | `services/assets.py`, `ui/asset_browser.py` | safe paths/search | MIGRATED |
| Visual preview | `EditorVisualPreview` | `ui/preview.py` | nearest-neighbor asset preview | MIGRATED |
| Undo/redo | `EditorCommand`, `CommandHistory` | `commands.py` | command tests | MIGRATED |
| Localization | `EditorLocalization` | `services/localization.py` | language preference | MIGRATED |
| Preferences | `EditorPreferences` | `services/preferences.py` | portable config path | MIGRATED |
| Autosave/dirty prompts | Win32 timer + documents | `model/session.py`, Qt close handling | save/reload tests | MIGRATED |
| Validate workspace | `content_check`, `ContentValidator` | `services/toolchain.py` | valid/invalid CLI integration | MIGRATED |
| DMAP export | `map_compile`, `WorldProjectDocument::exportDmaps` | `services/toolchain.py` | compiler integration | MIGRATED |
| Playtest | `EditorPlaytestSession` | temporary authored files + C++ game process | launch/cleanup integration | MIGRATED |
| Legacy C++ UI shell | `EditorApp`, `EditorUiContext`, `win32_editor` | Qt `MainWindow` and widgets | Python Qt smoke | RETIRED |

## Dependências e execução

```bash
python3 -m venv .venv
. .venv/bin/activate              # Linux/macOS
.venv\\Scripts\\activate         # Windows
python -m pip install -e .
python -m tools.content_studio --content path/to/content --asset-root path/to/assets
```

PySide6 é dependência somente das ferramentas. Para ambientes sem display, os
testes de modelo funcionam sem Qt; smoke tests Qt usam `QT_QPA_PLATFORM=offscreen`.
O empacotamento pode ser feito separadamente para cada plataforma, sem incluir
Python no pacote do jogo.

## Fronteira de validação

O editor Python faz validação estrutural/local para manter documentos incompletos
editáveis e apresentar referências ausentes. `content_check`, `map_compile` e
`world_compile` continuam sendo a autoridade para export e runtime. Falhas desses
processos aparecem no painel de diagnósticos e bloqueiam o fluxo que exige conteúdo
executável.

