#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/entity_handle.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/facing_direction.h"

#include <cstdint>
#include <span>
#include <vector>

namespace underworld::simulation { class EventBuffer; }
namespace underworld::world { class CollisionGrid; }

namespace underworld::game::gameplay {

struct Projectile final {
    static constexpr int speedPixelsPerTick = 4;
    static constexpr std::uint32_t lifetimeTicks = 120;
    static constexpr int hitboxSize = 6;

    simulation::EntityHandle handle{};
    simulation::EntityHandle owner{};
    Faction faction{Faction::neutral};
    AttackInstanceId attackInstance{};
    core::WorldPointI position{}; // visual/hitbox center
    FacingDirection direction{FacingDirection::up};
    DamageSpec damage{};
    std::uint32_t remainingTicks{lifetimeTicks};

    [[nodiscard]] world::AabbI hitbox() const noexcept;
};

class CombatSystem;

class ProjectileSystem final {
public:
    explicit ProjectileSystem(simulation::EntityHandlePool& handles) noexcept
        : handles_(handles) {}

    [[nodiscard]] simulation::EntityHandle spawn(
        simulation::EntityHandle owner, Faction faction, AttackInstanceId attackInstance,
        core::WorldPointI position, FacingDirection direction, DamageSpec damage);
    void update(const world::CollisionGrid& collision, int tileSize,
                std::span<CombatTarget*> targets, CombatSystem& combat,
                simulation::EventBuffer& events);

    [[nodiscard]] const std::vector<Projectile>& projectiles() const noexcept {
        return projectiles_;
    }

private:
    simulation::EntityHandlePool& handles_;
    std::vector<Projectile> projectiles_;
};

} // namespace underworld::game::gameplay
