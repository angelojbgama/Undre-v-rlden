#pragma once

#include "engine/platform/platform.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace underworld::platform {

struct HeadlessLogEntry final {
    LogLevel level{LogLevel::info};
    std::string message;
};

// Platform boundary used by audit/playtest code. It has no windowing dependency:
// callers advance time explicitly, provide logical input, and receive the real
// renderer framebuffer through present().
class HeadlessAuditPlatform final : public Platform {
public:
    explicit HeadlessAuditPlatform(ImageDecoder& decoder,
                                   std::filesystem::path executableDirectory = {});

    void pollEvents() override {}
    [[nodiscard]] bool isRunning() const noexcept override { return running_; }
    [[nodiscard]] bool isMinimized() const noexcept override { return false; }
    void waitForEvents() override {}
    [[nodiscard]] double nowSeconds() const noexcept override { return nowSeconds_; }
    bool present(core::PixelBufferView surface) override;
    void log(LogLevel level, std::string_view message) const override;
    [[nodiscard]] ImageDecoder& imageDecoder() noexcept override { return decoder_; }
    [[nodiscard]] std::filesystem::path executableDirectory() const override {
        return executableDirectory_;
    }
    [[nodiscard]] InputState consumeInputState() noexcept override;
    [[nodiscard]] DebugInputState consumeDebugInput() noexcept override;

    void stop() noexcept { running_ = false; }
    void start() noexcept { running_ = true; }
    void advanceSeconds(double seconds) noexcept;
    void advanceFixedTicks(std::uint64_t ticks,
                           double secondsPerTick = 1.0 / 60.0) noexcept;

    void setInput(std::uint64_t tick, InputState state);
    void holdInput(std::uint64_t firstTick, std::uint64_t lastTick, InputState state);
    void setDebugInput(std::uint64_t tick, DebugInputState state);
    void holdDebugInput(std::uint64_t firstTick, std::uint64_t lastTick,
                        DebugInputState state);
    void resetInputCursor() noexcept { nextInputTick_ = 1; currentInputTick_ = 0; }

    [[nodiscard]] core::PixelBufferView lastPresentedFrame() const noexcept;
    [[nodiscard]] const std::vector<HeadlessLogEntry>& logs() const noexcept { return logs_; }
    void clearLogs() noexcept { logs_.clear(); }

private:
    static void setRange(std::map<std::uint64_t, InputState>& script,
                         std::uint64_t firstTick, std::uint64_t lastTick, InputState state);
    static void setRange(std::map<std::uint64_t, DebugInputState>& script,
                         std::uint64_t firstTick, std::uint64_t lastTick,
                         DebugInputState state);

    ImageDecoder& decoder_;
    std::filesystem::path executableDirectory_;
    bool running_{true};
    double nowSeconds_{};
    std::map<std::uint64_t, InputState> inputScript_;
    std::map<std::uint64_t, DebugInputState> debugScript_;
    std::uint64_t nextInputTick_{1};
    std::uint64_t currentInputTick_{};
    std::vector<std::byte> lastFrame_;
    int lastFrameWidth_{};
    int lastFrameHeight_{};
    std::size_t lastFrameStride_{};
    mutable std::vector<HeadlessLogEntry> logs_;
};

} // namespace underworld::platform
