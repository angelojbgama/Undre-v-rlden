#include "game/runtime_visual_sync.h"

#include <stdexcept>

namespace underworld::game {

RuntimeVisualSyncResult synchronizeRuntimeWorldVisuals(
    const maps::RuntimeWorld& world, const EnemyVisualCatalog& enemyCatalog,
    std::vector<EnemyVisualInstance>& enemyVisuals,
    const WorldObjectVisualCatalog& objectCatalog,
    std::vector<WorldObjectVisualInstance>& objectVisuals) {
    std::vector<WorldObjectResidueVisualInstance> ignoredResidues;
    return synchronizeRuntimeWorldVisuals(world, enemyCatalog, enemyVisuals, objectCatalog,
                                          objectVisuals, ignoredResidues);
}

RuntimeVisualSyncResult synchronizeRuntimeWorldVisuals(
    const maps::RuntimeWorld& world, const EnemyVisualCatalog& enemyCatalog,
    std::vector<EnemyVisualInstance>& enemyVisuals,
    const WorldObjectVisualCatalog& objectCatalog,
    std::vector<WorldObjectVisualInstance>& objectVisuals,
    std::vector<WorldObjectResidueVisualInstance>& residueVisuals) {
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
        std::vector<WorldObjectResidueVisualInstance> newResidueVisuals;
        newResidueVisuals.reserve(world.destroyedObjectResidues().size());
        for (const auto& residue : world.destroyedObjectResidues()) {
            const auto& set = objectCatalog.require(residue.visualSetId);
            if (!set.destroyed) continue;
            newResidueVisuals.emplace_back(residue.persistentId, residue.visualSetId,
                                           residue.position, set);
        }
        enemyVisuals = std::move(newEnemyVisuals);
        objectVisuals = std::move(newObjectVisuals);
        residueVisuals = std::move(newResidueVisuals);
        return {true, {}};
    } catch (const std::exception& exception) {
        return {false, exception.what()};
    }
}

RuntimeVisualSyncResult synchronizeRuntimeWorldVisuals(
    const maps::RuntimeWorld& world, const EnemyVisualCatalog& enemyCatalog,
    std::vector<EnemyVisualInstance>& enemyVisuals,
    const WorldObjectVisualCatalog& objectCatalog,
    std::vector<WorldObjectVisualInstance>& objectVisuals,
    std::vector<WorldObjectResidueVisualInstance>& residueVisuals,
    const presentation::RuntimeNpcVisualCatalog& npcCatalog,
    std::vector<presentation::RuntimeNpcVisualInstance>& npcVisuals) {
    const auto base = synchronizeRuntimeWorldVisuals(
        world, enemyCatalog, enemyVisuals, objectCatalog, objectVisuals, residueVisuals);
    if (!base) return base;
    try {
        std::vector<presentation::RuntimeNpcVisualInstance> rebuilt;
        rebuilt.reserve(world.npcs().size());
        for (const auto& persistent : world.npcs()) {
            const auto& npc = persistent.instance;
            rebuilt.emplace_back(npc.handle(), npcCatalog.require(npc.definition().visualSetId));
            rebuilt.back().update(npc, 0);
        }
        npcVisuals = std::move(rebuilt);
        return {true, {}};
    } catch (const std::exception& exception) {
        return {false, exception.what()};
    }
}

RuntimeVisualSyncResult synchronizeRuntimeWorldVisuals(
    const maps::RuntimeWorld& world, const EnemyVisualCatalog& enemyCatalog,
    std::vector<EnemyVisualInstance>& enemyVisuals,
    const WorldObjectVisualCatalog& objectCatalog,
    std::vector<WorldObjectVisualInstance>& objectVisuals,
    const presentation::RuntimeNpcVisualCatalog& npcCatalog,
    std::vector<presentation::RuntimeNpcVisualInstance>& npcVisuals) {
    std::vector<WorldObjectResidueVisualInstance> ignoredResidues;
    return synchronizeRuntimeWorldVisuals(world, enemyCatalog, enemyVisuals, objectCatalog,
                                          objectVisuals, ignoredResidues, npcCatalog,
                                          npcVisuals);
}

} // namespace underworld::game
