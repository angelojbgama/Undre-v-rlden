#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/player_command.h"
#include "engine/world/collision.h"

#include <cstdint>

namespace underworld::world { class CollisionGrid; }

namespace underworld::game::gameplay {

enum class FacingDirection {
    down,
    up,
    left,
    right,
};

enum class PlayerMotionState {
    idle,
    walk,
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

private:
    simulation::PlayerId id_{};
    SubpixelPosition position_{};
    PlayerMovementConfig config_{};
    FacingDirection facing_{FacingDirection::down};
    PlayerMotionState motionState_{PlayerMotionState::idle};
    world::MovementResult lastMovement_{};
};

[[nodiscard]] const char* facingName(FacingDirection facing) noexcept;
[[nodiscard]] const char* motionStateName(PlayerMotionState state) noexcept;

} // namespace underworld::game::gameplay
