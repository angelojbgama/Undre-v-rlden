#include "engine/world/collision.h"

#include "engine/core/coordinates.h"
#include "engine/world/collision_grid.h"

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace underworld::world {

namespace {

void validateBody(AabbI body, int tileSize) {
    if (body.width <= 0 || body.height <= 0 || tileSize <= 0) {
        throw std::invalid_argument("collision requires a positive AABB and tile size");
    }
}

bool addWouldOverflow(int value, int delta) noexcept {
    return (delta > 0 && value > std::numeric_limits<int>::max() - delta) ||
           (delta < 0 && value < std::numeric_limits<int>::min() - delta);
}

} // namespace

CollisionQueryResult querySolidTiles(const CollisionGrid& grid, AabbI body, int tileSize) {
    validateBody(body, tileSize);
    const std::int64_t right = static_cast<std::int64_t>(body.x) + body.width;
    const std::int64_t bottom = static_cast<std::int64_t>(body.y) + body.height;
    const std::int64_t worldWidth = static_cast<std::int64_t>(grid.width()) * tileSize;
    const std::int64_t worldHeight = static_cast<std::int64_t>(grid.height()) * tileSize;
    if (body.x < 0 || body.y < 0 || right > worldWidth || bottom > worldHeight) {
        return {true, 1};
    }

    const int firstX = static_cast<int>(core::floorDiv(body.x, tileSize));
    const int firstY = static_cast<int>(core::floorDiv(body.y, tileSize));
    const int lastX = static_cast<int>(core::floorDiv(right - 1, tileSize));
    const int lastY = static_cast<int>(core::floorDiv(bottom - 1, tileSize));
    CollisionQueryResult result{};
    for (int y = firstY; y <= lastY; ++y) {
        for (int x = firstX; x <= lastX; ++x) {
            ++result.cellsTested;
            if (grid.isSolid(x, y)) {
                result.collides = true;
                return result;
            }
        }
    }
    return result;
}

MovementResult moveAgainstSolidTiles(const CollisionGrid& grid, AabbI& body,
                                     int deltaX, int deltaY, int tileSize) {
    validateBody(body, tileSize);
    MovementResult result{};

    auto moveAxis = [&](int requested, bool horizontal) {
        int remaining = requested;
        while (remaining != 0) {
            const int step = remaining > 0 ? 1 : -1;
            int& coordinate = horizontal ? body.x : body.y;
            if (addWouldOverflow(coordinate, step)) {
                if (horizontal) {
                    result.blockedX = true;
                } else {
                    result.blockedY = true;
                }
                return;
            }
            AabbI candidate = body;
            (horizontal ? candidate.x : candidate.y) += step;
            if (querySolidTiles(grid, candidate, tileSize).collides) {
                if (horizontal) {
                    result.blockedX = true;
                } else {
                    result.blockedY = true;
                }
                return;
            }
            coordinate += step;
            if (horizontal) {
                result.movedX += step;
            } else {
                result.movedY += step;
            }
            remaining -= step;
        }
    };

    moveAxis(deltaX, true);
    moveAxis(deltaY, false);
    return result;
}

} // namespace underworld::world
