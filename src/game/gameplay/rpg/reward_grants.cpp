#include "game/gameplay/rpg/reward_grants.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace underworld::game::gameplay::rpg {

void RewardGrantCatalog::add(RewardGrantDefinition definition) {
    if (definition.id.empty()) throw std::invalid_argument("reward grant id is empty");
    if (find(definition.id)) throw std::logic_error("duplicate reward grant id");
    definitions_.push_back(std::move(definition));
}
const RewardGrantDefinition* RewardGrantCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto it = std::find_if(definitions_.begin(), definitions_.end(), [&](const auto& value) { return value.id == id; });
    return it == definitions_.end() ? nullptr : &*it;
}
const RewardGrantDefinition& RewardGrantCatalog::require(const simulation::DefinitionId& id) const {
    const auto* value = find(id); if (!value) throw std::out_of_range("reward grant not found"); return *value;
}

RewardGrantResult RewardGrantService::grant(const RewardGrantDefinition& definition,
                                            PlayerProgressionState& progression,
                                            PlayerItems& items) const {
    auto& inventory = items.inventory().items();
    auto& bank = items.bank();
    // The source containers already perform the authoritative stack/capacity checks.
    // Preflight on local snapshots without mutating PlayerItems.
    auto inventorySlots = std::vector<std::optional<ItemStack>>{};
    auto bankSlots = std::vector<std::optional<ItemStack>>{};
    for (std::size_t i = 0; i < inventory.capacity(); ++i) inventorySlots.push_back(inventory.slot(i));
    for (std::size_t i = 0; i < bank.items().capacity(); ++i) bankSlots.push_back(bank.items().slot(i));
    const auto walletGold = items.wallet().gold();
    const auto bankGold = bank.gold();
    auto restore = [&] { inventory.restoreSlots(inventorySlots); bank.items().restoreSlots(bankSlots); items.wallet().restoreGold(walletGold); bank.restoreGold(bankGold); };
    for (const auto& item : definition.items) {
        auto remaining = item.quantity;
        const auto accepted = inventory.add(item.itemId, remaining);
        remaining = accepted.remainder;
        if (remaining) remaining = bank.items().add(item.itemId, remaining).remainder;
        if (remaining) { restore(); return {false, true, {}, 0}; }
    }
    auto goldRemaining = definition.gold;
    goldRemaining = items.wallet().addGold(goldRemaining);
    if (goldRemaining) goldRemaining = bank.addGold(goldRemaining);
    if (goldRemaining) { restore(); return {false, true, {}, 0}; }
    const auto gain = progression.grantExperience(definition.experience);
    return {true, false, gain, definition.gold};
}

} // namespace underworld::game::gameplay::rpg
