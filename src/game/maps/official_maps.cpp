#include "game/maps/official_maps.h"

#include <array>
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

} // namespace underworld::game::maps
