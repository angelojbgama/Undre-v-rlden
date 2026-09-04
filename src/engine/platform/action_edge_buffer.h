#pragma once

#include "engine/platform/input_state.h"

#include <array>
#include <cstddef>
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
    void pushInteract() noexcept { increment(interact_); }
    void pushToggleInventory() noexcept { increment(toggleInventory_); }
    void pushSaveGame() noexcept { increment(saveGame_); }
    void pushLoadGame() noexcept { increment(loadGame_); }
    void pushQuickSlot(std::size_t index) noexcept {
        if (index < quickSlots_.size()) { increment(quickSlots_[index]); }
    }
    void applyNext(InputState& state) noexcept {
        state.primaryAttackPressed = primary_ > 0;
        state.secondaryAttackPressed = secondary_ > 0;
        state.interactPressed = consume(interact_);
        state.toggleInventoryPressed = consume(toggleInventory_);
        state.quickSlot1Pressed = consume(quickSlots_[0]);
        state.quickSlot2Pressed = consume(quickSlots_[1]);
        state.quickSlot3Pressed = consume(quickSlots_[2]);
        state.quickSlot4Pressed = consume(quickSlots_[3]);
        state.saveGamePressed = consume(saveGame_);
        state.loadGamePressed = consume(loadGame_);
        if (primary_ > 0) { --primary_; }
        if (secondary_ > 0) { --secondary_; }
    }
    void clear() noexcept {
        primary_ = secondary_ = interact_ = toggleInventory_ = saveGame_ = loadGame_ = 0;
        quickSlots_.fill(0);
    }
    [[nodiscard]] std::uint32_t pendingPrimary() const noexcept { return primary_; }
    [[nodiscard]] std::uint32_t pendingSecondary() const noexcept { return secondary_; }

private:
    static void increment(std::uint32_t& value) noexcept {
        if (value != std::numeric_limits<std::uint32_t>::max()) { ++value; }
    }
    static bool consume(std::uint32_t& value) noexcept {
        if (value == 0) { return false; }
        --value;
        return true;
    }
    std::uint32_t primary_{};
    std::uint32_t secondary_{};
    std::uint32_t interact_{};
    std::uint32_t toggleInventory_{};
    std::uint32_t saveGame_{};
    std::uint32_t loadGame_{};
    std::array<std::uint32_t, 4> quickSlots_{};
};

} // namespace underworld::platform
