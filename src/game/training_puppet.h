#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/entity_handle.h"
#include "game/gameplay/combat_types.h"

namespace underworld::world { class CollisionGrid; }

namespace underworld::game {

class TrainingPuppet final {
public:
    static constexpr int maximumHealth = 5;

    TrainingPuppet(simulation::EntityHandle handle, core::WorldPointI feet);

    [[nodiscard]] gameplay::CombatTargetRef combatTarget() noexcept;
    [[nodiscard]] const gameplay::CombatantState& combatant() const noexcept {
        return combatant_;
    }
    [[nodiscard]] gameplay::CombatantState& combatant() noexcept { return combatant_; }
    [[nodiscard]] gameplay::CollisionBody collisionBody() const noexcept;
    [[nodiscard]] gameplay::Hurtbox hurtbox() const noexcept;
    [[nodiscard]] core::WorldPointI feetPosition() const noexcept { return feet_; }
    void applyKnockback(int deltaX, int deltaY, const world::CollisionGrid& collision,
                        int tileSize);

private:
    gameplay::CombatantState combatant_;
    core::WorldPointI feet_{};
};

} // namespace underworld::game
