#include "game/gameplay/world_pickups.h"

#include "engine/simulation/events.h"
#include "game/gameplay/combat_system.h"

#include <stdexcept>

namespace underworld::game::gameplay {

WorldPickup::WorldPickup(simulation::EntityHandle handle,
                         const PickupDefinition& definition, core::WorldPointI position)
    : handle_(handle), definition_(&definition), position_(position), payload_(definition.payload) {
    if (!handle || definition.id.empty() || definition.visualId.empty() ||
        definition.collectionBounds.width <= 0 || definition.collectionBounds.height <= 0) {
        throw std::invalid_argument("world pickup definition or handle is invalid");
    }
    if ((std::holds_alternative<HealthPickup>(payload_) &&
         std::get<HealthPickup>(payload_).amount <= 0) ||
        (std::holds_alternative<CurrencyPickup>(payload_) &&
         std::get<CurrencyPickup>(payload_).amount == 0) ||
        (std::holds_alternative<ItemPickup>(payload_) &&
         std::get<ItemPickup>(payload_).quantity == 0)) {
        throw std::invalid_argument("world pickup payload amount must be positive");
    }
}

world::AabbI WorldPickup::collectionArea() const noexcept {
    return {position_.x + definition_->collectionBounds.x,
            position_.y + definition_->collectionBounds.y,
            definition_->collectionBounds.width, definition_->collectionBounds.height};
}
simulation::PickupPayloadKind payloadKind(const PickupPayload& payload) noexcept {
    if (std::holds_alternative<HealthPickup>(payload)) {
        return simulation::PickupPayloadKind::health;
    }
    if (std::holds_alternative<CurrencyPickup>(payload)) {
        return simulation::PickupPayloadKind::currency;
    }
    return simulation::PickupPayloadKind::item;
}

PickupCollectionResult collectPickup(
    WorldPickup& pickup, simulation::EntityHandle collector, world::AabbI collectorArea,
    Health& health, ItemContainer& inventory, Wallet& wallet,
    simulation::EntityHandlePool& handles, simulation::EventBuffer& events) {
    PickupCollectionResult result{};
    if (!handles.valid(pickup.handle()) || !overlaps(collectorArea, pickup.collectionArea())) {
        return result;
    }
    const simulation::PickupPayloadKind kind = payloadKind(pickup.payload());
    std::optional<simulation::DefinitionId> itemId;
    if (auto* healthPickup = std::get_if<HealthPickup>(&pickup.payload())) {
        result.amount = static_cast<std::uint64_t>(health.restore(healthPickup->amount));
        result.fullyConsumed = result.amount > 0;
    } else if (auto* currencyPickup = std::get_if<CurrencyPickup>(&pickup.payload())) {
        const std::uint64_t original = currencyPickup->amount;
        currencyPickup->amount = wallet.addGold(original);
        result.amount = original - currencyPickup->amount;
        result.fullyConsumed = currencyPickup->amount == 0;
    } else {
        auto& item = std::get<ItemPickup>(pickup.payload());
        itemId = item.itemId;
        const AddResult added = inventory.add(item.itemId, item.quantity);
        result.amount = added.accepted;
        item.quantity = added.remainder;
        result.fullyConsumed = added.remainder == 0;
    }
    result.collected = result.amount > 0;
    if (!result.collected) { return result; }
    events.emit(simulation::PickupCollected{
        collector, pickup.handle(), kind, itemId, result.amount});
    if (result.fullyConsumed) {
        [[maybe_unused]] const bool destroyed = handles.destroy(pickup.handle());
    }
    return result;
}

} // namespace underworld::game::gameplay
