#include "game/gameplay/player_items.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay {

void QuickSlotBindings::bind(std::size_t index, simulation::DefinitionId itemId) {
    if (index >= slotCount) { throw std::out_of_range("quick slot index out of range"); }
    bindings_[index] = std::move(itemId);
}
void QuickSlotBindings::clear(std::size_t index) {
    if (index >= slotCount) { throw std::out_of_range("quick slot index out of range"); }
    bindings_[index].reset();
}
const std::optional<simulation::DefinitionId>& QuickSlotBindings::binding(
    std::size_t index) const {
    if (index >= slotCount) { throw std::out_of_range("quick slot index out of range"); }
    return bindings_[index];
}

ItemUseResult useItem(const simulation::DefinitionId& itemId, ItemContainer& inventory,
                      const ItemCatalog& catalog, Health& health) {
    if (inventory.count(itemId) == 0) { return {}; }
    const ItemDefinition& definition = catalog.require(itemId);
    if (!definition.use) { return {}; }
    ItemUseResult result{};
    if (definition.use->kind == ItemUseKind::restoreHealth) {
        result.healthRestored = health.restore(definition.use->amount);
        result.applied = result.healthRestored > 0;
    }
    if (result.applied && !inventory.consume(itemId)) {
        throw std::logic_error("successfully used item disappeared before consumption");
    }
    return result;
}

ItemUseResult PlayerItems::useQuickSlot(std::size_t index, const ItemCatalog& catalog,
                                        Health& health) {
    const auto& itemId = quickSlots_.binding(index);
    return itemId ? useItem(*itemId, inventory_.items(), catalog, health) : ItemUseResult{};
}

void InventoryOverlayState::moveSelection(int x, int y) noexcept {
    const int column = static_cast<int>(selection_ % columns);
    const int row = static_cast<int>(selection_ / columns);
    const int nextColumn = std::clamp(column + std::clamp(x, -1, 1), 0,
                                      static_cast<int>(columns - 1));
    const int nextRow = std::clamp(row + std::clamp(y, -1, 1), 0,
                                   static_cast<int>(rows - 1));
    selection_ = static_cast<std::size_t>(nextRow) * columns +
                 static_cast<std::size_t>(nextColumn);
}

} // namespace underworld::game::gameplay
