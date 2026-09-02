#pragma once

#include <cstddef>

namespace underworld::world {

class CollisionGrid;

// Half-open world AABB: [x,x+width) x [y,y+height). Touching is not overlap.
struct AabbI final {
    int x{};
    int y{};
    int width{};
    int height{};
    [[nodiscard]] constexpr bool operator==(const AabbI&) const noexcept = default;
};

struct CollisionQueryResult final {
    bool collides{};
    std::size_t cellsTested{};
};

struct MovementResult final {
    int movedX{};
    int movedY{};
    bool blockedX{};
    bool blockedY{};
};

[[nodiscard]] CollisionQueryResult querySolidTiles(const CollisionGrid& grid,
                                                   AabbI body, int tileSize);
[[nodiscard]] MovementResult moveAgainstSolidTiles(const CollisionGrid& grid,
                                                   AabbI& body, int deltaX, int deltaY,
                                                   int tileSize);

} // namespace underworld::world
