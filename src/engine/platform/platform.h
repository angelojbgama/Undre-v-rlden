#pragma once

#include "engine/core/pixel_buffer_view.h"

#include <string_view>

namespace underworld::platform {

enum class LogLevel {
    info,
    warning,
    error,
};

class Platform {
public:
    virtual ~Platform() = default;

    virtual void pollEvents() = 0;
    [[nodiscard]] virtual bool isRunning() const noexcept = 0;
    [[nodiscard]] virtual bool isMinimized() const noexcept = 0;
    virtual void waitForEvents() = 0;
    [[nodiscard]] virtual double nowSeconds() const noexcept = 0;
    virtual bool present(core::PixelBufferView surface) = 0;
    virtual void log(LogLevel level, std::string_view message) const = 0;
};

} // namespace underworld::platform
