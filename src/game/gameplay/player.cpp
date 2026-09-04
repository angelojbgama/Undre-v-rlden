#include "game/gameplay/player.h"

#include "engine/core/coordinates.h"
#include "engine/world/collision_grid.h"

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
               PlayerMovementConfig config)
    : id_(id), combatant_{entity, Faction::player, Health{maximumHealth}, 0, false},
      position_{checkedSubpixelCoordinate(feetPosition.x),
                checkedSubpixelCoordinate(feetPosition.y)},
      config_(config) {
    if (!entity) {
        throw std::invalid_argument("player requires a valid runtime entity handle");
    }
    if (config_.bodyWidth <= 0 || config_.bodyHeight <= 0) {
        throw std::invalid_argument("player collision body dimensions must be positive");
    }
}

core::WorldPointI Player::feetPosition() const {
    return {checkedPixelCoordinate(position_.x), checkedPixelCoordinate(position_.y)};
}

world::AabbI Player::collisionBody() const {
    const auto feet = feetPosition();
    return {feet.x + config_.bodyOffsetX, feet.y + config_.bodyOffsetY,
            config_.bodyWidth, config_.bodyHeight};
}

void Player::update(const simulation::PlayerCommand& command,
                    const world::CollisionGrid& collision, int tileSize) {
    if (command.playerId != id_) {
        throw std::invalid_argument("player command targets a different player id");
    }
    int moveX = command.movement.x;
    int moveY = command.movement.y;
    if (moveX < -1 || moveX > 1 || moveY < -1 || moveY > 1) {
        throw std::invalid_argument("player movement intent must be in the range -1 through 1");
    }
    if (combatant_.health.depleted()) {
        actionState_ = PlayerActionState::none;
        motionState_ = PlayerMotionState::idle;
        lastMovement_ = {};
        return;
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
    lastMovement_ = world::moveAgainstSolidTiles(
        collision, body, targetFeet.x - oldFeet.x, targetFeet.y - oldFeet.y, tileSize);

    const core::WorldPointI resolvedFeet{
        body.x - config_.bodyOffsetX,
        body.y - config_.bodyOffsetY};
    position_.x = lastMovement_.blockedX
                      ? checkedSubpixelCoordinate(resolvedFeet.x)
                      : target.x;
    position_.y = lastMovement_.blockedY
                      ? checkedSubpixelCoordinate(resolvedFeet.y)
                      : target.y;
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
                            const world::CollisionGrid& collision, int tileSize) {
    world::AabbI body = collisionBody();
    [[maybe_unused]] const world::MovementResult movement = world::moveAgainstSolidTiles(
        collision, body, deltaX, deltaY, tileSize);
    const core::WorldPointI resolvedFeet{
        body.x - config_.bodyOffsetX, body.y - config_.bodyOffsetY};
    position_.x = checkedSubpixelCoordinate(resolvedFeet.x);
    position_.y = checkedSubpixelCoordinate(resolvedFeet.y);
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
    }
    return "UNKNOWN";
}

} // namespace underworld::game::gameplay
