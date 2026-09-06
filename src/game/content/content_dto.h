#pragma once

#include "game/authoring/authoring_semantics.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/dialogue/dialogue_model.h"
#include "game/gameplay/items.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/gameplay/quests/quest_model.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/tilesets.h"

#include <vector>
#include <string>

namespace underworld::game::content {

enum class AuthoringCategory { enemy, object, pickup, npc };

struct AuthoringDescriptor final {
    simulation::DefinitionId definitionId{};
    std::string displayName;
    AuthoringCategory category{AuthoringCategory::enemy};
    std::vector<std::string> tags;
};

// Authored values are immutable, renderer-independent data, never runtime instances or catalogs.
struct AuthoredContentPack final {
    std::vector<TilesetDefinition> tilesets;
    std::vector<gameplay::ProjectileDefinition> projectiles;
    std::vector<gameplay::AttackDefinition> attacks;
    std::vector<gameplay::creatures::BehaviorProfile> behaviors;
    std::vector<gameplay::creatures::EnemyDefinition> enemies;
    std::vector<gameplay::ItemDefinition> items;
    std::vector<gameplay::WorldObjectDefinition> objects;
    std::vector<gameplay::PickupDefinition> pickups;
    std::vector<gameplay::npcs::NpcDefinition> npcs;
    std::vector<gameplay::npcs::NpcVisualSet> npcVisuals;
    std::vector<gameplay::dialogue::DialogueDefinition> dialogues;
    std::vector<gameplay::quests::QuestDefinition> quests;
    std::vector<AuthoringDescriptor> authoringDescriptors;
    std::vector<authoring::TileSemanticDefinition> tileSemantics;
    std::vector<authoring::StampDefinition> stamps;
};

} // namespace underworld::game::content
