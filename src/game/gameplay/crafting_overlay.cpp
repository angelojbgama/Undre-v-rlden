#include "game/gameplay/crafting_overlay.h"

#include <algorithm>

namespace underworld::game::gameplay {

void CraftingOverlayState::open(const CraftingCatalog& catalog) noexcept {
    open_ = true;
    selection_ = 0;
    feedback_.reset();
    static_cast<void>(catalog);
}

void CraftingOverlayState::close() noexcept {
    open_ = false;
    selection_ = 0;
    feedback_.reset();
}

void CraftingOverlayState::moveSelection(const CraftingCatalog& catalog, int delta) noexcept {
    const auto count = catalog.values().size();
    if (count == 0) { selection_ = 0; return; }
    if (delta < 0 && selection_ > 0) { --selection_; }
    if (delta > 0 && selection_ + 1 < count) { ++selection_; }
    selection_ = std::min(selection_, count - 1);
}

CraftingCommandResult routeCraftingCommand(CraftingOverlayState& overlay,
                                           const simulation::PlayerCommand& command,
                                           const CraftingCatalog& catalog,
                                           ItemContainer& inventory,
                                           const CraftingService& service) {
    overlay.moveSelection(catalog, command.movement.y);
    if (!command.actions.primaryAttackPressed && !command.actions.interactPressed) {
        return {true, std::nullopt};
    }
    const auto& recipes = catalog.values();
    if (overlay.selection() >= recipes.size()) { return {true, std::nullopt}; }
    const auto& recipe = recipes[overlay.selection()];
    CraftingResult transaction = service.craft(recipe, inventory);
    overlay.setFeedback(transaction.status);
    return {true, transaction};
}

} // namespace underworld::game::gameplay
