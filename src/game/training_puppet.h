#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/entity_handle.h"
#include "game/gameplay/combat_types.h"

namespace underworld::game {

class TrainingPuppet final {
public:
    static constexpr int maximumHealth = 5;

    TrainingPuppet(simulation::EntityHandle handle, core::WorldPointI feet);

    [[nodiscard]] gameplay::CombatTarget& combatTarget() noexcept { return target_; }
    [[nodiscard]] const gameplay::CombatTarget& combatTarget() const noexcept { return target_; }
    [[nodiscard]] core::WorldPointI feetPosition() const noexcept { return target_.feet; }

private:
    gameplay::CombatTarget target_;
};

} // namespace underworld::game
