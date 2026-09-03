#include "game/player_visual.h"

#include <stdexcept>
#include <utility>

namespace underworld::game {

namespace {

std::size_t directionIndex(gameplay::FacingDirection facing) noexcept {
    switch (facing) {
    case gameplay::FacingDirection::down: return 0;
    case gameplay::FacingDirection::up: return 1;
    case gameplay::FacingDirection::left:
    case gameplay::FacingDirection::right: return 2;
    }
    return 0;
}

} // namespace

PlayerVisual::PlayerVisual(DirectionalClips idleClips, DirectionalClips walkClips,
                           DirectionalClips swordClips, DirectionalClips bowClips)
    : idleClips_(std::move(idleClips)), walkClips_(std::move(walkClips)),
      swordClips_(std::move(swordClips)), bowClips_(std::move(bowClips)) {
    for (const auto& clip : idleClips_) {
        if (!clip) {
            throw std::invalid_argument("player visual requires every idle direction clip");
        }
    }
    for (const auto& clip : walkClips_) {
        if (!clip) {
            throw std::invalid_argument("player visual requires every walk direction clip");
        }
    }
    const bool hasSword = swordClips_[0] != nullptr;
    const bool hasBow = bowClips_[0] != nullptr;
    for (const auto& clip : swordClips_) {
        if ((clip != nullptr) != hasSword) {
            throw std::invalid_argument("player visual sword clips must be all present or absent");
        }
    }
    for (const auto& clip : bowClips_) {
        if ((clip != nullptr) != hasBow) {
            throw std::invalid_argument("player visual bow clips must be all present or absent");
        }
    }
}

const std::shared_ptr<const render::AnimationClip>& PlayerVisual::selectedClip(
    gameplay::PlayerMotionState motion, gameplay::FacingDirection facing,
    gameplay::PlayerActionState action) const noexcept {
    if (action == gameplay::PlayerActionState::swordAttack && swordClips_[0]) {
        return swordClips_[directionIndex(facing)];
    }
    if (action == gameplay::PlayerActionState::bowAttack && bowClips_[0]) {
        return bowClips_[directionIndex(facing)];
    }
    const auto& clips = motion == gameplay::PlayerMotionState::walk ? walkClips_ : idleClips_;
    return clips[directionIndex(facing)];
}

void PlayerVisual::update(gameplay::PlayerMotionState motion,
                          gameplay::FacingDirection facing,
                          gameplay::PlayerActionState action, std::uint64_t ticks) {
    if (!initialized_ || motion != motion_ || facing != facing_ || action != action_) {
        animator_.play(selectedClip(motion, facing, action));
        initialized_ = true;
    }
    motion_ = motion;
    facing_ = facing;
    action_ = action;
    flipX_ = facing == gameplay::FacingDirection::right;
    animator_.updateTicks(ticks, markerEvents_);
}

std::vector<render::AnimationMarkerEvent> PlayerVisual::consumeMarkerEvents() {
    std::vector<render::AnimationMarkerEvent> result;
    result.swap(markerEvents_);
    return result;
}

} // namespace underworld::game
