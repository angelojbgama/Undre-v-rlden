#include "game/content/builtin_content.h"

#include "game/content/content_compiler.h"

#include <stdexcept>
#include <utility>

namespace underworld::game::content {

AuthoredContentPack makeBuiltinAuthoredContent() {
    AuthoredContentPack pack;
    pack.tilesets.push_back({simulation::DefinitionId{"tileset.dungeon"}, "Dungeon",
                             "Tileset/tileset.png", 16, 19, 12});
    pack.attacks = {
        gameplay::makePlayerSwordAttackDefinition(), gameplay::makePlayerBowAttackDefinition(),
        gameplay::creatures::makeSoldierSwordAttackDefinition(),
        gameplay::creatures::makeSkullArrowAttackDefinition()};
    pack.projectiles = {gameplay::makePlayerArrowProjectileDefinition(),
                        gameplay::creatures::makeSkullArrowProjectileDefinition()};
    pack.behaviors = {gameplay::creatures::makeSoldierBehaviorProfile(),
                      gameplay::creatures::makeSkullBehaviorProfile()};
    pack.enemies = {gameplay::creatures::makeSoldierEnemyDefinition(),
                    gameplay::creatures::makeSkullEnemyDefinition()};
    pack.dialogues = {gameplay::dialogue::makeGuardDialogueDefinition(),
                      gameplay::dialogue::makeScholarDialogueDefinition()};
    pack.quests = {gameplay::quests::makeScholarQuestDefinition()};
    pack.npcs = {gameplay::npcs::makeGuardNpcDefinition(),
                 gameplay::npcs::makeScholarNpcDefinition()};
    pack.npcVisuals = {
        {simulation::DefinitionId{"visual.npc.guard"}, {70, 150, 240, 255}},
        {simulation::DefinitionId{"visual.npc.scholar"}, {220, 180, 70, 255}}};
    pack.items = {gameplay::makeLifePotionDefinition()};
    pack.objects = {
        {simulation::DefinitionId{"object.chest"}, simulation::DefinitionId{"visual.object.chest"},
         gameplay::ObjectInteractionDefinition{{-14, -18, 28, 22}},
         gameplay::ObjectContainerDefinition{5}, std::nullopt},
        {simulation::DefinitionId{"object.crate"}, simulation::DefinitionId{"visual.object.crate"},
         std::nullopt, std::nullopt,
         gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}}}};
    pack.pickups = {
        {simulation::DefinitionId{"pickup.heart"}, simulation::DefinitionId{"visual.pickup.heart"},
         {-5, -5, 10, 10}, gameplay::HealthPickup{2}},
        {simulation::DefinitionId{"pickup.money"}, simulation::DefinitionId{"visual.pickup.money"},
         {-5, -5, 10, 10}, gameplay::CurrencyPickup{1}},
        {simulation::DefinitionId{"pickup.life_potion"},
         simulation::DefinitionId{"visual.item.life_potion"}, {-5, -5, 10, 10},
         gameplay::ItemPickup{gameplay::lifePotionItemId(), 1}}};
    pack.authoringDescriptors = {
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
         {"npc", "dialogue"}}};

    authoring::AuthoringSemanticRegistry semantics;
    authoring::addBuiltinSemantics(semantics);
    pack.tileSemantics = semantics.tiles();
    pack.stamps = semantics.stamps();
    return pack;
}

GameContentRegistry compileBuiltinContentOrThrow() {
    const auto result = compileContent(makeBuiltinAuthoredContent());
    if (!result) {
        std::string message = "builtin content compilation failed";
        for (const auto& diagnostic : result.report.diagnostics) {
            if (diagnostic.severity == ContentDiagnosticSeverity::error) {
                message += " [" + diagnostic.code + "] " + diagnostic.message;
            }
        }
        throw std::runtime_error(message);
    }
    return std::move(*result.registry);
}

} // namespace underworld::game::content
