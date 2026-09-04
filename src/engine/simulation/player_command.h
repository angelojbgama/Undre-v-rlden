#pragma once

#include <cstdint>

namespace underworld::simulation {

using Tick = std::uint64_t;

struct PlayerId final {
    std::uint32_t value{};
    [[nodiscard]] constexpr bool operator==(const PlayerId&) const noexcept = default;
};

struct MovementIntent final {
    int x{};
    int y{};
    [[nodiscard]] constexpr bool operator==(const MovementIntent&) const noexcept = default;
};

struct ActionIntent final {
    bool primaryAttackPressed{};
    bool secondaryAttackPressed{};
    bool interactPressed{};
    bool toggleInventoryPressed{};
    int quickSlotPressed{-1};
    [[nodiscard]] constexpr bool operator==(const ActionIntent&) const noexcept = default;
};

struct PlayerCommand final {
    Tick tick{};
    PlayerId playerId{};
    std::uint32_t sequence{};
    MovementIntent movement{};
    ActionIntent actions{};
};

} // namespace underworld::simulation
