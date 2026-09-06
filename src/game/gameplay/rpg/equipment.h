#pragma once

#include "game/gameplay/rpg/player_progression.h"

#include <array>
#include <optional>

namespace underworld::game::gameplay { class ItemContainer; class ItemCatalog; }
namespace underworld::game::gameplay::rpg {

enum class EquipmentSlot { armor, accessory };
struct EquipmentModifiers final { int maximumHealthBonus{}; int playerAttackDamageBonus{}; };
struct EquipmentDefinition final { EquipmentSlot slot{EquipmentSlot::armor}; EquipmentModifiers modifiers{}; };

class PlayerEquipment final {
public:
    [[nodiscard]] const std::optional<simulation::DefinitionId>& item(EquipmentSlot slot) const noexcept;
    [[nodiscard]] bool equipFromInventory(EquipmentSlot slot, const simulation::DefinitionId& itemId,
                                           ::underworld::game::gameplay::ItemContainer& inventory,
                                           const ::underworld::game::gameplay::ItemCatalog& catalog);
    [[nodiscard]] bool unequipToInventory(EquipmentSlot slot,
                                          ::underworld::game::gameplay::ItemContainer& inventory,
                                          const ::underworld::game::gameplay::ItemCatalog& catalog);
    void restore(const std::optional<simulation::DefinitionId>& armor,
                 const std::optional<simulation::DefinitionId>& accessory);
private:
    [[nodiscard]] std::optional<simulation::DefinitionId>& itemRef(EquipmentSlot slot) noexcept;
    std::array<std::optional<simulation::DefinitionId>, 2> items_{};
};

struct PlayerDerivedStats final { int maximumHealth{}; int playerAttackDamageBonus{}; };

[[nodiscard]] PlayerDerivedStats derivePlayerStats(
    const PlayerBaseStats& baseStats, const PlayerEquipment& equipment,
    const ::underworld::game::gameplay::ItemCatalog& catalog);

} // namespace underworld::game::gameplay::rpg
