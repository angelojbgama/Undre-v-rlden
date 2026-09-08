#pragma once

#include "game/gameplay/attack_definitions.h"

#include <cstdint>
#include <vector>

namespace underworld::game::gameplay {

// A mask is an authoring aid. Runtime collision uses the compact rectangles
// produced by compileAttackShapeMask and never samples presentation pixels.
[[nodiscard]] std::vector<DirectionalBoxDefinition> compileAttackShapeMask(
    std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& cells,
    int originX = 0, int originY = 0);

[[nodiscard]] std::size_t facingIndex(FacingDirection facing) noexcept;

} // namespace underworld::game::gameplay
