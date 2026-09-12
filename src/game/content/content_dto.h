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
#include "game/gameplay/rpg/reward_grants.h"
#include "game/gameplay/rpg/shops.h"
#include "game/gameplay/world_objects.h"
#include "game/tilesets.h"
#include "game/presentation/presentation_effects.h"
#include "game/presentation/visual_content.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace underworld::game::content {

enum class AuthoringCategory { enemy, object, pickup, npc, player, item, rewardProfile, rewardGrant, shop };

struct AuthoredTileset final { simulation::DefinitionId id{}; std::string displayName; std::string relativeAssetPath; std::uint16_t tileSize{}; std::uint32_t columns{}; std::uint32_t rows{}; };
struct AuthoredProjectile final { simulation::DefinitionId id{}; simulation::DefinitionId visualId{}; gameplay::FacingDirection canonicalFacing{gameplay::FacingDirection::up}; int speedPixelsPerTick{}; std::uint32_t lifetimeTicks{}; int hitboxWidth{}; int hitboxHeight{}; gameplay::DirectionalOffsets spawnOffsets{}; };
struct AuthoredAttackShapeFrame final { std::uint32_t frameIndex{}; std::uint32_t tick{}; std::uint32_t width{}; std::uint32_t height{}; std::vector<std::uint8_t> cells; };
struct AuthoredDirectionalAttackShape final { gameplay::FacingDirection facing{gameplay::FacingDirection::down}; std::vector<AuthoredAttackShapeFrame> frames; };
struct AuthoredAttack final { simulation::DefinitionId id{}; gameplay::AttackKind kind{gameplay::AttackKind::meleeHitbox}; gameplay::DamageSpec damage{}; std::uint32_t totalTicks{}; std::uint32_t cooldownTicks{}; int minimumRangePixels{}; int maximumRangePixels{}; simulation::DefinitionId visualActionId{}; std::optional<gameplay::DirectionalBoxes> meleeHitboxes{}; std::optional<simulation::DefinitionId> projectileDefinitionId{}; std::vector<gameplay::AttackTimelineEvent> timeline{}; std::vector<AuthoredDirectionalAttackShape> shapes{}; };
struct AuthoredBehaviorProfile final { simulation::DefinitionId id{}; int detectionRangePixels{}; int disengageRangePixels{}; std::uint32_t idleDurationTicks{}; std::uint32_t wanderDurationTicks{}; };
struct AuthoredEnemy final { simulation::DefinitionId id{}; simulation::DefinitionId visualSetId{}; simulation::DefinitionId behaviorProfileId{}; gameplay::Faction faction{gameplay::Faction::enemy}; int maximumHealth{}; std::int64_t movementSpeedSubpixelsPerTick{}; gameplay::creatures::ActorBoxDefinition collisionBody{}; gameplay::creatures::ActorBoxDefinition hurtbox{}; std::vector<simulation::DefinitionId> attackIds{}; std::optional<simulation::DefinitionId> rewardProfileId{}; };
enum class AuthoredEquipmentSlot { armor, accessory };
struct AuthoredEquipmentModifiers final { int maximumHealthBonus{}; int playerAttackDamageBonus{}; };
struct AuthoredEquipment final { AuthoredEquipmentSlot slot{AuthoredEquipmentSlot::armor}; AuthoredEquipmentModifiers modifiers{}; };
struct AuthoredItem final { simulation::DefinitionId id{}; simulation::DefinitionId visualId{}; gameplay::ItemCategory category{gameplay::ItemCategory::misc}; std::uint32_t stackLimit{}; std::optional<gameplay::ItemUseDefinition> use{}; std::optional<AuthoredEquipment> equipment{}; };
struct AuthoredObjectCollision final { std::uint32_t width{}; std::uint32_t height{}; core::PointI origin{}; std::vector<std::uint8_t> cells; };
struct AuthoredObjectBankAccess final {};
struct AuthoredWorldObject final { simulation::DefinitionId id{}; simulation::DefinitionId visualSetId{}; std::optional<gameplay::ObjectInteractionDefinition> interactable{}; std::optional<gameplay::ObjectContainerDefinition> container{}; std::optional<gameplay::ObjectDestructibleDefinition> destructible{}; std::optional<AuthoredObjectBankAccess> bankAccess{}; std::optional<gameplay::ObjectDoorDefinition> door{}; std::optional<gameplay::ObjectActivationDefinition> activation{}; std::optional<AuthoredObjectCollision> collision{}; core::PointI depthAnchor{}; std::optional<gameplay::ObjectOcclusionDefinition> occlusion{}; };
struct AuthoredHealthPickup final { int amount{}; };
struct AuthoredCurrencyPickup final { std::uint64_t amount{}; };
struct AuthoredItemPickup final { simulation::DefinitionId itemId{}; std::uint32_t quantity{}; };
using AuthoredPickupPayload = std::variant<AuthoredHealthPickup, AuthoredCurrencyPickup, AuthoredItemPickup>;
struct AuthoredPickup final { simulation::DefinitionId id{}; simulation::DefinitionId visualId{}; world::AabbI collectionBounds{}; AuthoredPickupPayload payload{}; };
struct AuthoredVisualImage final { simulation::DefinitionId id{}; presentation::VisualAssetRoot root{presentation::VisualAssetRoot::gameAssets}; std::string relativePath; };
struct AuthoredStaticSprite final { simulation::DefinitionId id{}; simulation::DefinitionId imageId{}; std::optional<core::RectI> source; core::PointI anchor{}; };
struct AuthoredAnimationFrame final { core::RectI source{}; core::PointI anchor{}; core::PointI drawOffset{}; std::uint32_t durationTicks{}; std::vector<std::string> markers; };
struct AuthoredAnimation final { simulation::DefinitionId id{}; simulation::DefinitionId imageId{}; std::vector<AuthoredAnimationFrame> frames; bool loop{true}; };
struct AuthoredEnemyAttackVisual final { simulation::DefinitionId visualActionId{}; presentation::DirectionalAnimationRef clips; };
struct AuthoredEnemyVisual final { simulation::DefinitionId id{}; presentation::DirectionalAnimationRef idle; std::optional<presentation::DirectionalAnimationRef> move; std::optional<presentation::DirectionalAnimationRef> hurt; std::optional<presentation::DirectionalAnimationRef> death; std::optional<presentation::DirectionalAnimationRef> dead; // Keys are arbitrary visual actions, not a rigid attack list.
    std::vector<AuthoredEnemyAttackVisual> attacks; };
struct AuthoredWorldObjectVisual final { simulation::DefinitionId id{}; simulation::DefinitionId idleAnimationId{}; std::optional<simulation::DefinitionId> openedAnimationId; std::optional<simulation::DefinitionId> destroyingAnimationId; std::optional<simulation::DefinitionId> activationInactiveAnimationId; std::optional<simulation::DefinitionId> activationActiveAnimationId; std::optional<simulation::DefinitionId> doorLockedAnimationId; std::optional<simulation::DefinitionId> doorClosedAnimationId; std::optional<simulation::DefinitionId> doorOpenAnimationId; std::optional<simulation::DefinitionId> destroyedAnimationId; std::optional<simulation::DefinitionId> damagedAnimationId; };
struct AuthoredNpcVisualSet final { simulation::DefinitionId id{}; core::ColorRGBA8 markerColor{}; std::optional<presentation::DirectionalAnimationRef> idle; };
struct AuthoredNpc final { simulation::DefinitionId id{}; simulation::DefinitionId visualSetId{}; gameplay::InteractionArea interaction{}; simulation::DefinitionId defaultDialogueId{}; std::vector<std::string> tags; };

struct AuthoredDialogueCondition final { gameplay::dialogue::DialogueConditionKind kind{gameplay::dialogue::DialogueConditionKind::flagSet}; simulation::DefinitionId flagId{}; };
struct AuthoredDialogueAction final { gameplay::dialogue::DialogueActionKind kind{gameplay::dialogue::DialogueActionKind::setFlag}; simulation::DefinitionId targetId{}; };
struct AuthoredDialogueChoice final { std::string label; simulation::DefinitionId targetNodeId{}; std::vector<AuthoredDialogueCondition> conditions; std::vector<AuthoredDialogueAction> actions; };
struct AuthoredDialogueNode final { simulation::DefinitionId id{}; std::string speaker; std::vector<std::string> pages; simulation::DefinitionId nextNodeId{}; std::vector<AuthoredDialogueChoice> choices; };
struct AuthoredDialogue final { simulation::DefinitionId id{}; simulation::DefinitionId entryNodeId{}; std::vector<AuthoredDialogueNode> nodes; };
struct AuthoredQuestObjective final { simulation::DefinitionId id{}; gameplay::quests::QuestObjectiveKind kind{}; simulation::DefinitionId targetId{}; std::uint32_t requiredCount{1}; std::string description; };
struct AuthoredQuest final { simulation::DefinitionId id{}; std::string title; std::vector<AuthoredQuestObjective> objectives; std::vector<std::string> tags; std::optional<simulation::DefinitionId> rewardGrantId{}; };
struct AuthoredPlayer final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualSetId{};
    simulation::DefinitionId progressionId{};
};
struct AuthoredPlayerActionVisual final {
    std::string actionId;
    presentation::DirectionalAnimationRef clips;
};
struct AuthoredPlayerVisual final {
    simulation::DefinitionId id{};
    presentation::DirectionalAnimationRef idle;
    presentation::DirectionalAnimationRef walk;
    std::optional<presentation::DirectionalAnimationRef> hurt;
    std::vector<AuthoredPlayerActionVisual> actions;
};
struct AuthoredPlayerBaseStats final { int maximumHealth{}; };
struct AuthoredPlayerProgression final { simulation::DefinitionId id{}; AuthoredPlayerBaseStats baseStats{}; std::vector<std::uint64_t> cumulativeExperienceThresholds; };
struct AuthoredLootEntry final { simulation::DefinitionId pickupDefinitionId{}; std::uint32_t chanceBasisPoints{}; std::uint32_t minimumCount{1}; std::uint32_t maximumCount{1}; };
struct AuthoredRewardProfile final { simulation::DefinitionId id{}; std::uint64_t experience{}; std::vector<AuthoredLootEntry> loot; };
struct AuthoredRewardItemGrant final { simulation::DefinitionId itemId{}; std::uint32_t quantity{1}; };
struct AuthoredRewardGrant final { simulation::DefinitionId id{}; std::uint64_t experience{}; std::uint64_t gold{}; std::vector<AuthoredRewardItemGrant> items; };
struct AuthoredShopOffer final { simulation::DefinitionId itemId{}; std::optional<std::uint64_t> playerBuyPrice{}; std::optional<std::uint64_t> playerSellPrice{}; };
struct AuthoredShop final { simulation::DefinitionId id{}; std::vector<AuthoredShopOffer> offers; };
struct AuthoringDescriptor final { simulation::DefinitionId definitionId{}; std::string displayName; AuthoringCategory category{AuthoringCategory::enemy}; std::vector<std::string> tags; };

struct AuthoredPresentationEffect final {
    simulation::DefinitionId id{};
    presentation::PresentationEffectLifetime lifetime{presentation::PresentationEffectLifetime::transient};
    std::uint32_t durationTicks{};
    int priority{};
    std::optional<presentation::CameraShakeDefinition> cameraShake{};
    std::optional<presentation::ColorOverlayDefinition> overlay{};
    std::optional<presentation::VisionMaskDefinition> visionMask{};
    std::optional<presentation::FadeDefinition> fade{};
    [[nodiscard]] bool operator==(const AuthoredPresentationEffect&) const noexcept = default;
};

struct AuthoredTileSemantic final { simulation::DefinitionId id{}; simulation::DefinitionId tilesetId{}; std::uint32_t sourceIndex{}; std::string family; authoring::TileRole role{authoring::TileRole::unknown}; authoring::TileTopology topology{authoring::TileTopology::unknown}; authoring::EdgeProfile north{authoring::EdgeProfile::unknown}; authoring::EdgeProfile east{authoring::EdgeProfile::unknown}; authoring::EdgeProfile south{authoring::EdgeProfile::unknown}; authoring::EdgeProfile west{authoring::EdgeProfile::unknown}; std::string preferredLayer; bool flipXAllowed{}; authoring::SemanticConfidence visualConfidence{authoring::SemanticConfidence::confirmed}; authoring::SemanticConfidence semanticConfidence{authoring::SemanticConfidence::unverified}; authoring::SemanticConfidence gameplayConfidence{authoring::SemanticConfidence::unverified}; std::uint32_t variantWeight{1}; };
struct AuthoredStampCell final { int x{}; int y{}; simulation::DefinitionId tileId{}; };
struct AuthoredStamp final { simulation::DefinitionId id{}; std::string displayName; std::uint32_t width{}; std::uint32_t height{}; std::vector<AuthoredStampCell> cells; core::PointI anchor{}; bool flipXAllowed{}; bool atomic{}; authoring::SemanticConfidence confidence{authoring::SemanticConfidence::unverified}; };

struct AuthoredContentPack final {
    std::vector<AuthoredTileset> tilesets; std::vector<AuthoredProjectile> projectiles; std::vector<AuthoredAttack> attacks;
    std::vector<AuthoredBehaviorProfile> behaviors; std::vector<AuthoredEnemy> enemies; std::vector<AuthoredItem> items;
    std::vector<AuthoredWorldObject> objects; std::vector<AuthoredPickup> pickups; std::vector<AuthoredNpc> npcs;
    std::vector<AuthoredNpcVisualSet> npcVisuals; std::vector<AuthoredDialogue> dialogues; std::vector<AuthoredQuest> quests;
    std::vector<AuthoringDescriptor> authoringDescriptors; std::vector<AuthoredTileSemantic> tileSemantics; std::vector<AuthoredStamp> stamps;
    std::vector<AuthoredPlayer> players; std::vector<AuthoredPlayerProgression> playerProgressions; std::vector<AuthoredRewardProfile> rewardProfiles; std::vector<AuthoredRewardGrant> rewardGrants; std::vector<AuthoredShop> shops;
    std::vector<AuthoredPresentationEffect> presentationEffects;
    std::vector<AuthoredVisualImage> visualImages;
    std::vector<AuthoredStaticSprite> staticSprites;
    std::vector<AuthoredAnimation> animations;
    std::vector<AuthoredEnemyVisual> enemyVisuals;
    std::vector<AuthoredWorldObjectVisual> objectVisuals;
    std::vector<AuthoredPlayerVisual> playerVisuals;
};

} // namespace underworld::game::content
