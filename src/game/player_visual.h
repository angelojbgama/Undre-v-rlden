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

    PlayerVisual(DirectionalClips idleClips, DirectionalClips walkClips,
                 DirectionalClips swordClips = {}, DirectionalClips bowClips = {});

    void update(gameplay::PlayerMotionState motion, gameplay::FacingDirection facing,
                gameplay::PlayerActionState action,
                std::uint64_t ticks = 1);
    void update(gameplay::PlayerMotionState motion, gameplay::FacingDirection facing,
                std::uint64_t ticks = 1) {
        update(motion, facing, gameplay::PlayerActionState::none, ticks);
    }

    [[nodiscard]] const render::Animator& animator() const noexcept { return animator_; }
    [[nodiscard]] bool flipX() const noexcept { return flipX_; }
    [[nodiscard]] gameplay::FacingDirection facing() const noexcept { return facing_; }
    [[nodiscard]] gameplay::PlayerMotionState motion() const noexcept { return motion_; }
    [[nodiscard]] gameplay::PlayerActionState action() const noexcept { return action_; }
    [[nodiscard]] std::vector<render::AnimationMarkerEvent> consumeMarkerEvents();

private:
    [[nodiscard]] const std::shared_ptr<const render::AnimationClip>& selectedClip(
        gameplay::PlayerMotionState motion, gameplay::FacingDirection facing,
        gameplay::PlayerActionState action) const noexcept;

    DirectionalClips idleClips_{};
    DirectionalClips walkClips_{};
    DirectionalClips swordClips_{};
    DirectionalClips bowClips_{};
    render::Animator animator_{};
    gameplay::FacingDirection facing_{gameplay::FacingDirection::down};
    gameplay::PlayerMotionState motion_{gameplay::PlayerMotionState::idle};
    gameplay::PlayerActionState action_{gameplay::PlayerActionState::none};
    std::vector<render::AnimationMarkerEvent> markerEvents_;
    bool flipX_{};
    bool initialized_{};
};

} // namespace underworld::game
