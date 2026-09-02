#pragma once

namespace underworld::platform {

struct PresentationRect final {
    int x{};
    int y{};
    int width{};
    int height{};
    int integerScale{};

    [[nodiscard]] constexpr bool isDrawable() const noexcept {
        return integerScale > 0 && width > 0 && height > 0;
    }
};

[[nodiscard]] constexpr PresentationRect calculatePresentationRect(
    int clientWidth,
    int clientHeight,
    int logicalWidth,
    int logicalHeight) noexcept {
    if (clientWidth <= 0 || clientHeight <= 0 || logicalWidth <= 0 || logicalHeight <= 0) {
        return {};
    }

    const int horizontalScale = clientWidth / logicalWidth;
    const int verticalScale = clientHeight / logicalHeight;
    const int scale = horizontalScale < verticalScale ? horizontalScale : verticalScale;
    if (scale <= 0) {
        return {};
    }

    const int width = logicalWidth * scale;
    const int height = logicalHeight * scale;
    return {
        (clientWidth - width) / 2,
        (clientHeight - height) / 2,
        width,
        height,
        scale,
    };
}

} // namespace underworld::platform
