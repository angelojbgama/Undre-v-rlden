#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/entity_handle.h"
#include "engine/simulation/events.h"
#include "engine/world/collision.h"
#include "game/gameplay/items.h"
#include "game/gameplay/combat_types.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace underworld::simulation { class EventBuffer; }

namespace underworld::game::gameplay {

struct HealthPickup final { int amount{}; };
struct CurrencyPickup final { std::uint64_t amount{}; };
struct ItemPickup final { simulation::DefinitionId itemId{}; std::uint32_t quantity{}; };
using PickupPayload = std::variant<HealthPickup, CurrencyPickup, ItemPickup>;

struct PickupDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualId{};
    world::AabbI collectionBounds{};
    PickupPayload payload{};
};

class WorldPickup final {
public:
    WorldPickup(simulation::EntityHandle handle, const PickupDefinition& definition,
                core::WorldPointI position);
    WorldPickup(const WorldPickup&) = delete;
    WorldPickup& operator=(const WorldPickup&) = delete;
    WorldPickup(WorldPickup&&) noexcept = default;
    WorldPickup& operator=(WorldPickup&&) noexcept = default;
    [[nodiscard]] simulation::EntityHandle handle() const noexcept { return handle_; }
    [[nodiscard]] const PickupDefinition& definition() const noexcept { return *definition_; }
    [[nodiscard]] core::WorldPointI position() const noexcept { return position_; }
    [[nodiscard]] world::AabbI collectionArea() const noexcept;
    [[nodiscard]] const PickupPayload& payload() const noexcept { return payload_; }
    [[nodiscard]] PickupPayload& payload() noexcept { return payload_; }

private:
    simulation::EntityHandle handle_{};
    const PickupDefinition* definition_{};
    core::WorldPointI position_{};
    PickupPayload payload_{};
};

struct PickupCollectionResult final { bool collected{}; bool fullyConsumed{}; std::uint64_t amount{}; };

[[nodiscard]] simulation::PickupPayloadKind payloadKind(const PickupPayload& payload) noexcept;
[[nodiscard]] PickupCollectionResult collectPickup(
    WorldPickup& pickup, simulation::EntityHandle collector, world::AabbI collectorArea,
    Health& health, ItemContainer& inventory, Wallet& wallet,
    simulation::EntityHandlePool& handles, simulation::EventBuffer& events);

} // namespace underworld::game::gameplay
