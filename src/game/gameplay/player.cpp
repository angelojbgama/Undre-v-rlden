#include "game/gameplay/player.h"

#include "engine/core/coordinates.h"
#include "engine/world/collision_grid.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

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
               PlayerMovementConfig config,
               std::optional<ActorCollisionShapeDefinition> hurtboxShape,
               std::optional<PlayerHurtboxFrameProfile>
                   hurtboxFrameOverrides)
    : id_(id), combatant_{entity, Faction::player, Health{maximumHealth}, 0, false},
      position_{checkedSubpixelCoordinate(feetPosition.x),
                checkedSubpixelCoordinate(feetPosition.y)},
      config_(config), hurtboxShape_(std::move(hurtboxShape)),
      hurtboxFrameOverrides_(std::move(hurtboxFrameOverrides)) {
    if (!entity) {
        throw std::invalid_argument("player requires a valid runtime entity handle");
    }
    if (!config_.footprints.valid()) {
        throw std::invalid_argument("player movement footprints must be positive");
    }
    if (config_.collisionShapes && !config_.collisionShapes->valid()) {
        throw std::invalid_argument(
            "player authored movement collision must contain positive regions");
    }
    if (hurtboxShape_ && !hurtboxShape_->valid()) {
        throw std::invalid_argument(
            "player authored hurtbox must contain positive regions");
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

std::vector<world::AabbI> Player::collisionRegions() const {
    const auto feet = feetPosition();
    if (config_.collisionShapes) {
        return config_.collisionShapes->forFacing(facing_).at(feet);
    }
    return {config_.footprints.forFacing(facing_).at(feet)};
}

world::AabbI Player::collisionBody() const {
    const auto regions = collisionRegions();
    world::AabbI bounds = regions.front();
    int right = bounds.x + bounds.width;
    int bottom = bounds.y + bounds.height;
    for (std::size_t index = 1; index < regions.size(); ++index) {
        const auto& region = regions[index];
        const int regionRight = region.x + region.width;
        const int regionBottom = region.y + region.height;
        const int left = std::min(bounds.x, region.x);
        const int top = std::min(bounds.y, region.y);
        right = std::max(right, regionRight);
        bottom = std::max(bottom, regionBottom);
        bounds.x = left;
        bounds.y = top;
    }
    bounds.width = right - bounds.x;
    bounds.height = bottom - bounds.y;
    return bounds;
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
    const auto previousMotion = motionState_;
    const auto previousFacing = facing_;
    const auto previousAction = actionState_;

    if (combatant_.health.depleted()) {
        damageKnockbackRemainingX_ = 0;
        damageKnockbackRemainingY_ = 0;
        actionState_ = PlayerActionState::none;
        motionState_ = PlayerMotionState::idle;
        gameplayFrameTicks_ = 0;
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
    auto bodies = collisionRegions();
    lastMovement_ = world::moveAgainstSolidWorldWithCornerSlide(
        collision, std::span<world::AabbI>{bodies},
        targetFeet.x - oldFeet.x, targetFeet.y - oldFeet.y,
        tileSize, staticObstacles,
        {config_.cornerSlideMaxProbePixels, config_.cornerSlideCorrectionPixels});

    const core::WorldPointI resolvedFeet{
        oldFeet.x + lastMovement_.movedX,
        oldFeet.y + lastMovement_.movedY};

    position_.x = resolvedFeet.x == targetFeet.x
                      ? target.x
                      : checkedSubpixelCoordinate(resolvedFeet.x);
    position_.y = resolvedFeet.y == targetFeet.y
                      ? target.y
                      : checkedSubpixelCoordinate(resolvedFeet.y);

    if (motionState_ != previousMotion ||
        facing_ != previousFacing ||
        actionState_ != previousAction) {
        gameplayFrameTicks_ = 0;
    } else if (gameplayFrameTicks_ <
               std::numeric_limits<std::uint64_t>::max()) {
        ++gameplayFrameTicks_;
    }
}

const PlayerHurtboxTimeline* Player::currentHurtboxTimeline()
    const noexcept {
    if (!hurtboxFrameOverrides_) return nullptr;

    const DirectionalPlayerHurtboxTimelines* directional = nullptr;
    switch (actionState_) {
    case PlayerActionState::swordAttack: {
        const auto found =
            hurtboxFrameOverrides_->actions.find("sword");
        if (found != hurtboxFrameOverrides_->actions.end()) {
            directional = &found->second;
        }
        break;
    }
    case PlayerActionState::bowAttack: {
        const auto found =
            hurtboxFrameOverrides_->actions.find("bow");
        if (found != hurtboxFrameOverrides_->actions.end()) {
            directional = &found->second;
        }
        break;
    }
    case PlayerActionState::hurt:
        if (hurtboxFrameOverrides_->hurt) {
            directional = &*hurtboxFrameOverrides_->hurt;
        }
        break;
    case PlayerActionState::none:
        directional = motionState_ == PlayerMotionState::walk
            ? &hurtboxFrameOverrides_->walk
            : &hurtboxFrameOverrides_->idle;
        break;
    }

    if (directional == nullptr) return nullptr;
    const auto& timeline = directional->forFacing(facing_);
    return timeline.authored ? &timeline : nullptr;
}

const ActorCollisionShapeDefinition* Player::currentHurtboxOverride()
    const noexcept {
    const auto* timeline = currentHurtboxTimeline();
    if (timeline == nullptr ||
        timeline->samples.empty() ||
        timeline->totalTicks == 0) {
        return nullptr;
    }

    std::uint64_t tick = gameplayFrameTicks_;
    if (timeline->loop) {
        tick %= timeline->totalTicks;
    } else if (tick >= timeline->totalTicks) {
        tick = timeline->totalTicks - 1U;
    }

    const PlayerHurtboxFrameSample* selected = nullptr;
    for (const auto& sample : timeline->samples) {
        if (sample.tick > tick) break;
        selected = &sample;
    }
    if (selected == nullptr || !selected->shape) return nullptr;
    return &*selected->shape;
}

Hurtbox Player::hurtboxFromShape(
    const ActorCollisionShapeDefinition& shape) const {
    auto regions = shape.at(feetPosition());
    world::AabbI bounds = regions.front();
    int right = bounds.x + bounds.width;
    int bottom = bounds.y + bounds.height;
    for (std::size_t index = 1; index < regions.size(); ++index) {
        const auto& region = regions[index];
        const int regionRight = region.x + region.width;
        const int regionBottom = region.y + region.height;
        const int left = std::min(bounds.x, region.x);
        const int top = std::min(bounds.y, region.y);
        right = std::max(right, regionRight);
        bottom = std::max(bottom, regionBottom);
        bounds.x = left;
        bounds.y = top;
    }
    bounds.width = right - bounds.x;
    bounds.height = bottom - bounds.y;
    return {
        bounds, !combatant_.health.depleted(),
        std::move(regions)};
}

Hurtbox Player::hurtbox() const {
    if (const auto* overrideShape = currentHurtboxOverride()) {
        return hurtboxFromShape(*overrideShape);
    }
    if (hurtboxShape_) {
        return hurtboxFromShape(*hurtboxShape_);
    }

    const auto feet = feetPosition();
    return {{feet.x + hurtboxOffsetX, feet.y + hurtboxOffsetY,
             hurtboxWidth, hurtboxHeight},
            !combatant_.health.depleted(), {}};
}

CombatTargetRef Player::combatTarget() {
    return {combatant_, hurtbox()};
}

void Player::applyKnockback(int deltaX, int deltaY,
                            const world::CollisionGrid& collision, int tileSize,
                            std::span<const world::AabbI> staticObstacles) {
    const auto oldFeet = feetPosition();
    auto bodies = collisionRegions();
    const world::MovementResult movement = world::moveAgainstSolidWorld(
        collision, std::span<world::AabbI>{bodies},
        deltaX, deltaY, tileSize, staticObstacles);
    const core::WorldPointI resolvedFeet{
        oldFeet.x + movement.movedX,
        oldFeet.y + movement.movedY};
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
    gameplayFrameTicks_ = 0;
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
