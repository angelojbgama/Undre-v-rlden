#include "game/gameplay/combat_system.h"

#include "engine/simulation/events.h"
#include "engine/world/collision_grid.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game::gameplay {

bool overlaps(world::AabbI left, world::AabbI right) noexcept {
    return left.width > 0 && left.height > 0 && right.width > 0 && right.height > 0 &&
           left.x < right.x + right.width && right.x < left.x + left.width &&
           left.y < right.y + right.height && right.y < left.y + left.height;
}

bool CombatSystem::resolve(const Hitbox& attack, CombatTarget& target,
                           const world::CollisionGrid& collision, int tileSize,
                           simulation::EventBuffer& events) {
    if (attack.damage.amount <= 0 || attack.damage.knockbackPixels < 0) {
        throw std::invalid_argument("combat damage must be positive and knockback non-negative");
    }
    if (!attack.enabled || !target.hurtbox.enabled || target.health.depleted() ||
        attack.owner == target.handle || !factionsCanDamage(attack.faction, target.faction) ||
        !overlaps(attack.bounds, target.hurtbox.bounds) || target.invulnerabilityTicks > 0) {
        return false;
    }
    const auto duplicate = std::find_if(hits_.begin(), hits_.end(), [&](const HitRecord& hit) {
        return hit.attack == attack.attackInstance && hit.target == target.handle;
    });
    if (duplicate != hits_.end()) {
        return false;
    }
    hits_.push_back({attack.attackInstance, target.handle});
    if (!target.health.applyDamage(attack.damage.amount)) {
        return false;
    }
    target.invulnerabilityTicks = invulnerabilityDurationTicks;

    world::AabbI body = target.collisionBody.bounds;
    const auto movement = world::moveAgainstSolidTiles(
        collision, body, attack.knockbackX, attack.knockbackY, tileSize);
    target.feet.x += movement.movedX;
    target.feet.y += movement.movedY;
    target.collisionBody.bounds = body;
    target.hurtbox.bounds.x += movement.movedX;
    target.hurtbox.bounds.y += movement.movedY;

    events.emit(simulation::EntityDamaged{attack.owner, target.handle, attack.damage.amount,
                                         target.health.current, attack.attackInstance});
    if (target.health.depleted()) {
        target.hurtbox.enabled = false;
        if (!target.defeatEmitted) {
            events.emit(simulation::EntityDefeated{
                attack.owner, target.handle, attack.attackInstance});
            target.defeatEmitted = true;
        }
    }
    return true;
}

void CombatSystem::finishAttack(AttackInstanceId attack) noexcept {
    std::erase_if(hits_, [attack](const HitRecord& hit) { return hit.attack == attack; });
}

} // namespace underworld::game::gameplay
