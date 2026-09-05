#include "game/game_session.h"

#include <stdexcept>

namespace underworld::game {

GameSession::GameSession(simulation::EntityHandlePool& handles, simulation::PlayerId playerId,
                         core::WorldPointI initialPosition)
    : player_(playerId, handles.create(), initialPosition) {}

bool GameSession::initializeMap(const maps::MapCatalog& maps,
                                const maps::MapValidationCatalogs& catalogs,
                                const maps::RuntimeWorldBuilder& builder,
                                simulation::EntityHandlePool& handles,
                                const simulation::MapId& mapId,
                                const simulation::SpawnId& spawnId,
                                std::string& error) {
    auto candidate = std::make_unique<maps::MapSession>(maps, catalogs, builder, handles,
                                                         worldState_);
    const auto activated = candidate->activate(mapId, spawnId);
    if (!activated.changed) { error = activated.error; return false; }
    player_.relocate(activated.spawn.position, activated.spawn.facing);
    mapSession_ = std::move(candidate);
    return true;
}

void GameSession::tick(const simulation::PlayerCommand& command) {
    events_.clear();
    if (!mapSession_ || !mapSession_->world() || !mapSession_->data()) { return; }
    const auto& map = mapSession_->world()->map();
    player_.update(command, map.collision(), map.tileSize());
    mapSession_->beginTick();
    static_cast<void>(mapSession_->requestTransition(player_.collisionBody()));
    if (!mapSession_->pending()) { return; }
    const auto transition = mapSession_->commitPending();
    if (transition.changed) {
        player_.relocate(transition.spawn.position, transition.spawn.facing);
        events_.emit(simulation::MapEntered{mapSession_->world()->id()});
    }
}

const maps::RuntimeWorld& GameSession::world() const noexcept { return *mapSession_->world(); }

maps::RuntimeWorld& GameSession::worldForRuntime() noexcept { return *mapSession_->world(); }

const maps::MapData& GameSession::mapData() const {
    if (!mapSession_ || !mapSession_->data()) {
        throw std::logic_error("GameSession has no active map data");
    }
    return *mapSession_->data();
}

bool GameSession::restoreMap(const simulation::MapId& mapId,
                             const save::SessionWorldState& state, std::string& error) {
    if (!mapSession_) { error = "GameSession has no map session"; return false; }
    const auto restored = mapSession_->restore(mapId, state);
    if (!restored.changed) { error = restored.error; return false; }
    player_.relocate(restored.spawn.position, restored.spawn.facing);
    return true;
}

} // namespace underworld::game
