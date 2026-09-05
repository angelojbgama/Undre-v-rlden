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

echo [1/40] Compiling framebuffer...
cl.exe %COMMON_FLAGS% /Fo"build\obj\framebuffer.obj" "src\engine\render\framebuffer.cpp"
if errorlevel 1 goto :build_failed

echo [2/40] Compiling image...
cl.exe %COMMON_FLAGS% /Fo"build\obj\image.obj" "src\engine\render\image.cpp"
if errorlevel 1 goto :build_failed

echo [3/40] Compiling Renderer2D...
cl.exe %COMMON_FLAGS% /Fo"build\obj\renderer_2d.obj" "src\engine\render\renderer_2d.cpp"
if errorlevel 1 goto :build_failed

echo [4/40] Compiling sprites...
cl.exe %COMMON_FLAGS% /Fo"build\obj\sprite.obj" "src\engine\render\sprite.cpp"
if errorlevel 1 goto :build_failed

echo [5/40] Compiling animation...
cl.exe %COMMON_FLAGS% /Fo"build\obj\animation.obj" "src\engine\render\animation.cpp"
if errorlevel 1 goto :build_failed

echo [6/40] Compiling bitmap font...
cl.exe %COMMON_FLAGS% /Fo"build\obj\bitmap_font.obj" "src\engine\render\bitmap_font.cpp"
if errorlevel 1 goto :build_failed

echo [7/40] Compiling camera...
cl.exe %COMMON_FLAGS% /Fo"build\obj\camera_2d.obj" "src\engine\render\camera_2d.cpp"
if errorlevel 1 goto :build_failed

echo [8/40] Compiling asset cache...
cl.exe %COMMON_FLAGS% /Fo"build\obj\asset_manager.obj" "src\engine\assets\asset_manager.cpp"
if errorlevel 1 goto :build_failed

echo [9/40] Compiling tile atlas data...
cl.exe %COMMON_FLAGS% /Fo"build\obj\tile.obj" "src\engine\world\tile.cpp"
if errorlevel 1 goto :build_failed

echo [10/40] Compiling tile layers...
cl.exe %COMMON_FLAGS% /Fo"build\obj\tile_layer.obj" "src\engine\world\tile_layer.cpp"
if errorlevel 1 goto :build_failed

echo [11/40] Compiling collision grid...
cl.exe %COMMON_FLAGS% /Fo"build\obj\collision_grid.obj" "src\engine\world\collision_grid.cpp"
if errorlevel 1 goto :build_failed

echo [12/40] Compiling tile collision...
cl.exe %COMMON_FLAGS% /Fo"build\obj\collision.obj" "src\engine\world\collision.cpp"
if errorlevel 1 goto :build_failed

echo [13/40] Compiling runtime map...
cl.exe %COMMON_FLAGS% /Fo"build\obj\runtime_map.obj" "src\engine\world\runtime_map.cpp"
if errorlevel 1 goto :build_failed

echo [14/40] Compiling entity handles...
cl.exe %COMMON_FLAGS% /Fo"build\obj\entity_handle.obj" "src\engine\simulation\entity_handle.cpp"
if errorlevel 1 goto :build_failed

echo Compiling binary serialization...
cl.exe %COMMON_FLAGS% /Fo"build\obj\byte_io.obj" "src\engine\serialization\byte_io.cpp"
if errorlevel 1 goto :build_failed

echo Compiling persistent map data...
cl.exe %COMMON_FLAGS% /Fo"build\obj\map_data.obj" "src\game\maps\map_data.cpp"
if errorlevel 1 goto :build_failed

echo Compiling DMAP serialization...
cl.exe %COMMON_FLAGS% /Fo"build\obj\dmap.obj" "src\game\maps\dmap.cpp"
if errorlevel 1 goto :build_failed

echo Compiling runtime world builder...
cl.exe %COMMON_FLAGS% /Fo"build\obj\runtime_world.obj" "src\game\maps\runtime_world.cpp"
if errorlevel 1 goto :build_failed

echo Compiling save data...
cl.exe %COMMON_FLAGS% /Fo"build\obj\save_data.obj" "src\game\save\save_data.cpp"
if errorlevel 1 goto :build_failed

echo Compiling map catalog and transitions...
cl.exe %COMMON_FLAGS% /Fo"build\obj\map_catalog.obj" "src\game\maps\map_catalog.cpp"
if errorlevel 1 goto :build_failed

echo Compiling official gameplay map manifest...
cl.exe %COMMON_FLAGS% /Fo"build\obj\official_maps.obj" "src\game\maps\official_maps.cpp"
if errorlevel 1 goto :build_failed

echo Compiling shared game content...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game_content.obj" "src\game\game_content.cpp"
if errorlevel 1 goto :build_failed

echo Compiling shared tileset content...
cl.exe %COMMON_FLAGS% /Fo"build\obj\tilesets.obj" "src\game\tilesets.cpp"
if errorlevel 1 goto :build_failed

echo Compiling authoring semantics...
cl.exe %COMMON_FLAGS% /Fo"build\obj\authoring_semantics.obj" "src\game\authoring\authoring_semantics.cpp"
if errorlevel 1 goto :build_failed

echo Compiling map composition...
cl.exe %COMMON_FLAGS% /Fo"build\obj\map_composition.obj" "src\game\maps\map_composition.cpp"
if errorlevel 1 goto :build_failed

echo Compiling reachability validation...
cl.exe %COMMON_FLAGS% /Fo"build\obj\reachability.obj" "src\game\maps\reachability.cpp"
if errorlevel 1 goto :build_failed

echo Compiling map editor document...
cl.exe %COMMON_FLAGS% /Fo"build\obj\editor_document.obj" "src\editor\editor_document.cpp"
if errorlevel 1 goto :build_failed

echo Compiling map editor commands...
cl.exe %COMMON_FLAGS% /Fo"build\obj\editor_commands.obj" "src\editor\editor_commands.cpp"
if errorlevel 1 goto :build_failed

echo Compiling map editor UI...
cl.exe %COMMON_FLAGS% /Fo"build\obj\editor_ui.obj" "src\editor\editor_ui.cpp"
if errorlevel 1 goto :build_failed

echo Compiling map editor application...
cl.exe %COMMON_FLAGS% /Fo"build\obj\editor_app.obj" "src\editor\editor_app.cpp"
if errorlevel 1 goto :build_failed

echo Compiling map editor Win32 shell...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_editor.obj" "src\editor\win32_editor.cpp"
if errorlevel 1 goto :build_failed

echo Compiling editor playtest...
cl.exe %COMMON_FLAGS% /Fo"build\obj\editor_playtest.obj" "src\editor\editor_playtest.cpp"
if errorlevel 1 goto :build_failed

echo [15/40] Compiling combat data...
cl.exe %COMMON_FLAGS% /Fo"build\obj\combat_types.obj" "src\game\gameplay\combat_types.cpp"
if errorlevel 1 goto :build_failed

echo [16/40] Compiling attack definitions...
cl.exe %COMMON_FLAGS% /Fo"build\obj\attack_definitions.obj" "src\game\gameplay\attack_definitions.cpp"
if errorlevel 1 goto :build_failed

echo [17/40] Compiling combat system...
cl.exe %COMMON_FLAGS% /Fo"build\obj\combat_system.obj" "src\game\gameplay\combat_system.cpp"
if errorlevel 1 goto :build_failed

echo [18/40] Compiling projectiles...
cl.exe %COMMON_FLAGS% /Fo"build\obj\projectile_system.obj" "src\game\gameplay\projectile_system.cpp"
if errorlevel 1 goto :build_failed

echo [19/40] Compiling items...
cl.exe %COMMON_FLAGS% /Fo"build\obj\items.obj" "src\game\gameplay\items.cpp"
if errorlevel 1 goto :build_failed

echo [20/40] Compiling player items...
cl.exe %COMMON_FLAGS% /Fo"build\obj\player_items.obj" "src\game\gameplay\player_items.cpp"
if errorlevel 1 goto :build_failed

echo [21/40] Compiling world pickups...
cl.exe %COMMON_FLAGS% /Fo"build\obj\world_pickups.obj" "src\game\gameplay\world_pickups.cpp"
if errorlevel 1 goto :build_failed

echo [22/40] Compiling world objects...
cl.exe %COMMON_FLAGS% /Fo"build\obj\world_objects.obj" "src\game\gameplay\world_objects.cpp"
if errorlevel 1 goto :build_failed

echo Compiling NPC engine...
cl.exe %COMMON_FLAGS% /Fo"build\obj\npc_engine.obj" "src\game\gameplay\npcs\npc_engine.cpp"
if errorlevel 1 goto :build_failed

echo Compiling dialogue data model...
cl.exe %COMMON_FLAGS% /Fo"build\obj\dialogue_model.obj" "src\game\gameplay\dialogue\dialogue_model.cpp"
if errorlevel 1 goto :build_failed

echo Compiling dialogue session...
cl.exe %COMMON_FLAGS% /Fo"build\obj\dialogue_session.obj" "src\game\gameplay\dialogue\dialogue_session.cpp"
if errorlevel 1 goto :build_failed

echo Compiling quest definitions...
cl.exe %COMMON_FLAGS% /Fo"build\obj\quest_model.obj" "src\game\gameplay\quests\quest_model.cpp"
if errorlevel 1 goto :build_failed

echo Compiling quest state...
cl.exe %COMMON_FLAGS% /Fo"build\obj\quest_state.obj" "src\game\gameplay\quests\quest_state.cpp"
if errorlevel 1 goto :build_failed

echo Compiling quest event system...
cl.exe %COMMON_FLAGS% /Fo"build\obj\quest_system.obj" "src\game\gameplay\quests\quest_system.cpp"
if errorlevel 1 goto :build_failed

echo [23/40] Compiling game view model...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game_view_model.obj" "src\game\game_view_model.cpp"
if errorlevel 1 goto :build_failed

echo [24/40] Compiling world object visuals...
cl.exe %COMMON_FLAGS% /Fo"build\obj\world_object_visual.obj" "src\game\world_object_visual.cpp"
if errorlevel 1 goto :build_failed

echo [25/40] Compiling creature engine...
cl.exe %COMMON_FLAGS% /Fo"build\obj\creature_engine.obj" "src\game\gameplay\creatures\creature_engine.cpp"
if errorlevel 1 goto :build_failed

echo [26/40] Compiling enemy visuals...
cl.exe %COMMON_FLAGS% /Fo"build\obj\enemy_visual.obj" "src\game\enemy_visual.cpp"
if errorlevel 1 goto :build_failed

echo [27/40] Compiling training puppet...
cl.exe %COMMON_FLAGS% /Fo"build\obj\training_puppet.obj" "src\game\training_puppet.cpp"
if errorlevel 1 goto :build_failed

echo [28/40] Compiling effects...
cl.exe %COMMON_FLAGS% /Fo"build\obj\effect_system.obj" "src\game\effect_system.cpp"
if errorlevel 1 goto :build_failed

echo [29/40] Compiling player commands...
cl.exe %COMMON_FLAGS% /Fo"build\obj\command_builder.obj" "src\game\command_builder.cpp"
if errorlevel 1 goto :build_failed

echo [30/40] Compiling player gameplay...
cl.exe %COMMON_FLAGS% /Fo"build\obj\player.obj" "src\game\gameplay\player.cpp"
if errorlevel 1 goto :build_failed

echo [31/40] Compiling player visual...
cl.exe %COMMON_FLAGS% /Fo"build\obj\player_visual.obj" "src\game\player_visual.cpp"
if errorlevel 1 goto :build_failed

echo [32/40] Compiling Win32 clock...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_clock.obj" "src\engine\platform\win32\win32_clock.cpp"
if errorlevel 1 goto :build_failed

echo [33/40] Compiling WIC image decoder...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_image_decoder.obj" "src\engine\platform\win32\win32_image_decoder.cpp"
if errorlevel 1 goto :build_failed

echo [34/40] Compiling Phase 7 demo composition...
cl.exe %COMMON_FLAGS% /Fo"build\obj\phase5_demo.obj" "src\game\phase5_demo.cpp"
if errorlevel 1 goto :build_failed

echo Compiling game launch options...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game_launch.obj" "src\game\game_launch.cpp"
if errorlevel 1 goto :build_failed

echo Compiling runtime visual synchronization...
cl.exe %COMMON_FLAGS% /Fo"build\obj\runtime_visual_sync.obj" "src\game\runtime_visual_sync.cpp"
if errorlevel 1 goto :build_failed

echo Compiling audit snapshot...
cl.exe %COMMON_FLAGS% /Fo"build\obj\audit_snapshot.obj" "src\game\audit\audit_snapshot.cpp"
if errorlevel 1 goto :build_failed

echo Compiling audit session...
cl.exe %COMMON_FLAGS% /Fo"build\obj\audit_session.obj" "src\game\audit\audit_session.cpp"
if errorlevel 1 goto :build_failed

echo [35/40] Compiling game loop...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game.obj" "src\game\game.cpp"
if errorlevel 1 goto :build_failed

echo [36/40] Compiling Win32 platform...
cl.exe %COMMON_FLAGS% /Fo"build\obj\win32_platform.obj" "src\engine\platform\win32\win32_platform.cpp"
if errorlevel 1 goto :build_failed

echo Linking map_editor.exe...
link.exe /nologo /SUBSYSTEM:WINDOWS /OUT:"build\bin\map_editor.exe" ^
    "build\obj\framebuffer.obj" "build\obj\image.obj" "build\obj\renderer_2d.obj" ^
    "build\obj\bitmap_font.obj" "build\obj\asset_manager.obj" ^
    "build\obj\tile.obj" "build\obj\tile_layer.obj" "build\obj\collision_grid.obj" "build\obj\runtime_map.obj" ^
    "build\obj\collision.obj" "build\obj\entity_handle.obj" "build\obj\byte_io.obj" ^
    "build\obj\map_data.obj" "build\obj\dmap.obj" "build\obj\runtime_world.obj" "build\obj\game_content.obj" "build\obj\game_launch.obj" "build\obj\official_maps.obj" "build\obj\tilesets.obj" "build\obj\authoring_semantics.obj" "build\obj\map_composition.obj" ^
    "build\obj\combat_types.obj" "build\obj\attack_definitions.obj" ^
    "build\obj\combat_system.obj" "build\obj\projectile_system.obj" ^
    "build\obj\items.obj" "build\obj\world_pickups.obj" ^
    "build\obj\world_objects.obj" "build\obj\npc_engine.obj" "build\obj\dialogue_model.obj" "build\obj\dialogue_session.obj" "build\obj\quest_model.obj" "build\obj\quest_state.obj" "build\obj\quest_system.obj" "build\obj\creature_engine.obj" ^
    "build\obj\audit_snapshot.obj" "build\obj\audit_session.obj" ^
    "build\obj\win32_image_decoder.obj" "build\obj\editor_document.obj" ^
    "build\obj\editor_commands.obj" "build\obj\editor_ui.obj" ^
    "build\obj\editor_app.obj" "build\obj\editor_playtest.obj" "build\obj\win32_editor.obj" ^
    user32.lib gdi32.lib ole32.lib windowscodecs.lib comdlg32.lib
if errorlevel 1 goto :build_failed

echo [37/40] Linking game.exe...
link.exe /nologo /SUBSYSTEM:WINDOWS /OUT:"build\bin\game.exe" ^
    "build\obj\framebuffer.obj" "build\obj\image.obj" ^
    "build\obj\renderer_2d.obj" "build\obj\sprite.obj" ^
    "build\obj\animation.obj" "build\obj\bitmap_font.obj" ^
    "build\obj\camera_2d.obj" "build\obj\asset_manager.obj" ^
    "build\obj\tile.obj" "build\obj\tile_layer.obj" ^
    "build\obj\collision_grid.obj" "build\obj\collision.obj" ^
    "build\obj\runtime_map.obj" "build\obj\entity_handle.obj" ^
    "build\obj\byte_io.obj" "build\obj\map_data.obj" "build\obj\dmap.obj" "build\obj\game_launch.obj" ^
    "build\obj\runtime_world.obj" ^
    "build\obj\save_data.obj" "build\obj\map_catalog.obj" "build\obj\official_maps.obj" ^
    "build\obj\game_content.obj" "build\obj\tilesets.obj" "build\obj\authoring_semantics.obj" ^
    "build\obj\map_composition.obj" "build\obj\reachability.obj" ^
    "build\obj\combat_types.obj" "build\obj\attack_definitions.obj" ^
    "build\obj\combat_system.obj" "build\obj\projectile_system.obj" ^
    "build\obj\items.obj" "build\obj\player_items.obj" ^
    "build\obj\world_pickups.obj" "build\obj\world_objects.obj" "build\obj\npc_engine.obj" "build\obj\dialogue_model.obj" "build\obj\dialogue_session.obj" "build\obj\quest_model.obj" "build\obj\quest_state.obj" "build\obj\quest_system.obj" ^
    "build\obj\game_view_model.obj" "build\obj\world_object_visual.obj" "build\obj\runtime_visual_sync.obj" ^
    "build\obj\audit_snapshot.obj" "build\obj\audit_session.obj" ^
    "build\obj\creature_engine.obj" "build\obj\enemy_visual.obj" ^
    "build\obj\effect_system.obj" ^
    "build\obj\command_builder.obj" ^
    "build\obj\player.obj" "build\obj\player_visual.obj" ^
    "build\obj\win32_clock.obj" "build\obj\win32_image_decoder.obj" ^
    "build\obj\phase5_demo.obj" "build\obj\game.obj" ^
    "build\obj\win32_platform.obj" ^
    user32.lib gdi32.lib ole32.lib windowscodecs.lib shell32.lib
if errorlevel 1 goto :build_failed

echo [39/40] Compiling tests...
cl.exe %COMMON_FLAGS% /Fo"build\obj\tests.obj" "tests\test_main.cpp"
if errorlevel 1 goto :build_failed

echo [40/40] Linking tests.exe...
link.exe /nologo /SUBSYSTEM:CONSOLE /OUT:"build\bin\tests.exe" ^
    "build\obj\framebuffer.obj" "build\obj\image.obj" ^
    "build\obj\renderer_2d.obj" "build\obj\sprite.obj" ^
    "build\obj\animation.obj" "build\obj\bitmap_font.obj" ^
    "build\obj\camera_2d.obj" "build\obj\asset_manager.obj" ^
    "build\obj\tile.obj" "build\obj\tile_layer.obj" ^
    "build\obj\collision_grid.obj" "build\obj\collision.obj" ^
    "build\obj\runtime_map.obj" "build\obj\entity_handle.obj" ^
    "build\obj\byte_io.obj" "build\obj\map_data.obj" "build\obj\dmap.obj" "build\obj\game_launch.obj" ^
    "build\obj\runtime_world.obj" ^
    "build\obj\save_data.obj" "build\obj\map_catalog.obj" "build\obj\official_maps.obj" ^
    "build\obj\game_content.obj" "build\obj\tilesets.obj" "build\obj\authoring_semantics.obj" ^
    "build\obj\map_composition.obj" "build\obj\reachability.obj" ^
    "build\obj\editor_document.obj" "build\obj\editor_commands.obj" "build\obj\editor_playtest.obj" ^
    "build\obj\combat_types.obj" "build\obj\attack_definitions.obj" ^
    "build\obj\combat_system.obj" "build\obj\projectile_system.obj" ^
    "build\obj\items.obj" "build\obj\player_items.obj" ^
    "build\obj\world_pickups.obj" "build\obj\world_objects.obj" "build\obj\npc_engine.obj" "build\obj\dialogue_model.obj" "build\obj\dialogue_session.obj" "build\obj\quest_model.obj" "build\obj\quest_state.obj" "build\obj\quest_system.obj" ^
    "build\obj\game_view_model.obj" "build\obj\world_object_visual.obj" "build\obj\runtime_visual_sync.obj" ^
    "build\obj\audit_snapshot.obj" "build\obj\audit_session.obj" ^
    "build\obj\creature_engine.obj" "build\obj\enemy_visual.obj" ^
    "build\obj\training_puppet.obj" "build\obj\effect_system.obj" ^
    "build\obj\command_builder.obj" ^
    "build\obj\player.obj" "build\obj\player_visual.obj" ^
    "build\obj\win32_clock.obj" "build\obj\win32_image_decoder.obj" ^
    "build\obj\tests.obj" ^
    ole32.lib windowscodecs.lib
if errorlevel 1 goto :build_failed

echo.
echo Build succeeded:
echo   build\bin\game.exe
echo   build\bin\map_editor.exe
echo   build\bin\tests.exe

popd
exit /b 0

:build_failed

echo.
echo ERROR: build failed. See the compiler or linker diagnostics above.

popd
exit /b 1
