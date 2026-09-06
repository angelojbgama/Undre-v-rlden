#include "game/gameplay/rpg/equipment.h"
#include "game/gameplay/items.h"

#include <algorithm>
#include <limits>

namespace underworld::game::gameplay::rpg {

std::optional<simulation::DefinitionId>& PlayerEquipment::itemRef(EquipmentSlot slot) noexcept {
    return items_[slot == EquipmentSlot::armor ? 0 : 1];
}
const std::optional<simulation::DefinitionId>& PlayerEquipment::item(EquipmentSlot slot) const noexcept {
    return items_[slot == EquipmentSlot::armor ? 0 : 1];
}

bool PlayerEquipment::equipFromInventory(EquipmentSlot slot, const simulation::DefinitionId& itemId,
                                         ::underworld::game::gameplay::ItemContainer& inventory,
                                         const ::underworld::game::gameplay::ItemCatalog& catalog) {
    const auto* definition = catalog.find(itemId);
    if (definition == nullptr || definition->category != gameplay::ItemCategory::equipment ||
        !definition->equipment || definition->equipment->slot != slot || inventory.count(itemId) == 0) return false;
    auto& current = itemRef(slot);
    if (current && inventory.canAdd(*current, 1).remainder != 0) return false;
    if (inventory.remove(itemId, 1) != 1) return false;
    if (current) {
        if (inventory.add(*current, 1).remainder != 0) { static_cast<void>(inventory.add(itemId, 1)); return false; }
    }
    current = itemId;
    return true;
}

bool PlayerEquipment::unequipToInventory(EquipmentSlot slot,
                                         ::underworld::game::gameplay::ItemContainer& inventory,
                                         const ::underworld::game::gameplay::ItemCatalog& catalog) {
    auto& current = itemRef(slot);
    if (!current || inventory.canAdd(*current, 1).remainder != 0) return false;
    const auto id = *current;
    if (inventory.add(id, 1).remainder != 0) return false;
    current.reset();
    static_cast<void>(catalog);
    return true;
}

void PlayerEquipment::restore(const std::optional<simulation::DefinitionId>& armor,
                              const std::optional<simulation::DefinitionId>& accessory) {
    items_[0] = armor; items_[1] = accessory;
}

PlayerDerivedStats derivePlayerStats(const PlayerBaseStats& baseStats,
                                     const PlayerEquipment& equipment,
                                     const ::underworld::game::gameplay::ItemCatalog& catalog) {
    PlayerDerivedStats result{baseStats.maximumHealth, 0};
    for (const auto slot : {EquipmentSlot::armor, EquipmentSlot::accessory}) {
        const auto& equipped = equipment.item(slot);
        if (!equipped) continue;
        const auto* definition = catalog.find(*equipped);
        if (!definition || !definition->equipment) continue;
        const auto& modifiers = definition->equipment->modifiers;
        if (modifiers.maximumHealthBonus > std::numeric_limits<int>::max() - result.maximumHealth) {
            result.maximumHealth = std::numeric_limits<int>::max();
        } else {
            result.maximumHealth += modifiers.maximumHealthBonus;
        }
        if (modifiers.playerAttackDamageBonus >
            std::numeric_limits<int>::max() - result.playerAttackDamageBonus) {
            result.playerAttackDamageBonus = std::numeric_limits<int>::max();
        } else {
            result.playerAttackDamageBonus += modifiers.playerAttackDamageBonus;
        }
    }
    return result;
}
}
