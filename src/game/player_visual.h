#pragma once

#include "engine/render/animation.h"
#include "game/gameplay/player.h"

#include <array>
#include <memory>

namespace underworld::game {

class PlayerVisual final {
public:
    using DirectionalClips =
        std::array<std::shared_ptr<const render::AnimationClip>, 3>; // down, up, side-left

    PlayerVisual(DirectionalClips idleClips, DirectionalClips walkClips);

    void update(gameplay::PlayerMotionState motion, gameplay::FacingDirection facing,
                std::uint64_t ticks = 1);

    [[nodiscard]] const render::Animator& animator() const noexcept { return animator_; }
    [[nodiscard]] bool flipX() const noexcept { return flipX_; }
    [[nodiscard]] gameplay::FacingDirection facing() const noexcept { return facing_; }
    [[nodiscard]] gameplay::PlayerMotionState motion() const noexcept { return motion_; }

private:
    [[nodiscard]] const std::shared_ptr<const render::AnimationClip>& selectedClip(
        gameplay::PlayerMotionState motion, gameplay::FacingDirection facing) const noexcept;

    DirectionalClips idleClips_{};
    DirectionalClips walkClips_{};
    render::Animator animator_{};
    gameplay::FacingDirection facing_{gameplay::FacingDirection::down};
    gameplay::PlayerMotionState motion_{gameplay::PlayerMotionState::idle};
    bool flipX_{};
    bool initialized_{};
};

} // namespace underworld::game
