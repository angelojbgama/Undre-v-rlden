#include "game/gameplay/rpg/shops.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace underworld::game::gameplay::rpg {

void ShopCatalog::add(ShopDefinition definition) {
    if (definition.id.empty()) throw std::invalid_argument("shop id is empty");
    if (find(definition.id)) throw std::logic_error("duplicate shop id");
    definitions_.push_back(std::move(definition));
}
const ShopDefinition* ShopCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto it = std::find_if(definitions_.begin(), definitions_.end(), [&](const auto& value) { return value.id == id; });
    return it == definitions_.end() ? nullptr : &*it;
}
const ShopDefinition& ShopCatalog::require(const simulation::DefinitionId& id) const {
    const auto* value = find(id); if (!value) throw std::out_of_range("shop not found"); return *value;
}
const ShopOfferDefinition* findOffer(const ShopDefinition& shop, const simulation::DefinitionId& itemId) noexcept {
    const auto it = std::find_if(shop.offers.begin(), shop.offers.end(), [&](const auto& value) { return value.itemId == itemId; });
    return it == shop.offers.end() ? nullptr : &*it;
}

ShopTransactionResult ShopTransactionService::buyOne(const ShopDefinition& shop,
                                                      const simulation::DefinitionId& itemId,
                                                      PlayerItems& playerItems) const {
    const auto* offer = findOffer(shop, itemId);
    if (!offer) return {ShopTransactionStatus::offerNotFound, itemId};
    if (!offer->playerBuyPrice) return {ShopTransactionStatus::purchaseUnavailable, itemId};
    auto& inventory = playerItems.inventory().items();
    if (inventory.canAdd(itemId, 1).remainder != 0) return {ShopTransactionStatus::inventoryFull, itemId};
    const auto price = *offer->playerBuyPrice;
    if (playerItems.wallet().gold() < price) return {ShopTransactionStatus::insufficientGold, itemId};
    const auto added = inventory.add(itemId, 1);
    if (added.remainder != 0) return {ShopTransactionStatus::inventoryFull, itemId};
    static_cast<void>(playerItems.wallet().removeGold(price));
    return {ShopTransactionStatus::success, itemId, 1, price};
}

ShopTransactionResult ShopTransactionService::sellOneFromSlot(const ShopDefinition& shop,
                                                               std::size_t inventorySlot,
                                                               PlayerItems& playerItems) const {
    auto& inventory = playerItems.inventory().items();
    try {
        const auto& source = inventory.slot(inventorySlot);
        if (!source) return {ShopTransactionStatus::invalidInventorySlot};
        const auto itemId = source->itemId;
        const auto* offer = findOffer(shop, itemId);
        if (!offer) return {ShopTransactionStatus::offerNotFound, itemId};
        if (!offer->playerSellPrice) return {ShopTransactionStatus::saleUnavailable, itemId};
        const auto price = *offer->playerSellPrice;
        if (std::numeric_limits<std::uint64_t>::max() - playerItems.wallet().gold() < price)
            return {ShopTransactionStatus::walletCapacityExceeded, itemId};
        if (inventory.removeFromSlot(inventorySlot, 1) != 1) return {ShopTransactionStatus::invalidInventorySlot, itemId};
        const auto remainder = playerItems.wallet().addGold(price);
        if (remainder != 0) { throw std::logic_error("wallet changed unexpectedly during shop sale"); }
        return {ShopTransactionStatus::success, itemId, 1, price};
    } catch (const std::out_of_range&) {
        return {ShopTransactionStatus::invalidInventorySlot};
    }
}

} // namespace underworld::game::gameplay::rpg
