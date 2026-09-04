#pragma once

#include "game/maps/map_data.h"

namespace underworld::game::maps {

[[nodiscard]] const simulation::MapId& demoRoomAId();
[[nodiscard]] const simulation::MapId& demoRoomBId();
[[nodiscard]] MapData makeDemoRoomA();
[[nodiscard]] MapData makeDemoRoomB();

} // namespace underworld::game::maps
