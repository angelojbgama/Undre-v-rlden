#include "game/gameplay/attack_shapes.h"

#include <algorithm>
#include <limits>

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

} // namespace underworld::game::gameplay
