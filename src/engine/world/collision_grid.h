#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace underworld::world {

class CollisionGrid final {
public:
    CollisionGrid(int width, int height);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

    void setSolid(int x, int y, bool solid);
    // Runtime boundary policy: every tile outside the map is solid.
    [[nodiscard]] bool isSolid(int x, int y) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> cells() const noexcept { return cells_; }

private:
    [[nodiscard]] std::size_t checkedIndex(int x, int y) const;

    int width_{};
    int height_{};
    std::vector<std::uint8_t> cells_;
};

} // namespace underworld::world
