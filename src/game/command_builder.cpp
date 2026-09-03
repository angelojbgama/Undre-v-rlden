#include "game/command_builder.h"

#include "engine/platform/input_state.h"

namespace underworld::game {

simulation::PlayerCommand CommandBuilder::build(
    simulation::Tick tick, simulation::PlayerId playerId,
    const platform::InputState& input) noexcept {
    const int moveX = (input.moveRight ? 1 : 0) - (input.moveLeft ? 1 : 0);
    const int moveY = (input.moveDown ? 1 : 0) - (input.moveUp ? 1 : 0);
    // Primary has explicit priority when both edges reach the same fixed tick.
    const bool primary = input.primaryAttackPressed;
    const bool secondary = input.secondaryAttackPressed && !primary;
    return {tick, playerId, nextSequence_++, {moveX, moveY}, {primary, secondary}};
}

} // namespace underworld::game
