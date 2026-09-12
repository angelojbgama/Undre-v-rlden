#include "game/gameplay/player.h"

#include "engine/core/coordinates.h"
#include "engine/world/collision_grid.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace underworld::game::gameplay {

namespace {

int checkedPixelCoordinate(std::int64_t subpixels) {
    const std::int64_t pixels = core::floorDiv(
        subpixels, static_cast<int>(PlayerMovementConfig::subpixelsPerPixel));
    if (pixels < std::numeric_limits<int>::min() || pixels > std::numeric_limits<int>::max()) {
        throw std::overflow_error("player position is outside integer world coordinates");
    }
    return static_cast<int>(pixels);
}

std::int64_t checkedSubpixelCoordinate(int pixels) {
    return static_cast<std::int64_t>(pixels) * PlayerMovementConfig::subpixelsPerPixel;
}

std::int64_t checkedAdd(std::int64_t value, std::int64_t delta) {
    if ((delta > 0 && value > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 && value < std::numeric_limits<std::int64_t>::min() - delta)) {
        throw std::overflow_error("player subpixel movement overflow");
    }
    return value + delta;
}

} // namespace

Player::Player(simulation::PlayerId id, simulation::EntityHandle entity,
               core::WorldPointI feetPosition,
               int maximumHealth,
               PlayerMovementConfig config)
    : id_(id), combatant_{entity, Faction::player, Health{maximumHealth}, 0, false},
      position_{checkedSubpixelCoordinate(feetPosition.x),
                checkedSubpixelCoordinate(feetPosition.y)},
      config_(config) {
    if (!entity) {
        throw std::invalid_argument("player requires a valid runtime entity handle");
    }
    if (!config_.footprints.valid()) {
        throw std::invalid_argument("player movement footprints must be positive");
    }
    if (config_.cornerSlideMaxProbePixels < 0 ||
        (config_.cornerSlideMaxProbePixels > 0 &&
         config_.cornerSlideCorrectionPixels <= 0)) {
        throw std::invalid_argument("player corner slide configuration is invalid");
    }
}

core::WorldPointI Player::feetPosition() const {
    return {checkedPixelCoordinate(position_.x), checkedPixelCoordinate(position_.y)};
}

world::AabbI Player::collisionBody() const {
    return config_.footprints.forFacing(facing_).at(feetPosition());
}

void Player::update(const simulation::PlayerCommand& command,
                    const world::CollisionGrid& collision, int tileSize,
                    std::span<const world::AabbI> staticObstacles) {
    if (command.playerId != id_) {
        throw std::invalid_argument("player command targets a different player id");
    }
    int moveX = command.movement.x;
    int moveY = command.movement.y;
    if (moveX < -1 || moveX > 1 || moveY < -1 || moveY > 1) {
        throw std::invalid_argument("player movement intent must be in the range -1 through 1");
    }
    if (combatant_.health.depleted()) {
        damageKnockbackRemainingX_ = 0;
        damageKnockbackRemainingY_ = 0;
        actionState_ = PlayerActionState::none;
        motionState_ = PlayerMotionState::idle;
        lastMovement_ = {};
        return;
    }

    if (damageKnockbackRemainingX_ != 0 || damageKnockbackRemainingY_ != 0) {
        const auto step = [](int remaining) noexcept {
            if (remaining == 0) { return 0; }
            const int direction = remaining > 0 ? 1 : -1;
            constexpr int stepPixels = damageKnockbackPixels / damageKnockbackDurationTicks;
            return direction * std::min(stepPixels, std::abs(remaining));
        };
        const int stepX = step(damageKnockbackRemainingX_);
        const int stepY = step(damageKnockbackRemainingY_);
        applyKnockback(stepX, stepY, collision, tileSize, staticObstacles);
        damageKnockbackRemainingX_ -= stepX;
        damageKnockbackRemainingY_ -= stepY;
        if (actionState_ == PlayerActionState::hurt &&
            damageKnockbackRemainingX_ == 0 && damageKnockbackRemainingY_ == 0) {
            actionState_ = PlayerActionState::none;
        }
    }

    if (actionState_ == PlayerActionState::none) {
        if (command.actions.primaryAttackPressed) {
            actionState_ = PlayerActionState::swordAttack;
            attackInstance_ = nextAttackInstance_++;
        } else if (command.actions.secondaryAttackPressed) {
            actionState_ = PlayerActionState::bowAttack;
            attackInstance_ = nextAttackInstance_++;
        }
    }
    if (actionState_ != PlayerActionState::none) {
        moveX = 0;
        moveY = 0;
    }

    motionState_ = moveX == 0 && moveY == 0
                       ? PlayerMotionState::idle
                       : PlayerMotionState::walk;
    // Vertical intent has explicit priority for diagonal facing.
    if (moveY < 0) {
        facing_ = FacingDirection::up;
    } else if (moveY > 0) {
        facing_ = FacingDirection::down;
    } else if (moveX < 0) {
        facing_ = FacingDirection::left;
    } else if (moveX > 0) {
        facing_ = FacingDirection::right;
    }

    std::int64_t speed = PlayerMovementConfig::cardinalSpeedSubpixelsPerTick;
    if (moveX != 0 && moveY != 0) {
        speed = (speed * PlayerMovementConfig::diagonalScaleNumerator +
                 PlayerMovementConfig::diagonalScaleDenominator / 2) /
                PlayerMovementConfig::diagonalScaleDenominator;
    }

    const SubpixelPosition target{
        checkedAdd(position_.x, speed * moveX),
        checkedAdd(position_.y, speed * moveY)};
    const auto oldFeet = feetPosition();
    const core::WorldPointI targetFeet{
        checkedPixelCoordinate(target.x), checkedPixelCoordinate(target.y)};
    world::AabbI body = collisionBody();
    lastMovement_ = world::moveAgainstSolidWorldWithCornerSlide(
        collision, body, targetFeet.x - oldFeet.x, targetFeet.y - oldFeet.y,
        tileSize, staticObstacles,
        {config_.cornerSlideMaxProbePixels, config_.cornerSlideCorrectionPixels});

    const auto& footprint = config_.footprints.forFacing(facing_);
    const core::WorldPointI resolvedFeet{
        body.x - footprint.offsetX,
        body.y - footprint.offsetY};

    position_.x = resolvedFeet.x == targetFeet.x
                      ? target.x
                      : checkedSubpixelCoordinate(resolvedFeet.x);
    position_.y = resolvedFeet.y == targetFeet.y
                      ? target.y
                      : checkedSubpixelCoordinate(resolvedFeet.y);
}

Hurtbox Player::hurtbox() const noexcept {
    const auto feet = feetPosition();
    return {{feet.x + hurtboxOffsetX, feet.y + hurtboxOffsetY,
             hurtboxWidth, hurtboxHeight}, !combatant_.health.depleted()};
}

CombatTargetRef Player::combatTarget() noexcept {
    return {combatant_, hurtbox()};
}

void Player::applyKnockback(int deltaX, int deltaY,
                            const world::CollisionGrid& collision, int tileSize,
                            std::span<const world::AabbI> staticObstacles) {
    world::AabbI body = collisionBody();
    [[maybe_unused]] const world::MovementResult movement = world::moveAgainstSolidWorld(
        collision, body, deltaX, deltaY, tileSize, staticObstacles);
    const auto& footprint = config_.footprints.forFacing(facing_);
    const core::WorldPointI resolvedFeet{
        body.x - footprint.offsetX,
        body.y - footprint.offsetY};
    position_.x = checkedSubpixelCoordinate(resolvedFeet.x);
    position_.y = checkedSubpixelCoordinate(resolvedFeet.y);
}

void Player::applyDamageKnockback(int requestedX, int requestedY,
                                  const world::CollisionGrid& collision, int tileSize) {
    const int knockbackX = requestedX == 0
                               ? 0
                               : (requestedX > 0 ? damageKnockbackPixels
                                                  : -damageKnockbackPixels);
    const int knockbackY = requestedY == 0
                               ? 0
                               : (requestedY > 0 ? damageKnockbackPixels
                                                  : -damageKnockbackPixels);
    static_cast<void>(collision);
    static_cast<void>(tileSize);
    damageKnockbackRemainingX_ = knockbackX;
    damageKnockbackRemainingY_ = knockbackY;
}

void Player::relocate(core::WorldPointI feetPosition, FacingDirection facing) {
    position_ = {checkedSubpixelCoordinate(feetPosition.x),
                 checkedSubpixelCoordinate(feetPosition.y)};
    facing_ = facing;
    motionState_ = PlayerMotionState::idle;
    actionState_ = PlayerActionState::none;
    lastMovement_ = {};
    damageKnockbackRemainingX_ = 0;
    damageKnockbackRemainingY_ = 0;
}

InteractionArea Player::interactionArea() const noexcept {
    const auto feet = feetPosition();
    return {{feet.x - 11, feet.y - 14, 22, 18}, true};
}

const char* facingName(FacingDirection facing) noexcept {
    switch (facing) {
    case FacingDirection::down: return "DOWN";
    case FacingDirection::up: return "UP";
    case FacingDirection::left: return "LEFT";
    case FacingDirection::right: return "RIGHT";
    }
    return "UNKNOWN";
}

const char* motionStateName(PlayerMotionState state) noexcept {
    return state == PlayerMotionState::walk ? "WALK" : "IDLE";
}

const char* actionStateName(PlayerActionState state) noexcept {
    switch (state) {
    case PlayerActionState::none: return "NONE";
    case PlayerActionState::swordAttack: return "SWORD";
    case PlayerActionState::bowAttack: return "BOW";
    case PlayerActionState::hurt: return "HURT";
    }
    return "UNKNOWN";
}

} // namespace underworld::game::gameplay
