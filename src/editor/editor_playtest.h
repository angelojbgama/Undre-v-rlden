#pragma once

#include "game/maps/runtime_world.h"
#include "game/game_content.h"

#include <memory>
#include <optional>
#include <string>

namespace underworld::editor {

// Builds a private runtime snapshot through the same pipeline used by game.exe.
// It never holds or mutates an EditorDocument.
class EditorPlaytestSession final {
public:
    EditorPlaytestSession() = default;
    ~EditorPlaytestSession();
    EditorPlaytestSession(const EditorPlaytestSession&) = delete;
    EditorPlaytestSession& operator=(const EditorPlaytestSession&) = delete;

    [[nodiscard]] bool start(const game::maps::MapData& data,
                             const game::GameContentRegistry& content,
                             std::string& error);
    void stop() noexcept;

    [[nodiscard]] bool active() const noexcept { return world_ != nullptr; }
    [[nodiscard]] const game::maps::RuntimeWorld* world() const noexcept {
        return world_.get();
    }
    [[nodiscard]] const game::maps::MapData* sourceData() const noexcept {
        return sourceData_ ? &*sourceData_ : nullptr;
    }

private:
    void destroyRuntimeHandles() noexcept;

    simulation::EntityHandlePool handles_;
    std::unique_ptr<game::gameplay::creatures::EnemyFactory> enemyFactory_;
    std::unique_ptr<game::gameplay::WorldObjectFactory> objectFactory_;
    std::unique_ptr<game::maps::RuntimeWorld> world_;
    std::optional<game::maps::MapData> sourceData_;
};

} // namespace underworld::editor
