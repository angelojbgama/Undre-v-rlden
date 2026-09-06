#pragma once

#include "engine/simulation/events.h"
#include "game/gameplay/dialogue/dialogue_flags.h"
#include "game/maps/map_data.h"

#include <unordered_set>

namespace underworld::game::gameplay {

class WorldLogicSystem final {
public:
    void reset() noexcept;
    void consume(const std::vector<maps::WorldRuleDefinition>& rules,
                 const simulation::MapId& mapId,
                 dialogue::DialogueFlagSet& flags,
                 simulation::EventBuffer& events);

private:
    bool matches(const maps::WorldRuleDefinition& rule, const simulation::SimulationEvent& event,
                 const simulation::MapId& mapId, const dialogue::DialogueFlagSet& flags) const;
    std::unordered_set<simulation::DefinitionId, simulation::DefinitionIdHash> fired_;
    simulation::MapId mapId_{};
};

} // namespace underworld::game::gameplay
