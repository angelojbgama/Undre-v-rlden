#include "game/gameplay/crafting_overlay.h"

#include <algorithm>

namespace underworld::game::gameplay {

void CraftingOverlayState::reset() noexcept {
    tab_ = CraftingTab::craft;
    selection_ = 0;
    craftQuantity_ = 1;
    feedback_.reset();
}

void CraftingOverlayState::moveSelection(const CraftingCatalog& catalog,
                                         const CraftingKnowledge& knowledge, int delta) noexcept {
    const auto& recipes = catalog.values();
    if (recipes.empty()) { selection_ = 0; return; }
    auto step = [&](int direction) {
        std::size_t candidate = selection_;
        for (std::size_t tries = 0; tries < recipes.size(); ++tries) {
            if (direction > 0) {
                candidate = candidate + 1 < recipes.size() ? candidate + 1 : 0;
            } else {
                candidate = candidate == 0 ? recipes.size() - 1 : candidate - 1;
            }
            if (knowledge.known(recipes[candidate])) { selection_ = candidate; return; }
        }
        // Nothing else is known: keep the current selection valid if it is.
        if (selection_ < recipes.size() && !knowledge.known(recipes[selection_])) { selection_ = 0; }
    };
    if (delta < 0 || delta > 0) { step(delta); }
    if (selection_ >= recipes.size()) { selection_ = 0; }
}

void CraftingOverlayState::adjustCraftQuantity(const CraftingCatalog& catalog,
                                               const CraftingKnowledge& knowledge,
                                               const ItemContainer& inventory,
                                               const CraftingService& service, int delta) noexcept {
    const auto& recipes = catalog.values();
    if (selection_ >= recipes.size()) { return; }
    const auto& recipe = recipes[selection_];
    if (!knowledge.known(recipe)) { return; }
    const auto maximum = std::max<std::uint32_t>(service.maxCraftable(recipe, inventory), 1);
    if (delta < 0 && craftQuantity_ > 1) { --craftQuantity_; }
    if (delta > 0 && craftQuantity_ < maximum) { ++craftQuantity_; }
}

CraftingCommandResult routeCraftingCommand(CraftingOverlayState& overlay,
                                           const simulation::PlayerCommand& command,
                                           const CraftingCatalog& catalog,
                                           const CraftingKnowledge& knowledge,
                                           ItemContainer& inventory,
                                           const CraftingService& service) {
    if (command.actions.secondaryAttackPressed) { overlay.switchTab(); }
    if (overlay.tab() == CraftingTab::craft) {
        overlay.moveSelection(catalog, knowledge, command.movement.y);
        overlay.adjustCraftQuantity(catalog, knowledge, inventory, service, command.movement.x);
        if (!command.actions.primaryAttackPressed && !command.actions.interactPressed) {
            return {true, std::nullopt};
        }
        const auto& recipes = catalog.values();
        if (overlay.selection() >= recipes.size()) { return {true, std::nullopt}; }
        const auto& recipe = recipes[overlay.selection()];
        if (!knowledge.known(recipe)) {
            overlay.setFeedback(CraftingStatus::recipeLocked);
            return {true, CraftingResult{CraftingStatus::recipeLocked, recipe.id, 0, {}, {}}};
        }
        CraftingResult transaction = service.craft(recipe, inventory, overlay.craftQuantity());
        overlay.setFeedback(transaction.status);
        return {true, transaction};
    }
    return {true, std::nullopt};
}

} // namespace underworld::game::gameplay
