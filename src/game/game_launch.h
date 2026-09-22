#pragma once

#include "engine/simulation/persistent_id.h"
#include "game/maps/gameplay_map_discovery.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace underworld::game {

namespace maps { struct MapData; }

struct GameLaunchOptions final {
    std::optional<std::filesystem::path> mapPath;
    std::optional<std::filesystem::path> mapRoot;
    std::optional<std::filesystem::path> assetRoot;
    std::optional<std::filesystem::path> contentRoot;
    std::optional<simulation::SpawnId> spawnId;
    bool auditEnabled{};
    // Boot into the authored title shell (title screen + start/new-game slot
    // picker). Default false so headless/playtest drivers start in gameplay;
    // the game's own option parsing turns it on unless --no-title is passed.
    bool titleScreen{};
};

enum class StartupMapSource { explicitPath, officialGameplay };

struct StartupMapSelection final {
    StartupMapSource source{StartupMapSource::officialGameplay};
    std::filesystem::path path;
};

[[nodiscard]] std::optional<GameLaunchOptions> parseGameLaunchOptions(
    int argc, const wchar_t* const* argv, std::string& error);

[[nodiscard]] std::optional<GameLaunchOptions> parseGameLaunchOptions(
    int argc, const char* const* argv, std::string& error);

[[nodiscard]] StartupMapSelection selectStartupMap(
    const GameLaunchOptions& options, const std::filesystem::path& executableDirectory,
    const std::filesystem::path& currentDirectory);

[[nodiscard]] std::optional<StartupMapSelection> selectDiscoveredStartupMap(
    const GameLaunchOptions& options,
    const std::vector<maps::GameplayMapRecord>& discoveredMaps,
    std::string& error);

[[nodiscard]] std::optional<simulation::SpawnId> selectStartupSpawn(
    const maps::MapData& map, const std::optional<simulation::SpawnId>& requested,
    std::string& error);

} // namespace underworld::game
