#pragma once
#include "engine/simulation/player_command.h"
#include "game/gameplay/crafting.h"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace underworld::game::gameplay {

// The crafting UI lives as a tab of the inventory overlay (always available),
// so this type only tracks what the player is looking at inside that tab:
// craft/book page, selected known recipe, batch quantity and last feedback.
enum class CraftingTab { craft, book };

class CraftingOverlayState final {
public:
    void reset() noexcept;
    [[nodiscard]] CraftingTab tab() const noexcept { return tab_; }
    void switchTab() noexcept { tab_ = tab_ == CraftingTab::craft ? CraftingTab::book : CraftingTab::craft; }
    [[nodiscard]] std::size_t selection() const noexcept { return selection_; }
    [[nodiscard]] const std::optional<CraftingStatus>& feedback() const noexcept { return feedback_; }
    void setFeedback(CraftingStatus status) noexcept { feedback_ = status; }
    // Selection walks only recipes the player knows, so locked quest recipes
    // are never selectable in the craft page.
    void moveSelection(const CraftingCatalog& catalog, const CraftingKnowledge& knowledge,
                       int delta) noexcept;
    [[nodiscard]] std::uint32_t craftQuantity() const noexcept { return craftQuantity_; }
    // Batch size for the next craft; clamped to what the inventory can pay for
    // and absorb right now so the typed failure stays meaningful.
    void adjustCraftQuantity(const CraftingCatalog& catalog, const CraftingKnowledge& knowledge,
                             const ItemContainer& inventory, const CraftingService& service,
                             int delta) noexcept;

private:
    CraftingTab tab_{CraftingTab::craft};
    std::size_t selection_{};
    std::uint32_t craftQuantity_{1};
    std::optional<CraftingStatus> feedback_{};
};

struct CraftingCommandResult final { bool consumedTick{}; std::optional<CraftingResult> transaction{}; };
[[nodiscard]] CraftingCommandResult routeCraftingCommand(CraftingOverlayState& overlay,
    const simulation::PlayerCommand& command, const CraftingCatalog& catalog,
    const CraftingKnowledge& knowledge, ItemContainer& inventory, const CraftingService& service);
} // namespace underworld::game::gameplay
