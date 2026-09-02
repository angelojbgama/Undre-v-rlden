#pragma once

#include "engine/core/color_rgba8.h"
#include "engine/core/pixel_buffer_view.h"

#include <cstddef>
#include <span>
#include <vector>

namespace underworld::render {

// Logical framebuffer coordinates use (0,0) at the top-left, +x right, +y down.
class Framebuffer final {
public:
    Framebuffer(int width, int height);

    void clear(core::ColorRGBA8 color) noexcept;

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::size_t stride() const noexcept;
    [[nodiscard]] std::size_t byteSize() const noexcept;
    [[nodiscard]] std::span<core::ColorRGBA8> pixels() noexcept { return pixels_; }
    [[nodiscard]] std::span<const core::ColorRGBA8> pixels() const noexcept { return pixels_; }
    [[nodiscard]] core::PixelBufferView view() const noexcept;

private:
    int width_{};
    int height_{};
    std::vector<core::ColorRGBA8> pixels_{};
};

} // namespace underworld::render
