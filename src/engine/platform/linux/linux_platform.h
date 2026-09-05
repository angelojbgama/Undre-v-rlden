#pragma once

#include "engine/platform/platform.h"

#include <memory>

namespace underworld::platform::linux {

class LinuxPlatform final : public Platform {
public:
    struct Implementation;

    LinuxPlatform();
    ~LinuxPlatform() override;
    LinuxPlatform(const LinuxPlatform&) = delete;
    LinuxPlatform& operator=(const LinuxPlatform&) = delete;

    [[nodiscard]] bool initialize();
    void pollEvents() override;
    [[nodiscard]] bool isRunning() const noexcept override;
    [[nodiscard]] bool isMinimized() const noexcept override;
    void waitForEvents() override;
    [[nodiscard]] double nowSeconds() const noexcept override;
    bool present(core::PixelBufferView surface) override;
    void log(LogLevel level, std::string_view message) const override;
    [[nodiscard]] ImageDecoder& imageDecoder() noexcept override;
    [[nodiscard]] std::filesystem::path executableDirectory() const override;
    [[nodiscard]] InputState consumeInputState() noexcept override;
    [[nodiscard]] DebugInputState consumeDebugInput() noexcept override;

private:
    std::unique_ptr<Implementation> implementation_;
};

} // namespace underworld::platform::linux
