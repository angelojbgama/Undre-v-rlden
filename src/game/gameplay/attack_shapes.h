#pragma once

#include "engine/world/collision.h"
#include "game/gameplay/attack_definitions.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace underworld::game::gameplay {

// A mask is an authoring aid. Runtime collision uses the compact rectangles
// produced by compileAttackShapeMask and never samples presentation pixels.
[[nodiscard]] std::vector<DirectionalBoxDefinition> compileAttackShapeMask(
    std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& cells,
    int originX = 0, int originY = 0);

[[nodiscard]] std::size_t facingIndex(FacingDirection facing) noexcept;

// Clips a world-space attack region in its facing direction at the first
// gameplay-solid slice. Sprite alpha is presentation data and is never sampled.
[[nodiscard]] std::optional<world::AabbI> clipAttackRegionAgainstSolidTiles(
    const world::CollisionGrid& grid,
    world::AabbI region,
    FacingDirection facing,
    int tileSize);

[[nodiscard]] std::optional<world::AabbI> clipAttackRegionAgainstSolidWorld(
    const world::CollisionGrid& grid,
    world::AabbI region,
    FacingDirection facing,
    int tileSize,
    std::span<const world::AabbI> staticObstacles);

} // namespace underworld::game::gameplay
