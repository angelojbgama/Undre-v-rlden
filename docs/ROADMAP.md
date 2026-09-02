# Roadmap incremental

## Regras de execução

Cada item deve terminar em algo executável, observável ou testável. Não avançar com falhas conhecidas na base. Assets licenciados nunca viram fixtures públicas; testes de pixels usam imagens sintéticas próprias. Decisões adiadas não ganham pastas/interfaces vazias.

## Fase 0 — auditoria e decisões (atual)

- [x] Inventariar recursivamente PNGs, dimensões, alpha e duplicatas.
- [x] Inspecionar visualmente layouts, direções, frames e ambiguidades.
- [x] Registrar licença e risco de redistribuição.
- [x] Propor arquitetura, entidades, editor, `.dmap` e fases.
- [ ] Revisar/aprovar com o responsável antes de qualquer código.

Aceite: os quatro documentos existem, todos os 84 assets únicos estão catalogados, nenhuma imagem/código/build foi modificado ou criado.

## Fase 1 — plataforma, janela e primeiro pixel

1. Criar `build.bat` mínimo para x64/C++20 e programa console de smoke test. Aceite: build limpo e erro claro sem ambiente MSVC.
2. Criar contratos mínimos de `PlatformApp`, eventos e relógio, sem `windows.h` público. Aceite: headers de engine compilam isoladamente.
3. Criar janela Win32 DPI-aware e message pump. Aceite: abre, redimensiona e fecha corretamente.
4. Implementar relógio QPC. Aceite: teste mede intervalos monotônicos e converte frequência sem overflow observável.
5. Criar `Framebuffer(272,224)` RGBA8 e `clear`. Aceite: stride/tamanho e bounds testados.
6. Apresentar framebuffer por DIB. Aceite: padrão vermelho/verde/azul prova orientação e canais.
7. Implementar integer scale + letterbox + client minimum. Aceite: 1×–4× nítidos, centralizados, sem bilinear em resize.
8. Montar loop com accumulator 60 Hz, clamp e limite de catch-up. Aceite: contador de ticks permanece próximo de 60/s enquanto render varia.

Marco executável: janela mostra padrão gerado por CPU em 272×224, escalado corretamente.

## Fase 2 — primitivas, PNG e sprites

1. Implementar/testar `setPixel`, `fillRect` e clipping. Aceite: golden sintético cobre bordas negativas/positivas.
2. Implementar `drawImageRegion` opaco. Aceite: source/destination parcial correto.
3. Implementar alpha blend RGBA straight. Aceite: casos alpha 0/128/255 e canais têm valores esperados.
4. Definir `IImageDecoder` e `ImageData`. Aceite: nenhum tipo WIC no header público.
5. Implementar decoder WIC RGBA. Aceite: carrega uma cópia local licenciada em runtime e rejeita arquivo inválido com erro útil.
6. Criar ownership/cache mínimo de imagens por `AssetId`. Aceite: uma imagem não é decodificada duas vezes e lifetime é seguro.
7. Implementar flip X durante blit. Aceite: imagem sintética assimétrica espelha exatamente.
8. Definir `SpriteSheet`, `AnimationClip/Frame`, anchor e draw offset em dados.
9. Criar visualizador temporário de clip. Aceite: walk/attack trocam mantendo os pés sobre o mesmo pixel; valores da auditoria são confirmados/corrigidos.
10. Renderizar bitmap font 7×9 com tabela explícita. Aceite: A–Z, a–z, 0–9 e símbolos conhecidos; glyph ausente tem fallback.

Marco: sprites e texto pixel-art carregados por WIC e desenhados pelo renderer próprio.

## Fase 3 — tilemap, câmera e colisão

1. Centralizar `GameMetrics` e tipos de coordenadas. Aceite: conversões unitárias inclusive coordenadas negativas.
2. Criar mapa runtime em memória e uma layer de tile refs. Aceite: mapa maior que a tela é instanciado.
3. Renderizar apenas tiles visíveis. Aceite: câmera atravessa mapa sem desenhar fora do range.
4. Implementar câmera world→logical com clamp opcional. Aceite: bordas e mapas menores que viewport têm comportamento definido.
5. Adicionar múltiplas layers/passes. Aceite: ground, low e foreground ocluem na ordem esperada.
6. Criar collision grid separado. Aceite: overlay debug mostra célula visual diferente de célula sólida.
7. Implementar AABB versus tiles e resolução por eixo. Aceite: corpo pequeno desliza em paredes/cantos sem atravessar.
8. Adicionar broad-phase por células para corpos/queries. Aceite: custo depende da vizinhança, não do mapa inteiro.

Marco: sala grande navegável por câmera de debug, com colisões visualizadas.

## Fase 4 — comandos, entidade Player e animação

1. Implementar eventos físicos e `InputState` (held/pressed/released). Aceite: edges não se perdem entre frames e ticks.
2. Mapear input para `PlayerCommand` tickado. Aceite: teste injeta comandos sem janela.
3. Implementar handles com generation e stores dos componentes necessários. Aceite: handle destruído nunca resolve para entidade reutilizada.
4. Criar player por definição + componentes Transform/Body/Visual/Health/Controlled. Aceite: nenhuma classe monolítica.
5. Implementar MovementSystem consumindo intenção. Aceite: velocidade diagonal é definida e independente do FPS.
6. Implementar Animator em ticks, loop e flip por facing. Aceite: down/up/left/right usam as linhas e flip corretos.
7. Separar posição lógica, anchor, draw offset e body. Aceite: walk/idle/attack não movem os pés nem a collision box.
8. Adicionar Y-sort por baseline com desempate estável. Aceite: player passa à frente/atrás de um objeto alto sem flicker.

Marco: player anda/colide/anima em quatro direções via command pipeline.

## Fase 5 — combate mínimo

1. Definir hurtbox, hitbox, interaction e debug draw distintos. Aceite: cores/boxes podem ser ativadas independentemente.
2. Fazer frame markers serem emitidos uma vez. Aceite: sequência controlada gera `attack_on/off` nos ticks esperados.
3. Criar SwordAttackSystem e attack instance ID. Aceite: um swing acerta cada alvo no máximo uma vez.
4. Aplicar dano, invulnerabilidade curta e knockback simples. Aceite: testes cobrem factions e repetição.
5. Criar projétil/flecha via marker, sem lógica no renderer. Aceite: spawn, movimento, impacto e expiração.
6. Criar VFX transitório/pool. Aceite: poeira/explosão terminam e se removem sem estado persistente.

Marco: espada e flecha têm boxes/eventos verificáveis no boneco de treino.

## Fase 6 — um inimigo e arquitetura reutilizável

1. Definir `EnemyDefinition`/factory e validar referências. Aceite: slime nasce apenas de dados + componentes.
2. Implementar estados reutilizáveis idle/wander/chase/attack/dead. Aceite: transições são observáveis em debug.
3. Adicionar percepção/distância e movimento respeitando collision. Aceite: inimigo persegue sem atravessar parede simples.
4. Integrar dano player↔enemy, morte e VFX. Aceite: ambos recebem dano segundo faction/cooldowns.
5. Adicionar segundo perfil (ranged ou soldier) sem novo framework paralelo. Aceite: reutiliza systems e troca perfil/ataques.

Marco: combate completo contra ao menos um inimigo, segundo perfil prova extensibilidade.

## Fase 7 — objetos, pickup, HUD e inventário pequeno

1. Criar definição/instância genérica de objeto com capacidades (pickup/container/destructible/etc.).
2. Implementar pickup de coração/dinheiro. Aceite: evento + remoção + alteração de estado uma vez.
3. Criar inventário mínimo por stable item IDs. Aceite: add/remove/capacity testados.
4. Criar `GameViewModel` somente leitura para HUD. Aceite: HUD não referencia componente Player diretamente.
5. Desenhar corações, dinheiro e item equipado. Aceite: cheio/meio/vazio para valores-limite.
6. Implementar baú/interação e destrutível com animação. Aceite: caixas de interação/colisão mudam no momento explícito.

Marco: pickup, baú, objeto quebrável e HUD funcionam desacoplados.

## Fase 8 — `.dmap`, transições e save

1. Congelar `.dmap` v1 após revisão do documento; criar byte reader/writer bounds-checked.
2. Implementar header/chunks/string table e round-trip de mapa vazio.
3. Implementar tile layers/collision RLE e validação adversarial.
4. Implementar entities/spawns/links com IDs estáveis.
5. Converter DTO validado em mapa runtime. Aceite: erros nunca deixam mundo parcial.
6. Implementar porta→spawn e carregamento/transição. Aceite: ida/volta entre duas salas preserva facing/posição definida.
7. Projetar formato de save separado e salvar player + deltas persistentes.
8. Escrita atômica/backup e recuperação de erro. Aceite: interrupção simulada não destrói último save válido.

Marco: vertical slice completo com duas salas, porta e baú persistente.

## Fase 9 — Map Maker

1. Criar `map_editor.exe` usando as bibliotecas existentes. Aceite: abre janela/viewport sem depender do game executable.
2. Criar `EditorDocument`, new/open/save/dirty e validação.
3. Implementar UI mínima própria: panels, buttons, list, numeric/text fields e focus.
4. Implementar pan/zoom inteiro e conversões window/viewport/world/tile.
5. Tile palette + paint/erase; depois rectangle/fill e multi-selection.
6. Layers: criar, ordenar, ocultar e bloquear.
7. Pintar/visualizar collision.
8. Colocar/selecionar/mover entidades e editar properties com schema da definição.
9. Colocar spawns, triggers, regions e doors; validar links.
10. Implementar command transactions, undo/redo, copy/paste com novos IDs.
11. Playtest in-memory e retorno ao mesmo documento.
12. Autosave/backup e relatório de erros.

Aceite do marco: criar mapa do zero, salvar, reabrir byte-equivalente semanticamente, editar com undo/redo e testar sem reiniciar editor.

## Fases 10–12 — conteúdo sistêmico, uma capacidade por vez

### Fase 10: NPC e diálogo

Definições externas de NPC; interação/facing; renderer de caixa de diálogo com paginação; choices/conditions/actions pequenas; NPC colocado pelo editor. Aceite: dois NPCs reutilizam o mesmo sistema e um diálogo condicionado por flag funciona após reload.

### Fase 11: quests

Event stream estável; formato versionado de quest; objectives declarativos (`talk`, `kill`, `pickup`, `enter`, `open`, `deliver`); quest state/save; ferramentas de validação no editor. Aceite: quest multiobjetivo progride por eventos, não por polling acoplado, e sobrevive ao save/load.

### Fase 12: RPG

Stats derivados; XP/level curve; equipment slots/modifiers; loot tables; UI/inventário expandido. Aceite: cálculo é testado e conteúdo novo entra por definições, sem alterar engine base.

## Fases 13–14 — preparação medida e networking

### Fase 13: auditoria multiplayer

Medir e documentar autoridade, state ownership e conteúdo replicável; remover fontes reais de nondeterminismo relevantes; separar servidor sem janela/render; gravar/reproduzir command streams; numerar snapshots e network entity IDs. Aceite: simulação headless executa cenário gravado e chega ao estado esperado.

### Fase 14: rede

Só então: adapter Winsock, framing/handshake/versionamento, conexão, command upload, servidor autoritativo, snapshots/deltas, prediction/reconciliation se necessário, interpolação remota, timeouts/rate limits e testes com latência/perda simuladas. Aceite inicial: dois clientes em LAN veem movimento autoritativo; combate vem depois em incrementos próprios.

## Portões de escopo

- Não iniciar editor antes de mapa/serialização/runtime serem utilizáveis.
- Não iniciar quests antes de eventos e IDs persistentes estarem comprovados.
- Não iniciar RPG antes do vertical slice estar divertido/estável.
- Não iniciar rede por abstrações hipotéticas: primeiro simulação headless e replay.
- Decoder PNG próprio é um projeto separado depois de WIC + testes; trocar via `IImageDecoder` deve não afetar gameplay/render.
