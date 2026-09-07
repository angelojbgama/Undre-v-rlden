#pragma once

#include "engine/simulation/events.h"
#include "game/maps/map_data.h"

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace underworld::game::gameplay {

enum class EncounterState { inactive, active, completed };

struct EncounterRuntimeState final {
    simulation::MapId mapId{};
    simulation::DefinitionId encounterId{};
    EncounterState state{EncounterState::inactive};
    bool rewardClaimed{};
    [[nodiscard]] bool operator==(const EncounterRuntimeState&) const noexcept = default;
};

class EncounterSystem final {
public:
    [[nodiscard]] const EncounterRuntimeState* find(
        const std::vector<EncounterRuntimeState>& states,
        const simulation::MapId& mapId,
        const simulation::DefinitionId& encounterId) const noexcept;

    [[nodiscard]] EncounterState state(
        const std::vector<EncounterRuntimeState>& states,
        const simulation::MapId& mapId,
        const simulation::DefinitionId& encounterId) const noexcept;

    [[nodiscard]] bool start(const std::vector<maps::EncounterDefinition>& definitions,
                             const simulation::MapId& mapId,
                             const simulation::DefinitionId& encounterId,
                             std::vector<EncounterRuntimeState>& states,
                             simulation::EventBuffer& events) const;

    void evaluate(const std::vector<maps::EncounterDefinition>& definitions,
                  const simulation::MapId& mapId,
                  std::span<const simulation::PersistentInstanceId> aliveParticipants,
                  std::vector<EncounterRuntimeState>& states,
                  simulation::EventBuffer& events) const;
};

} // namespace underworld::game::gameplay
