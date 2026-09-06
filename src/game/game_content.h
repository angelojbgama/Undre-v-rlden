#pragma once

#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/dialogue/dialogue_model.h"
#include "game/gameplay/items.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/gameplay/quests/quest_model.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/tilesets.h"
#include "game/authoring/authoring_semantics.h"
#include "game/gameplay/rpg/player_progression.h"
#include "game/gameplay/rpg/rewards.h"
#include "game/gameplay/rpg/reward_grants.h"
#include "game/gameplay/rpg/shops.h"
#include "game/content/content_dto.h"

#include <string>
#include <vector>

namespace underworld::game::maps { struct MapValidationCatalogs; }
namespace underworld::game::content { class ContentCompiler; }

namespace underworld::game {

using content::AuthoringCategory;
using content::AuthoringDescriptor;

class GameContentRegistry final {
public:
    GameContentRegistry() = default;

    [[nodiscard]] const gameplay::AttackCatalog& attacks() const noexcept { return attacks_; }
    [[nodiscard]] const gameplay::ProjectileCatalog& projectiles() const noexcept {
        return projectiles_;
    }
    [[nodiscard]] const gameplay::creatures::BehaviorCatalog& behaviors() const noexcept {
        return behaviors_;
    }
    [[nodiscard]] const gameplay::creatures::EnemyCatalog& enemies() const noexcept {
        return enemies_;
    }
    [[nodiscard]] const gameplay::ItemCatalog& items() const noexcept { return items_; }
    [[nodiscard]] const gameplay::WorldObjectCatalog& objects() const noexcept {
        return objects_;
    }
    [[nodiscard]] const gameplay::npcs::NpcCatalog& npcs() const noexcept { return npcs_; }
    [[nodiscard]] const gameplay::npcs::NpcVisualCatalog& npcVisuals() const noexcept {
        return npcVisuals_;
    }
    [[nodiscard]] const gameplay::dialogue::DialogueCatalog& dialogues() const noexcept {
        return dialogues_;
    }
    [[nodiscard]] const gameplay::quests::QuestCatalog& quests() const noexcept {
        return quests_;
    }
    [[nodiscard]] const TilesetCatalog& tilesets() const noexcept { return tilesets_; }
    [[nodiscard]] const authoring::AuthoringSemanticRegistry& authoringSemantics() const noexcept {
        return authoringSemantics_;
    }
    [[nodiscard]] const gameplay::rpg::PlayerProgressionCatalog& progressions() const noexcept { return progressions_; }
    [[nodiscard]] const gameplay::rpg::RewardProfileCatalog& rewardProfiles() const noexcept { return rewards_; }
    [[nodiscard]] const gameplay::rpg::RewardProfileCatalog& rewards() const noexcept { return rewards_; }
    [[nodiscard]] const gameplay::rpg::RewardGrantCatalog& rewardGrants() const noexcept { return rewardGrants_; }
    [[nodiscard]] const gameplay::rpg::ShopCatalog& shops() const noexcept { return shops_; }

    [[nodiscard]] const gameplay::PickupDefinition* pickup(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const std::vector<gameplay::PickupDefinition>& pickups() const noexcept {
        return pickups_;
    }
    [[nodiscard]] const std::vector<AuthoringDescriptor>& authoringDescriptors() const noexcept {
        return authoringDescriptors_;
    }
    [[nodiscard]] std::vector<const AuthoringDescriptor*> authoringDescriptors(
        AuthoringCategory category) const;

private:
    friend class content::ContentCompiler;
    gameplay::AttackCatalog attacks_;
    gameplay::ProjectileCatalog projectiles_;
    gameplay::creatures::BehaviorCatalog behaviors_;
    gameplay::creatures::EnemyCatalog enemies_;
    gameplay::ItemCatalog items_;
    gameplay::WorldObjectCatalog objects_;
    gameplay::npcs::NpcCatalog npcs_;
    gameplay::npcs::NpcVisualCatalog npcVisuals_;
    gameplay::dialogue::DialogueCatalog dialogues_;
    gameplay::quests::QuestCatalog quests_;
    TilesetCatalog tilesets_;
    authoring::AuthoringSemanticRegistry authoringSemantics_;
    gameplay::rpg::PlayerProgressionCatalog progressions_;
    gameplay::rpg::RewardProfileCatalog rewards_;
    gameplay::rpg::RewardGrantCatalog rewardGrants_;
    gameplay::rpg::ShopCatalog shops_;
    std::vector<gameplay::PickupDefinition> pickups_;
    std::vector<AuthoringDescriptor> authoringDescriptors_;
};

[[nodiscard]] maps::MapValidationCatalogs mapValidationCatalogs(
    const GameContentRegistry& content) noexcept;

} // namespace underworld::game
