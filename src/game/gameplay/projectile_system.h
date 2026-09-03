#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/entity_handle.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/facing_direction.h"

#include <cstdint>
#include <span>
#include <vector>

namespace underworld::simulation { class EventBuffer; }
namespace underworld::world { class CollisionGrid; }

namespace underworld::game::gameplay {

struct Projectile final {
    simulation::EntityHandle handle{};
    AttackKey attack{};
    Faction faction{Faction::neutral};
    const ProjectileDefinition* definition{};
    core::WorldPointI position{}; // visual/hitbox center
    FacingDirection direction{FacingDirection::up};
    DamageSpec damage{};
    std::uint32_t remainingTicks{};

    [[nodiscard]] world::AabbI hitbox() const noexcept;
};

class CombatSystem;

class ProjectileSystem final {
public:
    ProjectileSystem(simulation::EntityHandlePool& handles,
                     const ProjectileCatalog& definitions) noexcept
        : handles_(handles), definitions_(definitions) {}

    [[nodiscard]] simulation::EntityHandle spawn(
        AttackKey attack, Faction faction, const simulation::DefinitionId& definitionId,
        core::WorldPointI position, FacingDirection direction, DamageSpec damage);
    void update(const world::CollisionGrid& collision, int tileSize,
                std::span<CombatTargetRef> targets, CombatSystem& combat,
                simulation::EventBuffer& events,
                std::vector<CombatResolution>& resolutions);

    [[nodiscard]] const std::vector<Projectile>& projectiles() const noexcept {
        return projectiles_;
    }

private:
    simulation::EntityHandlePool& handles_;
    const ProjectileCatalog& definitions_;
    std::vector<Projectile> projectiles_;
};

} // namespace underworld::game::gameplay
