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
B — captura do framebuffer lógico em BMP                 DEFERRED
C — HeadlessAuditPlatform                                DEFERRED
D — playtest runner/scripted input                       DEFERRED
E — integração de auditoria manual no Windows/F12       DEFERRED
F — build portátil Linux                                 DEFERRED
G — plataforma gráfica Linux                             DEFERRED
H — seeded stress playtest                              DEFERRED
```

Esta é uma antecipação pequena da futura trilha headless/replay. Formal replay,
hash de estado, IDs de rede, determinismo completo e multiplayer continuam fora do
escopo. O Block A é exercitado por testes portáveis; a integração no loop de execução
e a captura manual serão adicionadas somente nos blocos correspondentes.
