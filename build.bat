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

echo [1/24] Compiling framebuffer...
cl.exe %COMMON_FLAGS% /Fo"build\obj\framebuffer.obj" "src\engine\render\framebuffer.cpp"
if errorlevel 1 goto :build_failed

echo [2/24] Compiling image...
cl.exe %COMMON_FLAGS% /Fo"build\obj\image.obj" "src\engine\render\image.cpp"
if errorlevel 1 goto :build_failed

echo [3/24] Compiling Renderer2D...
cl.exe %COMMON_FLAGS% /Fo"build\obj\renderer_2d.obj" "src\engine\render\renderer_2d.cpp"
if errorlevel 1 goto :build_failed

echo [4/24] Compiling sprites...
cl.exe %COMMON_FLAGS% /Fo"build\obj\sprite.obj" "src\engine\render\sprite.cpp"
if errorlevel 1 goto :build_failed

echo [5/24] Compiling animation...
cl.exe %COMMON_FLAGS% /Fo"build\obj\animation.obj" "src\engine\render\animation.cpp"
if errorlevel 1 goto :build_failed

echo [6/24] Compiling bitmap font...
cl.exe %COMMON_FLAGS% /Fo"build\obj\bitmap_font.obj" "src\engine\render\bitmap_font.cpp"
if errorlevel 1 goto :build_failed

echo [7/24] Compiling camera...
cl.exe %COMMON_FLAGS% /Fo"build\obj\camera_2d.obj" "src\engine\render\camera_2d.cpp"
if errorlevel 1 goto :build_failed

echo [8/24] Compiling asset cache...
cl.exe %COMMON_FLAGS% /Fo"build\obj\asset_manager.obj" "src\engine\assets\asset_manager.cpp"
if errorlevel 1 goto :build_failed

echo [9/24] Compiling tile atlas data...
cl.exe %COMMON_FLAGS% /Fo"build\obj\tile.obj" "src\engine\world\tile.cpp"
if errorlevel 1 goto :build_failed

echo [10/24] Compiling tile layers...
cl.exe %COMMON_FLAGS% /Fo"build\obj\tile_layer.obj" "src\engine\world\tile_layer.cpp"
if errorlevel 1 goto :build_failed

echo [11/24] Compiling collision grid...
cl.exe %COMMON_FLAGS% /Fo"build\obj\collision_grid.obj" "src\engine\world\collision_grid.cpp"
if errorlevel 1 goto :build_failed

echo [12/24] Compiling tile collision...
cl.exe %COMMON_FLAGS% /Fo"build\obj\collision.obj" "src\engine\world\collision.cpp"
if errorlevel 1 goto :build_failed

echo [13/24] Compiling runtime map...
cl.exe %COMMON_FLAGS% /Fo"build\obj\runtime_map.obj" "src\engine\world\runtime_map.cpp"
if errorlevel 1 goto :build_failed

echo [14/24] Compiling player commands...
cl.exe %COMMON_FLAGS% /Fo"build\obj\command_builder.obj" "src\game\command_builder.cpp"
if errorlevel 1 goto :build_failed

echo [15/24] Compiling player gameplay...
cl.exe %COMMON_FLAGS% /Fo"build\obj\player.obj" "src\game\gameplay\player.cpp"
if errorlevel 1 goto :build_failed

echo [16/24] Compiling player visual...
cl.exe %COMMON_FLAGS% /Fo"build\obj\player_visual.obj" "src\game\player_visual.cpp"
if errorlevel 1 goto :build_failed

echo [17/24] Compiling Win32 clock...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_clock.obj" "src\engine\platform\win32\win32_clock.cpp"
if errorlevel 1 goto :build_failed

echo [18/24] Compiling WIC image decoder...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_image_decoder.obj" "src\engine\platform\win32\win32_image_decoder.cpp"
if errorlevel 1 goto :build_failed

echo [19/24] Compiling Phase 4 demo...
cl.exe %COMMON_FLAGS% /Fo"build\obj\phase4_demo.obj" "src\game\phase4_demo.cpp"
if errorlevel 1 goto :build_failed

echo [20/24] Compiling game loop...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game.obj" "src\game\game.cpp"
if errorlevel 1 goto :build_failed

echo [21/24] Compiling Win32 platform...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_platform.obj" "src\engine\platform\win32\win32_platform.cpp"
if errorlevel 1 goto :build_failed

echo [22/24] Linking game.exe...
link.exe /nologo /SUBSYSTEM:WINDOWS /OUT:"build\bin\game.exe" ^
    "build\obj\framebuffer.obj" "build\obj\image.obj" ^
    "build\obj\renderer_2d.obj" "build\obj\sprite.obj" ^
    "build\obj\animation.obj" "build\obj\bitmap_font.obj" ^
    "build\obj\camera_2d.obj" "build\obj\asset_manager.obj" ^
    "build\obj\tile.obj" "build\obj\tile_layer.obj" ^
    "build\obj\collision_grid.obj" "build\obj\collision.obj" ^
    "build\obj\runtime_map.obj" "build\obj\command_builder.obj" ^
    "build\obj\player.obj" "build\obj\player_visual.obj" "build\obj\win32_clock.obj" ^
    "build\obj\win32_image_decoder.obj" "build\obj\phase4_demo.obj" ^
    "build\obj\game.obj" "build\obj\win32_platform.obj" ^
    user32.lib gdi32.lib ole32.lib windowscodecs.lib
if errorlevel 1 goto :build_failed

echo [23/24] Compiling tests...
cl.exe %COMMON_FLAGS% /Fo"build\obj\tests.obj" "tests\test_main.cpp"
if errorlevel 1 goto :build_failed

echo [24/24] Linking tests.exe...
link.exe /nologo /SUBSYSTEM:CONSOLE /OUT:"build\bin\tests.exe" ^
    "build\obj\framebuffer.obj" "build\obj\image.obj" ^
    "build\obj\renderer_2d.obj" "build\obj\sprite.obj" ^
    "build\obj\animation.obj" "build\obj\bitmap_font.obj" ^
    "build\obj\camera_2d.obj" "build\obj\asset_manager.obj" ^
    "build\obj\tile.obj" "build\obj\tile_layer.obj" ^
    "build\obj\collision_grid.obj" "build\obj\collision.obj" ^
    "build\obj\runtime_map.obj" "build\obj\command_builder.obj" ^
    "build\obj\player.obj" "build\obj\player_visual.obj" "build\obj\win32_clock.obj" ^
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
