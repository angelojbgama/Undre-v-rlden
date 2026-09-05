#include "game/game_launch.h"

#include "game/maps/map_data.h"
#include "game/maps/official_maps.h"

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

void addCandidate(std::vector<std::filesystem::path>& candidates,
                  const std::filesystem::path& start,
                  const std::filesystem::path& relativePath) {
    std::filesystem::path current = start;
    for (int depth = 0; depth < 8 && !current.empty(); ++depth) {
        candidates.push_back(current / relativePath);
        const auto parent = current.parent_path();
        if (parent == current) { break; }
        current = parent;
    }
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
        error = "unknown game option: " + narrowId(argv[index]);
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
    std::vector<std::filesystem::path> candidates;
    const auto relativePath = maps::officialGameplayMaps().front().relativePath;
    addCandidate(candidates, currentDirectory, relativePath);
    addCandidate(candidates, executableDirectory, relativePath);
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return {StartupMapSource::officialGameplay, candidate};
        }
    }
    return {StartupMapSource::officialGameplay, candidates.front()};
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
