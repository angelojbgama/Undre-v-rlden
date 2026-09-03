#pragma once

#include "engine/assets/asset_manager.h"
#include "engine/platform/debug_input.h"
#include "engine/platform/input_state.h"
#include "engine/simulation/player_command.h"

#include <filesystem>
#include <memory>

namespace underworld::platform { class ImageDecoder; }
namespace underworld::render { class Framebuffer; }

namespace underworld::game {

class Phase4Demo final {
public:
    Phase4Demo(platform::ImageDecoder& decoder, const std::filesystem::path& assetRoot);
    ~Phase4Demo();

    void fixedTick(simulation::Tick tick, const platform::InputState& input,
                   platform::DebugInputState debugInput);
    void render(render::Framebuffer& framebuffer) const;

private:
    struct State;
    assets::AssetManager assets_;
    std::unique_ptr<State> state_;
};

[[nodiscard]] std::filesystem::path findLicensedAssetRoot(
    const std::filesystem::path& executableDirectory);

} // namespace underworld::game
