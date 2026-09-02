#include "engine/render/renderer_2d.h"

#include "engine/render/framebuffer.h"
#include "engine/render/image.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace underworld::render {

namespace {

std::uint8_t divideRounded(std::uint64_t numerator, std::uint64_t denominator) noexcept {
    return static_cast<std::uint8_t>((numerator + denominator / 2U) / denominator);
}

} // namespace

void Renderer2D::setPixel(int x, int y, core::ColorRGBA8 color) noexcept {
    if (x < 0 || y < 0 || x >= target_.width() || y >= target_.height()) {
        return;
    }
    auto pixels = target_.pixels();
    auto& destination = pixels[static_cast<std::size_t>(y) *
                                   static_cast<std::size_t>(target_.width()) +
                               static_cast<std::size_t>(x)];
    blend(color, destination);
}

void Renderer2D::fillRect(core::RectI rectangle, core::ColorRGBA8 color) noexcept {
    if (rectangle.empty()) {
        return;
    }
    const std::int64_t right = static_cast<std::int64_t>(rectangle.x) + rectangle.width;
    const std::int64_t bottom = static_cast<std::int64_t>(rectangle.y) + rectangle.height;
    const int left = std::max(rectangle.x, 0);
    const int top = std::max(rectangle.y, 0);
    const int clippedRight = static_cast<int>(std::min<std::int64_t>(right, target_.width()));
    const int clippedBottom = static_cast<int>(std::min<std::int64_t>(bottom, target_.height()));
    if (left >= clippedRight || top >= clippedBottom) {
        return;
    }

    auto pixels = target_.pixels();
    for (int y = top; y < clippedBottom; ++y) {
        for (int x = left; x < clippedRight; ++x) {
            auto& destination = pixels[static_cast<std::size_t>(y) *
                                           static_cast<std::size_t>(target_.width()) +
                                       static_cast<std::size_t>(x)];
            blend(color, destination);
        }
    }
}

void Renderer2D::drawImage(const Image& image, int destinationX, int destinationY) {
    drawImageRegion(image, {0, 0, image.width(), image.height()}, destinationX, destinationY);
}

void Renderer2D::drawImageRegion(const Image& image, core::RectI source, int destinationX,
                                 int destinationY) {
    drawImageRegionImpl(image, source, destinationX, destinationY, false);
}

void Renderer2D::drawImageRegionFlipX(const Image& image, core::RectI source, int destinationX,
                                      int destinationY) {
    drawImageRegionImpl(image, source, destinationX, destinationY, true);
}

void Renderer2D::drawImageRegionImpl(const Image& image, core::RectI source, int destinationX,
                                     int destinationY, bool flipX) {
    const std::int64_t sourceRight = static_cast<std::int64_t>(source.x) + source.width;
    const std::int64_t sourceBottom = static_cast<std::int64_t>(source.y) + source.height;
    if (source.empty() || source.x < 0 || source.y < 0 || sourceRight > image.width() ||
        sourceBottom > image.height()) {
        throw std::out_of_range("image source rectangle is outside the image");
    }

    const std::int64_t destinationRight = static_cast<std::int64_t>(destinationX) + source.width;
    const std::int64_t destinationBottom = static_cast<std::int64_t>(destinationY) + source.height;
    const int clippedLeft = std::max(destinationX, 0);
    const int clippedTop = std::max(destinationY, 0);
    const int clippedRight = static_cast<int>(
        std::min<std::int64_t>(destinationRight, target_.width()));
    const int clippedBottom = static_cast<int>(
        std::min<std::int64_t>(destinationBottom, target_.height()));
    if (clippedLeft >= clippedRight || clippedTop >= clippedBottom) {
        return;
    }

    const auto& bytes = image.bytes();
    auto targetPixels = target_.pixels();
    for (int y = clippedTop; y < clippedBottom; ++y) {
        const int sourceY = source.y + (y - destinationY);
        for (int x = clippedLeft; x < clippedRight; ++x) {
            const int localX = x - destinationX;
            const int sourceX = source.x + (flipX ? source.width - 1 - localX : localX);
            const std::size_t sourceOffset = static_cast<std::size_t>(sourceY) * image.stride() +
                                             static_cast<std::size_t>(sourceX) * 4U;
            const core::ColorRGBA8 sourceColor{
                bytes[sourceOffset], bytes[sourceOffset + 1U], bytes[sourceOffset + 2U],
                bytes[sourceOffset + 3U]};
            auto& destination = targetPixels[static_cast<std::size_t>(y) *
                                                 static_cast<std::size_t>(target_.width()) +
                                             static_cast<std::size_t>(x)];
            blend(sourceColor, destination);
        }
    }
}

void Renderer2D::blend(core::ColorRGBA8 source, core::ColorRGBA8& destination) noexcept {
    if (source.a == 0U) {
        return;
    }
    if (source.a == 255U) {
        destination = source;
        return;
    }

    // Straight-alpha source-over. Integer divisions round to the nearest value.
    const std::uint32_t inverseSourceAlpha = 255U - source.a;
    const std::uint32_t outputAlpha = source.a +
        (static_cast<std::uint32_t>(destination.a) * inverseSourceAlpha + 127U) / 255U;
    if (outputAlpha == 0U) {
        destination = {};
        return;
    }
    const std::uint64_t denominator = static_cast<std::uint64_t>(outputAlpha) * 255U;
    const auto compositeChannel = [&](std::uint8_t sourceChannel,
                                      std::uint8_t destinationChannel) {
        const std::uint64_t numerator =
            static_cast<std::uint64_t>(sourceChannel) * source.a * 255U +
            static_cast<std::uint64_t>(destinationChannel) * destination.a * inverseSourceAlpha;
        return divideRounded(numerator, denominator);
    };
    destination = {compositeChannel(source.r, destination.r),
                   compositeChannel(source.g, destination.g),
                   compositeChannel(source.b, destination.b),
                   static_cast<std::uint8_t>(outputAlpha)};
}

} // namespace underworld::render
