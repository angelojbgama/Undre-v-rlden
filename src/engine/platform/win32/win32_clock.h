#pragma once

#include <cstdint>

namespace underworld::platform::win32 {

class Win32Clock final {
public:
    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] double nowSeconds() const noexcept;
    [[nodiscard]] std::int64_t frequency() const noexcept { return frequency_; }

private:
    std::int64_t frequency_{};
};

} // namespace underworld::platform::win32
