#include "game/maps/official_maps.h"
#include "game/maps/gameplay_map_discovery.h"

#include "game/maps/dmap.h"

#include <array>
#include <algorithm>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace underworld::game::maps {
namespace {

const std::array<OfficialMapManifestEntry, 3> manifest{{
    {simulation::MapId{"map.dungeon.01"},
     std::filesystem::path{"maps/gameplay/dungeon_01_entry.dmap"}},
    {simulation::MapId{"map.dungeon.02"},
     std::filesystem::path{"maps/gameplay/dungeon_02_gallery.dmap"}},
    {simulation::MapId{"map.dungeon.03"},
     std::filesystem::path{"maps/gameplay/dungeon_03_depths.dmap"}},
}};

std::vector<std::filesystem::path> roots(const std::filesystem::path& start) {
    std::vector<std::filesystem::path> result;
    auto current = start;
    for (int depth = 0; depth < 8 && !current.empty(); ++depth) {
        result.push_back(current);
        const auto parent = current.parent_path();
        if (parent == current) { break; }
        current = parent;
    }
    return result;
}

} // namespace

std::span<const OfficialMapManifestEntry> officialGameplayMaps() noexcept {
    return manifest;
}

std::optional<std::filesystem::path> resolveOfficialGameplayMapPath(
    const std::filesystem::path& relativePath,
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& currentDirectory) noexcept {
    std::vector<std::filesystem::path> candidates;
    for (const auto& root : roots(currentDirectory)) { candidates.push_back(root / relativePath); }
    for (const auto& root : roots(executableDirectory)) { candidates.push_back(root / relativePath); }
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) { return candidate; }
    }
    return std::nullopt;
}

namespace {

std::vector<std::filesystem::path> gameplayRoots(const std::filesystem::path& start) {
    std::vector<std::filesystem::path> result;
    auto current = start;
    for (int depth = 0; depth < 8 && !current.empty(); ++depth) {
        result.push_back(current);
        const auto parent = current.parent_path();
        if (parent == current) { break; }
        current = parent;
    }
    return result;
}

std::vector<std::filesystem::path> gameplayCandidates(
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& currentDirectory) {
    std::vector<std::filesystem::path> result;
    for (const auto& root : gameplayRoots(currentDirectory)) {
        result.push_back(root / "maps" / "gameplay");
    }
    for (const auto& root : gameplayRoots(executableDirectory)) {
        result.push_back(root / "maps" / "gameplay");
    }
    return result;
}

} // namespace

std::filesystem::path gameplayMapsRoot(
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& currentDirectory) {
    for (const auto& candidate : gameplayCandidates(executableDirectory, currentDirectory)) {
        std::error_code error;
        if (std::filesystem::is_directory(candidate, error)) { return candidate; }
    }
    return currentDirectory / "maps" / "gameplay";
}

GameplayMapDiscoveryResult discoverGameplayMaps(
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& currentDirectory,
    const MapValidationCatalogs* catalogs,
    bool allowEmpty) {
    GameplayMapDiscoveryResult result;
    result.root = gameplayMapsRoot(executableDirectory, currentDirectory);

    std::vector<std::filesystem::path> paths;
    std::error_code iteratorError;
    if (std::filesystem::is_directory(result.root, iteratorError)) {
        std::filesystem::recursive_directory_iterator iterator(
            result.root, std::filesystem::directory_options::skip_permission_denied,
            iteratorError);
        const std::filesystem::recursive_directory_iterator end;
        for (; iterator != end; iterator.increment(iteratorError)) {
            if (iteratorError) {
                result.error = "could not enumerate gameplay maps under " +
                               result.root.string() + ": " + iteratorError.message();
                return result;
            }
            std::error_code fileError;
            if (iterator->is_regular_file(fileError) &&
                iterator->path().extension() == ".dmap") {
                paths.push_back(iterator->path());
            }
        }
    }
    std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        return left.generic_string() < right.generic_string();
    });
    if (paths.empty()) {
        if (!allowEmpty) {
            result.error = "no gameplay maps found in " + result.root.string();
        }
        return result;
    }

    std::unordered_map<simulation::MapId, std::filesystem::path,
                       simulation::MapIdHash> seen;
    for (const auto& path : paths) {
        const auto loaded = readDmap(path, catalogs);
        if (!loaded) {
            result.error = "could not load gameplay map '" + path.string() + "': " +
                           loaded.error;
            return result;
        }
        const auto duplicate = seen.find(loaded.data.id);
        if (duplicate != seen.end()) {
            result.error = "duplicate gameplay MapId '" + std::string(loaded.data.id.value()) + "'\n  " +
                           duplicate->second.string() + "\n  " + path.string();
            return result;
        }
        seen.emplace(loaded.data.id, path);
        result.catalog.add(loaded.data.id, path);
        result.maps.push_back({loaded.data.id, path, loaded.data});
    }
    if (const auto linkError = result.catalog.validateLinks(catalogs); !linkError.empty()) {
        result.error = "invalid gameplay map links: " + linkError;
    }
    return result;
}

} // namespace underworld::game::maps
