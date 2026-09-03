#pragma once

#include "engine/platform/input_state.h"

#include <cstdint>
#include <limits>

namespace underworld::platform {

// Preserves short logical action presses until a fixed tick consumes them.
class ActionEdgeBuffer final {
public:
    void pushPrimary() noexcept {
        if (primary_ != std::numeric_limits<std::uint32_t>::max()) { ++primary_; }
    }
    void pushSecondary() noexcept {
        if (secondary_ != std::numeric_limits<std::uint32_t>::max()) { ++secondary_; }
    }
    void applyNext(InputState& state) noexcept {
        state.primaryAttackPressed = primary_ > 0;
        state.secondaryAttackPressed = secondary_ > 0;
        if (primary_ > 0) { --primary_; }
        if (secondary_ > 0) { --secondary_; }
    }
    void clear() noexcept { primary_ = secondary_ = 0; }
    [[nodiscard]] std::uint32_t pendingPrimary() const noexcept { return primary_; }
    [[nodiscard]] std::uint32_t pendingSecondary() const noexcept { return secondary_; }

private:
    std::uint32_t primary_{};
    std::uint32_t secondary_{};
};

} // namespace underworld::platform
