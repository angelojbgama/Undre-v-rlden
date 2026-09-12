#pragma once

#include "engine/core/geometry.h"

#include <cstdint>
#include <memory>
#include <span>

namespace underworld::render {

class Image;
class Renderer2D;

class SpriteSheet final {
public:
    explicit SpriteSheet(std::shared_ptr<const Image> image);
    [[nodiscard]] const Image& image() const noexcept { return *image_; }

private:
    std::shared_ptr<const Image> image_;
};

struct SpriteFrame final {
    core::RectI source{};
    // Measured from source-rectangle edges, not as a pixel index. A centered
    // anchor x=16 in a 32 px frame therefore remains 16 after horizontal flip.
    core::PointI anchor{};
    core::PointI drawOffset{};
    bool flipX{};
};

struct PixelMaskView final {
    int width{};
    int height{};
    core::PointI origin{};
    std::span<const std::uint8_t> cells{};

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 &&
            cells.size() == static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height);
    }
};

void drawSprite(Renderer2D& renderer, const SpriteSheet& sheet, const SpriteFrame& frame,
                core::PointI logicalPosition, bool additionalFlipX = false);
void drawSpriteMasked(Renderer2D& renderer, const SpriteSheet& sheet,
                      const SpriteFrame& frame, core::PointI logicalPosition,
                      PixelMaskView mask, bool drawMaskedPixels,
                      bool additionalFlipX = false);

} // namespace underworld::render
