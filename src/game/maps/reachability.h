#pragma once

#include "engine/core/coordinates.h"
#include "game/maps/map_composition.h"

#include <optional>
#include <string>
#include <vector>

namespace underworld::game::maps {

struct ReachabilityIssue final {
    std::string code;
    std::string message;
    std::optional<core::TileCoord> tile;
    std::optional<RoomSide> side;
};

struct ReachabilityReport final {
    std::vector<ReachabilityIssue> issues;
    [[nodiscard]] bool valid() const noexcept { return issues.empty(); }
};

class ReachabilityValidator final {
public:
    [[nodiscard]] std::vector<core::TileCoord> reachableCells(
        const MapData& map, core::TileCoord start) const;
    [[nodiscard]] ReachabilityReport validate(
        const MapData& map, core::TileCoord start,
        const std::vector<core::TileCoord>& targets,
        const std::vector<RoomSide>* targetSides = nullptr) const;
    [[nodiscard]] ReachabilityReport validateRoomOpenings(
        const MapData& map, const RoomBlueprint& room,
        core::TileCoord start) const;
};

} // namespace underworld::game::maps
