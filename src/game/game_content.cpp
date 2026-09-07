#include "game/game_content.h"

#include "game/maps/map_data.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game {

const gameplay::PickupDefinition* GameContentRegistry::pickup(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = std::find_if(pickups_.begin(), pickups_.end(), [&](const auto& value) {
        return value.id == id;
    });
    return found == pickups_.end() ? nullptr : &*found;
}

std::vector<const AuthoringDescriptor*> GameContentRegistry::authoringDescriptors(
    AuthoringCategory category) const {
    std::vector<const AuthoringDescriptor*> result;
    for (const auto& descriptor : authoringDescriptors_) {
        if (descriptor.category == category) { result.push_back(&descriptor); }
    }
    return result;
}

maps::MapValidationCatalogs mapValidationCatalogs(
    const GameContentRegistry& content) noexcept {
    return {&content.enemies(), &content.objects(), &content.items(), &content.tilesets(),
            &content.npcs(), &content.rewardGrants(), &content.presentationEffects()};
}

} // namespace underworld::game
