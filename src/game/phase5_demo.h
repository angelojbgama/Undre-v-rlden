#pragma once

#include "engine/assets/asset_manager.h"
#include "engine/platform/debug_input.h"
#include "engine/platform/input_state.h"
#include "engine/simulation/player_command.h"
#include "game/game_launch.h"

#include <filesystem>
#include <memory>
#include <string>

namespace underworld::platform { class ImageDecoder; }
namespace underworld::render { class Framebuffer; }

namespace underworld::game {

class Phase7Demo final {
public:
    Phase7Demo(platform::ImageDecoder& decoder, const std::filesystem::path& assetRoot,
               const std::filesystem::path& executableDirectory,
               const GameLaunchOptions& launchOptions = {});
    ~Phase7Demo();

    void fixedTick(simulation::Tick tick, const platform::InputState& input,
                   platform::DebugInputState debugInput);
    void render(render::Framebuffer& framebuffer) const;
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
