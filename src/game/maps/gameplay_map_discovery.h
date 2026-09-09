#pragma once

#include "game/maps/map_catalog.h"

#include <filesystem>
#include <string>
#include <vector>

namespace underworld::game::maps {

struct GameplayMapRecord final {
    simulation::MapId id{};
    std::filesystem::path path;
    MapData data;
};

struct GameplayMapDiscoveryResult final {
    std::filesystem::path root;
    MapCatalog catalog;
    std::vector<GameplayMapRecord> maps;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

[[nodiscard]] std::filesystem::path gameplayMapsRoot(
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& currentDirectory);

[[nodiscard]] GameplayMapDiscoveryResult discoverGameplayMaps(
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& currentDirectory,
    const MapValidationCatalogs* catalogs = nullptr,
    bool allowEmpty = false);

} // namespace underworld::game::maps
