#include "engine/assets/asset_manager.h"

#include "engine/platform/image_decoder.h"
#include "engine/render/image.h"

#include <stdexcept>
#include <utility>

namespace underworld::assets {

std::shared_ptr<const render::Image> AssetManager::loadImage(
    AssetId id, const std::filesystem::path& path, platform::ImageDecoder& decoder) {
    if (id.empty()) {
        throw std::invalid_argument("asset id cannot be empty");
    }
    const std::filesystem::path normalized = path.lexically_normal();
    const auto found = images_.find(id);
    if (found != images_.end()) {
        if (found->second.path != normalized) {
            throw std::logic_error("asset id already refers to a different path: " + id);
        }
        return found->second.image;
    }

    auto image = std::make_shared<const render::Image>(decoder.decode(normalized));
    images_.emplace(std::move(id), CachedImage{normalized, image});
    return image;
}

std::shared_ptr<const render::Image> AssetManager::getImage(std::string_view id) const {
    const auto found = images_.find(std::string(id));
    if (found == images_.end()) {
        throw std::out_of_range("image asset is not loaded: " + std::string(id));
    }
    return found->second.image;
}

} // namespace underworld::assets
