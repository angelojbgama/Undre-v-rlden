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
    int quickSlot = -1;
    if (input.quickSlot1Pressed) { quickSlot = 0; }
    else if (input.quickSlot2Pressed) { quickSlot = 1; }
    else if (input.quickSlot3Pressed) { quickSlot = 2; }
    else if (input.quickSlot4Pressed) { quickSlot = 3; }
    return {tick, playerId, nextSequence_++, {moveX, moveY},
            {primary, secondary, input.interactPressed,
             input.toggleInventoryPressed, quickSlot}};
}

} // namespace underworld::game
