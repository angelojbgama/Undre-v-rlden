#pragma once

#include "engine/core/geometry.h"

#include <memory>

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

void drawSprite(Renderer2D& renderer, const SpriteSheet& sheet, const SpriteFrame& frame,
                core::PointI logicalPosition, bool additionalFlipX = false);

} // namespace underworld::render
