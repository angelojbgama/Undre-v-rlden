#pragma once

#include "game/game_launch.h"

namespace underworld::platform {
class Platform;
}

namespace underworld::game {

int run(platform::Platform& platform, const GameLaunchOptions& options = {});

} // namespace underworld::game
