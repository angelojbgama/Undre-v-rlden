#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/entity_handle.h"
#include "engine/world/collision.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/facing_direction.h"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace underworld::world { class CollisionGrid; }

namespace underworld::game::gameplay::creatures {

struct ActorBoxDefinition final {
    int offsetX{};
    int offsetY{};
    int width{};
    int height{};

    [[nodiscard]] world::AabbI at(core::WorldPointI feet) const noexcept {
        return {feet.x + offsetX, feet.y + offsetY, width, height};
    }
    [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0; }
};

enum class BehaviorState { idle, wander, chase, attack, dead };

struct BehaviorProfile final {
    simulation::DefinitionId id{};
    int detectionRangePixels{};
    int disengageRangePixels{};
    std::uint32_t idleDurationTicks{};
    std::uint32_t wanderDurationTicks{};
};

struct EnemyDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualSetId{};
    simulation::DefinitionId behaviorProfileId{};
    Faction faction{Faction::enemy};
    int maximumHealth{};
    std::int64_t movementSpeedSubpixelsPerTick{};
    ActorBoxDefinition collisionBody{};
    ActorBoxDefinition hurtbox{};
    std::vector<simulation::DefinitionId> attackIds{};
};

class BehaviorCatalog final {
public:
    void add(BehaviorProfile profile);
    [[nodiscard]] const BehaviorProfile* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const BehaviorProfile& require(const simulation::DefinitionId& id) const;

private:
    std::unordered_map<simulation::DefinitionId, BehaviorProfile,
                       simulation::DefinitionIdHash> profiles_;
};

class EnemyCatalog final {
public:
    void add(EnemyDefinition definition);
    [[nodiscard]] const EnemyDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const EnemyDefinition& require(const simulation::DefinitionId& id) const;

private:
    std::unordered_map<simulation::DefinitionId, EnemyDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

struct AttackCooldown final {
    simulation::DefinitionId attackId{};
    std::uint32_t remainingTicks{};
};

struct ActiveAttack final {
    const AttackDefinition* definition{};
    AttackKey key{};
    FacingDirection lockedFacing{FacingDirection::down};
    bool meleeHitboxActive{};
};

class EnemyInstance final {
public:
    static constexpr std::int64_t subpixelsPerPixel = 256;
    static constexpr std::int64_t diagonalScaleNumerator = 181;
    static constexpr std::int64_t diagonalScaleDenominator = 256;

    [[nodiscard]] simulation::EntityHandle handle() const noexcept {
        return combatant_.handle;
    }
    [[nodiscard]] const EnemyDefinition& definition() const noexcept { return *definition_; }
    [[nodiscard]] core::WorldPointI feetPosition() const;
    [[nodiscard]] world::AabbI collisionBody() const;
    [[nodiscard]] Hurtbox hurtbox() const noexcept;
    [[nodiscard]] CombatTargetRef combatTarget() noexcept;
    [[nodiscard]] CombatantState& combatant() noexcept { return combatant_; }
    [[nodiscard]] const CombatantState& combatant() const noexcept { return combatant_; }
    [[nodiscard]] FacingDirection facing() const noexcept { return facing_; }
    [[nodiscard]] BehaviorState state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t stateTimer() const noexcept { return stateTimer_; }
    [[nodiscard]] simulation::EntityHandle target() const noexcept { return target_; }
    [[nodiscard]] const std::optional<ActiveAttack>& activeAttack() const noexcept {
        return activeAttack_;
    }
    [[nodiscard]] std::optional<ActiveAttack>& activeAttack() noexcept { return activeAttack_; }
    [[nodiscard]] std::span<const AttackCooldown> cooldowns() const noexcept {
        return cooldowns_;
    }
    [[nodiscard]] AttackCooldown* cooldownFor(
        const simulation::DefinitionId& attackId) noexcept;
    [[nodiscard]] const AttackCooldown* cooldownFor(
        const simulation::DefinitionId& attackId) const noexcept;

    void move(int intentX, int intentY, const world::CollisionGrid& collision, int tileSize);
    void applyKnockback(int deltaX, int deltaY, const world::CollisionGrid& collision,
                        int tileSize);

private:
    friend class EnemyFactory;
    friend class EnemyBehaviorSystem;

    EnemyInstance(simulation::EntityHandle handle, const EnemyDefinition& definition,
                  core::WorldPointI feet, FacingDirection facing,
                  const BehaviorProfile& profile);

    const EnemyDefinition* definition_{};
    CombatantState combatant_{};
    std::int64_t positionX_{};
    std::int64_t positionY_{};
    FacingDirection facing_{FacingDirection::down};
    BehaviorState state_{BehaviorState::idle};
    simulation::EntityHandle target_{};
    std::uint32_t stateTimer_{};
    std::uint32_t wanderCycle_{};
    AttackInstanceId nextAttackInstance_{1};
    std::vector<AttackCooldown> cooldowns_;
    std::optional<ActiveAttack> activeAttack_{};
};

class EnemyFactory final {
public:
    EnemyFactory(simulation::EntityHandlePool& handles, const EnemyCatalog& enemies,
                 const BehaviorCatalog& behaviors, const AttackCatalog& attacks,
                 const ProjectileCatalog& projectiles,
                 std::span<const simulation::DefinitionId> availableVisualSets);

    [[nodiscard]] EnemyInstance create(const simulation::DefinitionId& definitionId,
                                       core::WorldPointI feet,
                                       FacingDirection facing = FacingDirection::down) const;

private:
    simulation::EntityHandlePool& handles_;
    const EnemyCatalog& enemies_;
    const BehaviorCatalog& behaviors_;
    const AttackCatalog& attacks_;
    const ProjectileCatalog& projectiles_;
    std::vector<simulation::DefinitionId> availableVisualSets_;
};

struct BehaviorUpdate final {
    bool stateChanged{};
    bool attackStarted{};
};

class EnemyBehaviorSystem final {
public:
    [[nodiscard]] BehaviorUpdate update(EnemyInstance& enemy,
                                        simulation::EntityHandle playerHandle,
                                        core::WorldPointI playerFeet, bool playerAlive,
                                        const BehaviorProfile& profile,
                                        const AttackCatalog& attacks,
                                        const world::CollisionGrid& collision,
                                        int tileSize) const;
    void finishAttack(EnemyInstance& enemy, const BehaviorProfile& profile) const;

    [[nodiscard]] const AttackDefinition* selectAttack(
        const EnemyInstance& enemy, core::WorldPointI targetFeet,
        const AttackCatalog& attacks) const noexcept;
};

[[nodiscard]] const char* behaviorStateName(BehaviorState state) noexcept;

[[nodiscard]] const simulation::DefinitionId& soldierBehaviorId();
[[nodiscard]] const simulation::DefinitionId& skullBehaviorId();
[[nodiscard]] const simulation::DefinitionId& soldierEnemyId();
[[nodiscard]] const simulation::DefinitionId& skullEnemyId();
[[nodiscard]] const simulation::DefinitionId& soldierVisualId();
[[nodiscard]] const simulation::DefinitionId& skullVisualId();
[[nodiscard]] const simulation::DefinitionId& soldierSwordAttackId();
[[nodiscard]] const simulation::DefinitionId& skullArrowAttackId();
[[nodiscard]] const simulation::DefinitionId& skullArrowProjectileId();

[[nodiscard]] BehaviorProfile makeSoldierBehaviorProfile();
[[nodiscard]] BehaviorProfile makeSkullBehaviorProfile();
[[nodiscard]] EnemyDefinition makeSoldierEnemyDefinition();
[[nodiscard]] EnemyDefinition makeSkullEnemyDefinition();
[[nodiscard]] AttackDefinition makeSoldierSwordAttackDefinition();
[[nodiscard]] AttackDefinition makeSkullArrowAttackDefinition();
[[nodiscard]] ProjectileDefinition makeSkullArrowProjectileDefinition();

} // namespace underworld::game::gameplay::creatures
