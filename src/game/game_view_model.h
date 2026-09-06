#pragma once

#include "engine/simulation/definition_id.h"
#include "game/gameplay/items.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/bank_overlay.h"
#include "game/gameplay/rpg/equipment.h"
#include "game/gameplay/rpg/shops.h"
#include "game/gameplay/shop_overlay.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace underworld::game::gameplay { class Player; }

namespace underworld::game {

struct ItemSlotView final {
    std::optional<simulation::DefinitionId> itemId{};
    std::optional<simulation::DefinitionId> visualId{};
    std::uint64_t quantity{};
};
struct ShopOfferView final {
    simulation::DefinitionId itemId{};
    std::optional<simulation::DefinitionId> visualId{};
    std::optional<std::uint64_t> playerBuyPrice{};
    std::optional<std::uint64_t> playerSellPrice{};
};

struct GameViewModel final {
    int playerHealth{};
    int playerMaximumHealth{};
    std::uint64_t gold{};
    std::array<ItemSlotView, gameplay::QuickSlotBindings::slotCount> quickSlots{};
    std::array<ItemSlotView, gameplay::PlayerInventory::slotCount> inventory{};
    bool inventoryOpen{};
    std::size_t inventorySelection{};
    gameplay::InventoryOverlayFocus inventoryFocus{gameplay::InventoryOverlayFocus::inventory};
    gameplay::rpg::EquipmentSlot equipmentSelection{gameplay::rpg::EquipmentSlot::armor};
    ItemSlotView armor{};
    ItemSlotView accessory{};
    int derivedMaximumHealth{};
    int playerAttackDamageBonus{};
    bool bankOpen{};
    gameplay::BankOverlayFocus bankFocus{gameplay::BankOverlayFocus::inventory};
    std::size_t bankSelection{};
    gameplay::BankGoldSelection bankGoldSelection{gameplay::BankGoldSelection::carried};
    std::array<ItemSlotView, gameplay::PlayerBank::slotCount> bank{};
    std::uint64_t bankGold{};
    bool shopOpen{};
    simulation::DefinitionId activeShopId{};
    gameplay::ShopOverlayMode shopMode{gameplay::ShopOverlayMode::buy};
    std::size_t shopBuySelection{};
    std::size_t shopInventorySelection{};
    std::vector<ShopOfferView> shopOffers;
    std::optional<gameplay::rpg::ShopTransactionStatus> shopFeedback{};
};

[[nodiscard]] GameViewModel buildGameViewModel(
    const gameplay::Player& player, const gameplay::PlayerItems& items,
    const gameplay::ItemCatalog& catalog,
    const gameplay::InventoryOverlayState& overlay,
    const gameplay::BankOverlayState& bankOverlay,
    const gameplay::rpg::PlayerDerivedStats& derivedStats,
    const gameplay::ShopOverlayState& shopOverlay,
    const gameplay::rpg::ShopCatalog& shops);
[[nodiscard]] GameViewModel buildGameViewModel(
    const gameplay::Player&, const gameplay::PlayerItems&, const gameplay::ItemCatalog&,
    const gameplay::InventoryOverlayState&, const gameplay::BankOverlayState&,
    const gameplay::rpg::PlayerDerivedStats&);

} // namespace underworld::game
