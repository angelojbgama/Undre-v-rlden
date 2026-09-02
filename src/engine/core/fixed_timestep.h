#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace underworld::core {

struct FixedStepConfig final {
    double fixedDt{1.0 / 60.0};
    double maxFrameDelta{0.25};
    std::uint32_t maxCatchUpTicks{5};
};

struct FixedStepResult final {
    std::uint32_t ticksExecuted{};
    double interpolationAlpha{};
    double discardedSeconds{};
    bool frameDeltaClamped{};
    bool catchUpLimited{};
};

class FixedStepAccumulator final {
public:
    explicit FixedStepAccumulator(FixedStepConfig config)
        : config_(config) {
        if (!(config_.fixedDt > 0.0) || !(config_.maxFrameDelta > 0.0) ||
            config_.maxCatchUpTicks == 0) {
            throw std::invalid_argument("invalid fixed-step configuration");
        }
    }

    template <typename TickFunction>
    FixedStepResult advance(double frameDelta, TickFunction&& fixedTick) {
        FixedStepResult result{};

        if (!std::isfinite(frameDelta) || frameDelta < 0.0) {
            result.discardedSeconds = std::isfinite(frameDelta) && frameDelta < 0.0
                                          ? -frameDelta
                                          : 0.0;
            frameDelta = 0.0;
            result.frameDeltaClamped = true;
        } else if (frameDelta > config_.maxFrameDelta) {
            result.discardedSeconds = frameDelta - config_.maxFrameDelta;
            frameDelta = config_.maxFrameDelta;
            result.frameDeltaClamped = true;
        }

        accumulator_ += frameDelta;

        while (accumulator_ >= config_.fixedDt &&
               result.ticksExecuted < config_.maxCatchUpTicks) {
            fixedTick();
            accumulator_ -= config_.fixedDt;
            ++result.ticksExecuted;
        }

        if (accumulator_ >= config_.fixedDt) {
            const double remainder = std::fmod(accumulator_, config_.fixedDt);
            result.discardedSeconds += accumulator_ - remainder;
            accumulator_ = remainder;
            result.catchUpLimited = true;
        }

        result.interpolationAlpha = accumulator_ / config_.fixedDt;
        return result;
    }

    [[nodiscard]] double accumulatorSeconds() const noexcept { return accumulator_; }

private:
    FixedStepConfig config_{};
    double accumulator_{};
};

} // namespace underworld::core
