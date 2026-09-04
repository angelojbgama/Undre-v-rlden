#include "game/game_view_model.h"

#include "game/gameplay/player.h"

namespace underworld::game {
namespace {

ItemSlotView makeSlot(const gameplay::ItemStack& stack,
                      const gameplay::ItemCatalog& catalog) {
    const auto& definition = catalog.require(stack.itemId);
    return {stack.itemId, definition.visualId, stack.quantity};
}

} // namespace

GameViewModel buildGameViewModel(const gameplay::Player& player,
                                 const gameplay::PlayerItems& items,
                                 const gameplay::ItemCatalog& catalog,
                                 const gameplay::InventoryOverlayState& overlay) {
    GameViewModel result;
    result.playerHealth = player.health().current;
    result.playerMaximumHealth = player.health().maximum;
    result.gold = items.wallet().gold();
    result.inventoryOpen = overlay.open();
    result.inventorySelection = overlay.selection();
    for (std::size_t index = 0; index < result.inventory.size(); ++index) {
        const auto& slot = items.inventory().items().slot(index);
        if (slot) { result.inventory[index] = makeSlot(*slot, catalog); }
    }
    for (std::size_t index = 0; index < result.quickSlots.size(); ++index) {
        const auto& binding = items.quickSlots().binding(index);
        if (!binding) { continue; }
        const auto& definition = catalog.require(*binding);
        result.quickSlots[index] = {*binding, definition.visualId,
            items.inventory().items().count(*binding)};
    }
    return result;
}

} // namespace underworld::game
