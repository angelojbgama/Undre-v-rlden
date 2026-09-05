#include "game/runtime_visual_sync.h"

#include <stdexcept>

namespace underworld::game {

RuntimeVisualSyncResult synchronizeRuntimeWorldVisuals(
    const maps::RuntimeWorld& world, const EnemyVisualCatalog& enemyCatalog,
    std::vector<EnemyVisualInstance>& enemyVisuals,
    const WorldObjectVisualCatalog& objectCatalog,
    std::vector<WorldObjectVisualInstance>& objectVisuals) {
    try {
        std::vector<EnemyVisualInstance> newEnemyVisuals;
        newEnemyVisuals.reserve(world.enemies().size());
        for (const auto& persistent : world.enemies()) {
            const auto& enemy = persistent.instance;
            newEnemyVisuals.emplace_back(enemy.handle(),
                enemyCatalog.require(enemy.definition().visualSetId));
            newEnemyVisuals.back().update(enemy, 0);
        }
        std::vector<WorldObjectVisualInstance> newObjectVisuals;
        newObjectVisuals.reserve(world.objects().size());
        for (const auto& persistent : world.objects()) {
            const auto& object = persistent.instance;
            newObjectVisuals.emplace_back(object.handle(),
                objectCatalog.require(object.definition().visualSetId));
            newObjectVisuals.back().update(object, 0);
        }
        if (newEnemyVisuals.size() != world.enemies().size() ||
            newObjectVisuals.size() != world.objects().size()) {
            return {false, "runtime visual synchronization count mismatch"};
        }
        enemyVisuals = std::move(newEnemyVisuals);
        objectVisuals = std::move(newObjectVisuals);
        return {true, {}};
    } catch (const std::exception& exception) {
        return {false, exception.what()};
    }
}

} // namespace underworld::game
