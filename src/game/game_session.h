#pragma once

#include "engine/simulation/entity_handle.h"
#include "engine/simulation/events.h"
#include "engine/simulation/player_command.h"
#include "game/gameplay/player.h"
#include "game/gameplay/combat_system.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/projectile_system.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/gameplay/dialogue/dialogue_session.h"
#include "game/gameplay/quests/quest_system.h"
#include "game/maps/map_catalog.h"
#include "game/save/save_data.h"

#include <memory>
#include <span>
#include <string>

namespace underworld::game {

// Authoritative gameplay slice. This type deliberately has no dependency on
// rendering, platform input, assets or presentation state.
class GameSession final {
public:
    GameSession(simulation::PlayerId playerId,
                core::WorldPointI initialPosition = {});

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
    [[nodiscard]] save::SaveData captureSaveData() const;
    [[nodiscard]] bool restoreSaveData(const save::SaveData& data, std::string& error);

    [[nodiscard]] const gameplay::Player& player() const noexcept { return player_; }
    [[nodiscard]] const gameplay::PlayerItems& playerItems() const noexcept { return *playerItems_; }
    [[nodiscard]] const gameplay::InventoryOverlayState& inventoryOverlay() const noexcept {
        return inventoryOverlay_;
    }
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
    [[nodiscard]] bool restoreNarrativeState(
        const gameplay::dialogue::DialogueFlagSet& flags,
        std::span<const gameplay::quests::QuestProgress> progress,
        std::string& error);

private:
    void startPlayerAttack();
    void advancePlayerAttack();
    void applyResolution(const gameplay::CombatResolution& resolution);
    void resolvePlayerSword();
    void resolveEnemyContacts();
    void updateEnemies();
    void removeDefeatedEnemies();
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
    [[nodiscard]] std::vector<gameplay::CombatTargetRef> combatTargets();

    simulation::EntityHandlePool handles_;
    gameplay::Player player_;
    simulation::EventBuffer events_;
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
    const gameplay::dialogue::DialogueCatalog* dialogueCatalog_{};
    const gameplay::quests::QuestCatalog* questCatalog_{};
    gameplay::dialogue::DialogueFlagSet dialogueFlags_;
    std::unique_ptr<gameplay::dialogue::DialogueSession> dialogue_;
    gameplay::quests::QuestStateStore questState_;
    std::unique_ptr<gameplay::quests::QuestSystem> questSystem_;
    gameplay::AttackInstanceId nextContactAttackInstance_{1};
};

} // namespace underworld::game
