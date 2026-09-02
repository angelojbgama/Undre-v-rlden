#include "engine/render/framebuffer.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace underworld::render {

namespace {

std::size_t checkedPixelCount(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("framebuffer dimensions must be positive");
    }

    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h) {
        throw std::length_error("framebuffer dimensions overflow");
    }
    return w * h;
}

} // namespace

Framebuffer::Framebuffer(int width, int height)
    : width_(width), height_(height), pixels_(checkedPixelCount(width, height)) {}

void Framebuffer::clear(core::ColorRGBA8 color) noexcept {
    std::fill(pixels_.begin(), pixels_.end(), color);
}

std::size_t Framebuffer::stride() const noexcept {
    return static_cast<std::size_t>(width_) * sizeof(core::ColorRGBA8);
}

std::size_t Framebuffer::byteSize() const noexcept {
    return pixels_.size() * sizeof(core::ColorRGBA8);
}

core::PixelBufferView Framebuffer::view() const noexcept {
    return {
        reinterpret_cast<const std::byte*>(pixels_.data()),
        width_,
        height_,
        stride(),
        core::PixelFormat::rgba8,
    };
}

} // namespace underworld::render
