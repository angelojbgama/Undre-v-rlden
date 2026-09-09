#pragma once

#include "engine/simulation/events.h"
#include "game/gameplay/dialogue/dialogue_flags.h"
#include "game/maps/map_data.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace underworld::game::gameplay {

struct WorldRuleState final {
    simulation::MapId mapId{};
    simulation::DefinitionId ruleId{};
    bool fired{};
    [[nodiscard]] bool operator==(const WorldRuleState&) const noexcept = default;
};

struct WorldLogicRuntime final {
    std::function<bool(const simulation::DefinitionId&)> encounterCompleted;
    std::function<bool(const simulation::DefinitionId&, simulation::EventBuffer&)> startEncounter;
    std::function<bool(simulation::PersistentInstanceId, maps::DoorState)> setDoorState;
    std::function<std::optional<maps::DoorState>(simulation::PersistentInstanceId)> doorState;
    std::function<std::optional<bool>(simulation::PersistentInstanceId)> objectActivation{};
    std::function<bool(const simulation::DefinitionId&)> requestScene{};
};

[[nodiscard]] bool executeWorldAction(const maps::WorldAction& action,
                                      const simulation::MapId& mapId,
                                      dialogue::DialogueFlagSet& flags,
                                      simulation::EventBuffer& events,
                                      const WorldLogicRuntime& runtime);

class WorldLogicSystem final {
public:
    void reset() noexcept;
    bool consume(const std::vector<maps::WorldRuleDefinition>& rules,
                 const simulation::MapId& mapId,
                 dialogue::DialogueFlagSet& flags,
                 simulation::EventBuffer& events,
                 std::vector<WorldRuleState>& persistentState,
                 const WorldLogicRuntime& runtime = {},
                 std::size_t firstEventIndex = 0);

private:
    bool matches(const maps::WorldRuleDefinition& rule, const simulation::SimulationEvent& event,
                 const simulation::MapId& mapId, const dialogue::DialogueFlagSet& flags,
                 const WorldLogicRuntime& runtime) const;
};

} // namespace underworld::game::gameplay
