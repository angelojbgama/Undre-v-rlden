#pragma once

#include "engine/simulation/definition_id.h"
#include "game/gameplay/items.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/bank_overlay.h"
#include "game/gameplay/rpg/equipment.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace underworld::game::gameplay { class Player; }

namespace underworld::game {

struct ItemSlotView final {
    std::optional<simulation::DefinitionId> itemId{};
    std::optional<simulation::DefinitionId> visualId{};
    std::uint64_t quantity{};
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
};

[[nodiscard]] GameViewModel buildGameViewModel(
    const gameplay::Player& player, const gameplay::PlayerItems& items,
    const gameplay::ItemCatalog& catalog,
    const gameplay::InventoryOverlayState& overlay,
    const gameplay::BankOverlayState& bankOverlay,
    const gameplay::rpg::PlayerDerivedStats& derivedStats);

} // namespace underworld::game
