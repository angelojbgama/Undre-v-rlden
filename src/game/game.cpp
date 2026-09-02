#include "game/game.h"

#include "engine/core/fixed_timestep.h"
#include "engine/core/game_metrics.h"
#include "engine/platform/platform.h"
#include "engine/render/framebuffer.h"

#include <cstdint>
#include <sstream>

namespace underworld::game {

namespace {

using core::ColorRGBA8;

constexpr core::FixedStepConfig fixedStepConfig{
    core::GameMetrics::fixedDt,
    0.25, // Never accept more than 250 ms from one rendered frame.
    5,    // At most 83.3 ms of simulation work is recovered per frame.
};

void renderSyntheticPattern(render::Framebuffer& framebuffer, std::uint64_t tickCount) {
    const int width = framebuffer.width();
    const int height = framebuffer.height();
    const int halfWidth = width / 2;
    const int halfHeight = height / 2;
    auto pixels = framebuffer.pixels();

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            ColorRGBA8 color{};
            if (x < halfWidth && y < halfHeight) {
                color = {255, 0, 0, 255};
            } else if (x >= halfWidth && y < halfHeight) {
                color = {0, 255, 0, 255};
            } else if (x < halfWidth) {
                color = {0, 0, 255, 255};
            } else {
                color = {255, 255, 255, 255};
            }
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] = color;
        }
    }

    // Asymmetric black L: its corner must remain at the logical top-left.
    for (int x = 0; x < 13; ++x) {
        pixels[static_cast<std::size_t>(x)] = {0, 0, 0, 255};
    }
    for (int y = 0; y < 9; ++y) {
        pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)] =
            {0, 0, 0, 255};
    }
    pixels[static_cast<std::size_t>(width) + 1U] = {255, 255, 0, 255};

    // A one-pixel white tick marker moves horizontally without renderer helpers.
    const int markerX = static_cast<int>((tickCount / 4U) % static_cast<std::uint64_t>(width));
    for (int y = halfHeight - 2; y <= halfHeight + 2; ++y) {
        pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(markerX)] = {255, 255, 255, 255};
    }
}

} // namespace

int run(platform::Platform& platform) {
    render::Framebuffer framebuffer(core::GameMetrics::logicalWidth,
                                    core::GameMetrics::logicalHeight);
    core::FixedStepAccumulator accumulator(fixedStepConfig);

    std::uint64_t tickCount = 0;
    std::uint64_t ticksAtLastReport = 0;
    std::uint64_t renderedFrames = 0;
    double previous = platform.nowSeconds();
    double lastReport = previous;

    platform.log(platform::LogLevel::info, "startup: entering fixed-step loop");

    while (platform.isRunning()) {
        platform.pollEvents();
        if (!platform.isRunning()) {
            break;
        }

        if (platform.isMinimized()) {
            platform.waitForEvents();
            previous = platform.nowSeconds();
            continue;
        }

        const double current = platform.nowSeconds();
        const double frameDelta = current - previous;
        previous = current;

        const core::FixedStepResult step = accumulator.advance(frameDelta, [&tickCount] {
            ++tickCount;
        });

        if (step.frameDeltaClamped || step.catchUpLimited) {
            std::ostringstream message;
            message << "timing protection discarded " << step.discardedSeconds << " seconds";
            platform.log(platform::LogLevel::warning, message.str());
        }

        renderSyntheticPattern(framebuffer, tickCount);
        if (!platform.present(framebuffer.view())) {
            platform.log(platform::LogLevel::error, "framebuffer presentation failed");
            return 1;
        }
        ++renderedFrames;

        if (current - lastReport >= 1.0) {
            const auto intervalTicks = tickCount - ticksAtLastReport;
            std::ostringstream message;
            message << "fixed ticks/s=" << intervalTicks << ", rendered frames=" << renderedFrames;
            platform.log(platform::LogLevel::info, message.str());
            lastReport = current;
            ticksAtLastReport = tickCount;
            renderedFrames = 0;
        }
    }

    platform.log(platform::LogLevel::info, "shutdown: fixed-step loop ended");
    return 0;
}

} // namespace underworld::game
