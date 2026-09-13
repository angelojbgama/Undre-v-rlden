#include "game/gameplay/attack_shapes.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace underworld::game::gameplay {

std::size_t facingIndex(FacingDirection facing) noexcept {
    switch (facing) {
    case FacingDirection::down: return 0;
    case FacingDirection::up: return 1;
    case FacingDirection::left: return 2;
    case FacingDirection::right: return 3;
    }
    return 0;
}

namespace {

template <typename Query>
std::optional<world::AabbI> clipAttackRegion(
    world::AabbI region,
    FacingDirection facing,
    Query&& collides) {
    if (region.width <= 0 || region.height <= 0) return std::nullopt;

    const auto horizontalSlice = [&](int x) {
        return world::AabbI{x, region.y, 1, region.height};
    };
    const auto verticalSlice = [&](int y) {
        return world::AabbI{region.x, y, region.width, 1};
    };

    if (facing == FacingDirection::right) {
        for (int offset = 0; offset < region.width; ++offset) {
            if (!collides(horizontalSlice(region.x + offset))) continue;
            if (offset == 0) return std::nullopt;
            region.width = offset;
            return region;
        }
        return region;
    }

    if (facing == FacingDirection::left) {
        for (int offset = 0; offset < region.width; ++offset) {
            const int x = region.x + region.width - 1 - offset;
            if (!collides(horizontalSlice(x))) continue;
            if (offset == 0) return std::nullopt;
            region.x = region.x + region.width - offset;
            region.width = offset;
            return region;
        }
        return region;
    }

    if (facing == FacingDirection::down) {
        for (int offset = 0; offset < region.height; ++offset) {
            if (!collides(verticalSlice(region.y + offset))) continue;
            if (offset == 0) return std::nullopt;
            region.height = offset;
            return region;
        }
        return region;
    }

    for (int offset = 0; offset < region.height; ++offset) {
        const int y = region.y + region.height - 1 - offset;
        if (!collides(verticalSlice(y))) continue;
        if (offset == 0) return std::nullopt;
        region.y = region.y + region.height - offset;
        region.height = offset;
        return region;
    }
    return region;
}

} // namespace

std::vector<DirectionalBoxDefinition> compileAttackShapeMask(
    std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& cells,
    int originX, int originY) {
    std::vector<DirectionalBoxDefinition> result;
    if (width == 0 || height == 0 || cells.size() != static_cast<std::size_t>(width) * height) {
        return result;
    }
    // First create deterministic horizontal runs for each row.
    struct Run final { int x{}; int y{}; int width{}; };
    std::vector<Run> runs;
    for (std::uint32_t y = 0; y < height; ++y) {
        std::uint32_t x = 0;
        while (x < width) {
            while (x < width && cells[static_cast<std::size_t>(y) * width + x] == 0) ++x;
            if (x == width) break;
            const auto start = x++;
            while (x < width && cells[static_cast<std::size_t>(y) * width + x] != 0) ++x;
            runs.push_back({static_cast<int>(start), static_cast<int>(y),
                            static_cast<int>(x - start)});
        }
    }
    // Merge only equal-width runs with equal x positions in adjacent rows.
    // This produces stable rectangles without unordered containers.
    for (const auto& run : runs) {
        if (!result.empty()) {
            auto& previous = result.back();
            if (previous.offsetX == originX + run.x &&
                previous.width == run.width &&
                previous.offsetY + previous.height == originY + run.y) {
                ++previous.height;
                continue;
            }
        }
        result.push_back({originX + run.x, originY + run.y, run.width, 1});
    }
    return result;
}

std::optional<world::AabbI> clipAttackRegionAgainstSolidTiles(
    const world::CollisionGrid& grid,
    world::AabbI region,
    FacingDirection facing,
    int tileSize) {
    return clipAttackRegion(
        region, facing,
        [&](world::AabbI slice) {
            return world::querySolidTiles(
                grid, slice, tileSize).collides;
        });
}

std::optional<world::AabbI> clipAttackRegionAgainstSolidWorld(
    const world::CollisionGrid& grid,
    world::AabbI region,
    FacingDirection facing,
    int tileSize,
    std::span<const world::AabbI> staticObstacles) {
    return clipAttackRegion(
        region, facing,
        [&](world::AabbI slice) {
            return world::querySolidWorld(
                grid, slice, tileSize, staticObstacles).collides;
        });
}

} // namespace underworld::game::gameplay
