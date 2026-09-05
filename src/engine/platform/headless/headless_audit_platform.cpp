#include "engine/platform/headless/headless_audit_platform.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

namespace underworld::platform {

HeadlessAuditPlatform::HeadlessAuditPlatform(ImageDecoder& decoder,
                                             std::filesystem::path executableDirectory)
    : decoder_(decoder), executableDirectory_(std::move(executableDirectory)) {}

bool HeadlessAuditPlatform::present(core::PixelBufferView surface) {
    if (!surface.isValid() || surface.format != core::PixelFormat::rgba8) { return false; }
    const auto width = static_cast<std::size_t>(surface.width);
    const auto height = static_cast<std::size_t>(surface.height);
    if (width > std::numeric_limits<std::size_t>::max() / 4U) { return false; }
    const auto rowBytes = width * 4U;
    if (height > std::numeric_limits<std::size_t>::max() / rowBytes) { return false; }
    const auto bytes = rowBytes * height;
    lastFrame_.resize(bytes);
    const auto* source = reinterpret_cast<const std::byte*>(surface.pixels);
    for (std::size_t y = 0; y < height; ++y) {
        std::memcpy(lastFrame_.data() + y * rowBytes,
                    source + y * surface.strideBytes, rowBytes);
    }
    lastFrameWidth_ = surface.width;
    lastFrameHeight_ = surface.height;
    lastFrameStride_ = rowBytes;
    return true;
}

void HeadlessAuditPlatform::log(LogLevel level, std::string_view message) const {
    logs_.push_back({level, std::string(message)});
}

InputState HeadlessAuditPlatform::consumeInputState() noexcept {
    currentInputTick_ = nextInputTick_++;
    const auto found = inputScript_.find(currentInputTick_);
    return found == inputScript_.end() ? InputState{} : found->second;
}

DebugInputState HeadlessAuditPlatform::consumeDebugInput() noexcept {
    const auto found = debugScript_.find(currentInputTick_);
    return found == debugScript_.end() ? DebugInputState{} : found->second;
}

void HeadlessAuditPlatform::advanceSeconds(double seconds) noexcept {
    if (seconds > 0.0) { nowSeconds_ += seconds; }
}

void HeadlessAuditPlatform::advanceFixedTicks(std::uint64_t ticks,
                                              double secondsPerTick) noexcept {
    if (secondsPerTick > 0.0) {
        nowSeconds_ += static_cast<double>(ticks) * secondsPerTick;
    }
}

void HeadlessAuditPlatform::setInput(std::uint64_t tick, InputState state) {
    inputScript_[tick] = state;
}

void HeadlessAuditPlatform::holdInput(std::uint64_t firstTick, std::uint64_t lastTick,
                                      InputState state) {
    setRange(inputScript_, firstTick, lastTick, state);
}

void HeadlessAuditPlatform::setDebugInput(std::uint64_t tick, DebugInputState state) {
    debugScript_[tick] = state;
}

void HeadlessAuditPlatform::holdDebugInput(std::uint64_t firstTick, std::uint64_t lastTick,
                                           DebugInputState state) {
    setRange(debugScript_, firstTick, lastTick, state);
}

void HeadlessAuditPlatform::setRange(std::map<std::uint64_t, InputState>& script,
                                     std::uint64_t firstTick, std::uint64_t lastTick,
                                     InputState state) {
    if (firstTick > lastTick) { return; }
    for (std::uint64_t tick = firstTick;; ++tick) {
        script[tick] = state;
        if (tick == lastTick || tick == std::numeric_limits<std::uint64_t>::max()) { break; }
    }
}

void HeadlessAuditPlatform::setRange(std::map<std::uint64_t, DebugInputState>& script,
                                     std::uint64_t firstTick, std::uint64_t lastTick,
                                     DebugInputState state) {
    if (firstTick > lastTick) { return; }
    for (std::uint64_t tick = firstTick;; ++tick) {
        script[tick] = state;
        if (tick == lastTick || tick == std::numeric_limits<std::uint64_t>::max()) { break; }
    }
}

core::PixelBufferView HeadlessAuditPlatform::lastPresentedFrame() const noexcept {
    return {lastFrame_.empty() ? nullptr : lastFrame_.data(), lastFrameWidth_, lastFrameHeight_,
            lastFrameStride_, core::PixelFormat::rgba8};
}

} // namespace underworld::platform
