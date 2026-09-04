#pragma once

#include "engine/core/coordinates.h"
#include "engine/render/animation.h"

#include <memory>
#include <vector>

namespace underworld::game {

struct EffectInstance final {
    core::WorldPointI position{};
    render::Animator animator{};
};

class EffectSystem final {
public:
    explicit EffectSystem(std::shared_ptr<const render::AnimationClip> impactClip);

    void spawnImpact(core::WorldPointI position);
    void update(std::uint64_t ticks = 1);
    void clear() noexcept { effects_.clear(); }
    [[nodiscard]] const std::vector<EffectInstance>& effects() const noexcept { return effects_; }

private:
    std::shared_ptr<const render::AnimationClip> impactClip_;
    std::vector<EffectInstance> effects_;
};

} // namespace underworld::game
