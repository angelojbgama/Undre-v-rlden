#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/entity_handle.h"
#include "engine/world/collision.h"

#include <cstdint>

namespace underworld::game::gameplay {

using AttackInstanceId = std::uint64_t;

enum class Faction {
    player,
    enemy,
    neutral,
};

enum class AttackKind {
    meleeHitbox,
    projectile,
};

struct CollisionBody final { world::AabbI bounds{}; };
struct Hurtbox final { world::AabbI bounds{}; bool enabled{true}; };
struct InteractionArea final { world::AabbI bounds{}; bool enabled{true}; };

struct Health final {
    int current{};
    int maximum{};

    explicit Health(int maximumHealth);
    [[nodiscard]] bool applyDamage(int amount);
    [[nodiscard]] bool depleted() const noexcept { return current == 0; }
};

struct DamageSpec final {
    int amount{1};
    int knockbackPixels{};
};

struct Hitbox final {
    world::AabbI bounds{};
    simulation::EntityHandle owner{};
    Faction faction{Faction::neutral};
    AttackInstanceId attackInstance{};
    DamageSpec damage{};
    int knockbackX{};
    int knockbackY{};
    bool enabled{};
};

struct CombatTarget final {
    simulation::EntityHandle handle{};
    Faction faction{Faction::neutral};
    core::WorldPointI feet{};
    CollisionBody collisionBody{};
    Hurtbox hurtbox{};
    Health health{1};
    std::uint32_t invulnerabilityTicks{};
    bool defeatEmitted{};
};

[[nodiscard]] bool factionsCanDamage(Faction attacker, Faction target) noexcept;
void tickInvulnerability(CombatTarget& target) noexcept;

} // namespace underworld::game::gameplay
