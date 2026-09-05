#include "engine/platform/linux/linux_platform.h"

#include "game/game.h"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string error;
    const auto options = underworld::game::parseGameLaunchOptions(argc, argv, error);
    if (!options) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }
    try {
        underworld::platform::linux::LinuxPlatform platform;
        if (!platform.initialize()) { return 1; }
        return underworld::game::run(platform, *options);
    } catch (const std::exception& exception) {
        std::cerr << "fatal: " << exception.what() << '\n';
        return 1;
    }
}
