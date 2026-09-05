#include "game/maps/reachability.h"

#include <array>
#include <cstddef>
#include <queue>

namespace underworld::game::maps {

std::vector<core::TileCoord> ReachabilityValidator::reachableCells(
    const MapData& map, core::TileCoord start) const {
    if (map.width == 0 || map.height == 0 || map.collision.size() !=
        static_cast<std::size_t>(map.width) * map.height ||
        start.x < 0 || start.y < 0 ||
        static_cast<std::uint32_t>(start.x) >= map.width ||
        static_cast<std::uint32_t>(start.y) >= map.height) {
        return {};
    }
    const auto index = [&](core::TileCoord tile) {
        return static_cast<std::size_t>(tile.y) * map.width +
               static_cast<std::size_t>(tile.x);
    };
    if (map.collision[index(start)] != 0U) { return {}; }
    std::vector<std::uint8_t> visited(map.collision.size(), 0U);
    std::queue<core::TileCoord> pending;
    pending.push(start);
    visited[index(start)] = 1U;
    std::vector<core::TileCoord> result;
    constexpr std::array<core::TileCoord, 4> directions{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    while (!pending.empty()) {
        const auto current = pending.front();
        pending.pop();
        result.push_back(current);
        for (const auto direction : directions) {
            const core::TileCoord next{current.x + direction.x, current.y + direction.y};
            if (next.x < 0 || next.y < 0 || static_cast<std::uint32_t>(next.x) >= map.width ||
                static_cast<std::uint32_t>(next.y) >= map.height ||
                map.collision[index(next)] != 0U || visited[index(next)] != 0U) {
                continue;
            }
            visited[index(next)] = 1U;
            pending.push(next);
        }
    }
    return result;
}

ReachabilityReport ReachabilityValidator::validate(
    const MapData& map, core::TileCoord start,
    const std::vector<core::TileCoord>& targets,
    const std::vector<RoomSide>* targetSides) const {
    ReachabilityReport report;
    if (targetSides && targetSides->size() != targets.size()) {
        report.issues.push_back({"reachability_target_metadata_mismatch",
                                 "target side metadata does not match target cells",
                                 std::nullopt, std::nullopt});
        return report;
    }
    if (start.x < 0 || start.y < 0 || static_cast<std::uint32_t>(start.x) >= map.width ||
        static_cast<std::uint32_t>(start.y) >= map.height) {
        report.issues.push_back({"spawn_out_of_bounds", "reachability start is outside the map",
                                 start, std::nullopt});
        return report;
    }
    const auto reached = reachableCells(map, start);
    const auto contains = [&](core::TileCoord tile) {
        for (const auto value : reached) if (value == tile) return true;
        return false;
    };
    if (reached.empty()) {
        report.issues.push_back({"spawn_not_walkable", "reachability start is in collision",
                                 start, std::nullopt});
        return report;
    }
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto target = targets[index];
        const auto side = targetSides ? std::optional<RoomSide>{(*targetSides)[index]} : std::nullopt;
        if (target.x < 0 || target.y < 0 || static_cast<std::uint32_t>(target.x) >= map.width ||
            static_cast<std::uint32_t>(target.y) >= map.height) {
            report.issues.push_back({"invalid_opening", "opening target is outside the map", target, side});
        } else if (!contains(target)) {
            report.issues.push_back({"unreachable_opening", "player spawn cannot reach the opening", target, side});
        }
    }
    return report;
}

ReachabilityReport ReachabilityValidator::validateRoomOpenings(
    const MapData& map, const RoomBlueprint& room, core::TileCoord start) const {
    const auto targets = openingCells(room);
    std::vector<RoomSide> sides;
    for (const auto& opening : room.openings) {
        for (std::uint32_t index = 0; index < opening.width; ++index) sides.push_back(opening.side);
    }
    return validate(map, start, targets, &sides);
}

} // namespace underworld::game::maps
