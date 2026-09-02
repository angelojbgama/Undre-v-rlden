#include "game/game.h"

#include "engine/core/fixed_timestep.h"
#include "engine/core/game_metrics.h"
#include "engine/platform/platform.h"
#include "engine/render/framebuffer.h"
#include "game/phase3_demo.h"

#include <cstdint>
#include <sstream>

namespace underworld::game {

namespace {

constexpr core::FixedStepConfig fixedStepConfig{
    core::GameMetrics::fixedDt,
    0.25, // Never accept more than 250 ms from one rendered frame.
    5,    // At most 83.3 ms of simulation work is recovered per frame.
};

} // namespace

int run(platform::Platform& platform) {
    render::Framebuffer framebuffer(core::GameMetrics::logicalWidth,
                                    core::GameMetrics::logicalHeight);
    Phase3Demo demo(platform.imageDecoder(),
                    findLicensedAssetRoot(platform.executableDirectory()));
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

        const core::FixedStepResult step = accumulator.advance(frameDelta, [&] {
            ++tickCount;
            demo.fixedTick(platform.consumeDebugInput());
        });

        if (step.frameDeltaClamped || step.catchUpLimited) {
            std::ostringstream message;
            message << "timing protection discarded " << step.discardedSeconds << " seconds";
            platform.log(platform::LogLevel::warning, message.str());
        }

        demo.render(framebuffer);
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
