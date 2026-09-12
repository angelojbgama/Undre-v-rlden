#pragma once

#include <cstddef>
#include <span>

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

struct CornerSlideConfig final {
    int maxProbeDistance{4};
    int correctionStep{1};
};

[[nodiscard]] CollisionQueryResult querySolidTiles(const CollisionGrid& grid,
                                                   AabbI body, int tileSize);
[[nodiscard]] CollisionQueryResult querySolidWorld(
    const CollisionGrid& grid, AabbI body, int tileSize,
    std::span<const AabbI> staticObstacles);
[[nodiscard]] MovementResult moveAgainstSolidTiles(const CollisionGrid& grid,
                                                   AabbI& body, int deltaX, int deltaY,
                                                   int tileSize);
[[nodiscard]] MovementResult moveAgainstSolidWorld(
    const CollisionGrid& grid, AabbI& body, int deltaX, int deltaY, int tileSize,
    std::span<const AabbI> staticObstacles);
[[nodiscard]] MovementResult moveAgainstSolidWorldWithCornerSlide(
    const CollisionGrid& grid, AabbI& body, int deltaX, int deltaY, int tileSize,
    std::span<const AabbI> staticObstacles, CornerSlideConfig config = {});

} // namespace underworld::world
