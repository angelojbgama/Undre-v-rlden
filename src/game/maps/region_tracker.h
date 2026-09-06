#pragma once

#include "engine/simulation/events.h"
#include "game/maps/map_data.h"

#include <unordered_set>

namespace underworld::game::maps {

class RegionTracker final {
public:
    void reset() noexcept { mapId_ = {}; inside_.clear(); initialized_ = false; }
    void update(const simulation::MapId& mapId, const std::vector<MapRegionDefinition>& regions,
                core::WorldPointI feet, simulation::EventBuffer& events);
private:
    simulation::MapId mapId_{};
    std::unordered_set<simulation::DefinitionId, simulation::DefinitionIdHash> inside_;
    bool initialized_{};
};

} // namespace underworld::game::maps
