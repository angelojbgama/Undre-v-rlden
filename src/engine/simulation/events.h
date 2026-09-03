#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/entity_handle.h"

#include <cstdint>
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

using SimulationEvent = std::variant<EntityDamaged, EntityDefeated, ProjectileImpact>;

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
