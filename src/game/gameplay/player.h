#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/player_command.h"
#include "engine/world/collision.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/facing_direction.h"

#include <cstdint>

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
};

struct PlayerMovementConfig final {
    static constexpr std::int64_t subpixelsPerPixel = 256;
    static constexpr std::int64_t cardinalSpeedSubpixelsPerTick = 384; // 90 px/s at 60 Hz.
    static constexpr std::int64_t diagonalScaleNumerator = 181;
    static constexpr std::int64_t diagonalScaleDenominator = 256;

    int bodyWidth{10};
    int bodyHeight{8};
    int bodyOffsetX{-5};
    int bodyOffsetY{-8};
};

struct SubpixelPosition final {
    std::int64_t x{};
    std::int64_t y{};
    [[nodiscard]] constexpr bool operator==(const SubpixelPosition&) const noexcept = default;
};

class Player final {
public:
    static constexpr int maximumHealth = 5;
    static constexpr int hurtboxWidth = 14;
    static constexpr int hurtboxHeight = 22;
    static constexpr int hurtboxOffsetX = -7;
    static constexpr int hurtboxOffsetY = -22;

    Player(simulation::PlayerId id, core::WorldPointI feetPosition,
           PlayerMovementConfig config = {});

    void update(const simulation::PlayerCommand& command,
                const world::CollisionGrid& collision, int tileSize);

    [[nodiscard]] simulation::PlayerId id() const noexcept { return id_; }
    [[nodiscard]] SubpixelPosition subpixelPosition() const noexcept { return position_; }
    [[nodiscard]] core::WorldPointI feetPosition() const;
    [[nodiscard]] world::AabbI collisionBody() const;
    [[nodiscard]] FacingDirection facing() const noexcept { return facing_; }
    [[nodiscard]] PlayerMotionState motionState() const noexcept { return motionState_; }
    [[nodiscard]] const PlayerMovementConfig& movementConfig() const noexcept { return config_; }
    [[nodiscard]] const world::MovementResult& lastMovement() const noexcept {
        return lastMovement_;
    }
    [[nodiscard]] PlayerActionState actionState() const noexcept { return actionState_; }
    [[nodiscard]] AttackInstanceId attackInstance() const noexcept { return attackInstance_; }
    [[nodiscard]] Health& health() noexcept { return health_; }
    [[nodiscard]] const Health& health() const noexcept { return health_; }
    [[nodiscard]] Hurtbox hurtbox() const noexcept;
    [[nodiscard]] InteractionArea interactionArea() const noexcept;
    void finishAttack() noexcept { actionState_ = PlayerActionState::none; }

private:
    simulation::PlayerId id_{};
    SubpixelPosition position_{};
    PlayerMovementConfig config_{};
    FacingDirection facing_{FacingDirection::down};
    PlayerMotionState motionState_{PlayerMotionState::idle};
    world::MovementResult lastMovement_{};
    PlayerActionState actionState_{PlayerActionState::none};
    AttackInstanceId attackInstance_{};
    AttackInstanceId nextAttackInstance_{1};
    Health health_{maximumHealth};
};

[[nodiscard]] const char* facingName(FacingDirection facing) noexcept;
[[nodiscard]] const char* motionStateName(PlayerMotionState state) noexcept;
[[nodiscard]] const char* actionStateName(PlayerActionState state) noexcept;

} // namespace underworld::game::gameplay
