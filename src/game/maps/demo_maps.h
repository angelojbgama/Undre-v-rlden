#pragma once

#include "game/maps/map_data.h"

namespace underworld::game::maps {

[[nodiscard]] const simulation::MapId& demoRoomAId();
[[nodiscard]] const simulation::MapId& demoRoomBId();
[[nodiscard]] const simulation::MapId& editorSmokeMapId();
[[nodiscard]] MapData makeDemoRoomA();
[[nodiscard]] MapData makeDemoRoomB();
[[nodiscard]] MapData makeEditorSmokeMap();

} // namespace underworld::game::maps
