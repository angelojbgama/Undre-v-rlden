#pragma once

namespace underworld::platform {

// Tool-only edge input. Gameplay never consumes this structure.
struct DebugInputState final {
    bool toggleCollisionPressed{};
};

} // namespace underworld::platform
