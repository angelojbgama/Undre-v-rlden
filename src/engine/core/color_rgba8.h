#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace underworld::core {

// Canonical pixel order in memory is R, G, B, A, one byte per channel.
struct ColorRGBA8 final {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{};

    [[nodiscard]] constexpr bool operator==(const ColorRGBA8&) const noexcept = default;
};

static_assert(std::is_standard_layout_v<ColorRGBA8>);
static_assert(sizeof(ColorRGBA8) == 4);
static_assert(offsetof(ColorRGBA8, r) == 0);
static_assert(offsetof(ColorRGBA8, g) == 1);
static_assert(offsetof(ColorRGBA8, b) == 2);
static_assert(offsetof(ColorRGBA8, a) == 3);

} // namespace underworld::core
