#pragma once

#include "game/authoring/authoring_semantics.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/dialogue/dialogue_model.h"
#include "game/gameplay/facing_direction.h"
#include "game/gameplay/items.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/gameplay/quests/quest_model.h"
#include "game/gameplay/rpg/player_progression.h"
#include "game/gameplay/rpg/rewards.h"
#include "game/gameplay/world_objects.h"
#include "game/tilesets.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace underworld::game::content {

enum class AuthoringCategory { enemy, object, pickup, npc, item, rewardProfile };

struct AuthoredTileset final { simulation::DefinitionId id{}; std::string displayName; std::string relativeAssetPath; std::uint16_t tileSize{}; std::uint32_t columns{}; std::uint32_t rows{}; };
struct AuthoredProjectile final { simulation::DefinitionId id{}; simulation::DefinitionId visualId{}; gameplay::FacingDirection canonicalFacing{gameplay::FacingDirection::up}; int speedPixelsPerTick{}; std::uint32_t lifetimeTicks{}; int hitboxWidth{}; int hitboxHeight{}; gameplay::DirectionalOffsets spawnOffsets{}; };
struct AuthoredAttack final { simulation::DefinitionId id{}; gameplay::AttackKind kind{gameplay::AttackKind::meleeHitbox}; gameplay::DamageSpec damage{}; std::uint32_t totalTicks{}; std::uint32_t cooldownTicks{}; int minimumRangePixels{}; int maximumRangePixels{}; simulation::DefinitionId visualActionId{}; std::optional<gameplay::DirectionalBoxes> meleeHitboxes{}; std::optional<simulation::DefinitionId> projectileDefinitionId{}; std::vector<gameplay::AttackTimelineEvent> timeline{}; };
struct AuthoredBehaviorProfile final { simulation::DefinitionId id{}; int detectionRangePixels{}; int disengageRangePixels{}; std::uint32_t idleDurationTicks{}; std::uint32_t wanderDurationTicks{}; };
struct AuthoredEnemy final { simulation::DefinitionId id{}; simulation::DefinitionId visualSetId{}; simulation::DefinitionId behaviorProfileId{}; gameplay::Faction faction{gameplay::Faction::enemy}; int maximumHealth{}; std::int64_t movementSpeedSubpixelsPerTick{}; gameplay::creatures::ActorBoxDefinition collisionBody{}; gameplay::creatures::ActorBoxDefinition hurtbox{}; std::vector<simulation::DefinitionId> attackIds{}; std::optional<simulation::DefinitionId> rewardProfileId{}; };
enum class AuthoredEquipmentSlot { armor, accessory };
struct AuthoredEquipmentModifiers final { int maximumHealthBonus{}; int playerAttackDamageBonus{}; };
struct AuthoredEquipment final { AuthoredEquipmentSlot slot{AuthoredEquipmentSlot::armor}; AuthoredEquipmentModifiers modifiers{}; };
struct AuthoredItem final { simulation::DefinitionId id{}; simulation::DefinitionId visualId{}; gameplay::ItemCategory category{gameplay::ItemCategory::misc}; std::uint32_t stackLimit{}; std::optional<gameplay::ItemUseDefinition> use{}; std::optional<AuthoredEquipment> equipment{}; };
struct AuthoredWorldObject final { simulation::DefinitionId id{}; simulation::DefinitionId visualSetId{}; std::optional<gameplay::ObjectInteractionDefinition> interactable{}; std::optional<gameplay::ObjectContainerDefinition> container{}; std::optional<gameplay::ObjectDestructibleDefinition> destructible{}; };
struct AuthoredHealthPickup final { int amount{}; };
struct AuthoredCurrencyPickup final { std::uint64_t amount{}; };
struct AuthoredItemPickup final { simulation::DefinitionId itemId{}; std::uint32_t quantity{}; };
using AuthoredPickupPayload = std::variant<AuthoredHealthPickup, AuthoredCurrencyPickup, AuthoredItemPickup>;
struct AuthoredPickup final { simulation::DefinitionId id{}; simulation::DefinitionId visualId{}; world::AabbI collectionBounds{}; AuthoredPickupPayload payload{}; };
struct AuthoredNpcVisualSet final { simulation::DefinitionId id{}; core::ColorRGBA8 markerColor{}; };
struct AuthoredNpc final { simulation::DefinitionId id{}; simulation::DefinitionId visualSetId{}; gameplay::InteractionArea interaction{}; simulation::DefinitionId defaultDialogueId{}; std::vector<std::string> tags; };

struct AuthoredDialogueCondition final { gameplay::dialogue::DialogueConditionKind kind{gameplay::dialogue::DialogueConditionKind::flagSet}; simulation::DefinitionId flagId{}; };
struct AuthoredDialogueAction final { gameplay::dialogue::DialogueActionKind kind{gameplay::dialogue::DialogueActionKind::setFlag}; simulation::DefinitionId targetId{}; };
struct AuthoredDialogueChoice final { std::string label; simulation::DefinitionId targetNodeId{}; std::vector<AuthoredDialogueCondition> conditions; std::vector<AuthoredDialogueAction> actions; };
struct AuthoredDialogueNode final { simulation::DefinitionId id{}; std::string speaker; std::vector<std::string> pages; simulation::DefinitionId nextNodeId{}; std::vector<AuthoredDialogueChoice> choices; };
struct AuthoredDialogue final { simulation::DefinitionId id{}; simulation::DefinitionId entryNodeId{}; std::vector<AuthoredDialogueNode> nodes; };
struct AuthoredQuestObjective final { simulation::DefinitionId id{}; gameplay::quests::QuestObjectiveKind kind{}; simulation::DefinitionId targetId{}; std::uint32_t requiredCount{1}; std::string description; };
struct AuthoredQuest final { simulation::DefinitionId id{}; std::string title; std::vector<AuthoredQuestObjective> objectives; std::vector<std::string> tags; };
struct AuthoredPlayerBaseStats final { int maximumHealth{}; };
struct AuthoredPlayerProgression final { simulation::DefinitionId id{}; AuthoredPlayerBaseStats baseStats{}; std::vector<std::uint64_t> cumulativeExperienceThresholds; };
struct AuthoredLootEntry final { simulation::DefinitionId pickupDefinitionId{}; std::uint32_t chanceBasisPoints{}; std::uint32_t minimumCount{1}; std::uint32_t maximumCount{1}; };
struct AuthoredRewardProfile final { simulation::DefinitionId id{}; std::uint64_t experience{}; std::vector<AuthoredLootEntry> loot; };
struct AuthoringDescriptor final { simulation::DefinitionId definitionId{}; std::string displayName; AuthoringCategory category{AuthoringCategory::enemy}; std::vector<std::string> tags; };

struct AuthoredTileSemantic final { simulation::DefinitionId id{}; simulation::DefinitionId tilesetId{}; std::uint32_t sourceIndex{}; std::string family; authoring::TileRole role{authoring::TileRole::unknown}; authoring::TileTopology topology{authoring::TileTopology::unknown}; authoring::EdgeProfile north{authoring::EdgeProfile::unknown}; authoring::EdgeProfile east{authoring::EdgeProfile::unknown}; authoring::EdgeProfile south{authoring::EdgeProfile::unknown}; authoring::EdgeProfile west{authoring::EdgeProfile::unknown}; std::string preferredLayer; bool flipXAllowed{}; authoring::SemanticConfidence visualConfidence{authoring::SemanticConfidence::confirmed}; authoring::SemanticConfidence semanticConfidence{authoring::SemanticConfidence::unverified}; authoring::SemanticConfidence gameplayConfidence{authoring::SemanticConfidence::unverified}; };
struct AuthoredStampCell final { int x{}; int y{}; simulation::DefinitionId tileId{}; };
struct AuthoredStamp final { simulation::DefinitionId id{}; std::string displayName; std::uint32_t width{}; std::uint32_t height{}; std::vector<AuthoredStampCell> cells; core::PointI anchor{}; bool flipXAllowed{}; bool atomic{}; authoring::SemanticConfidence confidence{authoring::SemanticConfidence::unverified}; };

struct AuthoredContentPack final {
    std::vector<AuthoredTileset> tilesets; std::vector<AuthoredProjectile> projectiles; std::vector<AuthoredAttack> attacks;
    std::vector<AuthoredBehaviorProfile> behaviors; std::vector<AuthoredEnemy> enemies; std::vector<AuthoredItem> items;
    std::vector<AuthoredWorldObject> objects; std::vector<AuthoredPickup> pickups; std::vector<AuthoredNpc> npcs;
    std::vector<AuthoredNpcVisualSet> npcVisuals; std::vector<AuthoredDialogue> dialogues; std::vector<AuthoredQuest> quests;
    std::vector<AuthoringDescriptor> authoringDescriptors; std::vector<AuthoredTileSemantic> tileSemantics; std::vector<AuthoredStamp> stamps;
    std::vector<AuthoredPlayerProgression> playerProgressions; std::vector<AuthoredRewardProfile> rewardProfiles;
};

} // namespace underworld::game::content
