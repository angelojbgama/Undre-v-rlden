#pragma once

#include <cstddef>

namespace underworld::core {

enum class PixelFormat {
    rgba8,
};

// Non-owning, read-only view. The owner must outlive every use of this value.
struct PixelBufferView final {
    const std::byte* pixels{};
    int width{};
    int height{};
    std::size_t strideBytes{};
    PixelFormat format{PixelFormat::rgba8};

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return pixels != nullptr && width > 0 && height > 0 &&
               strideBytes >= static_cast<std::size_t>(width) * 4U;
    }
};

} // namespace underworld::core
