#include "game/training_puppet.h"

#include "engine/world/collision_grid.h"

namespace underworld::game {

TrainingPuppet::TrainingPuppet(simulation::EntityHandle handle, core::WorldPointI feet)
    : combatant_{handle, gameplay::Faction::enemy, gameplay::Health{maximumHealth}, 0, false},
      feet_(feet) {}

gameplay::CollisionBody TrainingPuppet::collisionBody() const noexcept {
    return {{feet_.x - 5, feet_.y - 8, 10, 8}};
}

gameplay::Hurtbox TrainingPuppet::hurtbox() const noexcept {
    return {{feet_.x - 7, feet_.y - 22, 14, 22}, !combatant_.health.depleted()};
}

gameplay::CombatTargetRef TrainingPuppet::combatTarget() noexcept {
    return {combatant_, hurtbox()};
}

void TrainingPuppet::applyKnockback(int deltaX, int deltaY,
                                    const world::CollisionGrid& collision, int tileSize) {
    world::AabbI body = collisionBody().bounds;
    const world::MovementResult movement = world::moveAgainstSolidTiles(
        collision, body, deltaX, deltaY, tileSize);
    feet_.x += movement.movedX;
    feet_.y += movement.movedY;
}

} // namespace underworld::game
