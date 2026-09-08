#pragma once

#include "engine/simulation/player_command.h"
#include "game/game_session.h"
#include "game/maps/authored_world.h"

#include <memory>
#include <optional>
#include <string>

namespace underworld::editor {

// Compiles the current authored project in memory and reuses GameSession and
// MapSession for playtest transitions. No temporary DMAP is created.
class EditorPlaytestSession final {
public:
    EditorPlaytestSession() = default;
    ~EditorPlaytestSession();
    EditorPlaytestSession(const EditorPlaytestSession&) = delete;
    EditorPlaytestSession& operator=(const EditorPlaytestSession&) = delete;

    [[nodiscard]] bool start(const game::maps::MapData& data,
                             const game::GameContentRegistry& content,
                             std::string& error);
    [[nodiscard]] bool start(const game::maps::AuthoredWorldSource& source,
                             const game::GameContentRegistry& content,
                             const std::optional<simulation::MapId>& startMap,
                             std::string& error);
    void stop() noexcept;
    void tick(const simulation::PlayerCommand& command);
    void relocatePlayer(core::WorldPointI position,
                        game::gameplay::FacingDirection facing) noexcept;

    [[nodiscard]] bool active() const noexcept { return session_ != nullptr && world() != nullptr; }
    [[nodiscard]] const game::maps::RuntimeWorld* world() const noexcept;
    [[nodiscard]] const game::maps::MapData* sourceData() const noexcept;
    [[nodiscard]] const simulation::MapId* activeMapId() const noexcept;

private:
    game::maps::MapCatalog mapCatalog_;
    game::maps::MapValidationCatalogs validationCatalogs_{};
    std::unique_ptr<game::RuntimeTilesetCatalog> runtimeTilesets_;
    std::unique_ptr<game::gameplay::creatures::EnemyFactory> enemyFactory_;
    std::unique_ptr<game::gameplay::WorldObjectFactory> objectFactory_;
    std::unique_ptr<game::gameplay::npcs::NpcFactory> npcFactory_;
    std::unique_ptr<game::maps::RuntimeWorldBuilder> runtimeBuilder_;
    std::unique_ptr<game::GameSession> session_;
};

} // namespace underworld::editor
