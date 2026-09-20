# Content Studio — Auditoria de UI/UX por tela

> Status: backlog de refatoração. Itens já implementados ficam marcados com
> ✅ e o commit correspondente; os demais aguardam o plano por tela.

Data: 2026-09-19
Método: inspeção de código + renderização offscreen de **todas as telas** com o
projeto real do repositório (`build/uiux_audit/`, artefato não versionado):
janela completa nos dois temas, 15 seções do modo Mapas, 2 do modo Conteúdos,
inspetor com objeto selecionado, diagnósticos e 16 diálogos.

## Legenda de prioridade

| Prioridade | Significado |
|---|---|
| **P1** | Confunde, engana ou bloqueia o autor (erro provável de uso) |
| **P2** | Fricção alta / esforço desnecessário / risco de erro |
| **P3** | Polimento e consistência |

✅ = implementado. G1, G8, G5, G6 em `6e3b45e`; G2/S1 em `6869292`; C1, G7, G4, C2 em `7cb1a0e`; IN1 em `de909bd`; S2, IN2 em `caec6be`/`a247f38`; SS1, ST1-ST3 em `97c7547`; SE1-SE2 em `e7cc8bb`; SP1-SP2 em `3d9a318`; M1-M2, L1 neste commit.

Os IDs (`G*`, `S*`, `C*`, `M*`…) são a chave para o plano de conserto por tela.

---

## 1. Achados globais (afetam várias telas)

| ID | Pri | Problema | Proposta |
|---|---|---|---|
| G1 ✅ | P1 | **Idioma misturado PT/EN.** ContentBrowser ("Create/Delete/Duplicate/Place in Map/Find Usages/Back"), LayersPanel ("Add/Rename/Move Up/…"), SceneEditor inteiro em EN, StructuredInspector ("Add", "Remove Last", "Delete item", "(empty collection)"), status bar ("Tool: select"), diagnósticos ("No diagnostics"), buscas ("Search display name / definitionId"). | Migrar todas as strings para o `Translator` (pt-BR/en-US), como já feito nos painéis de tileset/porta. |
| G2 ✅ | P1 | **Navegação em 3–4 níveis de abas** (Modo → Seção → Coleções → [+ abas internas]) e as **15 abas de seção não cabem** — "Regras / Links" fica cortada atrás de setas de scroll discretas. | Sidebar vertical de seções com ícone + texto (ou dropdown agrupado por domínio: Mapa, Tiles, Conteúdo, Mundo); reduzir número de abas de topo. |
| G3 | P2 | **Empty states sem orientação.** Painéis grandes e vazios sem dizer o que fazer (Links, Scenes, Semantics, Layers). Alguns já têm ("Nenhum item criado", "Arraste um elemento…"). | Padronizar empty state com texto de ação (ex.: "Nenhuma porta ainda — crie um objeto com capability door em Objetos") + botão de ação primária. |
| G4 ✅ | P2 | **Feedback só na status bar.** Mensagens críticas de modo ("Placement active: X. Click the map or press Escape") somem; fácil perder o estado da ferramenta. | Banner/overlay persistente no canvas enquanto um modo de colocação estiver ativo (nome do conteúdo + Esc para cancelar). |
| G5 ✅ | P2 | **Ações destrutivas sem confirmação.** "Excluir mapa" (`remove_map`) apaga na hora; Layers "Remove"; Scenes "Delete". (Itens e conteúdo já confirmam.) | Padronizar `QMessageBox.question` com o nome do que será apagado, incluindo efeitos ("O mapa X será removido do projeto"). |
| G6 ✅ | P2 | **Botões truncados** em larguras padrão de painel: Objetos ("Configurar obje…", "Colocar no ma…"), Itens ("Excluir Iter…", "Colocar no m…"). | Layout de botões em grid 2×N com size policy adequada ou botões só-ícone com tooltip; testar em 260 px (largura salva nas preferences). |
| G7 ✅ | P2 | **Sem discoverability de atalhos/recursos:** Frame Map é só `Home` (nenhum botão), zoom só no scroll do mouse (sem `+`/`-`/botões), undo/redo só no menu. | Adicionar actions de Zoom In/Out (+ botões), tooltip com atalho nas actions ("Enquadrar Mapa (Home)"), botões de undo/redo na toolbar. |
| G8 ✅ | P2 | **Acentuação faltando em PT-BR** em strings novas (editor de porta): "Configuracao da porta", "Chave necessaria", "Persistencia", "Aplicar configuracao". | Revisar TODO o arquivo de traduções; adicionar teste que(strings pt-BR contenham acentos esperados?) ou revisão manual guiada. |
| G9 | P3 | **Buscas inconsistentes:** placeholders variados ("Procurar", "Procurar objetos", "Search display name / definitionId"), alguns sem filtro de categoria. | Padronizar placeholder e comportamento (filtrar por id + display name + tooltip). |
| G10 | P3 | **Sem menu de projetos recentes** (`preferences.lastProject` já é salvo mas não é usado na UI). | Menu Arquivo > Abrir Recente (5 itens). |
| G11 | P3 | **Sem tela de Settings** (asset root só via arquivo de preferências/CLI; idioma e tema espalhados em menus). | Diálogo único de preferências (idioma, tema, asset root, larguras). |
| G12 | P3 | **Mnemônicos/tooltips ausentes** em menus e botões. | Adicionar `&` nos menus principais e tooltips em botões-ícone. |

## 2. Shell da janela (menus, toolbar, abas, diagnostics)

| ID | Pri | Problema | Proposta |
|---|---|---|---|
| S1 ✅ | P2 | **Painel direito desperdiçado sem seleção:** "No selection" + botão "Excluir" desabilitado ocupam a coluna inteira. | Estado vazio compacto com dica ("Selecione um tile, objeto ou região no mapa") e colapsar editores contextuais (porta/transição) quando vazios. |
| S2 ✅ | P2 | **Diagnósticos como texto cru:** caminhos absolutos, severidade entre colchetes, sem cor, sem filtro, sem clique-para-abrir definição; contagens ("files: 1", "definitions: 313") aparecem como *warning*. | Lista estruturada (ícone por severidade, caminho relativo clicável, filtro por severidade); contagens como info, não warning. |
| S3 | P3 | Toolbar some no modo Conteúdos sem explicação; abas de modo não indicam conteúdo ("Mapas"/"Conteúdos" sem ícones). | Ícones nas abas de modo; considerar manter toolbar visível (desabilitando tools de mapa). |
| S4 | P3 | Título usa "*" para dirty — único indicador de mudanças não salvas. | Indicador por documento nos browsers (mapa/definição com dot) + "Salvar tudo" destacado quando dirty. |

## 3. Canvas do mapa

| ID | Pri | Problema | Proposta |
|---|---|---|---|
| C1 ✅ | P2 | **Sem controles de zoom** (roda do mouse apenas) nem leitura de coordenadas do tile sob o cursor. | Overlay discreto no canto: zoom %, x,y do tile, tamanho do mapa; botões +/−/fit. |
| C2 ✅ | P2 | Labels de links/spawns desenhados sobre o mapa ficam **sobrepostos e ilegíveis** (ex.: `region.break` colado no `player.start`). | Fundo semi-opaco nas labels, offsets anti-colisão, mostrar só com zoom mínimo ou hover. |
| C3 | P3 | Sem destaque de camada ativa ( Ground/Wall pintam igual; só LayersPanel muda a seleção). | Dim das camadas não ativas (toggle nas preferências). |
| C4 | P3 | Ghost preview de colocação existe mas é sutil (0.55 alpha) — sem célula/grid highlight do destino. | Highlight do tile alvo + borda do payload. |

## 4. Painéis do modo Mapas (uma linha por aba de seção)

### 4.1 Mapas (MapBrowser)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| M1 ✅ | P2 | **O mapa de entrada não é destacado** na árvore (só descobre via botão "Definir como entrada"). | Badge/ícone de flag no mapa de entrada + tooltip. |
| M2 ✅ | P3 | Duplo clique não abre propriedades (só context menu). | Duplo clique → propriedades. |

### 4.2 Camadas (LayersPanel)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| L1 ✅ | P2 | Visibilidade codificada como `● / ○` no texto; reorder só por botões Move Up/Down. | Ícone de olho clicável por linha + drag-and-drop para reordenar; botões restam como atalho. |

### 4.3 Tiles (TilesetLibrary)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| T1 | P2 | Badge vermelho "C" (pixel collision) é criptico; tooltip existe mas só depois de configurado. | Badge com ícone + tooltip fixo ("Colisão por pixel definida"). |
| T2 | P2 | Atlas mostra **todas as células vazias** do spritesheet (scroll horizontal enorme em tilesets esparsos). | Modo compacto (ocultar células vazias) ou "ir para primeiro/último tile usado". |
| T3 | P3 | Tile selecionado pintado de azul chapado esconde a arte. | Overlay de borda/marcação translúcida. |

### 4.4 Spritesheet / Animação (SpritesheetLibrary)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| SP1 ✅ | P2 | Lista mostra só display name — há **muitos "Idle/Down/Up" repetidos** e é impossível saber de qual spritesheet. | Mostrar `name (imageId)` ou agrupar por spritesheet; tooltip com id da animação. |
| SP2 ✅ | P3 | Play/Pause usam glifos unicode "▶/⏸" no texto — inconsistentes com o sistema de ícones. | Usar `icon("playtest")`/ícone dedicado + texto. |

### 4.5 Objeto (ObjectLibrary)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| O1 | P2 | Botões truncados (ver G6). | Ver G6. |
| O2 | P3 | Comportamento (baú/destrutível/door) só aparece como texto corrido nos detalhes. | Chips/badges de capability na lista e nos detalhes. |

### 4.6 Portas (DoorLibrary)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| D1 | P2 | Não explica **como criar** uma porta (fluxo começa em Objetos com capability door); painel só tem "Colocar no mapa". | Empty state com passo-a-passo ou botão "Criar porta…" que abre o fluxo de objeto com preset door. |

### 4.7 Player (PlayerLibrary)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| PL1 | P2 | Lista sem miniatura do player; prévia só aparece dentro do dialog de configuração. | Thumbnail na lista (frame idle down) como nos objetos. |

### 4.8 Itens (ItemLibrary)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| I1 | P2 | Botões truncados (ver G6). | Ver G6. |

### 4.9 Smart Terrain (SmartTerrainPalette)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| ST1 ✅ | P1 | **Cards com texto sobreposto** ("imported2" renderiza sobre outro rótulo) e mostram **ids crus** em vez de nomes de família. | Card com nome de exibição + mini-preview 3×3; corrigir layout/overlap. |
| ST2 ✅ | P2 | Mensagem contraditória: cards existem em cima e embaixo lê-se "Nenhuma família semântica disponível". | Mostrar estado real ("família sem regra completa — clique para editar") ou ocultar. |
| ST3 ✅ | P3 | "Seed determinística" e "Room / Area Brush" sem explicação (desabilitado sem motivo visível). | Tooltip + habilitar quando família selecionada com regra completa. |

### 4.10 Editor de Tile Semântico (TileSemanticEditor)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| SE1 ✅ | P2 | Os **9 vizinhos (Norte/Sul/Leste/Oeste…) são 9 combos soltos** — o conceito é espacial mas a UI é linear; o TerrainRuleDialog já resolve isso com grid 3×3. | Reusar o grid 3×3 visual do TerrainRuleDialog. |
| SE2 ✅ | P3 | Sem preview do tile sendo editado nem indicação do tileset/índice selecionado. | Cabeçalho com miniatura do tile + id. |

### 4.11 Semântica / Stamps (SemanticPalette)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| SS1 ✅ | P1 | Lista de **ids crus gigantes** (`semantic.rule.terrain.imported.wall.tileset.imported.outerCorner.north_west.40`) sem miniatura; inutilizável para escolher um stamp. | Lista por família/papel com miniatura do tile e nome curto; busca. |
| SS2 | P3 | Tabs internas "Semantics"/"Stamps" em EN (G1). | Traduzir. |

### 4.12 Elementos do Mapa (MapElementsPalette)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| ME1 | P2 | Três linhas de texto sem ícone nem estado visual de seleção; o "arraste" é invisível até tentar. | Cards visuais (ícone spawn/porta/região) com selected state e cursor de drag. |

### 4.13 Entidades (ContentBrowser enemies/npcs/objects/pickups)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| E1 | P2 | Reusa o ContentBrowser com os problemas G1/G9 (botões EN, sem thumbs). | Ver G1; thumbs como nos objetos. |

### 4.14 Cenas (SceneEditor)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| SC1 | P1 | **100% EN** (New Scene/Add Actor/Add Clip/Play/Restart/Markers…) — tela inteira fora do padrão (G1). | Traduzir. |
| SC2 | P2 | **Fluxos via QInputDialog em sequência** (Add Actor = 3 diálogos seguidos; Add Clip = 2). | Form único inline ("Adicionar ator" com slot+kind) ou painel de criação. |
| SC3 | P3 | Controles de timeline confusos ("Timeline zoom"/"Playhead" como checkboxes soltos); "No scene selected" duplicado (topo e rodapé). | Toolbar da timeline com ícones; um único empty state. |

### 4.15 Regras / Links (coleções: Links, Spawns, Regions, World Rules, Encounters)
| ID | Pri | Problema | Proposta |
|---|---|---|---|
| CO1 | P2 | Quarta camada de abas dentro da seção; formulários dependem do inspetor genérico cru (ver IN1); Add/Delete EN. | Consolidar em lista+formulário dedicado por tipo (Link: mapa A→B com pickers), como DoorInstanceEditor já faz. |

## 5. Inspetor genérico (StructuredInspector — mapa e conteúdos)

| ID | Pri | Problema | Proposta |
|---|---|---|---|
| IN1 ✅ | P1 | **Renderiza dados crus:** chaves técnicas (`definitionId`, `visualSetId`), `objects: 1` como título, posição x/y com spinboxes esticados, "(empty collection)" cortado com scrollbars. | Labels humanizadas (dicionário pt/en), layouts com largura máxima, grupos (Posição/Visual/Comportamento), widgets dedicados para campos conhecidos (position, facing, visualSetId já têm referências). |
| IN2 ✅ | P2 | Sem validação inline nem feedback de erro por campo (erros vão para o painel de diagnósticos). | Bordas/tooltips de erro por campo quando `validate_local` falhar. |
| IN3 | P3 | Remoção com "×" minúsculo e "Delete item" EN (G1). | Botão com ícone lixeira + confirm quando destrutivo. |

## 6. Modo Conteúdos

| ID | Pri | Problema | Proposta |
|---|---|---|---|
| CT1 | P2 | Lista "All" mistura **todas** as categorias com ids brutos (inclui `tileSemantics` enormes) e mostra itens duplicados na prática (mesmo id em categorias distintas). | Padrão = categoria selecionada (não "All"); thumbs para categorias visuais; colapsar tileSemantics. |
| CT2 | P2 | Prévia de conteúdo pode ficar espremida (no render ficou ~100 px) — sem largura mínima. | `setMinimumWidth`/stretch na coluna de prévia. |
| CT3 | P3 | "Find Usages" abre `QInputDialog` com lista modal de textos crus. | Diálogo próprio com lista clicável que navega para a definição. |

## 7. Diálogos

| ID | Pri | Tela | Problema | Proposta |
|---|---|---|---|---|
| DL1 | P2 | Propriedades do Mapa | Dica de redimensionamento aparece também no modo **novo** (irrelevante); sem presets de tamanho. | Mostrar dica só em modo edição; presets (16×16, 32×24, custom). |
| DL2 | P2 | Editor de Ataque | Denso mas bem organizado; labels mistos (`offsetX/offsetY` EN + `largura/altura` PT); 4 linhas fixas de hitbox mesmo sem melee. | Traduzir labels; ocultar seção hitbox para ataques projectile; anchors com nomes consistentes. |
| DL3 | P2 | Player — Sequência de Frames | Diálogo gigante (1180×900) com caixas vazias; botões ↑/↓ sem texto (só caractere); 9 botões de máscara em grade plana; help parágrafo enorme. | Compactar (tabs ou accordions por canal), botões com ícone+texto, encurtar help com tooltips dos InfoButtons (padrão do editor de ataques). |
| DL4 | P3 | Colisão Animada | Bom; Save/Cancel ficam EN (QDialogButtonBox padrão). | Botões traduzidos (como no resto). |
| DL5 | P3 | Imports (Tileset/Spritesheet) | Sugestão de id genérica ("tileset.authored"); good previews. | Sugerir id pelo nome do arquivo importado. |
| DL6 | P3 | Smart Terrain — Regras | Bom modelo (grid 3×3 + atlas); rodapé só "Cancelar" à direita (salvar à esquerda) — inconsistente. | Padronizar barra de botões OK/Cancelar com "Salvar lógica" como primary. |
| DL7 | P3 | Gerenciador de Ataques | Simples e claro; lista sem prévia do ataque. | Mini-preview do visual ao selecionar. |
| DL8 | P3 | Cenas — QInputDiálogos | Ver SC2. | Ver SC2. |
| DL9 | P3 | Editor de Item / Visual Picker | Formulário razoável; mesmo padrão de help denso. | Encurtar helps; InfoButtons como no editor de ataques. |

## 8. Backlog consolidado (ordem sugerida para o plano de conserto)

Fase 1 — **Fundação transversal** (desbloqueia o resto) — ✅ concluída:
1. ✅ **G1** Tradução completa (incl. SC1, SS2, CO1) + **G8** acentos.
2. ✅ **G2/S1** Navegação: sidebar de seções com ícones; estados vazios do painel direito.
3. ✅ **G5** Confirmações destrutivas padronizadas.
4. ✅ **G6** Correção de botões truncados (Objetos/Itens).

Fase 2 — **Fluxos de alta frequência**:
5. ✅ **C1/G7** Zoom + coordenadas + dica de atalhos no canvas.
6. ✅ **G4/C2** Banner de modo de colocação no canvas.
6. ✅ **G4/C2** Banner de modo de colocação no canvas.
7. ✅ **IN1/IN2** Inspetor genérico humanizado com validação inline por campo (borda vermelha + tooltip; erros sem campo vão para faixa própria).
8. ✅ **S2** Diagnósticos estruturados com severidade, filtro e navegação.
8. **S2** Diagnósticos estruturados com navegação.

Fase 3 — **Telas específicas** (uma por vez, no formato plano por tela):
9. ✅ **SS1/ST1-ST3** Semântica/Stamps (miniaturas, rótulos, busca) e Smart Terrain (overlap fantasma, status, tooltips).
10. ✅ **SP1/SP2** Animações clusterizadas por spritesheet + ícones de playback do registry.
11. ✅ **SE1/SE2** Grid 3×3 (bússola de vizinhos) + cabeçalho com miniatura no editor semântico.
12. ✅ **M1/M2/L1** Badge de entrada + duplo clique no MapBrowser; checkbox de visibilidade + drag reorder nas camadas.
13. **T2/T3** Atlas compacto de tilesets e seleção menos intrusiva.
14. **DL1–DL9** Polimento de diálogos (um por vez).
15. **G10–G12** Recentes, Settings, mnemônicos.
13. **DL1–DL9** Polimento de diálogos (um por vez).
14. **G10–G12** Recentes, Settings, mnemônicos.

> Cada item acima deve virar uma tarefa com: tela, escopo exato, critérios de
> aceite e smoke visual — seguindo o fluxo usual (implementar → testes →
> screenshots offscreen → commit).
