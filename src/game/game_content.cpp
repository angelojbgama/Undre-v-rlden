#include "game/game_content.h"

#include "game/maps/map_data.h"

#include <algorithm>

namespace underworld::game {

GameContentRegistry::GameContentRegistry() {
    tilesets_.add({simulation::DefinitionId{"tileset.dungeon"}, "Dungeon",
                   "Tileset/tileset.png", 16, 19, 12});
    attacks_.add(gameplay::creatures::makeSoldierSwordAttackDefinition());
    attacks_.add(gameplay::creatures::makeSkullArrowAttackDefinition());
    projectiles_.add(gameplay::creatures::makeSkullArrowProjectileDefinition());
    behaviors_.add(gameplay::creatures::makeSoldierBehaviorProfile());
    behaviors_.add(gameplay::creatures::makeSkullBehaviorProfile());
    enemies_.add(gameplay::creatures::makeSoldierEnemyDefinition());
    enemies_.add(gameplay::creatures::makeSkullEnemyDefinition());
    dialogues_.add(gameplay::dialogue::makeGuardDialogueDefinition());
    dialogues_.add(gameplay::dialogue::makeScholarDialogueDefinition());
    quests_.add(gameplay::quests::makeScholarQuestDefinition());
    npcs_.add(gameplay::npcs::makeGuardNpcDefinition());
    npcs_.add(gameplay::npcs::makeScholarNpcDefinition());
    npcVisuals_.add({simulation::DefinitionId{"visual.npc.guard"}, {70, 150, 240, 255}});
    npcVisuals_.add({simulation::DefinitionId{"visual.npc.scholar"}, {220, 180, 70, 255}});

    items_.add(gameplay::makeLifePotionDefinition());

    const simulation::DefinitionId chestVisualId{"visual.object.chest"};
    const simulation::DefinitionId crateVisualId{"visual.object.crate"};
    objects_.add({simulation::DefinitionId{"object.chest"}, chestVisualId,
        gameplay::ObjectInteractionDefinition{{-14, -18, 28, 22}},
        gameplay::ObjectContainerDefinition{5}, std::nullopt});
    objects_.add({simulation::DefinitionId{"object.crate"}, crateVisualId,
        std::nullopt, std::nullopt,
        gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}}});

    pickups_.push_back({simulation::DefinitionId{"pickup.heart"},
        simulation::DefinitionId{"visual.pickup.heart"}, {-5, -5, 10, 10},
        gameplay::HealthPickup{2}});
    pickups_.push_back({simulation::DefinitionId{"pickup.money"},
        simulation::DefinitionId{"visual.pickup.money"}, {-5, -5, 10, 10},
        gameplay::CurrencyPickup{1}});
    pickups_.push_back({simulation::DefinitionId{"pickup.life_potion"},
        simulation::DefinitionId{"visual.item.life_potion"}, {-5, -5, 10, 10},
        gameplay::ItemPickup{gameplay::lifePotionItemId(), 1}});

    authoringDescriptors_ = {
        {gameplay::creatures::soldierEnemyId(), "Evil Soldier", AuthoringCategory::enemy,
         {"melee", "hostile"}},
        {gameplay::creatures::skullEnemyId(), "Skull", AuthoringCategory::enemy,
         {"ranged", "hostile"}},
        {simulation::DefinitionId{"object.chest"}, "Chest", AuthoringCategory::object,
         {"container", "interactable"}},
        {simulation::DefinitionId{"object.crate"}, "Crate", AuthoringCategory::object,
         {"destructible"}},
        {simulation::DefinitionId{"pickup.heart"}, "Heart", AuthoringCategory::pickup,
         {"health"}},
        {simulation::DefinitionId{"pickup.money"}, "Money", AuthoringCategory::pickup,
         {"currency"}},
        {simulation::DefinitionId{"pickup.life_potion"}, "Life Potion",
         AuthoringCategory::pickup, {"item", "consumable"}},
        {gameplay::npcs::guardNpcId(), "Guard", AuthoringCategory::npc,
         {"npc", "dialogue"}},
        {gameplay::npcs::scholarNpcId(), "Scholar", AuthoringCategory::npc,
         {"npc", "dialogue"}},
    };
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
            &content.npcs(), &content.npcVisuals()};
}

} // namespace underworld::game
