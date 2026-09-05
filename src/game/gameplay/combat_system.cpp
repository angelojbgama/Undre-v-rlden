#include "game/gameplay/combat_system.h"

#include "engine/simulation/events.h"
#include <algorithm>
#include <stdexcept>

namespace underworld::game::gameplay {

bool overlaps(world::AabbI left, world::AabbI right) noexcept {
    return left.width > 0 && left.height > 0 && right.width > 0 && right.height > 0 &&
           left.x < right.x + right.width && right.x < left.x + left.width &&
           left.y < right.y + right.height && right.y < left.y + left.height;
}

CombatResolution CombatSystem::resolve(const Hitbox& attack, CombatTargetRef target,
                                       simulation::EventBuffer& events) {
    if (attack.damage.amount <= 0 || attack.damage.knockbackPixels < 0) {
        throw std::invalid_argument("combat damage must be positive and knockback non-negative");
    }
    CombatantState& combatant = target.combatant;
    CombatResolution result{combatant.handle};
    if (!attack.enabled || !target.hurtbox.enabled || combatant.health.depleted() ||
        attack.attack.owner == combatant.handle ||
        !factionsCanDamage(attack.faction, combatant.faction) ||
        !overlaps(attack.bounds, target.hurtbox.bounds) ||
        combatant.invulnerabilityTicks > 0) {
        return result;
    }
    const auto duplicate = std::find_if(hits_.begin(), hits_.end(), [&](const HitRecord& hit) {
        return hit.attack == attack.attack && hit.target == combatant.handle;
    });
    if (duplicate != hits_.end()) {
        return result;
    }
    hits_.push_back({attack.attack, combatant.handle});
    if (!combatant.health.applyDamage(attack.damage.amount)) {
        return result;
    }
    combatant.invulnerabilityTicks = invulnerabilityDurationTicks;
    result.damaged = true;
    result.requestedKnockbackX = attack.knockbackX;
    result.requestedKnockbackY = attack.knockbackY;

    events.emit(simulation::EntityDamaged{
        attack.attack.owner, combatant.handle, attack.damage.amount,
        combatant.health.current, attack.attack.localInstance});
    if (combatant.health.depleted()) {
        result.defeated = true;
        if (!combatant.defeatEmitted) {
            events.emit(simulation::EntityDefeated{
                attack.attack.owner, combatant.handle, attack.attack.localInstance,
                combatant.definitionId});
            combatant.defeatEmitted = true;
        }
    }
    return result;
}

void CombatSystem::finishAttack(AttackKey attack) noexcept {
    std::erase_if(hits_, [attack](const HitRecord& hit) { return hit.attack == attack; });
}

} // namespace underworld::game::gameplay
