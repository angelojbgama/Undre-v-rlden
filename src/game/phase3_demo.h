#pragma once

#include "engine/assets/asset_manager.h"
#include "engine/platform/debug_input.h"

#include <filesystem>
#include <memory>

namespace underworld::platform { class ImageDecoder; }
namespace underworld::render { class Framebuffer; }

namespace underworld::game {

// Tile/camera/collision diagnostic only. It is not a gameplay scene or Player system.
class Phase3Demo final {
public:
    Phase3Demo(platform::ImageDecoder& decoder, const std::filesystem::path& assetRoot);
    ~Phase3Demo();

    void fixedTick(platform::DebugInputState input);
    void render(render::Framebuffer& framebuffer) const;

private:
    struct State;
    assets::AssetManager assets_;
    std::unique_ptr<State> state_;
};

[[nodiscard]] std::filesystem::path findLicensedAssetRoot(
    const std::filesystem::path& executableDirectory);

} // namespace underworld::game
