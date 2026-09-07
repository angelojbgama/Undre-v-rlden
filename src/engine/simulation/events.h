#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/entity_handle.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/persistent_id.h"

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
    DefinitionId defeatedDefinitionId{};
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
    DefinitionId pickupDefinitionId{};
};

struct NpcTalked final {
    EntityHandle player{};
    EntityHandle npc{};
    DefinitionId npcDefinitionId{};
};

struct MapEntered final {
    MapId mapId{};
};

struct RegionEntered final { MapId mapId{}; DefinitionId regionId{}; };
struct RegionExited final { MapId mapId{}; DefinitionId regionId{}; };
struct EncounterStarted final { MapId mapId{}; DefinitionId encounterId{}; };
struct EncounterCompleted final { MapId mapId{}; DefinitionId encounterId{}; };

struct ObjectOpened final {
    EntityHandle player{};
    EntityHandle object{};
    DefinitionId objectDefinitionId{};
    PersistentInstanceId objectInstanceId{};
    MapId mapId{};
};

struct ItemDelivered final {
    EntityHandle player{};
    DefinitionId itemId{};
    std::uint64_t amount{};
};

struct ExperienceGranted final {
    EntityHandle player{};
    DefinitionId sourceDefinitionId{};
    std::uint64_t amount{};
    std::uint64_t totalExperience{};
    std::uint32_t previousLevel{};
    std::uint32_t newLevel{};
};

using SimulationEvent = std::variant<EntityDamaged, EntityDefeated, ProjectileImpact,
                                     PickupCollected, NpcTalked, MapEntered, RegionEntered,
                                     RegionExited, EncounterStarted, EncounterCompleted, ObjectOpened,
                                     ItemDelivered, ExperienceGranted>;

class EventBuffer final {
public:
    template <typename Event>
    void emit(Event event) { events_.emplace_back(std::move(event)); }
    void clear() noexcept { events_.clear(); }
    [[nodiscard]] const std::vector<SimulationEvent>& events() const noexcept { return events_; }
    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }
    [[nodiscard]] const SimulationEvent& eventAt(std::size_t index) const noexcept {
        return events_[index];
    }

private:
    std::vector<SimulationEvent> events_;
};

} // namespace underworld::simulation
