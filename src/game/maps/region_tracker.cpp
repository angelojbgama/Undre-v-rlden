#include "game/maps/region_tracker.h"

#include "game/gameplay/combat_types.h"

namespace underworld::game::maps {

void RegionTracker::update(const simulation::MapId& mapId,
                           const std::vector<MapRegionDefinition>& regions,
                           core::WorldPointI feet, simulation::EventBuffer& events) {
    if (!initialized_ || !(mapId_ == mapId)) {
        mapId_ = mapId;
        inside_.clear();
        initialized_ = true;
    }
    std::unordered_set<simulation::DefinitionId, simulation::DefinitionIdHash> current;
    for (const auto& region : regions) {
        const bool present = feet.x >= region.bounds.x && feet.x < region.bounds.x + region.bounds.width &&
            feet.y >= region.bounds.y && feet.y < region.bounds.y + region.bounds.height;
        if (!present) { continue; }
        current.insert(region.id);
        if (!inside_.contains(region.id)) events.emit(simulation::RegionEntered{mapId, region.id});
    }
    for (const auto& region : regions) {
        if (inside_.contains(region.id) && !current.contains(region.id)) {
            events.emit(simulation::RegionExited{mapId, region.id});
        }
    }
    inside_ = std::move(current);
}

} // namespace underworld::game::maps
