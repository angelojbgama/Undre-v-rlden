#pragma once

#include "engine/assets/asset_manager.h"
#include "engine/render/animation.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace underworld::platform { class ImageDecoder; }
namespace underworld::render {
class BitmapFont;
class Framebuffer;
}

namespace underworld::game {

// A renderer diagnostic only. It deliberately has no gameplay entity or world state.
class Phase2Demo final {
public:
    Phase2Demo(platform::ImageDecoder& decoder, const std::filesystem::path& assetRoot);
    ~Phase2Demo();

    void fixedTick();
    void render(render::Framebuffer& framebuffer) const;

private:
    struct Clips;
    assets::AssetManager assets_;
    std::unique_ptr<Clips> clips_;
    std::unique_ptr<render::BitmapFont> font_;
    render::Animator mainAnimator_;
    render::Animator sideAnimator_;
    render::Animator attackAnimator_;
    std::uint64_t ticks_{};
    int mainMode_{};
};

[[nodiscard]] std::filesystem::path findLicensedAssetRoot(
    const std::filesystem::path& executableDirectory);

} // namespace underworld::game
