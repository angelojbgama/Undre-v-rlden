#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/entity_handle.h"
#include "engine/simulation/definition_id.h"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>
#include <utility>

namespace underworld::simulation {

struct EntityDamaged final {
    EntityHandle attacker{};
    EntityHandle target{};
    int amount{};
    int remainingHealth{};
    std::uint64_t attackInstanceId{};
};

struct EntityDefeated final {
    EntityHandle attacker{};
    EntityHandle target{};
    std::uint64_t attackInstanceId{};
};

enum class ProjectileImpactKind { tile, target, expired };

struct ProjectileImpact final {
    EntityHandle projectile{};
    core::WorldPointI position{};
    ProjectileImpactKind kind{ProjectileImpactKind::tile};
};

enum class PickupPayloadKind { health, currency, item };

struct PickupCollected final {
    EntityHandle collector{};
    EntityHandle pickup{};
    PickupPayloadKind kind{PickupPayloadKind::health};
    std::optional<DefinitionId> itemId{};
    std::uint64_t amount{};
};

using SimulationEvent = std::variant<EntityDamaged, EntityDefeated, ProjectileImpact,
                                     PickupCollected>;

class EventBuffer final {
public:
    template <typename Event>
    void emit(Event event) { events_.emplace_back(std::move(event)); }
    void clear() noexcept { events_.clear(); }
    [[nodiscard]] const std::vector<SimulationEvent>& events() const noexcept { return events_; }

private:
    std::vector<SimulationEvent> events_;
};

} // namespace underworld::simulation
