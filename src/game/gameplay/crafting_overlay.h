#pragma once
#include "engine/simulation/player_command.h"
#include "game/gameplay/crafting.h"
#include <cstddef>
#include <optional>

namespace underworld::game::gameplay {

// Modal crafting UI state. The service stays the transactional authority;
// this type only tracks what the player is looking at and last feedback.
class CraftingOverlayState final {
public:
    void open(const CraftingCatalog& catalog) noexcept;
    void close() noexcept;
    [[nodiscard]] bool open() const noexcept { return open_; }
    [[nodiscard]] std::size_t selection() const noexcept { return selection_; }
    [[nodiscard]] const std::optional<CraftingStatus>& feedback() const noexcept { return feedback_; }
    void setFeedback(CraftingStatus status) noexcept { feedback_ = status; }
    // Selection is clamped to the live catalog size, so catalog changes never
    // leave the overlay pointing outside the list.
    void moveSelection(const CraftingCatalog& catalog, int delta) noexcept;

private:
    bool open_{};
    std::size_t selection_{};
    std::optional<CraftingStatus> feedback_{};
};

struct CraftingCommandResult final { bool consumedTick{}; std::optional<CraftingResult> transaction{}; };
[[nodiscard]] CraftingCommandResult routeCraftingCommand(CraftingOverlayState& overlay,
    const simulation::PlayerCommand& command, const CraftingCatalog& catalog,
    ItemContainer& inventory, const CraftingService& service);
} // namespace underworld::game::gameplay
