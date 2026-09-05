#include "game/maps/map_composition.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace underworld::game::maps {
namespace {

std::size_t indexOf(core::TileCoord tile, std::uint32_t width) {
    return static_cast<std::size_t>(tile.y) * width +
           static_cast<std::size_t>(tile.x);
}

void addIssue(CompositionReport& report, CompositionIssueSeverity severity,
              std::string code, std::string message,
              std::optional<core::TileCoord> tile = std::nullopt,
              std::optional<RoomSide> side = std::nullopt,
              std::optional<simulation::DefinitionId> semanticId = std::nullopt) {
    report.issues.push_back({severity, std::move(code), std::move(message), tile, side,
                             std::move(semanticId)});
}

std::uint32_t sideLength(const RoomBlueprint& room, RoomSide side) noexcept {
    return side == RoomSide::north || side == RoomSide::south ? room.width : room.height;
}

bool validSide(RoomSide side) noexcept {
    return side == RoomSide::north || side == RoomSide::east ||
           side == RoomSide::south || side == RoomSide::west;
}

core::TileCoord openingCell(const RoomBlueprint& room, const RoomOpening& opening,
                            std::uint32_t along) noexcept {
    switch (opening.side) {
    case RoomSide::north: return {static_cast<int>(opening.offset + along), 0};
    case RoomSide::east: return {static_cast<int>(room.width - 1),
                                 static_cast<int>(opening.offset + along)};
    case RoomSide::south: return {static_cast<int>(opening.offset + along),
                                  static_cast<int>(room.height - 1)};
    case RoomSide::west: return {0, static_cast<int>(opening.offset + along)};
    }
    return {};
}

const authoring::TileSemanticDefinition* chooseBoundaryTile(
    const MapCompositionProfile& profile,
    const authoring::AuthoringSemanticRegistry& semantics,
    CompositionReport& report) {
    const authoring::TileSemanticDefinition* tile = nullptr;
    if (!profile.boundaryTileId.empty()) {
        tile = semantics.findTile(profile.boundaryTileId);
        if (!tile) {
            addIssue(report, CompositionIssueSeverity::error,
                     "composition_missing_semantic",
                     "requested boundary semantic tile does not exist", std::nullopt,
                     std::nullopt, profile.boundaryTileId);
            return nullptr;
        }
    } else {
        // The current catalogue has no confirmed floor semantic.  A probable masonry
        // wall segment is the only visual role this first room is allowed to choose.
        for (const auto& candidate : semantics.tiles()) {
            if (candidate.role == authoring::TileRole::wall &&
                candidate.family == "masonry" &&
                candidate.topology == authoring::TileTopology::straightHorizontal) {
                tile = &candidate;
                break;
            }
        }
        if (!tile) {
            addIssue(report, CompositionIssueSeverity::error,
                     "composition_missing_semantic",
                     "no deterministic masonry boundary semantic is available");
            return nullptr;
        }
    }
    if (tile->role != authoring::TileRole::wall) {
        addIssue(report, CompositionIssueSeverity::error,
                 "composition_invalid_semantic",
                 "boundary semantic must have the wall role", std::nullopt, std::nullopt,
                 tile->id);
        return nullptr;
    }
    return tile;
}

bool validBlueprintDimensions(const RoomBlueprint& room, CompositionReport& report) {
    if (room.width < 3 || room.height < 3) {
        addIssue(report, CompositionIssueSeverity::error, "invalid_room_dimensions",
                 "room dimensions must leave a walkable interior and four corners");
        return false;
    }
    if (room.width > MapLimits::maximumDimension || room.height > MapLimits::maximumDimension) {
        addIssue(report, CompositionIssueSeverity::error, "invalid_room_dimensions",
                 "room dimensions exceed map safety limits");
        return false;
    }
    return true;
}

bool validateOpenings(const RoomBlueprint& room, CompositionReport& report) {
    if (room.openings.size() > 4U) {
        addIssue(report, CompositionIssueSeverity::error, "too_many_openings",
                 "a room supports at most four openings");
        return false;
    }
    const auto cellCount = static_cast<std::size_t>(room.width) * room.height;
    std::vector<bool> occupied(cellCount, false);
    bool valid = true;
    for (const auto& opening : room.openings) {
        if (!validSide(opening.side)) {
            addIssue(report, CompositionIssueSeverity::error, "invalid_opening",
                     "opening side is not a supported room side", std::nullopt, opening.side);
            valid = false;
            continue;
        }
        const auto length = sideLength(room, opening.side);
        if (opening.width == 0 || opening.offset < 1U ||
            opening.offset > length - 1U || opening.width > length - 1U ||
            opening.offset > (length - 1U) - opening.width) {
            addIssue(report, CompositionIssueSeverity::error, "invalid_opening",
                     "opening must be positive, in bounds, and preserve both corners",
                     std::nullopt, opening.side);
            valid = false;
            continue;
        }
        for (std::uint32_t along = 0; along < opening.width; ++along) {
            const auto cell = openingCell(room, opening, along);
            const auto position = indexOf(cell, room.width);
            if (occupied[position]) {
                addIssue(report, CompositionIssueSeverity::error, "opening_overlap",
                         "openings overlap on the same boundary cell", cell, opening.side);
                valid = false;
            }
            occupied[position] = true;
        }
    }
    return valid;
}

bool validSpawn(const RoomBlueprint& room, const RoomCompositionGrid& grid,
                CompositionReport& report) {
    if (!room.playerSpawn) { return true; }
    const auto& spawn = *room.playerSpawn;
    if (spawn.id.empty() || !grid.inBounds(spawn.tile)) {
        addIssue(report, CompositionIssueSeverity::error, "invalid_player_spawn",
                 "player spawn must have an ID and be inside the room", spawn.tile);
        return false;
    }
    const auto kind = grid.cell(spawn.tile);
    if (kind != RoomCellKind::walkable && kind != RoomCellKind::opening) {
        addIssue(report, CompositionIssueSeverity::error, "spawn_not_walkable",
                 "player spawn must be in a walkable or opening cell", spawn.tile);
        return false;
    }
    return true;
}

} // namespace

bool RoomCompositionGrid::inBounds(core::TileCoord tile) const noexcept {
    return tile.x >= 0 && tile.y >= 0 &&
           static_cast<std::uint32_t>(tile.x) < width_ &&
           static_cast<std::uint32_t>(tile.y) < height_;
}

RoomCellKind RoomCompositionGrid::cell(core::TileCoord tile) const {
    if (!inBounds(tile)) { throw std::out_of_range("room composition cell is outside bounds"); }
    return cells_[indexOf(tile, width_)];
}

bool CompositionReport::hasErrors() const noexcept { return errorCount() != 0; }

std::size_t CompositionReport::errorCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        issues.begin(), issues.end(), [](const CompositionIssue& issue) {
            return issue.severity == CompositionIssueSeverity::error;
        }));
}

MapCompositionResult MapComposer::compose(
    const MapBlueprint& blueprint,
    const authoring::AuthoringSemanticRegistry& semantics) const {
    MapCompositionResult result;
    if (blueprint.id.empty()) {
        addIssue(result.report, CompositionIssueSeverity::error, "invalid_map_id",
                 "map blueprint ID must not be empty");
        return result;
    }
    if (blueprint.tileSize == 0) {
        addIssue(result.report, CompositionIssueSeverity::error, "invalid_tile_size",
                 "map blueprint tile size must be positive");
        return result;
    }
    if (profile_.boundaryLayer.empty()) {
        addIssue(result.report, CompositionIssueSeverity::error, "invalid_layer_name",
                 "composition boundary layer name must not be empty");
        return result;
    }
    if (!validBlueprintDimensions(blueprint.room, result.report) ||
        !validateOpenings(blueprint.room, result.report)) {
        return result;
    }
    const auto boundaryTile = chooseBoundaryTile(profile_, semantics, result.report);
    if (!boundaryTile) { return result; }

    const std::size_t cellCount = static_cast<std::size_t>(blueprint.room.width) *
                                  blueprint.room.height;
    std::vector<RoomCellKind> cells(cellCount, RoomCellKind::walkable);
    for (std::uint32_t y = 0; y < blueprint.room.height; ++y) {
        for (std::uint32_t x = 0; x < blueprint.room.width; ++x) {
            if (x == 0 || y == 0 || x + 1U == blueprint.room.width ||
                y + 1U == blueprint.room.height) {
                cells[static_cast<std::size_t>(y) * blueprint.room.width + x] =
                    RoomCellKind::boundary;
            }
        }
    }
    for (const auto& opening : blueprint.room.openings) {
        for (std::uint32_t along = 0; along < opening.width; ++along) {
            const auto cell = openingCell(blueprint.room, opening, along);
            cells[indexOf(cell, blueprint.room.width)] = RoomCellKind::opening;
        }
    }
    result.grid = RoomCompositionGrid(blueprint.room.width, blueprint.room.height,
                                      std::move(cells));
    if (!validSpawn(blueprint.room, result.grid, result.report)) { return result; }

    MapData map;
    map.id = blueprint.id;
    map.width = blueprint.room.width;
    map.height = blueprint.room.height;
    map.tileSize = blueprint.tileSize;
    map.tileReferences.push_back(authoring::tileReferenceFor(*boundaryTile));
    MapTileLayer layer{profile_.boundaryLayer, true,
                       std::vector<std::optional<std::uint32_t>>(cellCount)};
    map.collision.assign(cellCount, 0U);
    for (std::uint32_t y = 0; y < map.height; ++y) {
        for (std::uint32_t x = 0; x < map.width; ++x) {
            const core::TileCoord tile{static_cast<int>(x), static_cast<int>(y)};
            const auto kind = result.grid.cell(tile);
            const auto cellIndex = indexOf(tile, map.width);
            if (kind == RoomCellKind::boundary) {
                layer.cells[cellIndex] = 0U;
                map.collision[cellIndex] = 1U;
            }
        }
    }
    map.layers.push_back(std::move(layer));
    if (blueprint.room.playerSpawn) {
        const auto& spawn = *blueprint.room.playerSpawn;
        const auto origin = core::tileToWorld(spawn.tile, blueprint.tileSize);
        map.playerSpawns.push_back({spawn.id,
                                    {origin.x + blueprint.tileSize / 2,
                                     origin.y + blueprint.tileSize / 2},
                                    spawn.facing});
    }
    const auto structural = validateMapData(map);
    if (!structural) {
        addIssue(result.report, CompositionIssueSeverity::error, "composition_invalid_map",
                 structural.error);
        return result;
    }
    result.map = std::move(map);
    return result;
}

const char* toString(RoomSide side) noexcept {
    switch (side) {
    case RoomSide::north: return "north";
    case RoomSide::east: return "east";
    case RoomSide::south: return "south";
    case RoomSide::west: return "west";
    }
    return "unknown";
}

const char* toString(RoomCellKind kind) noexcept {
    switch (kind) {
    case RoomCellKind::voidCell: return "void";
    case RoomCellKind::walkable: return "walkable";
    case RoomCellKind::boundary: return "boundary";
    case RoomCellKind::opening: return "opening";
    case RoomCellKind::reserved: return "reserved";
    }
    return "unknown";
}

std::vector<core::TileCoord> openingCells(const RoomBlueprint& room) {
    std::vector<core::TileCoord> result;
    for (const auto& opening : room.openings) {
        for (std::uint32_t along = 0; along < opening.width; ++along) {
            result.push_back(openingCell(room, opening, along));
        }
    }
    return result;
}

} // namespace underworld::game::maps
