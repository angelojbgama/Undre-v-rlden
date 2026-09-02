@echo off
setlocal
pushd "%~dp0"

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: cl.exe was not found.
    echo Run this script from an "x64 Native Tools Command Prompt for VS" or
    echo a Visual Studio Developer Command Prompt configured for x64.
    popd
    exit /b 1
)

if defined VSCMD_ARG_TGT_ARCH (
    if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
        echo ERROR: MSVC is configured for "%VSCMD_ARG_TGT_ARCH%", not x64.
        echo Open an x64 Native Tools Command Prompt and run build.bat again.
        popd
        exit /b 1
    )
)

if not exist "build\obj" mkdir "build\obj"
if not exist "build\bin" mkdir "build\bin"
del /q "build\obj\*.obj" >nul 2>nul

set "COMMON_FLAGS=/nologo /std:c++20 /W4 /permissive- /EHsc /Zc:__cplusplus /utf-8 /I src /c"

echo [1/7] Compiling framebuffer...
cl.exe %COMMON_FLAGS% /Fo"build\obj\framebuffer.obj" "src\engine\render\framebuffer.cpp"
if errorlevel 1 goto :build_failed

echo [2/7] Compiling Win32 clock...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_clock.obj" "src\engine\platform\win32\win32_clock.cpp"
if errorlevel 1 goto :build_failed

echo [3/7] Compiling game loop...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game.obj" "src\game\game.cpp"
if errorlevel 1 goto :build_failed

echo [4/7] Compiling Win32 platform...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_platform.obj" "src\engine\platform\win32\win32_platform.cpp"
if errorlevel 1 goto :build_failed

echo [5/7] Linking game.exe...
link.exe /nologo /SUBSYSTEM:WINDOWS /OUT:"build\bin\game.exe" ^
    "build\obj\framebuffer.obj" ^
    "build\obj\win32_clock.obj" ^
    "build\obj\game.obj" ^
    "build\obj\win32_platform.obj" ^
    user32.lib gdi32.lib
if errorlevel 1 goto :build_failed

echo [6/7] Compiling tests...
cl.exe %COMMON_FLAGS% /Fo"build\obj\tests.obj" "tests\test_main.cpp"
if errorlevel 1 goto :build_failed

echo [7/7] Linking tests.exe...
link.exe /nologo /SUBSYSTEM:CONSOLE /OUT:"build\bin\tests.exe" ^
    "build\obj\framebuffer.obj" ^
    "build\obj\win32_clock.obj" ^
    "build\obj\tests.obj"
if errorlevel 1 goto :build_failed

echo.
echo Build succeeded:
echo   build\bin\game.exe
echo   build\bin\tests.exe
popd
exit /b 0

:build_failed
echo.
echo ERROR: build failed. See the compiler or linker diagnostics above.
popd
exit /b 1
