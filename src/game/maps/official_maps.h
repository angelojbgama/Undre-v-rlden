#pragma once

#include "engine/simulation/persistent_id.h"

#include <filesystem>
#include <optional>
#include <span>

namespace underworld::game::maps {

struct OfficialMapManifestEntry final {
    simulation::MapId id{};
    std::filesystem::path relativePath;
};

[[nodiscard]] std::span<const OfficialMapManifestEntry> officialGameplayMaps() noexcept;
[[nodiscard]] std::optional<std::filesystem::path> resolveOfficialGameplayMapPath(
    const std::filesystem::path& relativePath,
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& currentDirectory) noexcept;
} // namespace underworld::game::maps
