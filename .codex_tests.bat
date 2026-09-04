@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl.exe /nologo /std:c++20 /W4 /permissive- /EHsc /Zc:__cplusplus /utf-8 /I src /c /Fo"build\obj\tests.obj" "tests\test_main.cpp"
if errorlevel 1 exit /b 1
link.exe /nologo /SUBSYSTEM:CONSOLE /OUT:"build\bin\tests.exe" ^
 "build\obj\framebuffer.obj" "build\obj\image.obj" "build\obj\renderer_2d.obj" "build\obj\sprite.obj" ^
 "build\obj\animation.obj" "build\obj\bitmap_font.obj" "build\obj\camera_2d.obj" "build\obj\asset_manager.obj" ^
 "build\obj\tile.obj" "build\obj\tile_layer.obj" "build\obj\collision_grid.obj" "build\obj\collision.obj" ^
 "build\obj\runtime_map.obj" "build\obj\entity_handle.obj" "build\obj\byte_io.obj" "build\obj\map_data.obj" ^
 "build\obj\dmap.obj" "build\obj\runtime_world.obj" "build\obj\save_data.obj" "build\obj\map_catalog.obj" ^
 "build\obj\demo_maps.obj" "build\obj\game_content.obj" "build\obj\editor_document.obj" "build\obj\editor_commands.obj" ^
 "build\obj\combat_types.obj" "build\obj\attack_definitions.obj" "build\obj\combat_system.obj" ^
 "build\obj\projectile_system.obj" "build\obj\items.obj" "build\obj\player_items.obj" "build\obj\world_pickups.obj" ^
 "build\obj\world_objects.obj" "build\obj\game_view_model.obj" "build\obj\world_object_visual.obj" ^
 "build\obj\creature_engine.obj" "build\obj\enemy_visual.obj" "build\obj\training_puppet.obj" ^
 "build\obj\effect_system.obj" "build\obj\command_builder.obj" "build\obj\player.obj" "build\obj\player_visual.obj" ^
 "build\obj\win32_clock.obj" "build\obj\win32_image_decoder.obj" "build\obj\tests.obj" ole32.lib windowscodecs.lib
