#pragma once

#include "game/enemy_visual.h"
#include "game/maps/runtime_world.h"
#include "game/world_object_visual.h"
#include "game/presentation/visual_content_loader.h"

#include <string>
#include <vector>

namespace underworld::game {

struct RuntimeVisualSyncResult final {
    bool success{};
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] RuntimeVisualSyncResult synchronizeRuntimeWorldVisuals(
    const maps::RuntimeWorld& world, const EnemyVisualCatalog& enemyCatalog,
    std::vector<EnemyVisualInstance>& enemyVisuals,
    const WorldObjectVisualCatalog& objectCatalog,
    std::vector<WorldObjectVisualInstance>& objectVisuals);

[[nodiscard]] RuntimeVisualSyncResult synchronizeRuntimeWorldVisuals(
    const maps::RuntimeWorld& world, const EnemyVisualCatalog& enemyCatalog,
    std::vector<EnemyVisualInstance>& enemyVisuals,
    const WorldObjectVisualCatalog& objectCatalog,
    std::vector<WorldObjectVisualInstance>& objectVisuals,
    const presentation::RuntimeNpcVisualCatalog& npcCatalog,
    std::vector<presentation::RuntimeNpcVisualInstance>& npcVisuals);

} // namespace underworld::game
