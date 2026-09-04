#pragma once

namespace underworld::platform {

// Platform-neutral held input. Physical keys are mapped at the platform edge.
struct InputState final {
    bool moveUp{};
    bool moveDown{};
    bool moveLeft{};
    bool moveRight{};
    bool primaryAttackPressed{};
    bool secondaryAttackPressed{};
    bool interactPressed{};
    bool toggleInventoryPressed{};
    bool quickSlot1Pressed{};
    bool quickSlot2Pressed{};
    bool quickSlot3Pressed{};
    bool quickSlot4Pressed{};
    bool saveGamePressed{};
    bool loadGamePressed{};

    void clear() noexcept { *this = {}; }
    [[nodiscard]] constexpr bool operator==(const InputState&) const noexcept = default;
};

} // namespace underworld::platform
