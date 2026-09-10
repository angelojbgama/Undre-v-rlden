@echo off
setlocal
pushd "%~dp0"
set "STUDIO_PYTHON=python"
if exist "%~dp0.venv\Scripts\python.exe" set "STUDIO_PYTHON=%~dp0.venv\Scripts\python.exe"
"%STUDIO_PYTHON%" -c "import PySide6" >nul 2>&1
if errorlevel 1 (
    echo Dungeon Underworld Content Studio requires PySide6.
    echo Set up the authoring environment with:
    echo   py -3.11 -m venv .venv
    echo   .venv\Scripts\python.exe -m pip install -e .
    echo Then run content_studio.bat again.
    popd
    exit /b 2
)
rem The repository-local assets directory is the default root. An explicit
rem --asset-root supplied by the caller appears later and overrides it.
"%STUDIO_PYTHON%" -m tools.content_studio --asset-root "%~dp0assets" %*
set "EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %EXIT_CODE%
