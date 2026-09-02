#include "engine/world/collision_grid.h"

#include <limits>
#include <stdexcept>

namespace underworld::world {

namespace {

std::size_t checkedCellCount(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("collision grid dimensions must be positive");
    }
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h) {
        throw std::length_error("collision grid dimensions overflow");
    }
    const std::size_t count = w * h;
    if (count > std::vector<std::uint8_t>().max_size()) {
        throw std::length_error("collision grid is too large");
    }
    return count;
}

} // namespace

CollisionGrid::CollisionGrid(int width, int height)
    : width_(width), height_(height), cells_(checkedCellCount(width, height), 0U) {}

void CollisionGrid::setSolid(int x, int y, bool solid) {
    cells_[checkedIndex(x, y)] = solid ? 1U : 0U;
}

bool CollisionGrid::isSolid(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return true;
    }
    const std::size_t index = static_cast<std::size_t>(y) *
                                  static_cast<std::size_t>(width_) +
                              static_cast<std::size_t>(x);
    return cells_[index] != 0U;
}

std::size_t CollisionGrid::checkedIndex(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        throw std::out_of_range("collision coordinate is outside the grid");
    }
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(x);
}

} // namespace underworld::world
