#pragma once

#include "engine/simulation/entity_handle.h"
#include "engine/simulation/events.h"
#include "engine/simulation/player_command.h"
#include "game/gameplay/player.h"
#include "game/maps/map_catalog.h"

#include <memory>
#include <string>

namespace underworld::game {

// Authoritative gameplay slice. This type deliberately has no dependency on
// rendering, platform input, assets or presentation state.
class GameSession final {
public:
    GameSession(simulation::EntityHandlePool& handles, simulation::PlayerId playerId,
                core::WorldPointI initialPosition = {});

    [[nodiscard]] bool initializeMap(const maps::MapCatalog& maps,
                                      const maps::MapValidationCatalogs& catalogs,
                                      const maps::RuntimeWorldBuilder& builder,
                                      simulation::EntityHandlePool& handles,
                                      const simulation::MapId& mapId,
                                      const simulation::SpawnId& spawnId,
                                      std::string& error);
    void tick(const simulation::PlayerCommand& command);

    [[nodiscard]] const gameplay::Player& player() const noexcept { return player_; }
    [[nodiscard]] gameplay::Player& playerForRuntime() noexcept { return player_; }
    [[nodiscard]] const simulation::EventBuffer& events() const noexcept { return events_; }
    [[nodiscard]] simulation::EventBuffer& eventsForRuntime() noexcept { return events_; }
    [[nodiscard]] const maps::RuntimeWorld& world() const noexcept;
    [[nodiscard]] maps::RuntimeWorld& worldForRuntime() noexcept;
    [[nodiscard]] const maps::MapData& mapData() const;
    [[nodiscard]] const save::SessionWorldState& worldState() const noexcept { return worldState_; }
    [[nodiscard]] save::SessionWorldState& worldStateForRuntime() noexcept { return worldState_; }
    [[nodiscard]] bool restoreMap(const simulation::MapId& mapId,
                                  const save::SessionWorldState& state, std::string& error);

private:
    gameplay::Player player_;
    simulation::EventBuffer events_;
    save::SessionWorldState worldState_;
    std::unique_ptr<maps::MapSession> mapSession_;
};

} // namespace underworld::game
