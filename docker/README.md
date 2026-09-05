# Docker builds

O Docker não substitui `build.bat` nem muda o pipeline MSVC. Ele apenas fornece
ambientes reproduzíveis para os dois hosts:

## Linux portátil

Em Linux, WSL ou Docker Desktop em modo Linux:

```bash
./docker/build_linux.sh
```

O container gera `build/linux/tests` e `build/linux/playtest_runner`, executa ambos
e usa o decoder sintético do runner quando os assets licenciados não estão montados.
O build Linux não produz janela gráfica nem usa Win32/WIC.

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
