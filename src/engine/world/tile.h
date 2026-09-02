#pragma once

#include "engine/core/geometry.h"

#include <cstdint>
#include <optional>

namespace underworld::world {

using TilesetId = std::uint32_t;
constexpr TilesetId invalidTilesetId = 0;

enum class TileFlags : std::uint8_t {
    none = 0,
    flipX = 1U << 0U,
};

[[nodiscard]] constexpr bool hasFlag(TileFlags value, TileFlags flag) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
}

struct TileDefinition final {
    TilesetId tilesetId{invalidTilesetId};
    std::uint32_t sourceIndex{};
    [[nodiscard]] constexpr bool operator==(const TileDefinition&) const noexcept = default;
};

struct TileRef final {
    TileDefinition definition{};
    TileFlags flags{TileFlags::none};
    [[nodiscard]] constexpr bool operator==(const TileRef&) const noexcept = default;
};

using TileCell = std::optional<TileRef>; // nullopt is empty; atlas tile index 0 remains usable.

class TileAtlasLayout final {
public:
    TileAtlasLayout(int imageWidth, int imageHeight, int tileSize);

    [[nodiscard]] int columns() const noexcept { return columns_; }
    [[nodiscard]] int rows() const noexcept { return rows_; }
    [[nodiscard]] int tileSize() const noexcept { return tileSize_; }
    [[nodiscard]] std::uint32_t tileCount() const noexcept { return tileCount_; }
    [[nodiscard]] core::RectI sourceRect(std::uint32_t sourceIndex) const;

private:
    int columns_{};
    int rows_{};
    int tileSize_{};
    std::uint32_t tileCount_{};
};

} // namespace underworld::world
