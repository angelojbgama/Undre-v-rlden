#include "editor/editor_playtest.h"

#include "game/game_launch.h"

#include <array>

namespace underworld::editor {

EditorPlaytestSession::~EditorPlaytestSession() { stop(); }

bool EditorPlaytestSession::start(const game::maps::MapData& data,
                                  const game::GameContentRegistry& content,
                                  std::string& error) {
    stop();
    error.clear();

    const auto catalogs = game::mapValidationCatalogs(content);
    const auto structural = game::maps::validateMapData(data, &catalogs);
    if (!structural) {
        error = "playtest map is structurally invalid: " + structural.error;
        return false;
    }

    std::string spawnError;
    const auto spawn = game::selectStartupSpawn(data, std::nullopt, spawnError);
    if (!spawn) {
        error = "playtest cannot start: " + spawnError;
        return false;
    }

    const std::array visuals{
        game::gameplay::creatures::soldierVisualId(),
        game::gameplay::creatures::skullVisualId()};
    enemyFactory_ = std::make_unique<game::gameplay::creatures::EnemyFactory>(
        handles_, content.enemies(), content.behaviors(), content.attacks(),
        content.projectiles(), visuals);
    objectFactory_ = std::make_unique<game::gameplay::WorldObjectFactory>(
        handles_, content.objects(), content.items());
    const game::RuntimeTilesetCatalog runtimeTilesets(content.tilesets());
    game::maps::RuntimeWorldBuilder builder(
        catalogs, *enemyFactory_, *objectFactory_, handles_, runtimeTilesets);
    auto built = builder.build(data, *spawn);
    if (!built) {
        error = "playtest runtime build failed: " + built.error;
        enemyFactory_.reset();
        objectFactory_.reset();
        return false;
    }

    sourceData_ = data;
    world_ = std::move(built.world);
    return true;
}

void EditorPlaytestSession::stop() noexcept {
    destroyRuntimeHandles();
    world_.reset();
    sourceData_.reset();
    enemyFactory_.reset();
    objectFactory_.reset();
}

void EditorPlaytestSession::destroyRuntimeHandles() noexcept {
    if (!world_) { return; }
    for (const auto& enemy : world_->enemies()) {
        static_cast<void>(handles_.destroy(enemy.instance.handle()));
    }
    for (const auto& object : world_->objects()) {
        static_cast<void>(handles_.destroy(object.instance.handle()));
    }
    for (const auto& pickup : world_->pickups()) {
        static_cast<void>(handles_.destroy(pickup.instance.handle()));
    }
}

} // namespace underworld::editor
