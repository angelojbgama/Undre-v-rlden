#pragma once

#include "engine/core/color_rgba8.h"
#include "engine/core/image_data.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace underworld::render {

class Image final {
public:
    explicit Image(core::ImageData data);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::size_t stride() const noexcept { return strideBytes_; }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return pixels_; }
    [[nodiscard]] core::ColorRGBA8 pixel(int x, int y) const noexcept;

private:
    int width_{};
    int height_{};
    std::size_t strideBytes_{};
    std::vector<std::uint8_t> pixels_{};
};

} // namespace underworld::render
