# Content Studio — deep authoring audit

Este documento registra a auditoria do código local do Content Studio e serve como
checklist de produção. A autoridade para as categorias continua sendo
`ContentWorkspaceDocument::categoryOrder()`; a tabela abaixo não substitui o código
nem marca uma capacidade como completa apenas porque existe uma operação no backend.

## Critério

Os estados usados nesta matriz são:

- `OK`: o campo/fluxo foi conferido e possui uma edição adequada;
- `IMPROVED`: recebeu uma melhoria concreta nesta passagem, mas ainda há campos ou
  affordances que não formam um editor completo;
- `PARTIAL`: existe editor tipado, porém ainda há lacunas de CRUD, referências ou
  edição visual;
- `BUG`: há um problema reproduzível que precisa de correção antes de considerar a
  categoria pronta;
- `READ-ONLY BY DESIGN`: definição builtin é navegável, mas não mutável.

## Matriz por categoria

| Categoria | Campos/coleções relevantes encontrados | UI atual | CRUD adequado | Reference picker | Quick inspect | Status |
|---|---|---|---|---|---|---|
| tileset | image path, tileSize, columns, rows | inspector + escolha de asset | NEW/DUPLICATE/DELETE quando authored | não aplicável ao próprio ID | sim, resumo | PARTIAL |
| projectile | visualId, facing, stats, spawn offsets | inspector tipado | operações typed existentes + ações gerais | visualId ainda requer revisão de affordance | genérico | PARTIAL |
| attack | damage, timing, legacy boxes, timeline, shapes opcionais | inspector + timeline | operações typed existentes | projectileId | genérico | PARTIAL |
| behavior | ranges e durations | inspector tipado | ações gerais | não aplicável | genérico | PARTIAL |
| enemy | visual, behavior, attacks[], rewardProfileId | inspector; attacks têm rows selecionáveis | add/remove selected + ordem | visual, behavior, reward, attack | sim | IMPROVED |
| item | visualId, category, stackLimit, use/equipment | inspector tipado | ações gerais | visualId | sim | PARTIAL |
| object | visual, interactable/container/destructible/door/activation | inspector tipado | ações gerais | visualId | sim | PARTIAL |
| pickup | visual, bounds, payload variant | inspector tipado | ações gerais | visualId/itemId | sim | PARTIAL |
| npc | visual, dialogue, tags[] | inspector; tags têm seleção própria | add/edit/remove selected + ordem | visual/dialogue | sim | IMPROVED |
| npcVisual | idle e bindings direcionais | inspector/preview | ações gerais | animation bindings | sim | PARTIAL |
| dialogue | nodes → pages/choices → conditions/actions | listas aninhadas com seleção separada | typed add/update/remove para níveis principais | targets ainda precisam de cobertura contextual maior | sim | IMPROVED |
| quest | title, objectives[], tags[], rewardGrantId | composer parcial; objectives/tags selecionáveis | add/remove selected + ordem | reward e targets | sim | IMPROVED |
| authoringDescriptor | displayName, category, tags[] | inspector + tags selecionáveis | add/edit/remove selected | category | sim | IMPROVED |
| tileSemantic | tileset/source/semantics | inspector tipado | ações gerais | tileset | sim | PARTIAL |
| stamp | grade, cells[], anchor e flags | inspector defensivo + célula selecionável | ações gerais; célula pode ser removida sem `pop_back` | tileId ainda é texto | sim | IMPROVED |
| playerProgression | baseStats, cumulativeExperienceThresholds[] | rows de thresholds selecionáveis | add/remove selected + update | não aplicável | sim | IMPROVED |
| rewardProfile | experience, loot[] | rows com pickup/chance/count e editor da entry | add/remove selected + update | pickup | sim, resumo | IMPROVED |
| rewardGrant | experience, gold, items[] | rows individuais com item/quantity | add/remove selected + update; duplicata bloqueada | item | sim, resumo | IMPROVED |
| shop | offers[] com item e preços | rows individuais com preços | add/remove selected + update | item | sim, resumo | IMPROVED |
| presentationEffect | lifetime, duration, priority, componentes | inspector tipado | ações gerais | referências conforme componente | genérico | PARTIAL |
| visualImage | root + relativePath | inspector + Asset Browser | NEW/DUPLICATE/DELETE + escolha de asset | não aplicável | sim | IMPROVED |
| staticSprite | imageId, source, anchor | inspector + preview existente | ações gerais | imageId | sim | PARTIAL |
| animation | imageId, frames[], markers[] | preview, frames e markers com edição typed | frame/marker operations existentes | imageId | sim | PARTIAL |
| enemyVisual | directional states e attacks[] | inspector/preview; actions typed | action rows existentes | animation bindings | sim | PARTIAL |
| objectVisual | idle/opened/destroying/destroyed/activation/door | inspector/preview | ações gerais | animation IDs | sim | PARTIAL |

As categorias builtin percorrem o mesmo índice e permanecem selecionáveis e
referenceable, porém `ContentWorkspaceDocument::writable()` mantém a proteção
read-only. O editor pode mostrar um workspace semanticamente inválido para que o
autor o corrija; validação/compile continuam sendo os pontos que bloqueiam conteúdo
inválido.

## Fronteira de autoria de entidades

O browser de entidades usa `AuthoredEntityIndex`, derivado diretamente do índice
authored atual de `ContentWorkspaceDocument`, para `enemy`, `npc`, `object` e
`pickup`. Ele não lê `GameContentRegistry` para descobrir a palette. Cada entrada
carrega a origem (`PROJECT` ou `ENGINE`/builtin) e o resultado da validação local da
definição mais sua cadeia mínima de dependências. Assim, um diagnóstico em uma quest
ou outra definição não relacionada não remove inimigos editáveis da palette; uma
dependência quebrada mantém a entrada visível, mas bloqueia `PLACE IN MAP` com o
diagnóstico correspondente.

MAP → Entities agora organiza as quatro categorias, oferece busca por display name e
`DefinitionId`, scroll real e placement repetido. `PLACE IN MAP` ativa a ferramenta
de entidade e mantém a definição até Escape, troca de ferramenta ou nova seleção. O
ghost usa `EditorVisualPreview` quando o visual é resolvível e um marker editor-only
quando não é; nenhum ghost altera o documento antes do clique. `PlaceEntityCommand`
continua sendo a única fronteira de mutação, portanto seleção, mover, remover,
undo/redo e IDs persistentes permanecem compartilhados.

Essa separação não relaxa runtime: compilação do workspace, validação de mapa,
export DMAP e playtest continuam exigindo o registry compilado válido. O caminho
builtin do editor é explícito no launcher, enquanto `EditorApp` não sintetiza builtin
silenciosamente quando recebe uma workspace ausente. Mapas novos começam sem spawn e
sem placements de gameplay; o diálogo de novo mapa mantém o spawn como opção explícita
para um template de trabalho.

O fluxo ainda é tooling incremental: a validação local cobre diagnósticos authored e
dependências de definição, enquanto disponibilidade física de assets continua sendo
diagnosticada pelo `VisualContentLoader`/preview. A matriz de categorias acima não é
promovida a `OK` apenas por causa do browser de placement.

## Correções desta passagem

### Crítico

- A seleção de célula de Stamp deixou de reutilizar o índice de marker de Animation.
  O inspector valida `cells` e o índice imediatamente antes de ler/remover, inclusive
  ao remover a última célula.

### Alto

- Reward Grant deixou de representar `items[]` somente como contador e `REMOVE` não
  faz mais `pop_back()`: cada item tem row, seleção, quantity editável e remoção
  individual.
- Reward Profile, Shop, progressão, attacks de Enemy, tags de NPC/descriptor e
  estruturas aninhadas de Dialogue/Quest receberam seleção independente e operações
  de remoção/reordenação sem índice compartilhado.
- Referências importantes usam `ContentReferencePicker`, filtragem case-insensitive,
  resumo e `Quick Inspect`; abrir a definição mantém uma pilha curta para `BACK`.
- Quest sem recompensa pode criar um `RewardGrant` authored no primeiro arquivo
  aberto e ligar `rewardGrantId` automaticamente. IDs duplicados e itens repetidos
  não são aceitos pelo fluxo de UI.

### Médio

- O Asset Browser cataloga somente paths relativos seguros e evita descoberta a cada
  frame. A decodificação de imagem permanece no preview/cache existente.
- A base de helpers de coleção, conversão de preview e compilação de máscaras de
  ataque agora é testável sem depender de pixel screenshots.

### Baixo / trabalho futuro

- Ainda existem inspectors legados com campos numéricos, labels em inglês e previews
  que podem ganhar thumbnails mais ricos.
- A tabela não considera concluída a autoria visual completa de tileset, sprite,
  animation e attack mask apenas por haver um helper: esses fluxos devem continuar
  evoluindo em incrementos concretos do Content Studio.

## Persistência e arquitetura

As operações continuam passando pelo `ContentWorkspaceDocument`, que altera DTOs
authored, revalida o workspace e grava Content JSON de forma canônica/atômica.
Collection Editor, picker, quick inspect, seleção, busca e navegação são estado de
tooling e não entram em UMAP, UWORLD, DMAP, DSAV ou nos Content JSON.

Não há mudança no formato de mapa/runtime nesta auditoria. A extensão opcional de
attack shapes permanece separada dos dados de apresentação; quando essa estrutura
for distribuída, seu versionamento deve seguir a política estrita do Content JSON.
