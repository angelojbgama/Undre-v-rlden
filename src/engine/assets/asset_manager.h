#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace underworld::platform { class ImageDecoder; }
namespace underworld::render { class Image; }

namespace underworld::assets {

using AssetId = std::string;

class AssetManager final {
public:
    [[nodiscard]] std::shared_ptr<const render::Image> loadImage(
        AssetId id, const std::filesystem::path& path, platform::ImageDecoder& decoder);
    [[nodiscard]] std::shared_ptr<const render::Image> getImage(std::string_view id) const;

private:
    struct CachedImage final {
        std::filesystem::path path;
        std::shared_ptr<const render::Image> image;
    };
    std::unordered_map<AssetId, CachedImage> images_;
};

} // namespace underworld::assets
