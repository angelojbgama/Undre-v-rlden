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
#include "game/maps/map_catalog.h"

#include <memory>
#include <string>

namespace underworld::game {

// Authoritative gameplay slice. This type deliberately has no dependency on
// rendering, platform input, assets or presentation state.
class GameSession final {
public:
    GameSession(simulation::EntityHandlePool& handles, simulation::PlayerId playerId,
                core::WorldPointI initialPosition = {});

    [[nodiscard]] bool initializeMap(const maps::MapCatalog& maps,
                                      const maps::MapValidationCatalogs& catalogs,
                                      const maps::RuntimeWorldBuilder& builder,
                                      simulation::EntityHandlePool& handles,
                                      const simulation::MapId& mapId,
                                      const simulation::SpawnId& spawnId,
                                      std::string& error);
    void tick(const simulation::PlayerCommand& command);

    void configureCombat(const gameplay::AttackCatalog& attacks,
                         const gameplay::ProjectileCatalog& projectiles,
                         const gameplay::creatures::BehaviorCatalog& behaviors,
                         const gameplay::AttackDefinition& sword,
                         const gameplay::AttackDefinition& bow);
    void configureItems(const gameplay::ItemCatalog& items);
    void clearCombatTransients() noexcept;

    [[nodiscard]] const gameplay::Player& player() const noexcept { return player_; }
    [[nodiscard]] gameplay::Player& playerForRuntime() noexcept { return player_; }
    [[nodiscard]] const gameplay::PlayerItems& playerItems() const noexcept { return *playerItems_; }
    [[nodiscard]] gameplay::PlayerItems& playerItemsForRuntime() noexcept { return *playerItems_; }
    [[nodiscard]] const gameplay::InventoryOverlayState& inventoryOverlay() const noexcept {
        return inventoryOverlay_;
    }
    [[nodiscard]] gameplay::InventoryOverlayState& inventoryOverlayForRuntime() noexcept {
        return inventoryOverlay_;
    }
    [[nodiscard]] const simulation::EventBuffer& events() const noexcept { return events_; }
    [[nodiscard]] simulation::EventBuffer& eventsForRuntime() noexcept { return events_; }
    [[nodiscard]] const maps::RuntimeWorld& world() const noexcept;
    [[nodiscard]] maps::RuntimeWorld& worldForRuntime() noexcept;
    [[nodiscard]] const maps::MapData& mapData() const;
    [[nodiscard]] const save::SessionWorldState& worldState() const noexcept { return worldState_; }
    [[nodiscard]] save::SessionWorldState& worldStateForRuntime() noexcept { return worldState_; }
    [[nodiscard]] const gameplay::ProjectileSystem& projectiles() const noexcept {
        return *projectiles_;
    }
    [[nodiscard]] const gameplay::Hitbox& activeSword() const noexcept { return activeSword_; }
    [[nodiscard]] const gameplay::AttackExecution* playerAttack() const noexcept {
        return playerAttack_ ? &*playerAttack_ : nullptr;
    }
    [[nodiscard]] bool restoreMap(const simulation::MapId& mapId,
                                  const save::SessionWorldState& state, std::string& error);

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
    [[nodiscard]] std::vector<gameplay::CombatTargetRef> combatTargets();

    simulation::EntityHandlePool& handles_;
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
    gameplay::AttackInstanceId nextContactAttackInstance_{1};
};

} // namespace underworld::game
