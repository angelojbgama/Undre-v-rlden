# Audit e playtest

## Estado atual

O Block A da trilha antecipada de observabilidade está implementado. Ele fornece
uma sessão de auditoria neutra em relação à plataforma e um snapshot somente-leitura
do estado do jogo:

```text
Game / gameplay state
        ↓ auditSnapshot()
GameAuditSnapshot
        ↓
AuditSession
        ↓
session.json
events.jsonl
state.jsonl
summary.txt
screenshots/
```

`AuditSession` não possui autoridade sobre gameplay e nenhum sistema de gameplay
abre arquivos diretamente. IDs de definição e IDs persistentes são serializados;
handles runtime e ponteiros não fazem parte do snapshot.

O Block B adiciona `writeBmp32`, que lê diretamente um `PixelBufferView` RGBA8. O
arquivo é BMP 32-bit bottom-up, com dimensões e pixels do framebuffer lógico; não há
captura da janela ou do desktop. `AuditSession::captureScreenshot` restringe o nome
ao diretório `screenshots/` da sessão e atualiza o contador do resumo.

O Block C adiciona `HeadlessAuditPlatform`, uma implementação do contrato real de
`Platform` sem janela, Win32, X11 ou outro backend gráfico. Ela oferece relógio
controlável, input lógico agendado por tick, `DebugInputState`, recepção e cópia do
último framebuffer apresentado, decoder injetado e logs operacionais observáveis.
O tempo só avança quando o chamador solicita; não há `sleep` nem agendamento de wall
clock dentro da plataforma.

Uma sessão habilitada usa um identificador explícito seguro ou gera um timestamp UTC.
O diretório padrão é `audit/<session-id>/`, que é somente artefato de desenvolvimento
e está no `.gitignore`. A sessão cria `screenshots/` desde já para manter o contrato
de diretórios; a escrita de BMP pertence ao Block B.

Eventos usam JSON Lines com escape próprio para strings, e checkpoints de estado são
gravados no primeiro estado, a cada 60 ticks por padrão ou quando o chamador força um
checkpoint. O `GameAuditSnapshot` atual cobre mapa/spawn, player, vida, wallet,
inventário, quick slots, inimigos, NPCs, objetos, pickups, diálogo, quests, flags e
projéteis ativos.

## Escopo por bloco

```text
A — AuditSession, eventos estruturados e snapshot       DONE
B — captura do framebuffer lógico em BMP                 DONE
C — HeadlessAuditPlatform                                DONE
D — playtest runner/scripted input                       DONE
E — integração de auditoria manual no Windows/F12       DONE (não executado neste host)
F — build portátil Linux                                 PREPARAÇÃO Docker DONE
G — plataforma gráfica Linux                             DEFERRED
H — seeded stress playtest                              DEFERRED
```

Esta é uma antecipação pequena da futura trilha headless/replay. Formal replay,
hash de estado, IDs de rede, determinismo completo e multiplayer continuam fora do
escopo. Blocks A–D são exercitados no Linux por testes portáveis e pelo runner. O
Block E integra a mesma sessão ao loop Win32: `game.exe --audit` registra o startup,
mudanças observáveis do snapshot e checkpoints importantes. F12 é mapeado na borda
Win32 para uma captura do framebuffer lógico e do snapshot no mesmo tick. A execução
Windows não foi possível neste host Linux/WSL.

## Block D — cenários automatizados

O `playtest_runner` instancia o `Phase7Demo` real com `HeadlessAuditPlatform`, injeta
somente `InputState` por tick e valida o resultado por `GameAuditSnapshot`. Cada
cenário escreve uma sessão independente, com screenshots de startup, checkpoints
relevantes e conclusão/falha. O runner retorna código diferente de zero para qualquer
assertion ou erro e captura estado/framebuffer da falha.

Ele aceita `--all`, `--scenario`, `--seed`, `--ticks`, `--audit-root` e
`--asset-root`/`UNDERWORLD_ASSET_ROOT`. Sem assets licenciados e decoder portátil,
os testes Linux usam um decoder sintético injetado apenas para exercitar o pipeline
real de runtime/renderização; a aparência dos assets locais continua sendo um gate
manual do Windows. Os nomes de quest permanecem registrados na matriz como smoke de
startup/update/render, pois ainda não existe um comando de jogador para iniciar uma
quest sem fabricar estado no harness.

## Block E — auditoria manual

`GameLaunchOptions` aceita `--audit`. Quando habilitado no `game.exe`, um observador
neutro acompanha o `GameAuditSnapshot` depois da apresentação do framebuffer real.
Ele registra transições de mapa, dano/cura/morte, derrotas, objetos, pickups,
diálogo e save/load quando essas mudanças aparecem no snapshot. Eventos e estados
importantes geram checkpoints e BMPs; a captura manual usa `F12` e o nome
`manual_tick_<tick>.bmp`. O observador não altera gameplay nem abre arquivos a
partir dos sistemas de domínio.

## Docker e Linux portátil

`docker/build_linux.sh` constrói uma imagem Debian mínima e executa os alvos
portáteis `build/linux/tests` e `build/linux/playtest_runner --all`. O container
não compila Win32, não inclui assets licenciados e não fornece janela gráfica. O
Dockerfile Windows (`docker/Dockerfile.windows`) é uma receita para Docker Desktop
em modo Windows containers num host Windows; um daemon Linux não pode executar
MSVC/Windows containers. A plataforma gráfica Linux permanece Block G.
