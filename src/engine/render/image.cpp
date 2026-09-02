#include "engine/render/image.h"

#include <stdexcept>
#include <utility>

namespace underworld::render {

Image::Image(core::ImageData data) {
    if (!data.isValid()) {
        throw std::invalid_argument("image data is not a valid RGBA8 image");
    }
    width_ = data.width;
    height_ = data.height;
    strideBytes_ = data.strideBytes;
    pixels_ = std::move(data.pixels);
}

core::ColorRGBA8 Image::pixel(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return {};
    }
    const std::size_t offset = static_cast<std::size_t>(y) * strideBytes_ +
                               static_cast<std::size_t>(x) * 4U;
    return {pixels_[offset], pixels_[offset + 1U], pixels_[offset + 2U], pixels_[offset + 3U]};
}

} // namespace underworld::render
