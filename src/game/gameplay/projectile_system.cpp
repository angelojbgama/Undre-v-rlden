#include "game/gameplay/projectile_system.h"

#include "engine/simulation/events.h"
#include "engine/world/collision.h"
#include "engine/world/collision_grid.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/combat_system.h"

#include <algorithm>

namespace underworld::game::gameplay {

world::AabbI Projectile::hitbox() const noexcept {
    return {position.x - hitboxSize / 2, position.y - hitboxSize / 2,
            hitboxSize, hitboxSize};
}

simulation::EntityHandle ProjectileSystem::spawn(
    simulation::EntityHandle owner, Faction faction, AttackInstanceId attackInstance,
    core::WorldPointI position, FacingDirection direction, DamageSpec damage) {
    const simulation::EntityHandle handle = handles_.create();
    projectiles_.push_back({handle, owner, faction, attackInstance, position,
                            direction, damage, Projectile::lifetimeTicks});
    return handle;
}

void ProjectileSystem::update(const world::CollisionGrid& collision, int tileSize,
                              std::span<CombatTarget*> targets, CombatSystem& combat,
                              simulation::EventBuffer& events) {
    for (Projectile& projectile : projectiles_) {
        bool destroyed = false;
        const core::WorldPointI direction = directionVector(projectile.direction);
        for (int step = 0; step < Projectile::speedPixelsPerTick && !destroyed; ++step) {
            projectile.position.x += direction.x;
            projectile.position.y += direction.y;
            if (world::querySolidTiles(collision, projectile.hitbox(), tileSize).collides) {
                events.emit(simulation::ProjectileImpact{
                    projectile.handle, projectile.position,
                    simulation::ProjectileImpactKind::tile});
                destroyed = true;
                break;
            }
            for (CombatTarget* target : targets) {
                if (target == nullptr) {
                    continue;
                }
                const Hitbox hitbox{projectile.hitbox(), projectile.owner, projectile.faction,
                                    projectile.attackInstance, projectile.damage,
                                    direction.x * projectile.damage.knockbackPixels,
                                    direction.y * projectile.damage.knockbackPixels, true};
                const bool validImpact = target->hurtbox.enabled &&
                    target->handle != projectile.owner &&
                    factionsCanDamage(projectile.faction, target->faction) &&
                    overlaps(hitbox.bounds, target->hurtbox.bounds);
                if (validImpact) {
                    [[maybe_unused]] const bool damaged = combat.resolve(
                        hitbox, *target, collision, tileSize, events);
                    events.emit(simulation::ProjectileImpact{
                        projectile.handle, projectile.position,
                        simulation::ProjectileImpactKind::target});
                    destroyed = true;
                    break;
                }
            }
        }
        if (!destroyed && projectile.remainingTicks > 0) {
            --projectile.remainingTicks;
        }
        if (!destroyed && projectile.remainingTicks == 0) {
            events.emit(simulation::ProjectileImpact{
                projectile.handle, projectile.position,
                simulation::ProjectileImpactKind::expired});
            destroyed = true;
        }
        if (destroyed) {
            projectile.remainingTicks = 0;
        }
    }

    std::erase_if(projectiles_, [&](const Projectile& projectile) {
        if (projectile.remainingTicks != 0) {
            return false;
        }
        combat.finishAttack(projectile.attackInstance);
        [[maybe_unused]] const bool destroyed = handles_.destroy(projectile.handle);
        return true;
    });
}

} // namespace underworld::game::gameplay
