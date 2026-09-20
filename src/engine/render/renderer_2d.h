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
    // Draws the sprite's alpha coverage as a flat color: used for mystery
    // crafting-book entries, where the item shape is shown without its art.
    void drawImageRegionSilhouette(const Image& image, core::RectI source,
                                   int destinationX, int destinationY,
                                   core::ColorRGBA8 color, bool flipX = false);
    void drawImageRegionQuarterTurn(const Image& image, core::RectI source, int destinationX,
                                    int destinationY, QuarterTurn rotation, bool flipX = false);
    void drawImageRegionNearest(const Image& image, core::RectI source,
                                core::RectI destination, bool flipX = false);

private:
    void drawImageRegionImpl(const Image& image, core::RectI source, int destinationX,
                             int destinationY, bool flipX);
    static void blend(core::ColorRGBA8 source, core::ColorRGBA8& destination) noexcept;

    Framebuffer& target_;
};

} // namespace underworld::render
