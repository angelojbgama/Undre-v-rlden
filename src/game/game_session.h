#pragma once

#include "engine/simulation/entity_handle.h"
#include "engine/simulation/events.h"
#include "engine/simulation/player_command.h"
#include "game/gameplay/player.h"
#include "game/gameplay/combat_system.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/projectile_system.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/bank_overlay.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/gameplay/dialogue/dialogue_session.h"
#include "game/gameplay/quests/quest_system.h"
#include "game/maps/map_catalog.h"
#include "game/maps/region_tracker.h"
#include "game/gameplay/world_logic.h"
#include "game/gameplay/encounter_system.h"
#include "game/save/save_data.h"
#include "game/gameplay/rpg/player_progression.h"
#include "game/gameplay/rpg/rewards.h"
#include "game/gameplay/rpg/reward_grants.h"
#include "game/gameplay/rpg/equipment.h"
#include "game/gameplay/shop_overlay.h"
#include "game/gameplay/scenes/scene_controller.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace underworld::game {

// Authoritative gameplay slice. This type deliberately has no dependency on
// rendering, platform input, assets or presentation state.
class GameSession final {
public:
    GameSession(simulation::PlayerId playerId,
                const gameplay::rpg::PlayerProgressionDefinition& progression,
                core::WorldPointI initialPosition = {},
                gameplay::PlayerMovementConfig movementConfig = {},
                std::optional<gameplay::ActorCollisionShapeDefinition>
                    hurtboxShape = {},
                std::optional<gameplay::PlayerHurtboxFrameProfile>
                    hurtboxFrameOverrides = {});

    [[nodiscard]] bool initializeMap(const maps::MapCatalog& maps,
                                      const maps::MapValidationCatalogs& catalogs,
                                      const maps::RuntimeWorldBuilder& builder,
                                      const simulation::MapId& mapId,
                                      const simulation::SpawnId& spawnId,
                                      std::string& error);
    void tick(const simulation::PlayerCommand& command);
    // Used by deterministic setup/teleport callers; normal gameplay movement
    // still enters through PlayerCommand and tick().
    void relocatePlayer(core::WorldPointI position, gameplay::FacingDirection facing) noexcept;

    void configureCombat(const gameplay::AttackCatalog& attacks,
                         const gameplay::ProjectileCatalog& projectiles,
                         const gameplay::creatures::BehaviorCatalog& behaviors,
                         const gameplay::AttackDefinition& sword,
                         const gameplay::AttackDefinition& bow);
    void configureItems(const gameplay::ItemCatalog& items);
    void configureNarrative(const gameplay::dialogue::DialogueCatalog& dialogues,
                            const gameplay::quests::QuestCatalog& quests);
    void configureRewards(const gameplay::rpg::RewardProfileCatalog& rewards,
                          const std::vector<gameplay::PickupDefinition>& pickups) noexcept {
        rewardCatalog_ = &rewards;
        pickupDefinitions_ = &pickups;
    }
    void configureRewardGrants(const gameplay::rpg::RewardGrantCatalog& grants) noexcept { rewardGrantCatalog_ = &grants; }
    void configureShops(const gameplay::rpg::ShopCatalog& shops) noexcept { shopCatalog_ = &shops; }
    [[nodiscard]] save::SaveData captureSaveData() const;
    [[nodiscard]] bool restoreSaveData(const save::SaveData& data, std::string& error);

    [[nodiscard]] const gameplay::Player& player() const noexcept { return player_; }
    [[nodiscard]] const gameplay::PlayerItems& playerItems() const noexcept { return *playerItems_; }
    [[nodiscard]] const gameplay::InventoryOverlayState& inventoryOverlay() const noexcept {
        return inventoryOverlay_;
    }
    [[nodiscard]] const gameplay::BankOverlayState& bankOverlay() const noexcept {
        return bankOverlay_;
    }
    [[nodiscard]] const gameplay::ShopOverlayState& shopOverlay() const noexcept { return shopOverlay_; }
    [[nodiscard]] const simulation::EventBuffer& events() const noexcept { return events_; }
    [[nodiscard]] const maps::RuntimeWorld& world() const noexcept;
    [[nodiscard]] const maps::MapData& mapData() const;
    [[nodiscard]] const save::SessionWorldState& worldState() const noexcept { return worldState_; }
    [[nodiscard]] const gameplay::ProjectileSystem& projectiles() const noexcept {
        return *projectiles_;
    }
    [[nodiscard]] const gameplay::Hitbox& activeSword() const noexcept { return activeSword_; }
    [[nodiscard]] const gameplay::AttackExecution* playerAttack() const noexcept {
        return playerAttack_ ? &*playerAttack_ : nullptr;
    }
    [[nodiscard]] const gameplay::dialogue::DialogueFlagSet& dialogueFlags() const noexcept {
        return dialogueFlags_;
    }
    [[nodiscard]] const gameplay::dialogue::DialogueSession& dialogue() const noexcept {
        return *dialogue_;
    }
    [[nodiscard]] const gameplay::quests::QuestStateStore& questState() const noexcept {
        return questState_;
    }
    [[nodiscard]] const gameplay::rpg::PlayerProgressionState& progression() const noexcept {
        return progression_;
    }
    [[nodiscard]] const gameplay::rpg::PlayerDerivedStats& derivedPlayerStats() const noexcept {
        return derivedPlayerStats_;
    }
    [[nodiscard]] bool restoreNarrativeState(
        const gameplay::dialogue::DialogueFlagSet& flags,
        std::span<const gameplay::quests::QuestProgress> progress,
        std::string& error);
    [[nodiscard]] bool sceneActive() const noexcept { return sceneController_.active(); }
    [[nodiscard]] bool sceneWaitingForDialogue() const noexcept {
        return sceneController_.waitingForDialogue();
    }
    [[nodiscard]] const gameplay::scenes::ScenePresentationState& scenePresentation() const noexcept {
        return sceneController_.presentation();
    }
    [[nodiscard]] const std::string& sceneError() const noexcept {
        return sceneController_.lastError();
    }

private:
    void startPlayerAttack();
    void advancePlayerAttack();
    void applyResolution(const gameplay::CombatResolution& resolution);
    void resolvePlayerSword();
    void resolveEnemyContacts();
    void updateEnemies();
    void removeDefeatedEnemies();
    void resolveDefeatRewards();
    void resolveObjectDestructionReward(const maps::PersistentObject& object);
    void collectNearbyPickups();
    void updateObjects();
    void interactWithWorld();
    void captureWorldState();
    void clearCombatTransients() noexcept;
    void closeDialogue() noexcept;
    [[nodiscard]] bool restoreMap(const simulation::MapId& mapId,
                                  const save::SessionWorldState& state, std::string& error);
    [[nodiscard]] bool handleDialogueCommand(const simulation::PlayerCommand& command);
    void applyDialogueActions();
    void consumeQuestEvents();
    void consumeWorldLogic();
    void resolvePendingQuestRewards();
    void resolveEncounterRewards();
    [[nodiscard]] bool requestScene(const simulation::DefinitionId& sceneId);
    [[nodiscard]] bool startPendingScene();
    [[nodiscard]] bool startScene(const simulation::DefinitionId& sceneId);
    [[nodiscard]] gameplay::WorldLogicRuntime worldLogicRuntime();
    [[nodiscard]] gameplay::scenes::SceneRuntimeHooks sceneRuntimeHooks();
    void refreshDerivedPlayerStats();
    [[nodiscard]] gameplay::DamageSpec effectivePlayerDamage(
        const gameplay::DamageSpec& base) const noexcept;
    [[nodiscard]] std::vector<gameplay::CombatTargetRef> combatTargets();

    simulation::EntityHandlePool handles_;
    gameplay::Player player_;
    gameplay::rpg::PlayerProgressionState progression_;
    gameplay::rpg::PlayerDerivedStats derivedPlayerStats_{};
    simulation::EventBuffer events_;
    maps::RegionTracker regionTracker_;
    gameplay::WorldLogicSystem worldLogic_;
    std::size_t worldLogicEventCursor_{};
    gameplay::EncounterSystem encounters_;
    bool mapEnteredPending_{};
    save::SessionWorldState worldState_;
    std::unique_ptr<maps::MapSession> mapSession_;
    const gameplay::AttackCatalog* attackCatalog_{};
    const gameplay::ProjectileCatalog* projectileCatalog_{};
    const gameplay::creatures::BehaviorCatalog* behaviorCatalog_{};
    const gameplay::AttackDefinition* swordDefinition_{};
    const gameplay::AttackDefinition* bowDefinition_{};
    const gameplay::ItemCatalog* itemCatalog_{};
    gameplay::creatures::EnemyBehaviorSystem enemyBehavior_;
    gameplay::CombatSystem combat_;
    std::unique_ptr<gameplay::ProjectileSystem> projectiles_;
    gameplay::Hitbox activeSword_{};
    std::optional<gameplay::AttackExecution> playerAttack_{};
    std::unique_ptr<gameplay::PlayerItems> playerItems_;
    gameplay::InventoryOverlayState inventoryOverlay_;
    gameplay::BankOverlayState bankOverlay_;
    gameplay::ShopOverlayState shopOverlay_;
    const gameplay::dialogue::DialogueCatalog* dialogueCatalog_{};
    const gameplay::quests::QuestCatalog* questCatalog_{};
    const gameplay::rpg::RewardProfileCatalog* rewardCatalog_{};
    const gameplay::rpg::RewardGrantCatalog* rewardGrantCatalog_{};
    const gameplay::rpg::ShopCatalog* shopCatalog_{};
    const std::vector<gameplay::PickupDefinition>* pickupDefinitions_{};
    gameplay::rpg::RewardResolver rewardResolver_;
    gameplay::rpg::RewardGrantService rewardGrantService_;
    gameplay::rpg::ShopTransactionService shopTransactionService_;
    gameplay::scenes::SceneController sceneController_;
    std::optional<simulation::DefinitionId> pendingSceneId_;
    gameplay::dialogue::DialogueFlagSet dialogueFlags_;
    std::unique_ptr<gameplay::dialogue::DialogueSession> dialogue_;
    gameplay::quests::QuestStateStore questState_;
    std::unique_ptr<gameplay::quests::QuestSystem> questSystem_;
    gameplay::AttackInstanceId nextContactAttackInstance_{1};
};

} // namespace underworld::game
