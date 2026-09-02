#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "engine/platform/win32/win32_clock.h"

namespace underworld::platform::win32 {

bool Win32Clock::initialize() noexcept {
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) == FALSE || frequency.QuadPart <= 0) {
        frequency_ = 0;
        return false;
    }
    frequency_ = frequency.QuadPart;
    return true;
}

double Win32Clock::nowSeconds() const noexcept {
    if (frequency_ <= 0) {
        return 0.0;
    }

    LARGE_INTEGER counter{};
    if (QueryPerformanceCounter(&counter) == FALSE) {
        return 0.0;
    }
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency_);
}

} // namespace underworld::platform::win32
