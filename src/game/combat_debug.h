#pragma once

#include "engine/platform/debug_input.h"

namespace underworld::game {

struct CombatDebugVisibility final {
    bool collisionBody{};
    bool hurtbox{};
    bool hitbox{};
    bool interaction{};

    void apply(platform::DebugInputState input) noexcept {
        if (input.toggleCollisionBodyPressed) { collisionBody = !collisionBody; }
        if (input.toggleHurtboxPressed) { hurtbox = !hurtbox; }
        if (input.toggleHitboxPressed) { hitbox = !hitbox; }
        if (input.toggleInteractionPressed) { interaction = !interaction; }
    }
};

} // namespace underworld::game
