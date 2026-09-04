#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/entity_handle.h"
#include "engine/world/collision.h"

#include <cstdint>

namespace underworld::game::gameplay {

using AttackInstanceId = std::uint64_t;

struct AttackKey final {
    simulation::EntityHandle owner{};
    AttackInstanceId localInstance{};
    [[nodiscard]] constexpr bool operator==(const AttackKey&) const noexcept = default;
    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return static_cast<bool>(owner) && localInstance != 0;
    }
};

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
    [[nodiscard]] int restore(int amount);
    [[nodiscard]] bool depleted() const noexcept { return current == 0; }
};

struct DamageSpec final {
    int amount{1};
    int knockbackPixels{};
};

struct Hitbox final {
    world::AabbI bounds{};
    AttackKey attack{};
    Faction faction{Faction::neutral};
    DamageSpec damage{};
    int knockbackX{};
    int knockbackY{};
    bool enabled{};
};

struct CombatantState final {
    simulation::EntityHandle handle{};
    Faction faction{Faction::neutral};
    Health health{1};
    std::uint32_t invulnerabilityTicks{};
    bool defeatEmitted{};
};

// Ephemeral receiver view. Actor position and geometry remain owned by the actor.
struct CombatTargetRef final {
    CombatantState& combatant;
    Hurtbox hurtbox{};
};

struct CombatResolution final {
    simulation::EntityHandle target{};
    bool damaged{};
    bool defeated{};
    int requestedKnockbackX{};
    int requestedKnockbackY{};
};

[[nodiscard]] bool factionsCanDamage(Faction attacker, Faction target) noexcept;
void tickInvulnerability(CombatantState& target) noexcept;

} // namespace underworld::game::gameplay
