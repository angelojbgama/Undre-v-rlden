#include "game/gameplay/encounter_system.h"

#include <algorithm>

namespace underworld::game::gameplay {

const EncounterRuntimeState* EncounterSystem::find(
    const std::vector<EncounterRuntimeState>& states,
    const simulation::MapId& mapId,
    const simulation::DefinitionId& encounterId) const noexcept {
    const auto found = std::find_if(states.begin(), states.end(), [&](const auto& value) {
        return value.mapId == mapId && value.encounterId == encounterId;
    });
    return found == states.end() ? nullptr : &*found;
}

EncounterState EncounterSystem::state(
    const std::vector<EncounterRuntimeState>& states,
    const simulation::MapId& mapId,
    const simulation::DefinitionId& encounterId) const noexcept {
    const auto* value = find(states, mapId, encounterId);
    return value == nullptr ? EncounterState::inactive : value->state;
}

bool EncounterSystem::start(const std::vector<maps::EncounterDefinition>& definitions,
                            const simulation::MapId& mapId,
                            const simulation::DefinitionId& encounterId,
                            std::vector<EncounterRuntimeState>& states,
                            simulation::EventBuffer& events) const {
    const auto definition = std::find_if(definitions.begin(), definitions.end(),
        [&](const auto& value) { return value.id == encounterId; });
    if (definition == definitions.end()) return false;
    auto found = std::find_if(states.begin(), states.end(), [&](const auto& value) {
        return value.mapId == mapId && value.encounterId == encounterId;
    });
    if (found == states.end()) {
        states.push_back({mapId, encounterId, EncounterState::active, false});
        events.emit(simulation::EncounterStarted{mapId, encounterId});
        return true;
    }
    // Starting an already active or completed encounter is a valid idempotent
    // no-op. Unknown encounter IDs remain an error.
    if (found->state != EncounterState::inactive) return true;
    found->state = EncounterState::active;
    events.emit(simulation::EncounterStarted{mapId, encounterId});
    return true;
}

void EncounterSystem::evaluate(
    const std::vector<maps::EncounterDefinition>& definitions,
    const simulation::MapId& mapId,
    std::span<const simulation::PersistentInstanceId> aliveParticipants,
    std::vector<EncounterRuntimeState>& states,
    simulation::EventBuffer& events) const {
    for (const auto& definition : definitions) {
        auto found = std::find_if(states.begin(), states.end(), [&](const auto& value) {
            return value.mapId == mapId && value.encounterId == definition.id;
        });
        if (found == states.end() || found->state != EncounterState::active) continue;
        const bool complete = std::all_of(definition.participants.begin(),
            definition.participants.end(), [&](const auto id) {
                return std::none_of(aliveParticipants.begin(), aliveParticipants.end(),
                                    [&](const auto alive) { return alive == id; });
            });
        if (!complete) continue;
        found->state = EncounterState::completed;
        events.emit(simulation::EncounterCompleted{mapId, definition.id});
    }
}

} // namespace underworld::game::gameplay
