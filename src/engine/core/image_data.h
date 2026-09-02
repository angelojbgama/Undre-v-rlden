#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace underworld::core {

// Owned decoder output. Each pixel occupies four bytes in explicit R, G, B, A order.
struct ImageData final {
    int width{};
    int height{};
    std::size_t strideBytes{};
    std::vector<std::uint8_t> pixels{};

    [[nodiscard]] bool isValid() const noexcept {
        if (width <= 0 || height <= 0) {
            return false;
        }
        const auto w = static_cast<std::size_t>(width);
        const auto h = static_cast<std::size_t>(height);
        if (w > std::numeric_limits<std::size_t>::max() / 4U) {
            return false;
        }
        const std::size_t minimumStride = w * 4U;
        if (strideBytes < minimumStride || h > std::numeric_limits<std::size_t>::max() / strideBytes) {
            return false;
        }
        return pixels.size() >= strideBytes * h;
    }
};

} // namespace underworld::core
