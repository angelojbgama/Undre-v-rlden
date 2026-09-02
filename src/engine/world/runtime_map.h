#pragma once

#include "engine/world/collision_grid.h"
#include "engine/world/tile_layer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace underworld::world {

class RuntimeMap final {
public:
    RuntimeMap(int widthTiles, int heightTiles, int tileSize);

    [[nodiscard]] int widthTiles() const noexcept { return widthTiles_; }
    [[nodiscard]] int heightTiles() const noexcept { return heightTiles_; }
    [[nodiscard]] int tileSize() const noexcept { return tileSize_; }
    [[nodiscard]] int worldWidthPixels() const noexcept { return worldWidthPixels_; }
    [[nodiscard]] int worldHeightPixels() const noexcept { return worldHeightPixels_; }

    [[nodiscard]] std::size_t addLayer(std::string name, bool visible = true);
    [[nodiscard]] TileLayer& layer(std::size_t index);
    [[nodiscard]] const TileLayer& layer(std::size_t index) const;
    [[nodiscard]] std::size_t layerCount() const noexcept { return layers_.size(); }
    [[nodiscard]] CollisionGrid& collision() noexcept { return collision_; }
    [[nodiscard]] const CollisionGrid& collision() const noexcept { return collision_; }

private:
    int widthTiles_{};
    int heightTiles_{};
    int tileSize_{};
    int worldWidthPixels_{};
    int worldHeightPixels_{};
    std::vector<TileLayer> layers_;
    CollisionGrid collision_;
};

} // namespace underworld::world
