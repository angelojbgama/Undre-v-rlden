#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace underworld::core {

struct LogicalPointI final {
    int x{};
    int y{};
    [[nodiscard]] constexpr bool operator==(const LogicalPointI&) const noexcept = default;
};

struct WorldPointI final {
    int x{};
    int y{};
    [[nodiscard]] constexpr bool operator==(const WorldPointI&) const noexcept = default;
};

struct TileCoord final {
    int x{};
    int y{};
    [[nodiscard]] constexpr bool operator==(const TileCoord&) const noexcept = default;
};

// Unlike C++ integer division, these functions round the quotient toward -infinity.
[[nodiscard]] constexpr std::int64_t floorDiv(std::int64_t value, int positiveDivisor) {
    if (positiveDivisor <= 0) {
        throw std::invalid_argument("floorDiv divisor must be positive");
    }
    const std::int64_t divisor = positiveDivisor;
    const std::int64_t quotient = value / divisor;
    const std::int64_t remainder = value % divisor;
    return quotient - (remainder < 0 ? 1 : 0);
}

[[nodiscard]] constexpr int floorMod(int value, int positiveDivisor) {
    if (positiveDivisor <= 0) {
        throw std::invalid_argument("floorMod divisor must be positive");
    }
    const int remainder = value % positiveDivisor;
    return remainder < 0 ? remainder + positiveDivisor : remainder;
}

[[nodiscard]] inline TileCoord worldToTile(WorldPointI world, int tileSize) {
    const auto x = floorDiv(world.x, tileSize);
    const auto y = floorDiv(world.y, tileSize);
    return {static_cast<int>(x), static_cast<int>(y)};
}

[[nodiscard]] inline WorldPointI tileToWorld(TileCoord tile, int tileSize) {
    if (tileSize <= 0) {
        throw std::invalid_argument("tile size must be positive");
    }
    const std::int64_t x = static_cast<std::int64_t>(tile.x) * tileSize;
    const std::int64_t y = static_cast<std::int64_t>(tile.y) * tileSize;
    if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
        y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max()) {
        throw std::overflow_error("tile to world conversion overflows integer coordinates");
    }
    return {static_cast<int>(x), static_cast<int>(y)};
}

} // namespace underworld::core
