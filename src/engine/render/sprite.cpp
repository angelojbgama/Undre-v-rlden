#include "engine/render/sprite.h"

#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"

#include <stdexcept>
#include <utility>

namespace underworld::render {

SpriteSheet::SpriteSheet(std::shared_ptr<const Image> image) : image_(std::move(image)) {
    if (!image_) {
        throw std::invalid_argument("sprite sheet requires an image");
    }
}

void drawSprite(Renderer2D& renderer, const SpriteSheet& sheet, const SpriteFrame& frame,
                core::PointI logicalPosition, bool additionalFlipX) {
    const bool flipX = frame.flipX != additionalFlipX;
    const int effectiveAnchorX = flipX ? frame.source.width - frame.anchor.x : frame.anchor.x;
    const int drawX = logicalPosition.x - effectiveAnchorX + frame.drawOffset.x;
    const int drawY = logicalPosition.y - frame.anchor.y + frame.drawOffset.y;
    if (flipX) {
        renderer.drawImageRegionFlipX(sheet.image(), frame.source, drawX, drawY);
    } else {
        renderer.drawImageRegion(sheet.image(), frame.source, drawX, drawY);
    }
}

void drawSpriteMasked(Renderer2D& renderer, const SpriteSheet& sheet,
                      const SpriteFrame& frame, core::PointI logicalPosition,
                      PixelMaskView mask, bool drawMaskedPixels,
                      bool additionalFlipX) {
    if (!mask.valid()) throw std::invalid_argument("sprite pixel mask is invalid");
    const bool flipX = frame.flipX != additionalFlipX;
    const int effectiveAnchorX = flipX ? frame.source.width - frame.anchor.x : frame.anchor.x;
    const int drawX = logicalPosition.x - effectiveAnchorX + frame.drawOffset.x;
    const int drawY = logicalPosition.y - frame.anchor.y + frame.drawOffset.y;
    const auto& image = sheet.image();
    const auto& bytes = image.bytes();

    for (int localY = 0; localY < frame.source.height; ++localY) {
        const int relativeY = drawY + localY - logicalPosition.y;
        const int maskY = relativeY - mask.origin.y;
        for (int localX = 0; localX < frame.source.width; ++localX) {
            const int relativeX = drawX + localX - logicalPosition.x;
            const int maskX = relativeX - mask.origin.x;
            const bool selected =
                maskX >= 0 && maskY >= 0 && maskX < mask.width && maskY < mask.height &&
                mask.cells[static_cast<std::size_t>(maskY) *
                               static_cast<std::size_t>(mask.width) +
                           static_cast<std::size_t>(maskX)] != 0;
            if (selected != drawMaskedPixels) continue;

            const int sourceLocalX = flipX ? frame.source.width - 1 - localX : localX;
            const int sourceX = frame.source.x + sourceLocalX;
            const int sourceY = frame.source.y + localY;
            const std::size_t offset =
                static_cast<std::size_t>(sourceY) * image.stride() +
                static_cast<std::size_t>(sourceX) * 4U;
            renderer.setPixel(
                drawX + localX, drawY + localY,
                {bytes[offset], bytes[offset + 1U], bytes[offset + 2U], bytes[offset + 3U]});
        }
    }
}

} // namespace underworld::render
