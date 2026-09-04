#pragma once

#include "game/maps/map_data.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace underworld::game::maps {

inline constexpr std::uint16_t dmapMajorVersion = 1;
inline constexpr std::uint16_t dmapMinorVersion = 0;

struct DmapLoadResult final {
    bool success{};
    MapData data{};
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] std::vector<std::uint8_t> serializeDmap(const MapData& data);
[[nodiscard]] DmapLoadResult deserializeDmap(
    std::span<const std::uint8_t> bytes, const MapValidationCatalogs* catalogs = nullptr);
[[nodiscard]] bool writeDmap(const std::filesystem::path& path, const MapData& data,
                             std::string& error);
[[nodiscard]] DmapLoadResult readDmap(
    const std::filesystem::path& path, const MapValidationCatalogs* catalogs = nullptr);

} // namespace underworld::game::maps
