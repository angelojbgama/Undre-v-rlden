#pragma once

#include "game/gameplay/combat_types.h"

#include <vector>

namespace underworld::simulation { class EventBuffer; }
namespace underworld::world { class CollisionGrid; }

namespace underworld::game::gameplay {

class CombatSystem final {
public:
    static constexpr std::uint32_t invulnerabilityDurationTicks = 12;

    [[nodiscard]] CombatResolution resolve(const Hitbox& attack, CombatTargetRef target,
                                           simulation::EventBuffer& events);
    void finishAttack(AttackKey attack) noexcept;
    void clearTransientRecords() noexcept { hits_.clear(); }

private:
    struct HitRecord final {
        AttackKey attack{};
        simulation::EntityHandle target{};
    };
    std::vector<HitRecord> hits_;
};

[[nodiscard]] bool overlaps(world::AabbI left, world::AabbI right) noexcept;

} // namespace underworld::game::gameplay
