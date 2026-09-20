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
    static const gameplay::CraftingCatalog noCrafting;
    static const gameplay::CraftingOverlayState noCraftingOverlay;
    return buildGameViewModel(player, items, catalog, overlay, bankOverlay, derivedStats,
                              noShopOverlay, noShops, noCraftingOverlay, noCrafting);
}

GameViewModel buildGameViewModel(const gameplay::Player& player,
                                 const gameplay::PlayerItems& items,
                                 const gameplay::ItemCatalog& catalog,
                                 const gameplay::InventoryOverlayState& overlay,
                                 const gameplay::BankOverlayState& bankOverlay,
                                 const gameplay::rpg::PlayerDerivedStats& derivedStats,
                                 const gameplay::ShopOverlayState& shopOverlay,
                                 const gameplay::rpg::ShopCatalog& shops) {
    static const gameplay::CraftingCatalog noCrafting;
    static const gameplay::CraftingOverlayState noCraftingOverlay;
    return buildGameViewModel(player, items, catalog, overlay, bankOverlay, derivedStats,
                              shopOverlay, shops, noCraftingOverlay, noCrafting);
}

GameViewModel buildGameViewModel(const gameplay::Player& player,
                                 const gameplay::PlayerItems& items,
                                 const gameplay::ItemCatalog& catalog,
                                 const gameplay::InventoryOverlayState& overlay,
                                 const gameplay::BankOverlayState& bankOverlay,
                                 const gameplay::rpg::PlayerDerivedStats& derivedStats,
                                 const gameplay::ShopOverlayState& shopOverlay,
                                 const gameplay::rpg::ShopCatalog& shops,
                                 const gameplay::CraftingOverlayState& craftingOverlay,
                                 const gameplay::CraftingCatalog& crafting) {
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
    result.craftingOpen = craftingOverlay.open();
    result.craftingSelection = craftingOverlay.selection();
    result.craftingFeedback = craftingOverlay.feedback();
    if (craftingOverlay.open()) {
        const gameplay::CraftingService craftingService;
        result.craftingRecipes.reserve(crafting.values().size());
        for (const auto& recipe : crafting.values()) {
            CraftingRecipeView recipeView;
            recipeView.recipeId = recipe.id;
            bool craftable = true;
            for (const auto& input : recipe.inputs) {
                const auto owned = items.inventory().items().count(input.itemId);
                if (owned < input.quantity) { craftable = false; }
                recipeView.inputs.push_back({input.itemId, catalog.find(input.itemId) != nullptr
                    ? std::optional<simulation::DefinitionId>{catalog.require(input.itemId).visualId}
                    : std::nullopt, owned, input.quantity});
            }
            for (const auto& output : recipe.outputs) {
                recipeView.outputs.push_back({output.itemId, catalog.find(output.itemId) != nullptr
                    ? std::optional<simulation::DefinitionId>{catalog.require(output.itemId).visualId}
                    : std::nullopt, output.quantity});
            }
            recipeView.maxCraftable = craftingService.maxCraftable(recipe, items.inventory().items());
            recipeView.craftable = craftable && recipeView.maxCraftable > 0;
            result.craftingRecipes.push_back(std::move(recipeView));
        }
    }
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
