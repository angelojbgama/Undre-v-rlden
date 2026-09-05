#pragma once

#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/dialogue/dialogue_model.h"
#include "game/gameplay/items.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/tilesets.h"
#include "game/authoring/authoring_semantics.h"

#include <string>
#include <vector>

namespace underworld::game::maps { struct MapValidationCatalogs; }

namespace underworld::game {

enum class AuthoringCategory { enemy, object, pickup, npc };

struct AuthoringDescriptor final {
    simulation::DefinitionId definitionId{};
    std::string displayName;
    AuthoringCategory category{AuthoringCategory::enemy};
    std::vector<std::string> tags;
};

class GameContentRegistry final {
public:
    GameContentRegistry();

    [[nodiscard]] gameplay::AttackCatalog& attacks() noexcept { return attacks_; }
    [[nodiscard]] const gameplay::AttackCatalog& attacks() const noexcept { return attacks_; }
    [[nodiscard]] gameplay::ProjectileCatalog& projectiles() noexcept { return projectiles_; }
    [[nodiscard]] const gameplay::ProjectileCatalog& projectiles() const noexcept {
        return projectiles_;
    }
    [[nodiscard]] gameplay::creatures::BehaviorCatalog& behaviors() noexcept {
        return behaviors_;
    }
    [[nodiscard]] const gameplay::creatures::BehaviorCatalog& behaviors() const noexcept {
        return behaviors_;
    }
    [[nodiscard]] gameplay::creatures::EnemyCatalog& enemies() noexcept { return enemies_; }
    [[nodiscard]] const gameplay::creatures::EnemyCatalog& enemies() const noexcept {
        return enemies_;
    }
    [[nodiscard]] gameplay::ItemCatalog& items() noexcept { return items_; }
    [[nodiscard]] const gameplay::ItemCatalog& items() const noexcept { return items_; }
    [[nodiscard]] gameplay::WorldObjectCatalog& objects() noexcept { return objects_; }
    [[nodiscard]] const gameplay::WorldObjectCatalog& objects() const noexcept {
        return objects_;
    }
    [[nodiscard]] gameplay::npcs::NpcCatalog& npcs() noexcept { return npcs_; }
    [[nodiscard]] const gameplay::npcs::NpcCatalog& npcs() const noexcept { return npcs_; }
    [[nodiscard]] gameplay::npcs::NpcVisualCatalog& npcVisuals() noexcept { return npcVisuals_; }
    [[nodiscard]] const gameplay::npcs::NpcVisualCatalog& npcVisuals() const noexcept {
        return npcVisuals_;
    }
    [[nodiscard]] gameplay::dialogue::DialogueCatalog& dialogues() noexcept {
        return dialogues_;
    }
    [[nodiscard]] const gameplay::dialogue::DialogueCatalog& dialogues() const noexcept {
        return dialogues_;
    }
    [[nodiscard]] TilesetCatalog& tilesets() noexcept { return tilesets_; }
    [[nodiscard]] const TilesetCatalog& tilesets() const noexcept { return tilesets_; }
    [[nodiscard]] const authoring::AuthoringSemanticRegistry& authoringSemantics() const noexcept {
        return authoringSemantics_;
    }

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
    gameplay::AttackCatalog attacks_;
    gameplay::ProjectileCatalog projectiles_;
    gameplay::creatures::BehaviorCatalog behaviors_;
    gameplay::creatures::EnemyCatalog enemies_;
    gameplay::ItemCatalog items_;
    gameplay::WorldObjectCatalog objects_;
    gameplay::npcs::NpcCatalog npcs_;
    gameplay::npcs::NpcVisualCatalog npcVisuals_;
    gameplay::dialogue::DialogueCatalog dialogues_;
    TilesetCatalog tilesets_;
    authoring::AuthoringSemanticRegistry authoringSemantics_;
    std::vector<gameplay::PickupDefinition> pickups_;
    std::vector<AuthoringDescriptor> authoringDescriptors_;
};

[[nodiscard]] maps::MapValidationCatalogs mapValidationCatalogs(
    const GameContentRegistry& content) noexcept;

} // namespace underworld::game
