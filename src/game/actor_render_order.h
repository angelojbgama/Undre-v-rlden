#pragma once

#include "engine/simulation/entity_handle.h"

namespace underworld::game {

struct ActorRenderKey final {
    int feetY{};
    simulation::EntityHandle handle{};
};

[[nodiscard]] constexpr bool actorRendersBefore(ActorRenderKey left,
                                                ActorRenderKey right) noexcept {
    if (left.feetY != right.feetY) {
        return left.feetY < right.feetY;
    }
    if (left.handle.index != right.handle.index) {
        return left.handle.index < right.handle.index;
    }
    return left.handle.generation < right.handle.generation;
}

} // namespace underworld::game
