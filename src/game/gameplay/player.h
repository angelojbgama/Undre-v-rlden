#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/player_command.h"
#include "engine/world/collision.h"
#include "game/gameplay/actor_footprint.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/facing_direction.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace underworld::world { class CollisionGrid; }

namespace underworld::game::gameplay {

enum class PlayerMotionState {
    idle,
    walk,
};

enum class PlayerActionState {
    none,
    swordAttack,
    bowAttack,
    hurt,
};

struct PlayerMovementConfig final {
    static constexpr std::int64_t subpixelsPerPixel = 256;
    static constexpr std::int64_t cardinalSpeedSubpixelsPerTick = 384; // 90 px/s at 60 Hz.
    static constexpr std::int64_t diagonalScaleNumerator = 181;
    static constexpr std::int64_t diagonalScaleDenominator = 256;

    // Stable per-facing movement footprints at the player's feet.
    // Side views extend 2 px toward the facing direction to account for the
    // hand/body silhouette without making up/down movement artificially wide.
    DirectionalActorFootprints footprints{{
        ActorFootprintDefinition{-8, -8, 16, 8},  // down
        ActorFootprintDefinition{-8, -8, 16, 8},  // up
        ActorFootprintDefinition{-10, -8, 18, 8}, // left
        ActorFootprintDefinition{-8, -8, 18, 8},  // right
    }};
    std::optional<DirectionalActorCollisionShapes> collisionShapes{};

    int cornerSlideMaxProbePixels{4};
    int cornerSlideCorrectionPixels{1};
};

struct SubpixelPosition final {
    std::int64_t x{};
    std::int64_t y{};
    [[nodiscard]] constexpr bool operator==(const SubpixelPosition&) const noexcept = default;
};

class Player final {
public:
    static constexpr int damageKnockbackPixels = 32;
    static constexpr int damageKnockbackDurationTicks = 8;
    static constexpr int hurtboxWidth = 14;
    static constexpr int hurtboxHeight = 22;
    static constexpr int hurtboxOffsetX = -7;
    static constexpr int hurtboxOffsetY = -22;

    Player(simulation::PlayerId id, simulation::EntityHandle entity,
           core::WorldPointI feetPosition,
           int maximumHealth,
           PlayerMovementConfig config = {},
           std::optional<ActorCollisionShapeDefinition> hurtboxShape = {});

    void update(const simulation::PlayerCommand& command,
                const world::CollisionGrid& collision, int tileSize,
                std::span<const world::AabbI> staticObstacles = {});

    [[nodiscard]] simulation::PlayerId id() const noexcept { return id_; }
    [[nodiscard]] simulation::EntityHandle entityHandle() const noexcept {
        return combatant_.handle;
    }
    [[nodiscard]] SubpixelPosition subpixelPosition() const noexcept { return position_; }
    [[nodiscard]] core::WorldPointI feetPosition() const;
    [[nodiscard]] world::AabbI collisionBody() const;
    [[nodiscard]] std::vector<world::AabbI> collisionRegions() const;
    [[nodiscard]] FacingDirection facing() const noexcept { return facing_; }
    [[nodiscard]] PlayerMotionState motionState() const noexcept { return motionState_; }
    [[nodiscard]] const PlayerMovementConfig& movementConfig() const noexcept { return config_; }
    [[nodiscard]] const world::MovementResult& lastMovement() const noexcept {
        return lastMovement_;
    }
    [[nodiscard]] PlayerActionState actionState() const noexcept { return actionState_; }
    [[nodiscard]] AttackInstanceId attackInstance() const noexcept { return attackInstance_; }
    [[nodiscard]] Health& health() noexcept { return combatant_.health; }
    [[nodiscard]] const Health& health() const noexcept { return combatant_.health; }
    [[nodiscard]] CombatantState& combatant() noexcept { return combatant_; }
    [[nodiscard]] const CombatantState& combatant() const noexcept { return combatant_; }
    [[nodiscard]] CombatTargetRef combatTarget();
    [[nodiscard]] Hurtbox hurtbox() const;
    [[nodiscard]] InteractionArea interactionArea() const noexcept;
    void applyKnockback(int deltaX, int deltaY, const world::CollisionGrid& collision,
                        int tileSize, std::span<const world::AabbI> staticObstacles = {});
    // All damage sources use this entry point so the Player's hit reaction
    // remains consistent even when an attack definition requests another
    // knockback distance.
    void applyDamageKnockback(int requestedX, int requestedY,
                              const world::CollisionGrid& collision, int tileSize);
    void relocate(core::WorldPointI feetPosition, FacingDirection facing);
    void beginHurt() noexcept {
        actionState_ = PlayerActionState::hurt;
        motionState_ = PlayerMotionState::idle;
        lastMovement_ = {};
    }
    void finishAttack() noexcept { actionState_ = PlayerActionState::none; }

private:
    simulation::PlayerId id_{};
    CombatantState combatant_{};
    SubpixelPosition position_{};
    PlayerMovementConfig config_{};
    std::optional<ActorCollisionShapeDefinition> hurtboxShape_{};
    FacingDirection facing_{FacingDirection::down};
    PlayerMotionState motionState_{PlayerMotionState::idle};
    world::MovementResult lastMovement_{};
    PlayerActionState actionState_{PlayerActionState::none};
    AttackInstanceId attackInstance_{};
    AttackInstanceId nextAttackInstance_{1};
    int damageKnockbackRemainingX_{};
    int damageKnockbackRemainingY_{};
};

[[nodiscard]] const char* facingName(FacingDirection facing) noexcept;
[[nodiscard]] const char* motionStateName(PlayerMotionState state) noexcept;
[[nodiscard]] const char* actionStateName(PlayerActionState state) noexcept;

} // namespace underworld::game::gameplay
