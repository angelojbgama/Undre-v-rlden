#include "game/gameplay/projectile_system.h"

#include "engine/simulation/events.h"
#include "engine/world/collision.h"
#include "engine/world/collision_grid.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/combat_system.h"

#include <algorithm>

namespace underworld::game::gameplay {

world::AabbI Projectile::hitbox() const noexcept {
    if (definition == nullptr) {
        return {};
    }
    return {position.x - definition->hitboxWidth / 2,
            position.y - definition->hitboxHeight / 2,
            definition->hitboxWidth, definition->hitboxHeight};
}

simulation::EntityHandle ProjectileSystem::spawn(
    AttackKey attack, Faction faction, const simulation::DefinitionId& definitionId,
    core::WorldPointI position, FacingDirection direction, DamageSpec damage) {
    const ProjectileDefinition& definition = definitions_.require(definitionId);
    const simulation::EntityHandle handle = handles_.create();
    projectiles_.push_back({handle, attack, faction, &definition, position,
                            direction, damage, definition.lifetimeTicks});
    return handle;
}

void ProjectileSystem::update(const world::CollisionGrid& collision, int tileSize,
                              std::span<CombatTargetRef> targets, CombatSystem& combat,
                              simulation::EventBuffer& events,
                              std::vector<CombatResolution>& resolutions) {
    for (Projectile& projectile : projectiles_) {
        bool destroyed = false;
        const core::WorldPointI direction = directionVector(projectile.direction);
        const int speed = projectile.definition != nullptr
                              ? projectile.definition->speedPixelsPerTick : 0;
        for (int step = 0; step < speed && !destroyed; ++step) {
            projectile.position.x += direction.x;
            projectile.position.y += direction.y;
            if (world::querySolidTiles(collision, projectile.hitbox(), tileSize).collides) {
                events.emit(simulation::ProjectileImpact{
                    projectile.handle, projectile.position,
                    simulation::ProjectileImpactKind::tile});
                destroyed = true;
                break;
            }
            for (CombatTargetRef target : targets) {
                const Hitbox hitbox{projectile.hitbox(), projectile.attack, projectile.faction,
                                    projectile.damage,
                                    direction.x * projectile.damage.knockbackPixels,
                                    direction.y * projectile.damage.knockbackPixels, true};
                const bool validImpact = target.hurtbox.enabled &&
                    target.combatant.handle != projectile.attack.owner &&
                    factionsCanDamage(projectile.faction, target.combatant.faction) &&
                    overlaps(hitbox.bounds, target.hurtbox.bounds);
                if (validImpact) {
                    const CombatResolution resolution = combat.resolve(hitbox, target, events);
                    if (resolution.damaged) {
                        resolutions.push_back(resolution);
                    }
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
        combat.finishAttack(projectile.attack);
        [[maybe_unused]] const bool destroyed = handles_.destroy(projectile.handle);
        return true;
    });
}

void ProjectileSystem::clear(CombatSystem& combat) noexcept {
    for (const Projectile& projectile : projectiles_) {
        combat.finishAttack(projectile.attack);
        static_cast<void>(handles_.destroy(projectile.handle));
    }
    projectiles_.clear();
}

} // namespace underworld::game::gameplay
