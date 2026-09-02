#pragma once

#include "engine/world/tile.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::world {

class TileLayer final {
public:
    TileLayer(std::string name, int width, int height, bool visible = true);

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void setVisible(bool visible) noexcept { visible_ = visible; }

    [[nodiscard]] const TileCell& cell(int x, int y) const;
    [[nodiscard]] TileCell& cell(int x, int y);
    void set(int x, int y, TileCell value);
    [[nodiscard]] std::span<const TileCell> cells() const noexcept { return cells_; }

private:
    [[nodiscard]] std::size_t checkedIndex(int x, int y) const;

    std::string name_;
    int width_{};
    int height_{};
    bool visible_{true};
    std::vector<TileCell> cells_;
};

} // namespace underworld::world
