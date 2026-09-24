#pragma once

#include "engine/assets/asset_manager.h"
#include "engine/platform/debug_input.h"
#include "engine/platform/input_state.h"
#include "engine/simulation/player_command.h"
#include "game/audit/audit_snapshot.h"
#include "game/game_launch.h"
#include "game/content/content_dto.h"
#include "game/game_content.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace underworld::platform { class ImageDecoder; }
namespace underworld::render { class Framebuffer; }

namespace underworld::game {

// Save slot files: slot 0 keeps the historical savegame.sav name; slots 1/2
// live beside it. `base` is the savegame.sav file path (GameRuntime passes
// its savePath member), never a directory — writeSaveAtomic renames `base`
// to the .bak backup, so a directory base would rename a whole directory.
// The authored saves menu acts on these through the slot-indexed actions.
[[nodiscard]] std::filesystem::path saveSlotPath(const std::filesystem::path& base,
                                                 int slot);

class GameRuntime final {
public:
    GameRuntime(platform::ImageDecoder& decoder, const std::filesystem::path& assetRoot,
               const std::filesystem::path& executableDirectory,
               GameContentRegistry content,
               const GameLaunchOptions& launchOptions = {},
               std::optional<std::filesystem::path> contentWorkspaceRoot = std::nullopt);
    ~GameRuntime();

    void fixedTick(simulation::Tick tick, const platform::InputState& input,
                   platform::DebugInputState debugInput);
    void render(render::Framebuffer& framebuffer) const;
    [[nodiscard]] audit::GameAuditSnapshot auditSnapshot() const;
    [[nodiscard]] const std::string& startupSummary() const noexcept { return startupSummary_; }

private:
    struct State;
    assets::AssetManager assets_;
    std::unique_ptr<State> state_;
    std::string startupSummary_;
};

[[nodiscard]] std::filesystem::path findLicensedAssetRoot(
    const std::filesystem::path& executableDirectory);

} // namespace underworld::game
