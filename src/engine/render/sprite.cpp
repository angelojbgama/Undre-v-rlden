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

} // namespace underworld::render
