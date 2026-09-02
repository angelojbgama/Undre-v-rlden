#pragma once

namespace underworld::core {

struct PointI final {
    int x{};
    int y{};

    [[nodiscard]] constexpr bool operator==(const PointI&) const noexcept = default;
};

struct RectI final {
    int x{};
    int y{};
    int width{};
    int height{};

    [[nodiscard]] constexpr bool operator==(const RectI&) const noexcept = default;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return width <= 0 || height <= 0;
    }
};

} // namespace underworld::core
