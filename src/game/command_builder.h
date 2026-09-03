#pragma once

#include "engine/simulation/player_command.h"

#include <cstdint>

namespace underworld::platform { struct InputState; }

namespace underworld::game {

// Composition boundary from a neutral input snapshot to a ticked simulation command.
class CommandBuilder final {
public:
    [[nodiscard]] simulation::PlayerCommand build(
        simulation::Tick tick, simulation::PlayerId playerId,
        const platform::InputState& input) noexcept;
    [[nodiscard]] std::uint32_t nextSequence() const noexcept { return nextSequence_; }

private:
    std::uint32_t nextSequence_{};
};

} // namespace underworld::game
