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

echo [1/15] Compiling framebuffer...
cl.exe %COMMON_FLAGS% /Fo"build\obj\framebuffer.obj" "src\engine\render\framebuffer.cpp"
if errorlevel 1 goto :build_failed

echo [2/15] Compiling image...
cl.exe %COMMON_FLAGS% /Fo"build\obj\image.obj" "src\engine\render\image.cpp"
if errorlevel 1 goto :build_failed

echo [3/15] Compiling Renderer2D...
cl.exe %COMMON_FLAGS% /Fo"build\obj\renderer_2d.obj" "src\engine\render\renderer_2d.cpp"
if errorlevel 1 goto :build_failed

echo [4/15] Compiling sprites...
cl.exe %COMMON_FLAGS% /Fo"build\obj\sprite.obj" "src\engine\render\sprite.cpp"
if errorlevel 1 goto :build_failed

echo [5/15] Compiling animation...
cl.exe %COMMON_FLAGS% /Fo"build\obj\animation.obj" "src\engine\render\animation.cpp"
if errorlevel 1 goto :build_failed

echo [6/15] Compiling bitmap font...
cl.exe %COMMON_FLAGS% /Fo"build\obj\bitmap_font.obj" "src\engine\render\bitmap_font.cpp"
if errorlevel 1 goto :build_failed

echo [7/15] Compiling asset cache...
cl.exe %COMMON_FLAGS% /Fo"build\obj\asset_manager.obj" "src\engine\assets\asset_manager.cpp"
if errorlevel 1 goto :build_failed

echo [8/15] Compiling Win32 clock...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_clock.obj" "src\engine\platform\win32\win32_clock.cpp"
if errorlevel 1 goto :build_failed

echo [9/15] Compiling WIC image decoder...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_image_decoder.obj" "src\engine\platform\win32\win32_image_decoder.cpp"
if errorlevel 1 goto :build_failed

echo [10/15] Compiling Phase 2 demo...
cl.exe %COMMON_FLAGS% /Fo"build\obj\phase2_demo.obj" "src\game\phase2_demo.cpp"
if errorlevel 1 goto :build_failed

echo [11/15] Compiling game loop...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game.obj" "src\game\game.cpp"
if errorlevel 1 goto :build_failed

echo [12/15] Compiling Win32 platform...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_platform.obj" "src\engine\platform\win32\win32_platform.cpp"
if errorlevel 1 goto :build_failed

echo [13/15] Linking game.exe...
link.exe /nologo /SUBSYSTEM:WINDOWS /OUT:"build\bin\game.exe" ^
    "build\obj\framebuffer.obj" "build\obj\image.obj" ^
    "build\obj\renderer_2d.obj" "build\obj\sprite.obj" ^
    "build\obj\animation.obj" "build\obj\bitmap_font.obj" ^
    "build\obj\asset_manager.obj" "build\obj\win32_clock.obj" ^
    "build\obj\win32_image_decoder.obj" "build\obj\phase2_demo.obj" ^
    "build\obj\game.obj" "build\obj\win32_platform.obj" ^
    user32.lib gdi32.lib ole32.lib windowscodecs.lib
if errorlevel 1 goto :build_failed

echo [14/15] Compiling tests...
cl.exe %COMMON_FLAGS% /Fo"build\obj\tests.obj" "tests\test_main.cpp"
if errorlevel 1 goto :build_failed

echo [15/15] Linking tests.exe...
link.exe /nologo /SUBSYSTEM:CONSOLE /OUT:"build\bin\tests.exe" ^
    "build\obj\framebuffer.obj" "build\obj\image.obj" ^
    "build\obj\renderer_2d.obj" "build\obj\sprite.obj" ^
    "build\obj\animation.obj" "build\obj\bitmap_font.obj" ^
    "build\obj\asset_manager.obj" "build\obj\win32_clock.obj" ^
    "build\obj\win32_image_decoder.obj" "build\obj\tests.obj" ^
    ole32.lib windowscodecs.lib
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
