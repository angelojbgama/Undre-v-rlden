#pragma once

namespace underworld::platform {

// Platform-neutral held input. Physical keys are mapped at the platform edge.
struct InputState final {
    bool moveUp{};
    bool moveDown{};
    bool moveLeft{};
    bool moveRight{};

    void clear() noexcept { *this = {}; }
    [[nodiscard]] constexpr bool operator==(const InputState&) const noexcept = default;
};

} // namespace underworld::platform
