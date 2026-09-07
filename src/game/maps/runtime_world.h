#pragma once

#include "engine/simulation/persistent_id.h"
#include "engine/simulation/events.h"
#include "engine/world/runtime_map.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/maps/map_data.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace underworld::game::maps {

template<class Instance>
struct PersistentRuntimeInstance final {
    simulation::PersistentInstanceId persistentId{};
    Instance instance;
};

using PersistentEnemy = PersistentRuntimeInstance<gameplay::creatures::EnemyInstance>;
using PersistentNpc = PersistentRuntimeInstance<gameplay::npcs::NpcInstance>;
using PersistentObject = PersistentRuntimeInstance<gameplay::WorldObjectInstance>;
struct PersistentPickup final {
    simulation::PersistentInstanceId persistentId{};
    bool transient{};
    gameplay::WorldPickup instance;
};

struct RuntimeDoorCell final {
    int x{};
    int y{};
    bool baseSolid{};
};
struct RuntimeDoor final {
    simulation::PersistentInstanceId id{};
    gameplay::DoorState state{gameplay::DoorState::closed};
    std::vector<RuntimeDoorCell> cells;
};

class RuntimeWorld final {
public:
    RuntimeWorld(simulation::MapId id, world::RuntimeMap map, PlayerSpawn spawn)
        : id_(std::move(id)), map_(std::move(map)), spawn_(std::move(spawn)) {}
    RuntimeWorld(const RuntimeWorld&) = delete;
    RuntimeWorld& operator=(const RuntimeWorld&) = delete;
    RuntimeWorld(RuntimeWorld&&) noexcept = default;
    RuntimeWorld& operator=(RuntimeWorld&&) noexcept = default;

    [[nodiscard]] const simulation::MapId& id() const noexcept { return id_; }
    [[nodiscard]] world::RuntimeMap& map() noexcept { return map_; }
    [[nodiscard]] const world::RuntimeMap& map() const noexcept { return map_; }
    [[nodiscard]] const PlayerSpawn& spawn() const noexcept { return spawn_; }
    [[nodiscard]] std::vector<PersistentEnemy>& enemies() noexcept { return enemies_; }
    [[nodiscard]] std::vector<PersistentNpc>& npcs() noexcept { return npcs_; }
    [[nodiscard]] std::vector<PersistentObject>& objects() noexcept { return objects_; }
    [[nodiscard]] std::vector<PersistentPickup>& pickups() noexcept { return pickups_; }
    [[nodiscard]] const std::vector<PersistentEnemy>& enemies() const noexcept { return enemies_; }
    [[nodiscard]] const std::vector<PersistentNpc>& npcs() const noexcept { return npcs_; }
    [[nodiscard]] const std::vector<PersistentObject>& objects() const noexcept { return objects_; }
    [[nodiscard]] const std::vector<PersistentPickup>& pickups() const noexcept { return pickups_; }
    [[nodiscard]] bool setDoorState(simulation::PersistentInstanceId id,
                                     gameplay::DoorState state) noexcept;
    [[nodiscard]] std::optional<gameplay::DoorState> doorState(
        simulation::PersistentInstanceId id) const noexcept;
    [[nodiscard]] bool interactDoor(simulation::PersistentInstanceId id) noexcept;
    [[nodiscard]] bool setObjectActivation(simulation::PersistentInstanceId id, bool active) noexcept;
    [[nodiscard]] std::optional<bool> objectActivation(
        simulation::PersistentInstanceId id) const noexcept;
    [[nodiscard]] bool toggleObjectActivation(simulation::PersistentInstanceId id) noexcept;
    void updatePressureActivations(core::WorldPointI playerFeet,
                                   simulation::EventBuffer& events) noexcept;
    [[nodiscard]] const std::vector<RuntimeDoor>& doors() const noexcept { return doors_; }

private:
    friend class RuntimeWorldBuilder;
    simulation::MapId id_;
    world::RuntimeMap map_;
    PlayerSpawn spawn_;
    std::vector<gameplay::PickupDefinition> pickupDefinitions_;
    std::vector<PersistentEnemy> enemies_;
    std::vector<PersistentNpc> npcs_;
    std::vector<PersistentObject> objects_;
    std::vector<PersistentPickup> pickups_;
    std::vector<RuntimeDoor> doors_;
};

struct RuntimeWorldBuildResult final {
    std::unique_ptr<RuntimeWorld> world;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return world != nullptr; }
};

class RuntimeWorldBuilder final {
public:
    RuntimeWorldBuilder(const MapValidationCatalogs& catalogs,
                        const gameplay::creatures::EnemyFactory& enemies,
                        const gameplay::WorldObjectFactory& objects,
                        const RuntimeTilesetCatalog& tilesets,
                        const gameplay::npcs::NpcFactory* npcs = nullptr)
        : catalogs_(catalogs), enemyFactory_(enemies), objectFactory_(objects),
          tilesets_(tilesets), npcFactory_(npcs) {}
    RuntimeWorldBuilder(const MapValidationCatalogs& catalogs,
                        const gameplay::creatures::EnemyFactory& enemies,
                        const gameplay::WorldObjectFactory& objects,
                        simulation::EntityHandlePool& handles,
                        const RuntimeTilesetCatalog& tilesets,
                        const gameplay::npcs::NpcFactory* npcs = nullptr)
        : catalogs_(catalogs), enemyFactory_(enemies), objectFactory_(objects),
          legacyHandles_(&handles), tilesets_(tilesets), npcFactory_(npcs) {}
    [[nodiscard]] RuntimeWorldBuildResult build(const MapData& data,
                                                simulation::EntityHandlePool& handles,
                                                const simulation::SpawnId& spawnId) const;
    [[nodiscard]] RuntimeWorldBuildResult build(const MapData& data,
                                                const simulation::SpawnId& spawnId) const;
private:
    MapValidationCatalogs catalogs_;
    const gameplay::creatures::EnemyFactory& enemyFactory_;
    const gameplay::WorldObjectFactory& objectFactory_;
    simulation::EntityHandlePool* legacyHandles_{};
    RuntimeTilesetCatalog tilesets_;
    const gameplay::npcs::NpcFactory* npcFactory_{};
};

} // namespace underworld::game::maps
