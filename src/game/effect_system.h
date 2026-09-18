#pragma once

#include "engine/core/coordinates.h"
#include "engine/render/animation.h"
#include "engine/render/renderer_2d.h"

#include <memory>
#include <vector>

namespace underworld::game {

struct EffectInstance final {
    core::WorldPointI position{};
    render::Animator animator{};
    render::QuarterTurn rotation{render::QuarterTurn::r0};
    // When set the effect never erases: it keeps drawing the animation's
    // last frame in place (e.g. an arrow stuck in the ground).
    bool holdLastFrame{false};
};

class EffectSystem final {
public:
    explicit EffectSystem(std::shared_ptr<const render::AnimationClip> impactClip);

    void spawnImpact(core::WorldPointI position);
    void spawnAnimation(core::WorldPointI position,
                        std::shared_ptr<const render::AnimationClip> clip,
                        bool holdLastFrame = false,
                        render::QuarterTurn rotation = render::QuarterTurn::r0);
    void update(std::uint64_t ticks = 1);
    void clear() noexcept { effects_.clear(); }
    [[nodiscard]] const std::vector<EffectInstance>& effects() const noexcept { return effects_; }

private:
    std::shared_ptr<const render::AnimationClip> impactClip_;
    std::vector<EffectInstance> effects_;
};

} // namespace underworld::game
