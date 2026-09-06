#include "game/game_content.h"

#include "game/maps/map_data.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game {

void GameContentRegistry::addCompiled(content::AuthoredContentPack pack) {
    for (auto& value : pack.tilesets) tilesets_.add(std::move(value));
    for (auto& value : pack.projectiles) projectiles_.add(std::move(value));
    for (auto& value : pack.attacks) {
        const auto id = value.id;
        try { attacks_.add(std::move(value)); }
        catch (const std::exception& exception) {
            throw std::logic_error("attack " + std::string(id.value()) + ": " + exception.what());
        }
    }
    for (auto& value : pack.behaviors) behaviors_.add(std::move(value));
    for (auto& value : pack.enemies) enemies_.add(std::move(value));
    for (auto& value : pack.items) items_.add(std::move(value));
    for (auto& value : pack.objects) objects_.add(std::move(value));
    for (auto& value : pack.npcs) npcs_.add(std::move(value));
    for (auto& value : pack.npcVisuals) npcVisuals_.add(std::move(value));
    for (auto& value : pack.dialogues) dialogues_.add(std::move(value));
    for (auto& value : pack.quests) quests_.add(std::move(value));
    for (auto& value : pack.pickups) pickups_.push_back(std::move(value));
    authoringDescriptors_ = std::move(pack.authoringDescriptors);
    for (auto& value : pack.tileSemantics) authoringSemantics_.addTile(std::move(value));
    for (auto& value : pack.stamps) authoringSemantics_.addStamp(std::move(value));
}

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
            &content.npcs()};
}

} // namespace underworld::game
