#include "engine/render/camera_2d.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace underworld::render {

namespace {

int saturatingAdd(int value, int delta) noexcept {
    const std::int64_t result = static_cast<std::int64_t>(value) + delta;
    return static_cast<int>(std::clamp<std::int64_t>(
        result, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}

} // namespace

Camera2D::Camera2D(int viewportWidth, int viewportHeight)
    : viewportWidth_(viewportWidth), viewportHeight_(viewportHeight) {
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        throw std::invalid_argument("camera viewport dimensions must be positive");
    }
}

void Camera2D::move(int deltaX, int deltaY) noexcept {
    position_.x = saturatingAdd(position_.x, deltaX);
    position_.y = saturatingAdd(position_.y, deltaY);
}

void Camera2D::clampToWorld(int worldWidthPixels, int worldHeightPixels) noexcept {
    const int maximumX = worldWidthPixels > viewportWidth_
                             ? worldWidthPixels - viewportWidth_
                             : 0;
    const int maximumY = worldHeightPixels > viewportHeight_
                             ? worldHeightPixels - viewportHeight_
                             : 0;
    position_.x = std::clamp(position_.x, 0, maximumX);
    position_.y = std::clamp(position_.y, 0, maximumY);
}

core::LogicalPointI Camera2D::worldToLogical(core::WorldPointI world) const noexcept {
    return {world.x - position_.x, world.y - position_.y};
}

core::WorldPointI Camera2D::logicalToWorld(core::LogicalPointI logical) const noexcept {
    return {logical.x + position_.x, logical.y + position_.y};
}

VisibleTileRange Camera2D::visibleTiles(int mapWidthTiles, int mapHeightTiles,
                                       int tileSize) const {
    if (mapWidthTiles <= 0 || mapHeightTiles <= 0 || tileSize <= 0) {
        throw std::invalid_argument("visible tile calculation requires positive dimensions");
    }
    const std::int64_t right = static_cast<std::int64_t>(position_.x) + viewportWidth_ - 1;
    const std::int64_t bottom = static_cast<std::int64_t>(position_.y) + viewportHeight_ - 1;
    const auto unclampedFirstX = core::floorDiv(position_.x, tileSize);
    const auto unclampedFirstY = core::floorDiv(position_.y, tileSize);
    const auto unclampedLastX = core::floorDiv(right, tileSize);
    const auto unclampedLastY = core::floorDiv(bottom, tileSize);

    VisibleTileRange result{
        static_cast<int>(std::clamp<std::int64_t>(unclampedFirstX, 0, mapWidthTiles - 1)),
        static_cast<int>(std::clamp<std::int64_t>(unclampedFirstY, 0, mapHeightTiles - 1)),
        static_cast<int>(std::clamp<std::int64_t>(unclampedLastX, 0, mapWidthTiles - 1)),
        static_cast<int>(std::clamp<std::int64_t>(unclampedLastY, 0, mapHeightTiles - 1))};
    if (unclampedLastX < 0 || unclampedLastY < 0 ||
        unclampedFirstX >= mapWidthTiles || unclampedFirstY >= mapHeightTiles) {
        return {};
    }
    return result;
}

} // namespace underworld::render
