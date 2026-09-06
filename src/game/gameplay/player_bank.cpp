#include "game/gameplay/player_bank.h"

#include <algorithm>
#include <limits>

namespace underworld::game::gameplay {

std::uint32_t PlayerBank::depositItem(ItemContainer& inventory,
                                      const simulation::DefinitionId& itemId,
                                      std::uint32_t quantity) {
    return inventory.transferTo(items_, itemId, quantity);
}

std::uint32_t PlayerBank::withdrawItem(ItemContainer& inventory,
                                       const simulation::DefinitionId& itemId,
                                       std::uint32_t quantity) {
    return items_.transferTo(inventory, itemId, quantity);
}

std::uint64_t PlayerBank::depositGold(Wallet& wallet, std::uint64_t amount) noexcept {
    const auto available = wallet.gold();
    const auto requested = std::min(amount, available);
    const auto space = std::numeric_limits<std::uint64_t>::max() - gold_;
    const auto moved = std::min(requested, space);
    static_cast<void>(wallet.removeGold(moved));
    gold_ += moved;
    return moved;
}

std::uint64_t PlayerBank::withdrawGold(Wallet& wallet, std::uint64_t amount) noexcept {
    const auto moved = std::min(amount, gold_);
    const auto space = std::numeric_limits<std::uint64_t>::max() - wallet.gold();
    const auto accepted = std::min(moved, space);
    static_cast<void>(wallet.addGold(accepted));
    gold_ -= accepted;
    return accepted;
}

} // namespace underworld::game::gameplay
