#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>

#include "engine/core/game_metrics.h"
#include "engine/platform/platform.h"
#include "engine/platform/presentation.h"
#include "engine/platform/win32/win32_clock.h"
#include "engine/platform/win32/win32_image_decoder.h"
#include "game/game.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace underworld::platform::win32 {

namespace {

constexpr wchar_t windowClassName[] = L"UnderworldCpuFramebufferWindow";
constexpr wchar_t windowTitle[] = L"Underworld Engine - Phase 4 Player";

class Win32Platform final : public Platform {
public:
    Win32Platform(HINSTANCE instance, int showCommand) noexcept
        : instance_(instance), showCommand_(showCommand) {}

    ~Win32Platform() override {
        if (window_ != nullptr && IsWindow(window_) != FALSE) {
            DestroyWindow(window_);
        }
        if (classRegistered_) {
            UnregisterClassW(windowClassName, instance_);
        }
    }

    [[nodiscard]] bool initialize() {
        if (!initializeDpiAwareness()) {
            log(LogLevel::warning, "DPI awareness was already configured or could not be changed");
        }
        if (!clock_.initialize()) {
            showFatalError(L"QueryPerformanceFrequency failed.");
            return false;
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        windowClass.lpfnWndProc = &Win32Platform::windowProcedure;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = windowClassName;

        if (RegisterClassExW(&windowClass) == 0) {
            showFatalError(L"Could not register the Win32 window class.");
            return false;
        }
        classRegistered_ = true;

        constexpr DWORD style = WS_OVERLAPPEDWINDOW;
        constexpr DWORD extendedStyle = 0;
        RECT windowRect{0, 0, core::GameMetrics::logicalWidth * 2,
                        core::GameMetrics::logicalHeight * 2};
        const UINT dpi = GetDpiForSystem();
        if (AdjustWindowRectExForDpi(&windowRect, style, FALSE, extendedStyle, dpi) == FALSE) {
            showFatalError(L"Could not calculate the initial Win32 window size.");
            return false;
        }

        window_ = CreateWindowExW(
            extendedStyle,
            windowClassName,
            windowTitle,
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr,
            nullptr,
            instance_,
            this);

        if (window_ == nullptr) {
            showFatalError(L"Could not create the Win32 window.");
            return false;
        }

        RECT clientRect{};
        if (GetClientRect(window_, &clientRect) == FALSE) {
            showFatalError(L"Could not query the Win32 client area.");
            return false;
        }
        clientWidth_ = clientRect.right - clientRect.left;
        clientHeight_ = clientRect.bottom - clientRect.top;

        ShowWindow(window_, showCommand_);
        UpdateWindow(window_);
        running_ = true;

        std::ostringstream message;
        message << "startup: client area " << clientWidth_ << 'x' << clientHeight_;
        log(LogLevel::info, message.str());
        return true;
    }

    void pollEvents() override {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) {
                running_ = false;
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    [[nodiscard]] bool isRunning() const noexcept override { return running_; }
    [[nodiscard]] bool isMinimized() const noexcept override { return minimized_; }

    void waitForEvents() override {
        if (running_) {
            WaitMessage();
        }
    }

    [[nodiscard]] double nowSeconds() const noexcept override {
        return clock_.nowSeconds();
    }

    bool present(core::PixelBufferView surface) override {
        if (!prepareDib(surface)) {
            return false;
        }
        if (window_ == nullptr || minimized_) {
            return true;
        }

        HDC deviceContext = GetDC(window_);
        if (deviceContext == nullptr) {
            return false;
        }
        const bool succeeded = paint(deviceContext);
        ReleaseDC(window_, deviceContext);
        return succeeded;
    }

    void log(LogLevel level, std::string_view message) const override {
        const char* prefix = "[info] ";
        if (level == LogLevel::warning) {
            prefix = "[warning] ";
        } else if (level == LogLevel::error) {
            prefix = "[error] ";
        }

        std::string line(prefix);
        line.append(message);
        line.push_back('\n');
        OutputDebugStringA(line.c_str());
        std::clog << line;
    }

    [[nodiscard]] ImageDecoder& imageDecoder() noexcept override { return imageDecoder_; }

    [[nodiscard]] std::filesystem::path executableDirectory() const override {
        std::wstring path(260, L'\0');
        for (;;) {
            const DWORD length = GetModuleFileNameW(
                nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0) {
                throw std::runtime_error("Could not determine the executable directory");
            }
            if (length < path.size()) {
                path.resize(length);
                return std::filesystem::path(path).parent_path();
            }
            if (path.size() > 32768U) {
                throw std::runtime_error("Executable path is unexpectedly long");
            }
            path.resize(path.size() * 2U);
        }
    }

    [[nodiscard]] InputState inputState() const noexcept override { return inputState_; }

    [[nodiscard]] DebugInputState consumeDebugInput() noexcept override {
        DebugInputState result = debugInput_;
        debugInput_.toggleCollisionPressed = false;
        return result;
    }

private:
    [[nodiscard]] bool initializeDpiAwareness() const noexcept {
        if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE) {
            return true;
        }
        return GetLastError() == ERROR_ACCESS_DENIED;
    }

    void showFatalError(const wchar_t* message) const noexcept {
        OutputDebugStringW(message);
        OutputDebugStringW(L"\n");
        MessageBoxW(nullptr, message, L"Underworld initialization error", MB_OK | MB_ICONERROR);
    }

    [[nodiscard]] bool prepareDib(core::PixelBufferView surface) {
        if (!surface.isValid() || surface.format != core::PixelFormat::rgba8) {
            return false;
        }

        const std::size_t width = static_cast<std::size_t>(surface.width);
        const std::size_t height = static_cast<std::size_t>(surface.height);
        constexpr std::size_t bytesPerPixel = 4;
        if (height > dibPixels_.max_size() / width / bytesPerPixel) {
            return false;
        }
        dibPixels_.resize(width * height * bytesPerPixel);

        for (int y = 0; y < surface.height; ++y) {
            const auto* sourceRow = reinterpret_cast<const std::uint8_t*>(surface.pixels) +
                                    static_cast<std::size_t>(y) * surface.strideBytes;
            auto* destinationRow = dibPixels_.data() +
                                   static_cast<std::size_t>(y) * width * bytesPerPixel;
            for (int x = 0; x < surface.width; ++x) {
                const std::size_t source = static_cast<std::size_t>(x) * bytesPerPixel;
                destinationRow[source + 0] = sourceRow[source + 2]; // B
                destinationRow[source + 1] = sourceRow[source + 1]; // G
                destinationRow[source + 2] = sourceRow[source + 0]; // R
                destinationRow[source + 3] = 0;                     // DIB BGRX
            }
        }

        dibWidth_ = surface.width;
        dibHeight_ = surface.height;
        return true;
    }

    [[nodiscard]] bool paint(HDC deviceContext) const {
        RECT clientRect{};
        if (GetClientRect(window_, &clientRect) == FALSE) {
            return false;
        }

        if (dibPixels_.empty() || clientWidth_ <= 0 || clientHeight_ <= 0) {
            FillRect(deviceContext, &clientRect,
                     static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return true;
        }

        const PresentationRect destination = calculatePresentationRect(
            clientWidth_, clientHeight_, dibWidth_, dibHeight_);
        if (!destination.isDrawable()) {
            FillRect(deviceContext, &clientRect,
                     static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return true;
        }

        const HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        const int destinationRight = destination.x + destination.width;
        const int destinationBottom = destination.y + destination.height;
        const RECT topMargin{0, 0, clientWidth_, destination.y};
        const RECT bottomMargin{0, destinationBottom, clientWidth_, clientHeight_};
        const RECT leftMargin{0, destination.y, destination.x, destinationBottom};
        const RECT rightMargin{destinationRight, destination.y, clientWidth_, destinationBottom};
        FillRect(deviceContext, &topMargin, black);
        FillRect(deviceContext, &bottomMargin, black);
        FillRect(deviceContext, &leftMargin, black);
        FillRect(deviceContext, &rightMargin, black);

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = dibWidth_;
        bitmapInfo.bmiHeader.biHeight = -dibHeight_; // Negative means top-down: row 0 is the top.
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(deviceContext, COLORONCOLOR);
        const int copiedLines = StretchDIBits(
            deviceContext,
            destination.x,
            destination.y,
            destination.width,
            destination.height,
            0,
            0,
            dibWidth_,
            dibHeight_,
            dibPixels_.data(),
            &bitmapInfo,
            DIB_RGB_COLORS,
            SRCCOPY);
        return copiedLines != 0 && copiedLines != GDI_ERROR;
    }

    void updateClientSize(WPARAM sizeKind, LPARAM dimensions) {
        minimized_ = sizeKind == SIZE_MINIMIZED;
        clientWidth_ = LOWORD(dimensions);
        clientHeight_ = HIWORD(dimensions);
        if (!minimized_) {
            std::ostringstream message;
            message << "window resized: client area " << clientWidth_ << 'x' << clientHeight_;
            log(LogLevel::info, message.str());
        }
    }

    void applyMinimumWindowSize(MINMAXINFO& info) const noexcept {
        constexpr DWORD style = WS_OVERLAPPEDWINDOW;
        constexpr DWORD extendedStyle = 0;
        RECT minimum{0, 0, core::GameMetrics::logicalWidth, core::GameMetrics::logicalHeight};
        const UINT dpi = window_ != nullptr ? GetDpiForWindow(window_) : GetDpiForSystem();
        if (AdjustWindowRectExForDpi(&minimum, style, FALSE, extendedStyle, dpi) != FALSE) {
            info.ptMinTrackSize.x = minimum.right - minimum.left;
            info.ptMinTrackSize.y = minimum.bottom - minimum.top;
        }
    }

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
            running_ = false;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            updateClientSize(wParam, lParam);
            return 0;
        case WM_GETMINMAXINFO:
            applyMinimumWindowSize(*reinterpret_cast<MINMAXINFO*>(lParam));
            return 0;
        case WM_KEYDOWN:
            updateKey(wParam, true, (lParam & (1LL << 30)) != 0);
            return 0;
        case WM_KEYUP:
            updateKey(wParam, false, false);
            return 0;
        case WM_KILLFOCUS:
            keyDown_.fill(false);
            inputState_.clear();
            debugInput_ = {};
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paintState{};
            HDC deviceContext = BeginPaint(window_, &paintState);
            if (deviceContext != nullptr) {
                if (!paint(deviceContext)) {
                    log(LogLevel::error, "WM_PAINT framebuffer presentation failed");
                }
            }
            EndPaint(window_, &paintState);
            return 0;
        }
        default:
            return DefWindowProcW(window_, message, wParam, lParam);
        }
    }

    void updateKey(WPARAM key, bool down, bool wasDown) noexcept {
        if (key < keyDown_.size()) {
            keyDown_[static_cast<std::size_t>(key)] = down;
        }
        inputState_.moveLeft = keyDown_['A'] || keyDown_[VK_LEFT];
        inputState_.moveRight = keyDown_['D'] || keyDown_[VK_RIGHT];
        inputState_.moveUp = keyDown_['W'] || keyDown_[VK_UP];
        inputState_.moveDown = keyDown_['S'] || keyDown_[VK_DOWN];

        switch (key) {
        case 'C':
            if (down && !wasDown) {
                debugInput_.toggleCollisionPressed = true;
            }
            break;
        default: break;
        }
    }

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam,
                                             LPARAM lParam) {
        Win32Platform* self = reinterpret_cast<Win32Platform*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));

        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<Win32Platform*>(create->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        }

        if (self == nullptr) {
            return DefWindowProcW(window, message, wParam, lParam);
        }

        if (message == WM_NCDESTROY) {
            const LRESULT result = DefWindowProcW(window, message, wParam, lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            self->window_ = nullptr;
            return result;
        }
        return self->handleMessage(message, wParam, lParam);
    }

    HINSTANCE instance_{};
    int showCommand_{};
    HWND window_{};
    bool classRegistered_{};
    bool running_{};
    bool minimized_{};
    int clientWidth_{};
    int clientHeight_{};
    Win32Clock clock_{};
    Win32ImageDecoder imageDecoder_{};
    std::array<bool, 256> keyDown_{};
    InputState inputState_{};
    DebugInputState debugInput_{};
    std::vector<std::uint8_t> dibPixels_{};
    int dibWidth_{};
    int dibHeight_{};
};

} // namespace

} // namespace underworld::platform::win32

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    try {
        underworld::platform::win32::Win32Platform platform(instance, showCommand);
        if (!platform.initialize()) {
            return 1;
        }
        return underworld::game::run(platform);
    } catch (const std::exception& exception) {
        OutputDebugStringA(exception.what());
        MessageBoxA(nullptr, exception.what(), "Underworld fatal error", MB_OK | MB_ICONERROR);
        return 1;
    } catch (...) {
        constexpr char message[] = "Unknown fatal initialization error.";
        OutputDebugStringA(message);
        MessageBoxA(nullptr, message, "Underworld fatal error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
