#pragma once

#include "game/gameplay/combat_types.h"
#include "game/gameplay/facing_direction.h"

#include <cstdint>

namespace underworld::game::gameplay {

enum class AttackId { sword, bow };

struct AttackDefinition final {
    AttackId id{AttackId::sword};
    AttackKind kind{AttackKind::meleeHitbox};
    DamageSpec damage{};
    std::uint32_t totalTicks{};
};

inline constexpr AttackDefinition swordAttack{
    AttackId::sword, AttackKind::meleeHitbox, {1, 8}, 24};
inline constexpr AttackDefinition bowAttack{
    AttackId::bow, AttackKind::projectile, {1, 6}, 16};

[[nodiscard]] world::AabbI swordHitboxBounds(core::WorldPointI feet,
                                             FacingDirection facing) noexcept;
[[nodiscard]] core::WorldPointI directionVector(FacingDirection facing) noexcept;
[[nodiscard]] core::WorldPointI arrowSpawnPosition(core::WorldPointI feet,
                                                   FacingDirection facing) noexcept;

} // namespace underworld::game::gameplay
