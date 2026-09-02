#include "engine/world/tile.h"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace underworld::world {

TileAtlasLayout::TileAtlasLayout(int imageWidth, int imageHeight, int tileSize)
    : tileSize_(tileSize) {
    if (imageWidth <= 0 || imageHeight <= 0 || tileSize <= 0 ||
        imageWidth % tileSize != 0 || imageHeight % tileSize != 0) {
        throw std::invalid_argument("tile atlas dimensions must be positive multiples of tile size");
    }
    columns_ = imageWidth / tileSize;
    rows_ = imageHeight / tileSize;
    const std::uint64_t count = static_cast<std::uint64_t>(columns_) *
                                static_cast<std::uint64_t>(rows_);
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("tile atlas contains too many cells");
    }
    tileCount_ = static_cast<std::uint32_t>(count);
}

core::RectI TileAtlasLayout::sourceRect(std::uint32_t sourceIndex) const {
    if (sourceIndex >= tileCount_) {
        throw std::out_of_range("source tile index is outside the atlas");
    }
    const int x = static_cast<int>(sourceIndex % static_cast<std::uint32_t>(columns_));
    const int y = static_cast<int>(sourceIndex / static_cast<std::uint32_t>(columns_));
    return {x * tileSize_, y * tileSize_, tileSize_, tileSize_};
}

} // namespace underworld::world
