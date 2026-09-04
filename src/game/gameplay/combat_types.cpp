#include "game/gameplay/combat_types.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game::gameplay {

Health::Health(int maximumHealth) : current(maximumHealth), maximum(maximumHealth) {
    if (maximumHealth <= 0) {
        throw std::invalid_argument("health maximum must be positive");
    }
}

bool Health::applyDamage(int amount) {
    if (amount < 0) {
        throw std::invalid_argument("damage amount cannot be negative");
    }
    const int previous = current;
    current = std::max(0, current - amount);
    return current != previous;
}

int Health::restore(int amount) {
    if (amount < 0) { throw std::invalid_argument("restore amount cannot be negative"); }
    const int previous = current;
    current = std::min(maximum, current + amount);
    return current - previous;
}

bool factionsCanDamage(Faction attacker, Faction target) noexcept {
    return attacker != target && attacker != Faction::neutral && target != Faction::neutral;
}

void tickInvulnerability(CombatantState& target) noexcept {
    if (target.invulnerabilityTicks > 0) {
        --target.invulnerabilityTicks;
    }
}

} // namespace underworld::game::gameplay
