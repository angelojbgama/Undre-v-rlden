#include "engine/world/tile_layer.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace underworld::world {

namespace {

std::size_t checkedCellCount(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("tile layer dimensions must be positive");
    }
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h) {
        throw std::length_error("tile layer dimensions overflow");
    }
    const std::size_t count = w * h;
    if (count > std::vector<TileCell>().max_size()) {
        throw std::length_error("tile layer is too large");
    }
    return count;
}

} // namespace

TileLayer::TileLayer(std::string name, int width, int height, bool visible)
    : name_(std::move(name)), width_(width), height_(height), visible_(visible),
      cells_(checkedCellCount(width, height)) {
    if (name_.empty()) {
        throw std::invalid_argument("tile layer name cannot be empty");
    }
}

const TileCell& TileLayer::cell(int x, int y) const {
    return cells_[checkedIndex(x, y)];
}

TileCell& TileLayer::cell(int x, int y) {
    return cells_[checkedIndex(x, y)];
}

void TileLayer::set(int x, int y, TileCell value) {
    cells_[checkedIndex(x, y)] = std::move(value);
}

std::size_t TileLayer::checkedIndex(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        throw std::out_of_range("tile layer coordinate is outside its bounds");
    }
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(x);
}

} // namespace underworld::world
