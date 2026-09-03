#include "game/effect_system.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace underworld::game {

EffectSystem::EffectSystem(std::shared_ptr<const render::AnimationClip> impactClip)
    : impactClip_(std::move(impactClip)) {
    if (!impactClip_ || impactClip_->loops()) {
        throw std::invalid_argument("effect system requires a non-looping impact clip");
    }
}

void EffectSystem::spawnImpact(core::WorldPointI position) {
    EffectInstance effect{position, {}};
    effect.animator.play(impactClip_);
    effects_.push_back(std::move(effect));
}

void EffectSystem::update(std::uint64_t ticks) {
    for (EffectInstance& effect : effects_) {
        effect.animator.updateTicks(ticks);
    }
    std::erase_if(effects_, [](const EffectInstance& effect) {
        return effect.animator.finished();
    });
}

} // namespace underworld::game
