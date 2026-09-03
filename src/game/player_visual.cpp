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

PlayerVisual::PlayerVisual(DirectionalClips idleClips, DirectionalClips walkClips)
    : idleClips_(std::move(idleClips)), walkClips_(std::move(walkClips)) {
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
}

const std::shared_ptr<const render::AnimationClip>& PlayerVisual::selectedClip(
    gameplay::PlayerMotionState motion, gameplay::FacingDirection facing) const noexcept {
    const auto& clips = motion == gameplay::PlayerMotionState::walk ? walkClips_ : idleClips_;
    return clips[directionIndex(facing)];
}

void PlayerVisual::update(gameplay::PlayerMotionState motion,
                          gameplay::FacingDirection facing, std::uint64_t ticks) {
    if (!initialized_ || motion != motion_ || facing != facing_) {
        animator_.play(selectedClip(motion, facing));
        initialized_ = true;
    }
    motion_ = motion;
    facing_ = facing;
    flipX_ = facing == gameplay::FacingDirection::right;
    animator_.updateTicks(ticks);
}

} // namespace underworld::game
