# Docker builds

O Docker não substitui `build.bat` nem muda o pipeline MSVC. Ele apenas fornece
ambientes reproduzíveis para os dois hosts:

## Linux portátil

Em Linux, WSL ou Docker Desktop em modo Linux:

```bash
./docker/build_linux.sh
```

O container gera `build/linux/tests`, `build/linux/playtest_runner` e
`build/linux/game`, executa os testes e o runner e instala as dependências de sistema
X11/libpng necessárias ao runtime Linux. O jogo Linux usa uma janela X11; para
executá-lo graficamente, o host precisa expor um display (por exemplo WSLg) e os
assets licenciados devem ser fornecidos via `--asset-root`. Sem assets locais, o
runner continua usando seu decoder sintético de testes.

## Windows/MSVC

O Dockerfile Windows precisa ser construído em um host Windows com Docker Desktop
em Windows container mode. Ele instala o workload oficial de C++ Build Tools da
Microsoft, monta o repositório e executa o `build.bat` existente:

```powershell
.\docker\build_windows.ps1
```

Um daemon Docker Linux não pode executar essa imagem. O build Windows continua
dependendo de MSVC x64, Windows SDK e da licença/compatibilidade do Build Tools.
O `build.bat` segue sendo a fonte de verdade e os assets licenciados locais nunca
são copiados para a imagem nem versionados.

Os artefatos `build/` e `audit/` continuam ignorados pelo Git.
