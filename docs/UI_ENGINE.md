# UI Engine & UI Composer — decisão e desenho

Status: **EM IMPLEMENTAÇÃO POR BLOCOS** (decisão aprovada em 2026-09-20). Aprovação do dono do projeto
(2026-09-20) para evoluir a interface do jogo para conteúdo authored, modelável
visualmente no Content Studio. Este documento preserva a decisão, define o modelo
de dados mínimo, a arquitetura runtime, o desenho do Composer no Studio e a ordem
de incrementos. **Nenhum código deste documento existe ainda.** Implementar
somente seguindo os blocos da seção 9, um por vez, com os critérios do
`AGENTS.md` (seção 26) e as provas de aceite da seção 10.

---

## 1. Problema e objetivo

Hoje a interface (HUD, inventário, banco, loja, crafting) é desenhada em C++:
`GamePresentation` consulta o snapshot `GameViewModel` e o desenho de cada tela é
código dedicado. Cada tela nova multiplica código de desenho e rotas de input.

Objetivo: interface como conteúdo authored — telas compostas por definições
(árvore de nós + bindings + ações) que o runtime C++ interpreta e o Studio
permite montar, mover e reestilizar sem recompilar C++ e sem tocar em gameplay.

A UI permanece apresentação: derivada do estado de gameplay, nunca autoridade.

## 2. Princípios vinculantes

1. **Dado ≠ aparência.** Um binding liga um dado do runtime a um widget; o widget
   decide o visual. `player.health` pode ser corações, barra ou orbe.
2. **UI = representação/intenção; gameplay = autoridade.** Eventos de UI viram
   ações do Action Registry que mapeiam para comandos/intents já existentes. A UI
   nunca muta gameplay diretamente e nunca valida regra de jogo.
3. **UI é conteúdo.** Definições entram pelo `AuthoredContentPack` →
   `ContentValidator`/`ContentCompiler` (Content JSON v7, retrocompatível com
   v1–v6). Validação rejeita binding, ação, componente ou asset desconhecido —
   mesmo padrão de `presentationEffectId` e `unlockQuestId`.
4. **Estado de UI é runtime-only.** Foco, seleção, estado transitório nunca vão
   para o DSAV — apresentação não é persistida (mesma regra dos fades).
5. **Keyboard-first.** Foco e navegação via `InputState`/`CommandBuilder` e o
   roteamento modal existente. Não existe mouse nem gamepad: sem hover, sem
   cursor, sem navegação por analógico.
6. **Sem scripting nem linguagem de expressão.** Valores derivados
   (`player.health.percentage`) são fornecidos prontos pelo Binding Registry.
   Condições de estado são comparações fixas contra constantes.
7. **Registries só expõem o que existe no runtime.** Nenhum binding de magia,
   settings ou saves enquanto esses sistemas não existirem.

## 3. Limites físicos do projeto (o que NÃO copiar de engines genéricas)

```text
resolução lógica fixa 272 × 224, integer scaling, letterbox
    → layout em pixels lógicos com anchors
    → SEM responsive layout, SEM flow-wrap, SEM preview multi-resolução

input apenas teclado
    → estados de interação: Normal / Focused / Pressed / Disabled (foco por teclado)
    → SEM Hover

sem sistema de áudio
    → estados e animações NÃO referenciam sons
```

Animação na primeira versão: `AnimatedImage` (spritesheet + fps + loop) reutiliza
`AnimationClip`/`Animator` existentes. Efeitos de tela (fade, vignette, shake)
continuam no `PresentationEffectSystem` — a UI não duplica essa fronteira.
Timeline autoral de animação é incremento futuro e só quando houver caso real.

## 4. Arquitetura em camadas

Caminho de leitura (dados):

```text
GameSession (gameplay autoritativo)
    ↓
GameViewModel (snapshot somente leitura — já existe)
    ↓
UIBindingRegistry (caminhos de leitura tipados sobre o snapshot)
    ↓
UI Runtime (árvore instanciada, foco, bindings, estados visuais)
    ↓
componentes UI (panel / image / text / meter / slot / ...)
    ↓
layout (anchors + containers)
    ↓
Renderer2D → Framebuffer (recursos existentes, nada novo em nível de pixel)
```

Caminho de intenção (ações):

```text
InputState → CommandBuilder → roteamento modal existente
    ↓
UIActionRegistry (ids estáveis)
    ↓
comandos/overlays da GameSession (autoridade continua no gameplay)
```

Decisão de escopo importante: **a ativação de telas continua em C++** no bloco
inicial. O roteamento modal atual (`InventoryOverlayState`, banco, loja,
crafting) decide quem está aberto; as definições descrevem apenas o visual, os
bindings e as ações de cada tela. A tela/stack/navigation autoral entra somente
no bloco UI-5, quando existir o primeiro menu real.

## 5. Modelo de dados (entidades mínimas)

Definições imutáveis compiladas (mesmo padrão dos demais catálogos):

```text
UIScreenDefinition
    id
    kind: hud | overlay | screen
    root: UINodeDefinition

UINodeDefinition
    id
    component            # referência ao Component Registry
    layout               # anchor, offset, size, z, visible
    properties           # props tipadas por componente
    bindings[]           # property ← caminho do Binding Registry (+ converter fixo)
    states[]             # id + condição + deltas visuais
    actions[]            # evento (activate/press) → id do Action Registry
    children[]
```

```text
UIStateDefinition
    id                   # normal, focused, lowHealth, ...
    condition            # source + op (eq/lte/gte) + constante — sem expressões
    visual               # tint, opacity, sprite swap, visibility

UIComponentDefinition (template/prefab, bloco futuro)
    id + nós + parâmetros; instanciado por referência
```

`UIThemeDefinition` fica **deferido**: começa com estilo implícito por
componente; só entra quando duas telas reais precisarem de skins diferentes.

### Component Registry (fixo em C++ no início)

```text
Bloco UI-1:  group, panel, image, animatedImage, text, meter
Bloco UI-4:  slot, repeater, grid/list
Depois:      scroll, tooltip — somente com caso real
```

`meter` é o componente que substitui o HUD de corações: modos `segmented`,
`fillHorizontal`, `fillVertical` com sprites full/half/empty ou fill — cobrindo
corações, barra e orbe com o mesmo binding.

### Binding Registry (fonte real: `GameViewModel`)

Primeira versão expõe exatamente o que o snapshot já carrega:

```text
player.health.current / player.health.max / player.health.percentage (derivado pronto)
player.gold
player.ammo.itemId / player.ammo.amount / player.ammo.icon
player.quickSlots[0..3]
player.inventory[0..29].itemId / .amount / .icon
player.armor / player.accessory / player.derivedMaximumHealth / player.attackDamageBonus
overlay.inventoryOpen / overlay.inventoryFocus / overlay.inventorySelection
overlay.bankOpen / overlay.bankFocus / overlay.bankSelection / overlay.bankGold
overlay.shopOpen / overlay.shopMode / overlay.shopFeedback
overlay.craftingOpen / overlay.craftingTab / overlay.craftingFeedback
crafting.recipes[] (coleção — só consumida quando o Repeater existir)
crafting.selectedRecipe / crafting.maxCraftable
```

O runtime exporta esses caminhos como manifest (JSON) para o Studio popular os
pickers — mesmo padrão das referências tipadas `itemId → items` do crafting.

### Action Registry (mapeia para comandos que já existem)

```text
inventory.toggle / inventory.use / inventory.drop
inventory.equip / inventory.unequip
crafting.toggleTab / crafting.craft
bank.focus / bank.depositAll / bank.withdrawAll
shop.buy / shop.sell
screen.close            # fecha o overlay ativo (equivalente atual)
game.save / game.load   # equivalentes lógicos de F5/F9
```

Nomes finais são decididos no UI-1; a regra é: cada id corresponde a um comando
ou intenção **já implementada** na `GameSession`/`CommandBuilder`. Ação que não
tem comando real hoje não entra no registry.

## 6. Formato — Content JSON v7

Categoria nova `uiScreens` (futuramente `uiComponents`/`uiThemes`). Arquivo com
`version < 7` contendo `uiScreens` é erro; arquivos v1–v6 continuam lendo. O
formato exato é congelado no bloco UI-1; o exemplo abaixo ilustra a filosofia:

```json
{
  "format": "dungeon-underworld-content",
  "version": 7,
  "uiScreens": [
    {
      "id": "screen.hud",
      "kind": "hud",
      "root": {
        "id": "hud.root",
        "component": "group",
        "layout": { "anchor": "topLeft", "offset": [0, 0], "size": [272, 224] },
        "children": [
          {
            "id": "hud.health",
            "component": "meter",
            "layout": { "anchor": "topLeft", "offset": [8, 8] },
            "meter": {
              "mode": "segmented",
              "segmentValue": 2,
              "sprites": { "full": "hud.heart.full", "half": "hud.heart.half", "empty": "hud.heart.empty" }
            },
            "bindings": [
              { "property": "value", "source": "player.health.current" },
              { "property": "maximum", "source": "player.health.max" }
            ],
            "states": [
              {
                "id": "lowHealth",
                "when": { "source": "player.health.percentage", "op": "lte", "value": 30 },
                "visual": { "tint": [180, 40, 40] }
              }
            ]
          }
        ]
      }
    }
  ]
}
```

Trocar `meter.mode`/sprites pela variante barra ou orbe é mudança de definição,
não de código — é exatamente a prova de aceite da seção 10.

## 7. UI Runtime

- `UIDocument` compilado e imutável (como os demais catálogos); `UIRuntime`
  residente na camada de apresentação, ao lado de `GamePresentation`.
- Por tick: snapshot do `GameViewModel` → resolve bindings → avalia condições de
  estado → produz draw via `Renderer2D` (sprites, bitmap font, tint/opacity já
  existentes). Nenhum recurso novo de pixel.
- Foco: ordem determinística derivada da árvore; navegação direcional fica para
  quando houver tela de menu real.
- Migração incremental: HUD primeiro. O overlay de inventário/banco/loja/crafting
  continua desenhado como está até o bloco UI-4.

## 8. UI Composer no Studio

Novo modo **UI** no shell existente (MAP / CONTENT / UI), mesmo padrão de
documentos vivos por documento, dirty state e save canônico atômico.

```text
┌──────────────┬──────────────────────────────┬───────────────────┐
│ Screens      │  Canvas 272×224 (zoom        │  Inspector        │
│ Hierarchy    │  inteiro, grid, snap,        │  Layout           │
│ (árvore)     │  drag/resize, seleção)       │  Componente       │
│              │                              │  Bindings         │
│              │  preview data aplicado       │  States / Events  │
├──────────────┴──────────────────────────────┴───────────────────┤
│ Preview Data │ Assets (visualImages/staticSprites) │ Diagnósticos │
└──────────────┴─────────────────────────────────────┴──────────────┘
```

- **Hierarchy**: árvore de nós; adicionar/remover/reordenar componentes a partir
  do Component Registry; drag para reorganizar painéis.
- **Canvas**: render da definição com os valores de Preview Data; seleção e
  manipulação direta; a resolução é sempre 272×224 (não existe preview
  multi-resolução neste projeto).
- **Inspector contextual**: campos populados pelos manifests exportados pelo C++
  (componentes, bindings, ações) — referências tipadas, sem digitar caminhos
  livres; validação espelha o C++ (erros semânticos editáveis mas bloqueiam
  export/playtest, padrão atual do Studio).
- **Preview Data**: valores simulados por binding (HP 37/100, gold 152,
  inventário com N itens) e toggles de estado (lowHealth etc.) para testar a UI
  sem abrir o jogo.
- Fora do primeiro Composer: play interativo, timeline de animação, editor de
  themes — incrementos futuros com caso real.

## 9. Blocos de implementação (ordem)

```text
UI-0  este documento                                        DECISÃO REGISTRADA
UI-1a núcleo de dados: DTOs + decoder/encoder v7 +
      validação + ScreenCatalog no GameContentRegistry;
      Binding/Action Registry HUD              CONCLUÍDO
UI-1b runtime de apresentação (UiPresenter +
      GameViewModelBindings); HUD de corações migrado
      para definição com visual idêntico       (prova 1a) CONCLUÍDO
UI-2  estados condicionais (tint/visibilidade) + variantes
      barra (fillHorizontal) e orbe (fillVertical)
      alternando somente a definição           (prova 1b) CONCLUÍDO
UI-3  UI Composer no Studio (hierarchy/canvas/inspector/
      preview data) + round-trip + validação espelhada      PENDENTE
UI-4  slot/repeater/grid + migração do overlay de
      inventário/crafting                      (prova 2)   PENDENTE
UI-5  screen/navigation quando existir o primeiro menu
      real (ex.: tela de save/load); conecta com a trilha
      Scene/Game-State do roadmap                          PENDENTE
```

Notas de implementação vigentes (UI-1/2): o tint do delta visual é
multiplicativo (`drawImageRegionTinted`, arredondamento determinístico);
o alpha do delta visual e o `animatedImage` permanecem deferidos até o
primeiro uso real; `fillHorizontal` cresce da esquerda para a direita e
`fillVertical` cresce de baixo para cima (líquido circular = orbe); a
troca corações/barra/orbe acontece authoring `screen.hud` no workspace,
que sobrescreve o builtin por id sem tocar em C++.

Cada bloco termina em: testes portáteis passando, `content_check` cobrindo as
rejeições novas, e gates Win32 (`build.bat`, smoke) quando tocar apresentação.
Não iniciar o bloco seguinte com a prova do bloco atual reprovada.

## 10. Aceite (provas)

- **Prova 1** — `player.health` renderizado como corações (visual idêntico ao
  atual), depois como barra e como orbe, alternando **somente a definição**,
  sem alterar gameplay nem C++.
- **Prova 2** — painéis do overlay de inventário/crafting reorganizados
  (ordem/posição/tamanho) **somente pela definição**.

Se qualquer prova falhar, a arquitetura não está desacoplada: parar e corrigir
antes de avançar, sem "consertar" a prova com branch por conteúdo.

## 11. Fora de escopo (explícito)

```text
mouse / hover                     gamepad / navegação analógica
áudio em estados/animações        responsive layout / multi-resolução
linguagem de expressões           two-way bindings / tela de settings
themes (até 2 telas reais)        modding de UI
shaders / timeline / LLM          spellbook, shops UI próprios (só com o gameplay real)
networking / multiplayer          (fora de escopo permanente do projeto)
```

## 12. Relação com sistemas existentes

- `GameViewModel` continua a única fonte de leitura; o Binding Registry é a
  formalização dessa fronteira, não uma camada paralela.
- `PresentationEffectSystem` continua responsável por efeitos de tela; a UI não
  reimplementa fades/vignette.
- O roteamento modal atual (`InventoryOverlayState` e overlays de bank/shop/
  crafting) continua decidindo ativação; definições descrevem visual/bindings/
  ações até o bloco UI-5.
- DSAV não persiste nada de UI; `VisualContentLoader` continua a fronteira de
  assets visuais (a UI referencia `visualImages`/`staticSprites`/`animations`
  existentes, sem carregar PNG por conta própria).
