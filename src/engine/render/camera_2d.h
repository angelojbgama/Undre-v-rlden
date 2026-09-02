#pragma once

#include "engine/core/coordinates.h"

#include <cstddef>

namespace underworld::render {

struct VisibleTileRange final {
    int firstX{};
    int firstY{};
    int lastX{-1}; // Inclusive.
    int lastY{-1}; // Inclusive.

    [[nodiscard]] constexpr bool empty() const noexcept {
        return firstX > lastX || firstY > lastY;
    }
    [[nodiscard]] constexpr std::size_t tileCount() const noexcept {
        return empty() ? 0U :
            static_cast<std::size_t>(lastX - firstX + 1) *
            static_cast<std::size_t>(lastY - firstY + 1);
    }
    [[nodiscard]] constexpr bool operator==(const VisibleTileRange&) const noexcept = default;
};

class Camera2D final {
public:
    Camera2D(int viewportWidth, int viewportHeight);

    [[nodiscard]] core::WorldPointI position() const noexcept { return position_; }
    [[nodiscard]] int viewportWidth() const noexcept { return viewportWidth_; }
    [[nodiscard]] int viewportHeight() const noexcept { return viewportHeight_; }

    void setPosition(core::WorldPointI position) noexcept { position_ = position; }
    void move(int deltaX, int deltaY) noexcept;
    void clampToWorld(int worldWidthPixels, int worldHeightPixels) noexcept;

    [[nodiscard]] core::LogicalPointI worldToLogical(core::WorldPointI world) const noexcept;
    [[nodiscard]] core::WorldPointI logicalToWorld(core::LogicalPointI logical) const noexcept;
    [[nodiscard]] VisibleTileRange visibleTiles(int mapWidthTiles, int mapHeightTiles,
                                                int tileSize) const;

private:
    core::WorldPointI position_{};
    int viewportWidth_{};
    int viewportHeight_{};
};

} // namespace underworld::render
