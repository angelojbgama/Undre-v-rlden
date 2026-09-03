#include "game/command_builder.h"

#include "engine/platform/input_state.h"

namespace underworld::game {

simulation::PlayerCommand CommandBuilder::build(
    simulation::Tick tick, simulation::PlayerId playerId,
    const platform::InputState& input) noexcept {
    const int moveX = (input.moveRight ? 1 : 0) - (input.moveLeft ? 1 : 0);
    const int moveY = (input.moveDown ? 1 : 0) - (input.moveUp ? 1 : 0);
    return {tick, playerId, nextSequence_++, {moveX, moveY}};
}

} // namespace underworld::game
