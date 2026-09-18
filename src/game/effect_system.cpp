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
    spawnAnimation(position, impactClip_);
}

void EffectSystem::spawnAnimation(core::WorldPointI position,
                                  std::shared_ptr<const render::AnimationClip> clip,
                                  bool holdLastFrame,
                                  render::QuarterTurn rotation,
                                  bool flipX) {
    if (!clip || clip->loops()) {
        // Looping clips would never finish, so they can never be cleaned up.
        return;
    }
    EffectInstance effect{position, {}, rotation};
    effect.animator.play(std::move(clip));
    effect.holdLastFrame = holdLastFrame;
    effect.flipX = flipX;
    effects_.push_back(std::move(effect));
}

void EffectSystem::update(std::uint64_t ticks) {
    for (EffectInstance& effect : effects_) {
        effect.animator.updateTicks(ticks);
    }
    std::erase_if(effects_, [](const EffectInstance& effect) {
        return effect.animator.finished() && !effect.holdLastFrame;
    });
}

} // namespace underworld::game
