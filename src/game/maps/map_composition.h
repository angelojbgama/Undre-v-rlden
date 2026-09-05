#pragma once

#include "engine/core/coordinates.h"
#include "engine/core/game_metrics.h"
#include "engine/simulation/persistent_id.h"
#include "game/authoring/authoring_semantics.h"
#include "game/gameplay/facing_direction.h"
#include "game/maps/map_data.h"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace underworld::game::maps {

enum class RoomSide { north, east, south, west };

struct RoomOpening final {
    RoomSide side{RoomSide::north};
    // Offset along the selected side, measured in tiles from the map origin.
    // The two corner cells are excluded from the legal opening interval.
    std::uint32_t offset{};
    std::uint32_t width{};
};

struct PlayerSpawnBlueprint final {
    simulation::SpawnId id{std::string{"entry.start"}};
    core::TileCoord tile{};
    gameplay::FacingDirection facing{gameplay::FacingDirection::down};
};

struct RoomBlueprint final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<RoomOpening> openings;
    std::optional<PlayerSpawnBlueprint> playerSpawn;
};

struct MapBlueprint final {
    simulation::MapId id{std::string{"map.composed.room"}};
    RoomBlueprint room;
    std::uint16_t tileSize{static_cast<std::uint16_t>(core::GameMetrics::tileSize)};
};

enum class RoomCellKind { voidCell, walkable, boundary, opening, reserved };

class RoomCompositionGrid final {
public:
    RoomCompositionGrid() = default;
    RoomCompositionGrid(std::uint32_t width, std::uint32_t height,
                        std::vector<RoomCellKind> cells)
        : width_(width), height_(height), cells_(std::move(cells)) {}

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] bool empty() const noexcept { return cells_.empty(); }
    [[nodiscard]] bool inBounds(core::TileCoord tile) const noexcept;
    [[nodiscard]] RoomCellKind cell(core::TileCoord tile) const;
    [[nodiscard]] const std::vector<RoomCellKind>& cells() const noexcept { return cells_; }

private:
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<RoomCellKind> cells_;
};

enum class CompositionIssueSeverity { error, warning, info };

struct CompositionIssue final {
    CompositionIssueSeverity severity{CompositionIssueSeverity::error};
    std::string code;
    std::string message;
    std::optional<core::TileCoord> tile;
    std::optional<RoomSide> side;
    std::optional<simulation::DefinitionId> semanticId;
};

struct CompositionReport final {
    std::vector<CompositionIssue> issues;
    [[nodiscard]] bool hasErrors() const noexcept;
    [[nodiscard]] std::size_t errorCount() const noexcept;
};

struct MapCompositionProfile final {
    std::string boundaryLayer{"walls"};
    simulation::DefinitionId boundaryTileId{};
};

struct MapCompositionResult final {
    std::optional<MapData> map;
    RoomCompositionGrid grid;
    CompositionReport report;
    [[nodiscard]] explicit operator bool() const noexcept {
        return map.has_value() && !report.hasErrors();
    }
};

class MapComposer final {
public:
    explicit MapComposer(MapCompositionProfile profile = {}) : profile_(std::move(profile)) {}

    [[nodiscard]] MapCompositionResult compose(
        const MapBlueprint& blueprint,
        const authoring::AuthoringSemanticRegistry& semantics) const;

private:
    MapCompositionProfile profile_;
};

[[nodiscard]] const char* toString(RoomSide side) noexcept;
[[nodiscard]] const char* toString(RoomCellKind kind) noexcept;
[[nodiscard]] std::vector<core::TileCoord> openingCells(
    const RoomBlueprint& room);

} // namespace underworld::game::maps
