# Linux build incremental

O build Linux usa Docker para fornecer o compilador e `make` para manter objetos
portáveis em `build/linux/obj`. Cada compilação gera também um arquivo `.d` com
`-MMD -MP`; mudanças em headers invalidam somente as translation units dependentes.
Os objetos comuns são compilados uma vez e compartilhados por `tests`,
`playtest_runner` e `game`.

```bash
./docker/build_linux.sh build
./docker/build_linux.sh tests
./docker/build_linux.sh playtest melee_combat
./docker/build_linux.sh game
./docker/build_linux.sh clean
```

Para ciclos locais, a imagem já construída pode ser reutilizada:

```bash
UNDERWORLD_SKIP_IMAGE_BUILD=1 ./docker/build_linux.sh tests
```

O modo padrão `all` compila os três targets, executa os testes e roda todos os
playtests. Falhas históricas de pickups continuam sendo reportadas pelo runner.
O agendamento usa `nproc` (ou `UNDERWORLD_BUILD_JOBS`) e mantém `-Werror` ativo.
`ccache` não é necessário para a reutilização primária: objetos e dependências
persistem no bind mount do repositório.
