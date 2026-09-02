#pragma once

namespace underworld::core {

struct GameMetrics final {
    static constexpr int logicalWidth = 272;
    static constexpr int logicalHeight = 224;
    static constexpr int tileSize = 16;
    static constexpr int tickRate = 60;
    static constexpr double fixedDt = 1.0 / static_cast<double>(tickRate);
};

} // namespace underworld::core
