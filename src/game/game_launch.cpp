#include "game/game_launch.h"

#include "game/maps/map_data.h"
#include <algorithm>
#include <vector>

namespace underworld::game {
namespace {

std::string narrowId(const wchar_t* value) {
    std::string result;
    if (value == nullptr) { return result; }
    for (const wchar_t* cursor = value; *cursor != L'\0'; ++cursor) {
        const auto codePoint = static_cast<unsigned long>(*cursor);
        result.push_back(codePoint <= 0x7FU ? static_cast<char>(codePoint) : '?');
    }
    return result;
}

} // namespace

std::optional<GameLaunchOptions> parseGameLaunchOptions(
    int argc, const wchar_t* const* argv, std::string& error) {
    error.clear();
    GameLaunchOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument(argv[index] == nullptr ? L"" : argv[index]);
        const auto consumeValue = [&](const wchar_t* name, const char* displayName,
                                      std::optional<std::filesystem::path>& target)
            -> std::optional<bool> {
            if (argument == name) {
                if (index + 1 >= argc || argv[index + 1] == nullptr ||
                    std::wstring(argv[index + 1]).empty()) {
                    error = std::string(displayName) + " requires a value";
                    return false;
                }
                target = std::filesystem::path(argv[++index]);
                return true;
            }
            const std::wstring prefix = std::wstring(name) + L"=";
            if (argument.rfind(prefix, 0) == 0) {
                if (argument.size() == prefix.size()) {
                    error = std::string(displayName) + " requires a value";
                    return false;
                }
                target = std::filesystem::path(argument.substr(prefix.size()));
                return true;
            }
            return std::optional<bool>{};
        };

        const auto mapResult = consumeValue(L"--map", "--map", options.mapPath);
        if (mapResult.has_value()) {
            if (!*mapResult) { return std::nullopt; }
            continue;
        }
        const bool hadMapRoot = options.mapRoot.has_value();
        const auto mapRootResult = consumeValue(L"--map-root", "--map-root", options.mapRoot);
        if (mapRootResult.has_value()) {
            if (!*mapRootResult) { return std::nullopt; }
            if (hadMapRoot) { error = "duplicate --map-root option"; return std::nullopt; }
            continue;
        }
        const auto assetResult = consumeValue(L"--asset-root", "--asset-root", options.assetRoot);
        if (assetResult.has_value()) {
            if (!*assetResult) { return std::nullopt; }
            continue;
        }
        const bool hadContent = options.contentRoot.has_value();
        const auto contentResult = consumeValue(L"--content", "--content", options.contentRoot);
        if (contentResult.has_value()) {
            if (!*contentResult) { return std::nullopt; }
            if (hadContent) { error = "duplicate --content option"; return std::nullopt; }
            continue;
        }
        if (argument == L"--spawn") {
            if (index + 1 >= argc || argv[index + 1] == nullptr ||
                std::wstring(argv[index + 1]).empty()) {
                error = "--spawn requires a value";
                return std::nullopt;
            }
            options.spawnId = simulation::SpawnId{narrowId(argv[++index])};
            continue;
        }
        if (argument.rfind(L"--spawn=", 0) == 0) {
            const auto value = argument.substr(8);
            if (value.empty()) {
                error = "--spawn requires a value";
                return std::nullopt;
            }
            options.spawnId = simulation::SpawnId{narrowId(value.c_str())};
            continue;
        }
        if (argument == L"--audit") {
            options.auditEnabled = true;
            continue;
        }
        error = "unknown game option: " + narrowId(argv[index]);
        return std::nullopt;
    }
    return options;
}

std::optional<GameLaunchOptions> parseGameLaunchOptions(
    int argc, const char* const* argv, std::string& error) {
    error.clear();
    GameLaunchOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] == nullptr ? "" : argv[index];
        const auto consumeValue = [&](const char* name, std::optional<std::filesystem::path>& target)
            -> std::optional<bool> {
            if (argument == name) {
                if (index + 1 >= argc || argv[index + 1] == nullptr ||
                    std::string(argv[index + 1]).empty()) {
                    error = std::string(name) + " requires a value";
                    return false;
                }
                target = std::filesystem::path(argv[++index]);
                return true;
            }
            const std::string prefix = std::string(name) + "=";
            if (argument.rfind(prefix, 0) == 0) {
                if (argument.size() == prefix.size()) {
                    error = std::string(name) + " requires a value";
                    return false;
                }
                target = std::filesystem::path(argument.substr(prefix.size()));
                return true;
            }
            return std::optional<bool>{};
        };
        const auto mapResult = consumeValue("--map", options.mapPath);
        if (mapResult.has_value()) {
            if (!*mapResult) { return std::nullopt; }
            continue;
        }
        const bool hadMapRoot = options.mapRoot.has_value();
        const auto mapRootResult = consumeValue("--map-root", options.mapRoot);
        if (mapRootResult.has_value()) {
            if (!*mapRootResult) { return std::nullopt; }
            if (hadMapRoot) { error = "duplicate --map-root option"; return std::nullopt; }
            continue;
        }
        const auto assetResult = consumeValue("--asset-root", options.assetRoot);
        if (assetResult.has_value()) {
            if (!*assetResult) { return std::nullopt; }
            continue;
        }
        const bool hadContent = options.contentRoot.has_value();
        const auto contentResult = consumeValue("--content", options.contentRoot);
        if (contentResult.has_value()) {
            if (!*contentResult) { return std::nullopt; }
            if (hadContent) { error = "duplicate --content option"; return std::nullopt; }
            continue;
        }
        if (argument == "--spawn" || argument.rfind("--spawn=", 0) == 0) {
            std::string value;
            if (argument == "--spawn") {
                if (index + 1 >= argc || argv[index + 1] == nullptr ||
                    std::string(argv[index + 1]).empty()) {
                    error = "--spawn requires a value";
                    return std::nullopt;
                }
                value = argv[++index];
            } else {
                value = argument.substr(8);
                if (value.empty()) { error = "--spawn requires a value"; return std::nullopt; }
            }
            options.spawnId = simulation::SpawnId{value};
            continue;
        }
        if (argument == "--audit") { options.auditEnabled = true; continue; }
        error = "unknown game option: " + argument;
        return std::nullopt;
    }
    return options;
}

StartupMapSelection selectStartupMap(const GameLaunchOptions& options,
                                     const std::filesystem::path& executableDirectory,
                                     const std::filesystem::path& currentDirectory) {
    if (options.mapPath) {
        return {StartupMapSource::explicitPath, *options.mapPath};
    }
    std::string error;
    const auto discovered = maps::discoverGameplayMaps(executableDirectory, currentDirectory);
    if (discovered) {
        if (const auto selected = selectDiscoveredStartupMap(
                options, discovered.maps, error)) {
            return *selected;
        }
    }
    return {StartupMapSource::officialGameplay, discovered.root / "startup.dmap"};
}

std::optional<StartupMapSelection> selectDiscoveredStartupMap(
    const GameLaunchOptions& options,
    const std::vector<maps::GameplayMapRecord>& discoveredMaps,
    std::string& error) {
    error.clear();
    if (options.mapPath) {
        return StartupMapSelection{StartupMapSource::explicitPath, *options.mapPath};
    }
    if (discoveredMaps.empty()) {
        error = "no gameplay maps found";
        return std::nullopt;
    }
    if (discoveredMaps.size() == 1) {
        return StartupMapSelection{StartupMapSource::officialGameplay,
                                    discoveredMaps.front().path};
    }
    std::vector<const maps::GameplayMapRecord*> entryMaps;
    for (const auto& map : discoveredMaps) {
        const auto found = std::find_if(map.data.playerSpawns.begin(),
                                        map.data.playerSpawns.end(), [](const auto& spawn) {
                                            return spawn.id == simulation::SpawnId{"entry.start"};
                                        });
        if (found != map.data.playerSpawns.end()) { entryMaps.push_back(&map); }
    }
    if (entryMaps.size() == 1) {
        return StartupMapSelection{StartupMapSource::officialGameplay,
                                    entryMaps.front()->path};
    }
    if (entryMaps.empty()) {
        error = "multiple gameplay maps found but none has player spawn 'entry.start'; "
                "select one explicitly with --map";
    } else {
        error = "startup map selection is ambiguous: multiple gameplay maps have player spawn 'entry.start'; "
                "select one explicitly with --map";
    }
    return std::nullopt;
}

std::optional<simulation::SpawnId> selectStartupSpawn(
    const maps::MapData& map,
    const std::optional<simulation::SpawnId>& requested, std::string& error) {
    error.clear();
    if (map.playerSpawns.empty()) {
        error = "map has no player spawn";
        return std::nullopt;
    }
    if (requested) {
        const auto found = std::find_if(map.playerSpawns.begin(), map.playerSpawns.end(),
            [&](const auto& spawn) { return spawn.id == *requested; });
        if (found == map.playerSpawns.end()) {
            error = "requested player spawn does not exist";
            return std::nullopt;
        }
        return found->id;
    }
    const auto canonical = std::find_if(map.playerSpawns.begin(), map.playerSpawns.end(),
        [](const auto& spawn) { return spawn.id == simulation::SpawnId{"entry.start"}; });
    if (canonical != map.playerSpawns.end()) { return canonical->id; }
    return std::min_element(map.playerSpawns.begin(), map.playerSpawns.end(),
        [](const auto& left, const auto& right) { return left.id.value() < right.id.value(); })->id;
}

} // namespace underworld::game
