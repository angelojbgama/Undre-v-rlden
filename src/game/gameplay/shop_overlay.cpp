#include "game/gameplay/shop_overlay.h"
#include <algorithm>
namespace underworld::game::gameplay {
void ShopOverlayState::open(const rpg::ShopDefinition& shop) noexcept {
    open_ = true; activeShopId_ = shop.id; buySelection_ = 0; inventorySelection_ = 0; feedback_.reset();
    mode_ = std::any_of(shop.offers.begin(), shop.offers.end(), [](const auto& offer) { return offer.playerBuyPrice.has_value(); }) ? ShopOverlayMode::buy : ShopOverlayMode::sell;
    moveBuySelection(shop, 0);
}
void ShopOverlayState::close() noexcept { open_ = false; activeShopId_ = {}; feedback_.reset(); }
void ShopOverlayState::moveBuySelection(const rpg::ShopDefinition& shop, int delta) noexcept {
    std::vector<std::size_t> available;
    for (std::size_t i = 0; i < shop.offers.size(); ++i) if (shop.offers[i].playerBuyPrice) available.push_back(i);
    if (available.empty()) { buySelection_ = 0; return; }
    auto it = std::find(available.begin(), available.end(), buySelection_);
    std::size_t position = it == available.end() ? 0 : static_cast<std::size_t>(it - available.begin());
    if (delta < 0 && position > 0) --position;
    if (delta > 0 && position + 1 < available.size()) ++position;
    buySelection_ = available[position];
}
void ShopOverlayState::moveInventorySelection(int x, int y) noexcept {
    const int col = static_cast<int>(inventorySelection_ % columns), row = static_cast<int>(inventorySelection_ / columns);
    inventorySelection_ = static_cast<std::size_t>(std::clamp(row + std::clamp(y, -1, 1), 0, static_cast<int>(inventoryRows - 1))) * columns + static_cast<std::size_t>(std::clamp(col + std::clamp(x, -1, 1), 0, static_cast<int>(columns - 1)));
}
void ShopOverlayState::switchMode(const rpg::ShopDefinition& shop) noexcept {
    const auto wanted = mode_ == ShopOverlayMode::buy ? ShopOverlayMode::sell : ShopOverlayMode::buy;
    if (std::any_of(shop.offers.begin(), shop.offers.end(), [&](const auto& offer) { return wanted == ShopOverlayMode::buy ? offer.playerBuyPrice.has_value() : offer.playerSellPrice.has_value(); })) { mode_ = wanted; feedback_.reset(); }
}
ShopCommandResult routeShopCommand(ShopOverlayState& overlay, const simulation::PlayerCommand& command, const rpg::ShopDefinition& shop, PlayerItems& items, const rpg::ShopTransactionService& service) {
    if (command.actions.secondaryAttackPressed) overlay.switchMode(shop);
    if (overlay.mode() == ShopOverlayMode::buy) overlay.moveBuySelection(shop, command.movement.y); else overlay.moveInventorySelection(command.movement.x, command.movement.y);
    if (!command.actions.primaryAttackPressed && !command.actions.interactPressed) return {true, std::nullopt};
    rpg::ShopTransactionResult transaction;
    if (overlay.mode() == ShopOverlayMode::buy) {
        if (overlay.buySelection() >= shop.offers.size()) return {true, std::nullopt};
        transaction = service.buyOne(shop, shop.offers[overlay.buySelection()].itemId, items);
    } else transaction = service.sellOneFromSlot(shop, overlay.inventorySelection(), items);
    overlay.setFeedback(transaction.status); return {true, transaction};
}
} // namespace underworld::game::gameplay
