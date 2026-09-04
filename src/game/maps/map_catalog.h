#pragma once

#include "game/maps/dmap.h"
#include "game/maps/runtime_world.h"
#include "game/save/save_data.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace underworld::game::maps {

class MapCatalog final {
public:
    void add(simulation::MapId id, std::filesystem::path path);
    [[nodiscard]] const std::filesystem::path* find(const simulation::MapId& id) const noexcept;
    [[nodiscard]] DmapLoadResult load(const simulation::MapId& id,
                                      const MapValidationCatalogs* catalogs = nullptr) const;
    [[nodiscard]] std::string validateLinks(const MapValidationCatalogs* catalogs = nullptr) const;
private:
    std::unordered_map<simulation::MapId, std::filesystem::path, simulation::MapIdHash> paths_;
};

struct PendingMapTransition final {
    simulation::MapId targetMapId{};
    simulation::SpawnId targetSpawnId{};
};

struct TransitionResult final {
    bool changed{};
    PlayerSpawn spawn{};
    std::string error;
};

class MapSession final {
public:
    MapSession(const MapCatalog& maps, const MapValidationCatalogs& catalogs,
               const RuntimeWorldBuilder& builder, simulation::EntityHandlePool& handles,
               save::SessionWorldState& state)
        : maps_(maps), catalogs_(catalogs), builder_(builder), handles_(handles), state_(state) {}
    [[nodiscard]] TransitionResult activate(const simulation::MapId& mapId,
                                            const simulation::SpawnId& spawnId);
    [[nodiscard]] bool requestTransition(world::AabbI playerArea);
    [[nodiscard]] TransitionResult commitPending();
    void beginTick() noexcept { if (transitionLatch_ > 0) --transitionLatch_; }
    [[nodiscard]] RuntimeWorld* world() noexcept { return world_.get(); }
    [[nodiscard]] const RuntimeWorld* world() const noexcept { return world_.get(); }
    [[nodiscard]] const MapData* data() const noexcept { return data_ ? &*data_ : nullptr; }
    [[nodiscard]] const std::optional<PendingMapTransition>& pending() const noexcept { return pending_; }
private:
    [[nodiscard]] TransitionResult prepareAndSwap(const simulation::MapId& mapId,
                                                  const simulation::SpawnId& spawnId);
    void destroyRuntimeHandles(RuntimeWorld& world) noexcept;
    const MapCatalog& maps_;
    MapValidationCatalogs catalogs_;
    const RuntimeWorldBuilder& builder_;
    simulation::EntityHandlePool& handles_;
    save::SessionWorldState& state_;
    std::optional<MapData> data_;
    std::unique_ptr<RuntimeWorld> world_;
    std::optional<PendingMapTransition> pending_;
    unsigned transitionLatch_{};
};

} // namespace underworld::game::maps
