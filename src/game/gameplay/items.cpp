#include "game/gameplay/items.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay {
namespace {

const simulation::DefinitionId lifePotion{"item.life_potion"};

void validate(const ItemDefinition& definition) {
    if (definition.id.empty() || definition.visualId.empty() || definition.stackLimit == 0) {
        throw std::invalid_argument("item definition is incomplete or invalid");
    }
    if (definition.category == ItemCategory::equipment && definition.stackLimit != 1) {
        throw std::invalid_argument("equipment item definitions must use stack limit one");
    }
    if (definition.use && definition.use->amount <= 0) {
        throw std::invalid_argument("item use amount must be positive");
    }
}

} // namespace

void ItemCatalog::add(ItemDefinition definition) {
    validate(definition);
    const auto [position, inserted] = definitions_.emplace(definition.id, std::move(definition));
    static_cast<void>(position);
    if (!inserted) { throw std::logic_error("duplicate item definition id"); }
}

const ItemDefinition* ItemCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}

const ItemDefinition& ItemCatalog::require(const simulation::DefinitionId& id) const {
    const ItemDefinition* definition = find(id);
    if (definition == nullptr) { throw std::out_of_range("item definition was not found"); }
    return *definition;
}

ItemContainer::ItemContainer(std::size_t capacity, const ItemCatalog& catalog)
    : catalog_(&catalog), slots_(capacity) {
    if (capacity == 0) { throw std::invalid_argument("item container capacity must be positive"); }
}

const std::optional<ItemStack>& ItemContainer::slot(std::size_t index) const {
    if (index >= slots_.size()) { throw std::out_of_range("item container slot out of range"); }
    return slots_[index];
}

AddResult ItemContainer::canAdd(const simulation::DefinitionId& itemId,
                                std::uint32_t quantity) const {
    const std::uint32_t limit = catalog_->require(itemId).stackLimit;
    std::uint64_t available{};
    for (const auto& candidate : slots_) {
        if (!candidate) {
            available += limit;
        } else if (candidate->itemId == itemId) {
            available += limit - candidate->quantity;
        }
        if (available >= quantity) { return {quantity, 0}; }
    }
    const auto accepted = static_cast<std::uint32_t>(available);
    return {accepted, quantity - accepted};
}

AddResult ItemContainer::add(const simulation::DefinitionId& itemId, std::uint32_t quantity) {
    const std::uint32_t limit = catalog_->require(itemId).stackLimit;
    std::uint32_t remaining = quantity;
    for (auto& candidate : slots_) {
        if (remaining == 0) { break; }
        if (candidate && candidate->itemId == itemId) {
            const std::uint32_t accepted = std::min(remaining, limit - candidate->quantity);
            candidate->quantity += accepted;
            remaining -= accepted;
        }
    }
    for (auto& candidate : slots_) {
        if (remaining == 0) { break; }
        if (!candidate) {
            const std::uint32_t accepted = std::min(remaining, limit);
            candidate = ItemStack{itemId, accepted};
            remaining -= accepted;
        }
    }
    return {quantity - remaining, remaining};
}

std::uint32_t ItemContainer::remove(const simulation::DefinitionId& itemId,
                                    std::uint32_t quantity) {
    std::uint32_t remaining = quantity;
    for (auto& candidate : slots_) {
        if (remaining == 0) { break; }
        if (!candidate || candidate->itemId != itemId) { continue; }
        const std::uint32_t removed = std::min(remaining, candidate->quantity);
        candidate->quantity -= removed;
        remaining -= removed;
        if (candidate->quantity == 0) { candidate.reset(); }
    }
    return quantity - remaining;
}

bool ItemContainer::consume(const simulation::DefinitionId& itemId) {
    return remove(itemId, 1) == 1;
}

std::uint64_t ItemContainer::count(const simulation::DefinitionId& itemId) const noexcept {
    std::uint64_t total{};
    for (const auto& candidate : slots_) {
        if (candidate && candidate->itemId == itemId) { total += candidate->quantity; }
    }
    return total;
}

std::uint32_t ItemContainer::transferTo(ItemContainer& destination,
                                        const simulation::DefinitionId& itemId,
                                        std::uint32_t quantity) {
    if (this == &destination || quantity == 0) { return 0; }
    const auto sourceAvailable = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(count(itemId), quantity));
    const std::uint32_t accepted = destination.canAdd(itemId, sourceAvailable).accepted;
    if (accepted == 0) { return 0; }
    const std::uint32_t removed = remove(itemId, accepted);
    const AddResult added = destination.add(itemId, removed);
    if (added.remainder != 0) {
        [[maybe_unused]] const AddResult restored = add(itemId, added.remainder);
        throw std::logic_error("item transfer destination changed unexpectedly");
    }
    return added.accepted;
}

void ItemContainer::restoreSlots(std::span<const std::optional<ItemStack>> slots) {
    if (slots.size() != slots_.size()) {
        throw std::invalid_argument("restored item slots do not match container capacity");
    }
    std::vector<std::optional<ItemStack>> validated(slots.begin(), slots.end());
    for (const auto& slot : validated) {
        if (!slot) { continue; }
        const auto& definition = catalog_->require(slot->itemId);
        if (slot->quantity == 0 || slot->quantity > definition.stackLimit) {
            throw std::invalid_argument("restored item stack quantity is invalid");
        }
    }
    slots_ = std::move(validated);
}

std::uint64_t Wallet::addGold(std::uint64_t amount) noexcept {
    const std::uint64_t space = std::numeric_limits<std::uint64_t>::max() - gold_;
    const std::uint64_t accepted = std::min(amount, space);
    gold_ += accepted;
    return amount - accepted;
}

const simulation::DefinitionId& lifePotionItemId() { return lifePotion; }

ItemDefinition makeLifePotionDefinition() {
    return {lifePotion, simulation::DefinitionId{"visual.item.life_potion"},
            ItemCategory::consumable, 66,
            ItemUseDefinition{ItemUseKind::restoreHealth, 2}};
}

} // namespace underworld::game::gameplay
