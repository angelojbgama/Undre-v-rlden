#pragma once

#include "engine/core/color_rgba8.h"
#include "engine/core/geometry.h"

namespace underworld::render {

class Framebuffer;
class Image;

enum class QuarterTurn {
    r0,
    r90,
    r180,
    r270,
};

class Renderer2D final {
public:
    explicit Renderer2D(Framebuffer& target) noexcept : target_(target) {}

    void setPixel(int x, int y, core::ColorRGBA8 color) noexcept;
    void fillRect(core::RectI rectangle, core::ColorRGBA8 color) noexcept;
    void drawImage(const Image& image, int destinationX, int destinationY);
    void drawImageRegion(const Image& image, core::RectI source, int destinationX,
                         int destinationY);
    void drawImageRegionFlipX(const Image& image, core::RectI source, int destinationX,
                              int destinationY);
    void drawImageRegionQuarterTurn(const Image& image, core::RectI source, int destinationX,
                                    int destinationY, QuarterTurn rotation);

private:
    void drawImageRegionImpl(const Image& image, core::RectI source, int destinationX,
                             int destinationY, bool flipX);
    static void blend(core::ColorRGBA8 source, core::ColorRGBA8& destination) noexcept;

    Framebuffer& target_;
};

} // namespace underworld::render
