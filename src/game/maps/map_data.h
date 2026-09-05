#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/persistent_id.h"
#include "engine/world/collision.h"
#include "engine/world/tile.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/facing_direction.h"
#include "game/gameplay/items.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/tilesets.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace underworld::game::maps {

struct MapLimits final {
    static constexpr std::uint32_t maximumDimension = 4096;
    static constexpr std::uint32_t maximumLayers = 64;
    static constexpr std::uint32_t maximumTileReferences = 65536;
    static constexpr std::uint32_t maximumPlacements = 100000;
    static constexpr std::uint32_t maximumStrings = 100000;
    static constexpr std::uint32_t maximumStringBytes = 4096;
    static constexpr std::uint64_t maximumChunkBytes = 64ULL * 1024ULL * 1024ULL;
    static constexpr std::uint64_t maximumFileBytes = 256ULL * 1024ULL * 1024ULL;
};

struct MapTileReference final {
    simulation::DefinitionId tilesetId{};
    std::uint32_t sourceIndex{};
    world::TileFlags flags{world::TileFlags::none};
    [[nodiscard]] bool operator==(const MapTileReference&) const noexcept = default;
};

struct MapTileLayer final {
    std::string name;
    bool visible{true};
    std::vector<std::optional<std::uint32_t>> cells;
    [[nodiscard]] bool operator==(const MapTileLayer&) const noexcept = default;
};

struct PlayerSpawn final {
    simulation::SpawnId id{};
    core::WorldPointI position{};
    gameplay::FacingDirection facing{gameplay::FacingDirection::down};
    [[nodiscard]] bool operator==(const PlayerSpawn&) const noexcept = default;
};

struct EnemyPlacement final {
    simulation::PersistentInstanceId id{};
    simulation::DefinitionId definitionId{};
    core::WorldPointI position{};
    gameplay::FacingDirection facing{gameplay::FacingDirection::down};
    [[nodiscard]] bool operator==(const EnemyPlacement&) const noexcept = default;
};

struct NpcPlacement final {
    simulation::PersistentInstanceId id{};
    simulation::DefinitionId definitionId{};
    core::WorldPointI position{};
    gameplay::FacingDirection facing{gameplay::FacingDirection::down};
    [[nodiscard]] bool operator==(const NpcPlacement&) const noexcept = default;
};

struct ObjectPlacement final {
    simulation::PersistentInstanceId id{};
    simulation::DefinitionId definitionId{};
    core::WorldPointI position{};
    std::vector<gameplay::ItemStack> initialContents;
};

struct PickupPlacement final {
    simulation::PersistentInstanceId id{};
    simulation::DefinitionId definitionId{};
    simulation::DefinitionId visualId{};
    core::WorldPointI position{};
    world::AabbI collectionBounds{};
    gameplay::PickupPayload payload{};
};

struct MapLink final {
    std::string id;
    world::AabbI trigger{};
    simulation::MapId targetMapId{};
    simulation::SpawnId targetSpawnId{};
    [[nodiscard]] bool operator==(const MapLink&) const noexcept = default;
};

struct MapData final {
    simulation::MapId id{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint16_t tileSize{};
    std::vector<MapTileReference> tileReferences;
    std::vector<MapTileLayer> layers;
    std::vector<std::uint8_t> collision;
    std::vector<PlayerSpawn> playerSpawns;
    std::vector<EnemyPlacement> enemies;
    std::vector<NpcPlacement> npcs;
    std::vector<ObjectPlacement> objects;
    std::vector<PickupPlacement> pickups;
    std::vector<MapLink> links;
};

struct MapValidationCatalogs final {
    const gameplay::creatures::EnemyCatalog* enemies{};
    const gameplay::WorldObjectCatalog* objects{};
    const gameplay::ItemCatalog* items{};
    const TilesetCatalog* tilesets{};
    const gameplay::npcs::NpcCatalog* npcs{};
    const gameplay::npcs::NpcVisualCatalog* npcVisuals{};
};

struct MapValidationResult final {
    bool valid{};
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return valid; }
};

[[nodiscard]] MapValidationResult validateMapData(
    const MapData& data, const MapValidationCatalogs* catalogs = nullptr);
[[nodiscard]] bool semanticallyEqual(const MapData& left, const MapData& right) noexcept;

} // namespace underworld::game::maps
