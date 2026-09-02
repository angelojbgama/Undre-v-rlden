#include "engine/world/runtime_map.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace underworld::world {

namespace {

int checkedWorldExtent(int tiles, int tileSize) {
    if (tiles <= 0 || tileSize <= 0) {
        throw std::invalid_argument("runtime map dimensions and tile size must be positive");
    }
    const std::int64_t pixels = static_cast<std::int64_t>(tiles) * tileSize;
    if (pixels > std::numeric_limits<int>::max()) {
        throw std::length_error("runtime map pixel extent overflows integer world coordinates");
    }
    return static_cast<int>(pixels);
}

} // namespace

RuntimeMap::RuntimeMap(int widthTiles, int heightTiles, int tileSize)
    : widthTiles_(widthTiles), heightTiles_(heightTiles), tileSize_(tileSize),
      worldWidthPixels_(checkedWorldExtent(widthTiles, tileSize)),
      worldHeightPixels_(checkedWorldExtent(heightTiles, tileSize)),
      collision_(widthTiles, heightTiles) {}

std::size_t RuntimeMap::addLayer(std::string name, bool visible) {
    layers_.emplace_back(std::move(name), widthTiles_, heightTiles_, visible);
    return layers_.size() - 1U;
}

TileLayer& RuntimeMap::layer(std::size_t index) {
    if (index >= layers_.size()) {
        throw std::out_of_range("runtime map layer index is outside its bounds");
    }
    return layers_[index];
}

const TileLayer& RuntimeMap::layer(std::size_t index) const {
    if (index >= layers_.size()) {
        throw std::out_of_range("runtime map layer index is outside its bounds");
    }
    return layers_[index];
}

} // namespace underworld::world
