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

bool overlaps(AabbI left, AabbI right) noexcept {
    const auto leftRight = static_cast<std::int64_t>(left.x) + left.width;
    const auto leftBottom = static_cast<std::int64_t>(left.y) + left.height;
    const auto rightRight = static_cast<std::int64_t>(right.x) + right.width;
    const auto rightBottom = static_cast<std::int64_t>(right.y) + right.height;
    return static_cast<std::int64_t>(left.x) < rightRight &&
           leftRight > static_cast<std::int64_t>(right.x) &&
           static_cast<std::int64_t>(left.y) < rightBottom &&
           leftBottom > static_cast<std::int64_t>(right.y);
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

CollisionQueryResult querySolidWorld(const CollisionGrid& grid, AabbI body, int tileSize,
                                          std::span<const AabbI> staticObstacles) {
    CollisionQueryResult result = querySolidTiles(grid, body, tileSize);
    if (result.collides) return result;
    for (const auto& obstacle : staticObstacles) {
        ++result.cellsTested;
        if (overlaps(body, obstacle)) {
            result.collides = true;
            return result;
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

MovementResult moveAgainstSolidWorld(const CollisionGrid& grid, AabbI& body,
                                     int deltaX, int deltaY, int tileSize,
                                     std::span<const AabbI> staticObstacles) {
    validateBody(body, tileSize);
    MovementResult result{};
    auto moveAxis = [&](int requested, bool horizontal) {
        int remaining = requested;
        while (remaining != 0) {
            const int step = remaining > 0 ? 1 : -1;
            int& coordinate = horizontal ? body.x : body.y;
            if (addWouldOverflow(coordinate, step)) {
                if (horizontal) result.blockedX = true;
                else result.blockedY = true;
                return;
            }
            AabbI candidate = body;
            (horizontal ? candidate.x : candidate.y) += step;
            if (querySolidWorld(grid, candidate, tileSize, staticObstacles).collides) {
                if (horizontal) result.blockedX = true;
                else result.blockedY = true;
                return;
            }
            coordinate += step;
            if (horizontal) result.movedX += step;
            else result.movedY += step;
            remaining -= step;
        }
    };
    moveAxis(deltaX, true);
    moveAxis(deltaY, false);
    return result;
}

MovementResult moveAgainstSolidWorldWithCornerSlide(
    const CollisionGrid& grid, AabbI& body, int deltaX, int deltaY, int tileSize,
    std::span<const AabbI> staticObstacles, CornerSlideConfig config) {
    if (config.maxProbeDistance < 0) {
        throw std::invalid_argument("corner slide probe distance cannot be negative");
    }
    if (config.maxProbeDistance > 0 && config.correctionStep <= 0) {
        throw std::invalid_argument("corner slide correction step must be positive");
    }

    MovementResult result =
        moveAgainstSolidWorld(grid, body, deltaX, deltaY, tileSize, staticObstacles);

    if (config.maxProbeDistance == 0) return result;

    const bool horizontalAssist =
        deltaX != 0 && deltaY == 0 && result.blockedX;
    const bool verticalAssist =
        deltaY != 0 && deltaX == 0 && result.blockedY;
    if (!horizontalAssist && !verticalAssist) return result;

    const bool primaryHorizontal = horizontalAssist;
    const int primaryStep =
        primaryHorizontal ? (deltaX > 0 ? 1 : -1) : (deltaY > 0 ? 1 : -1);

    auto canEscapeAt = [&](int perpendicularOffset) {
        AabbI shifted = body;
        MovementResult perpendicularMove =
            primaryHorizontal
                ? moveAgainstSolidWorld(grid, shifted, 0, perpendicularOffset, tileSize,
                                        staticObstacles)
                : moveAgainstSolidWorld(grid, shifted, perpendicularOffset, 0, tileSize,
                                        staticObstacles);

        const int movedPerpendicular =
            primaryHorizontal ? perpendicularMove.movedY : perpendicularMove.movedX;
        if (movedPerpendicular != perpendicularOffset) return false;

        AabbI forward = shifted;
        if (primaryHorizontal) forward.x += primaryStep;
        else forward.y += primaryStep;
        return !querySolidWorld(grid, forward, tileSize, staticObstacles).collides;
    };

    int chosenOffset = 0;
    for (int distance = 1; distance <= config.maxProbeDistance; ++distance) {
        const bool negativeFree = canEscapeAt(-distance);
        const bool positiveFree = canEscapeAt(distance);
        if (!negativeFree && !positiveFree) continue;
        chosenOffset = negativeFree ? -distance : distance;
        break;
    }

    if (chosenOffset == 0) return result;

    const int direction = chosenOffset < 0 ? -1 : 1;
    const int absoluteDistance = chosenOffset < 0 ? -chosenOffset : chosenOffset;
    const int correctionMagnitude =
        config.correctionStep < absoluteDistance
            ? config.correctionStep
            : absoluteDistance;
    const int correction = direction * correctionMagnitude;

    const MovementResult slide =
        primaryHorizontal
            ? moveAgainstSolidWorld(grid, body, 0, correction, tileSize, staticObstacles)
            : moveAgainstSolidWorld(grid, body, correction, 0, tileSize, staticObstacles);

    result.movedX += slide.movedX;
    result.movedY += slide.movedY;
    result.blockedX = result.blockedX || slide.blockedX;
    result.blockedY = result.blockedY || slide.blockedY;
    return result;
}

} // namespace underworld::world
