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

GameViewModel buildGameViewModel(const gameplay::Player& player, const gameplay::PlayerItems& items,
                                 const gameplay::ItemCatalog& catalog,
                                 const gameplay::InventoryOverlayState& overlay,
                                 const gameplay::BankOverlayState& bankOverlay,
                                 const gameplay::rpg::PlayerDerivedStats& derivedStats) {
    static const gameplay::rpg::ShopCatalog noShops;
    static const gameplay::ShopOverlayState noShopOverlay;
    return buildGameViewModel(player, items, catalog, overlay, bankOverlay, derivedStats,
                              noShopOverlay, noShops);
}

GameViewModel buildGameViewModel(const gameplay::Player& player,
                                 const gameplay::PlayerItems& items,
                                 const gameplay::ItemCatalog& catalog,
                                 const gameplay::InventoryOverlayState& overlay,
                                 const gameplay::BankOverlayState& bankOverlay,
                                 const gameplay::rpg::PlayerDerivedStats& derivedStats,
                                 const gameplay::ShopOverlayState& shopOverlay,
                                 const gameplay::rpg::ShopCatalog& shops) {
    GameViewModel result;
    result.playerHealth = player.health().current;
    result.playerMaximumHealth = player.health().maximum;
    result.gold = items.wallet().gold();
    result.inventoryOpen = overlay.open();
    result.inventorySelection = overlay.selection();
    result.inventoryFocus = overlay.equipmentFocused()
        ? gameplay::InventoryOverlayFocus::equipment : gameplay::InventoryOverlayFocus::inventory;
    result.equipmentSelection = overlay.equipmentSelection();
    result.derivedMaximumHealth = derivedStats.maximumHealth;
    result.playerAttackDamageBonus = derivedStats.playerAttackDamageBonus;
    result.bankOpen = bankOverlay.open();
    result.bankFocus = bankOverlay.focus();
    result.bankSelection = bankOverlay.bankSelection();
    result.bankGoldSelection = bankOverlay.goldSelection();
    result.bankGold = items.bank().gold();
    result.shopOpen = shopOverlay.open();
    result.shopMode = shopOverlay.mode();
    result.shopBuySelection = shopOverlay.buySelection();
    result.shopInventorySelection = shopOverlay.inventorySelection();
    result.shopFeedback = shopOverlay.feedback();
    if (shopOverlay.open()) {
        result.activeShopId = shopOverlay.activeShopId();
        if (const auto* shop = shops.find(result.activeShopId)) {
            for (const auto& offer : shop->offers) {
                const auto& item = catalog.require(offer.itemId);
                result.shopOffers.push_back({offer.itemId, item.visualId, offer.playerBuyPrice, offer.playerSellPrice});
            }
        }
    }
    const auto makeEquipment = [&](gameplay::rpg::EquipmentSlot slot) {
        const auto& item = items.equipment().item(slot);
        if (!item) { return ItemSlotView{}; }
        return makeSlot(gameplay::ItemStack{*item, 1}, catalog);
    };
    result.armor = makeEquipment(gameplay::rpg::EquipmentSlot::armor);
    result.accessory = makeEquipment(gameplay::rpg::EquipmentSlot::accessory);
    for (std::size_t index = 0; index < result.inventory.size(); ++index) {
        const auto& slot = items.inventory().items().slot(index);
        if (slot) { result.inventory[index] = makeSlot(*slot, catalog); }
    }
    for (std::size_t index = 0; index < result.bank.size(); ++index) {
        const auto& slot = items.bank().items().slot(index);
        if (slot) { result.bank[index] = makeSlot(*slot, catalog); }
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
