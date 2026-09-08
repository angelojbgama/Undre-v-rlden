@echo off
setlocal
pushd "%~dp0"

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: cl.exe was not found.
    echo Run this script from an x64 Visual Studio Developer Command Prompt.
    popd
    exit /b 1
)

if not exist "build\obj\game.obj" (
    echo ERROR: incremental objects are missing.
    echo Run build.bat once before using build_game.bat.
    popd
    exit /b 1
)

set "COMMON_FLAGS=/nologo /std:c++20 /W4 /WX /permissive- /EHsc /Zc:__cplusplus /utf-8 /I src /c"

echo Compiling UTF-8 core utilities...
cl.exe %COMMON_FLAGS% /Fo"build\obj\utf8.obj" "src\engine\core\utf8.cpp"
if errorlevel 1 goto :build_failed

echo Compiling authored content boundary...
cl.exe %COMMON_FLAGS% /Fo"build\obj\json.obj" "src\engine\data\json.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\content_json.obj" "src\game\content\content_json.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\content_json_decoder.obj" "src\game\content\content_json_decoder.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\content_validation.obj" "src\game\content\content_validation.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\content_compiler.obj" "src\game\content\content_compiler.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\content_workspace.obj" "src\game\content\content_workspace.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\content_source.obj" "src\game\content\content_source.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\builtin_content.obj" "src\game\content\builtin_content.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\presentation_effects.obj" "src\game\presentation\presentation_effects.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\presentation_effect_renderer.obj" "src\game\presentation\presentation_effect_renderer.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\presentation_feedback_controller.obj" "src\game\presentation\presentation_feedback_controller.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\visual_content_loader.obj" "src\game\presentation\visual_content_loader.cpp"
if errorlevel 1 goto :build_failed

echo Compiling authored map source...
cl.exe %COMMON_FLAGS% /Fo"build\obj\authored_map.obj" "src\game\maps\authored_map.cpp"
if errorlevel 1 goto :build_failed

echo Compiling changed Player gameplay...
cl.exe %COMMON_FLAGS% /Fo"build\obj\player.obj" "src\game\gameplay\player.cpp"
if errorlevel 1 goto :build_failed

echo Compiling changed Player progression...
cl.exe %COMMON_FLAGS% /Fo"build\obj\player_progression.obj" "src\game\gameplay\rpg\player_progression.cpp"
cl.exe %COMMON_FLAGS% /Fo"build\obj\equipment.obj" "src\game\gameplay\rpg\equipment.cpp"
if errorlevel 1 goto :build_failed

cl.exe %COMMON_FLAGS% /Fo"build\obj\rewards.obj" "src\game\gameplay\rpg\rewards.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\reward_grants.obj" "src\game\gameplay\rpg\reward_grants.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\player_bank.obj" "src\game\gameplay\player_bank.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\bank_overlay.obj" "src\game\gameplay\bank_overlay.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\shop_overlay.obj" "src\game\gameplay\shop_overlay.cpp"
if errorlevel 1 goto :build_failed

if not exist "build\obj\dialogue_flags.obj" (
    echo Compiling missing dialogue flags object...
    cl.exe %COMMON_FLAGS% /Fo"build\obj\dialogue_flags.obj" "src\game\gameplay\dialogue\dialogue_flags.cpp"
    if errorlevel 1 goto :build_failed
)

echo Compiling changed Player visual...
cl.exe %COMMON_FLAGS% /Fo"build\obj\player_visual.obj" "src\game\player_visual.cpp"
if errorlevel 1 goto :build_failed

echo Compiling changed game runtime composition...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game_runtime.obj" "src\game\game_runtime.cpp"
if errorlevel 1 goto :build_failed

echo Compiling changed game presentation...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game_presentation.obj" "src\game\game_presentation.cpp"
if errorlevel 1 goto :build_failed

echo Compiling changed game session...
cl.exe %COMMON_FLAGS% /Fo"build\obj\game_session.obj" "src\game\game_session.cpp"
if errorlevel 1 goto :build_failed

echo Compiling world logic and encounters...
cl.exe %COMMON_FLAGS% /Fo"build\obj\world_logic.obj" "src\game\gameplay\world_logic.cpp"
if errorlevel 1 goto :build_failed
cl.exe %COMMON_FLAGS% /Fo"build\obj\encounter_system.obj" "src\game\gameplay\encounter_system.cpp"
if errorlevel 1 goto :build_failed

echo Linking game.exe...
link.exe /nologo /SUBSYSTEM:WINDOWS /OUT:"build\bin\game.exe" ^
    "build\obj\framebuffer.obj" "build\obj\image.obj" "build\obj\renderer_2d.obj" "build\obj\sprite.obj" ^
    "build\obj\animation.obj" "build\obj\bitmap_font.obj" "build\obj\utf8.obj" "build\obj\camera_2d.obj" "build\obj\asset_manager.obj" ^
    "build\obj\tile.obj" "build\obj\tile_layer.obj" "build\obj\collision_grid.obj" "build\obj\collision.obj" ^
    "build\obj\runtime_map.obj" "build\obj\entity_handle.obj" "build\obj\byte_io.obj" "build\obj\json.obj" "build\obj\map_data.obj" ^
    "build\obj\dmap.obj" "build\obj\authored_map.obj" "build\obj\game_launch.obj" "build\obj\runtime_world.obj" "build\obj\save_data.obj" ^
    "build\obj\map_catalog.obj" "build\obj\official_maps.obj" "build\obj\game_content.obj" "build\obj\content_validation.obj" "build\obj\content_compiler.obj" "build\obj\content_json_decoder.obj" "build\obj\content_workspace.obj" "build\obj\content_source.obj" "build\obj\builtin_content.obj" "build\obj\tilesets.obj" ^
    "build\obj\authoring_semantics.obj" "build\obj\map_composition.obj" "build\obj\reachability.obj" "build\obj\world_logic.obj" "build\obj\encounter_system.obj" ^
    "build\obj\combat_types.obj" "build\obj\attack_definitions.obj" "build\obj\player_progression.obj" "build\obj\equipment.obj" "build\obj\rewards.obj" "build\obj\reward_grants.obj" "build\obj\player_bank.obj" "build\obj\bank_overlay.obj" "build\obj\shop_overlay.obj" "build\obj\combat_system.obj" ^
    "build\obj\projectile_system.obj" "build\obj\items.obj" "build\obj\player_items.obj" ^
    "build\obj\world_pickups.obj" "build\obj\world_objects.obj" "build\obj\npc_engine.obj" ^
    "build\obj\dialogue_flags.obj" "build\obj\dialogue_model.obj" "build\obj\dialogue_session.obj" "build\obj\quest_model.obj" ^
    "build\obj\quest_state.obj" "build\obj\quest_system.obj" "build\obj\game_view_model.obj" ^
    "build\obj\world_object_visual.obj" "build\obj\presentation_effects.obj" "build\obj\presentation_effect_renderer.obj" "build\obj\presentation_feedback_controller.obj" "build\obj\visual_content_loader.obj" "build\obj\game_presentation.obj" "build\obj\game_session.obj" "build\obj\runtime_visual_sync.obj" ^
    "build\obj\audit_snapshot.obj" "build\obj\audit_session.obj" "build\obj\bmp_writer.obj" ^
    "build\obj\headless_audit_platform.obj" "build\obj\creature_engine.obj" "build\obj\enemy_visual.obj" ^
    "build\obj\effect_system.obj" "build\obj\command_builder.obj" "build\obj\player.obj" ^
    "build\obj\player_visual.obj" "build\obj\win32_clock.obj" "build\obj\win32_image_decoder.obj" ^
    "build\obj\game_runtime.obj" "build\obj\game.obj" "build\obj\win32_platform.obj" ^
    user32.lib gdi32.lib ole32.lib windowscodecs.lib shell32.lib
if errorlevel 1 goto :build_failed

echo.
echo Incremental game build succeeded:
echo   build\bin\game.exe
popd
exit /b 0

:build_failed
echo.
echo ERROR: incremental game build failed.
popd
exit /b 1
