#include "engine/platform/linux/linux_platform.h"

#include "engine/core/game_metrics.h"
#include "engine/platform/action_edge_buffer.h"
#include "engine/platform/linux/linux_image_decoder.h"
#include "engine/platform/presentation.h"
#include "game/game.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace underworld::platform::linux {

struct LinuxPlatform::Implementation final {
    Display* display{};
    int screen{};
    Window window{};
    Atom closeAtom{};
    GC graphicsContext{};
    bool running{};
    bool minimized{};
    int clientWidth{};
    int clientHeight{};
    InputState input{};
    DebugInputState debugInput{};
    ActionEdgeBuffer actionEdges{};
    LinuxImageDecoder decoder{};
    std::chrono::steady_clock::time_point start{};
};

namespace {

void clearInput(LinuxPlatform::Implementation& state) noexcept {
    state.input.clear();
    state.debugInput = {};
    state.actionEdges.clear();
}

bool keyIsDown(KeySym key) {
    return key == XK_w || key == XK_W || key == XK_a || key == XK_A ||
           key == XK_s || key == XK_S || key == XK_d || key == XK_D ||
           key == XK_Up || key == XK_Down || key == XK_Left || key == XK_Right;
}

void updateHeldInput(LinuxPlatform::Implementation& state, KeySym key, bool down) noexcept {
    switch (key) {
    case XK_w: case XK_W: case XK_Up: state.input.moveUp = down; break;
    case XK_s: case XK_S: case XK_Down: state.input.moveDown = down; break;
    case XK_a: case XK_A: case XK_Left: state.input.moveLeft = down; break;
    case XK_d: case XK_D: case XK_Right: state.input.moveRight = down; break;
    default: break;
    }
}

void updateKey(LinuxPlatform::Implementation& state, KeySym key, bool down) noexcept {
    updateHeldInput(state, key, down);
    if (!down) { return; }
    switch (key) {
    case XK_z: case XK_Z: state.actionEdges.pushPrimary(); break;
    case XK_x: case XK_X: state.actionEdges.pushSecondary(); break;
    case XK_e: case XK_E: state.actionEdges.pushInteract(); break;
    case XK_i: case XK_I: state.actionEdges.pushToggleInventory(); break;
    case XK_1: case XK_2: case XK_3: case XK_4:
        state.actionEdges.pushQuickSlot(static_cast<std::size_t>(key - XK_1)); break;
    case XK_F1: state.debugInput.toggleCollisionBodyPressed = true; break;
    case XK_F2: state.debugInput.toggleHurtboxPressed = true; break;
    case XK_F3: state.debugInput.toggleHitboxPressed = true; break;
    case XK_F4: state.debugInput.toggleInteractionPressed = true; break;
    case XK_F5: state.actionEdges.pushSaveGame(); break;
    case XK_F9: state.actionEdges.pushLoadGame(); break;
    case XK_F12: state.debugInput.captureAuditSnapshotPressed = true; break;
    case XK_c: case XK_C: state.debugInput.toggleCollisionPressed = true; break;
    default: break;
    }
}

std::uint32_t packChannel(std::uint8_t value, unsigned long mask) noexcept {
    if (mask == 0) { return 0; }
    unsigned shift = 0;
    while ((mask & 1UL) == 0) { mask >>= 1U; ++shift; }
    const unsigned maxValue = static_cast<unsigned>(mask);
    return (static_cast<unsigned>(value) * maxValue / 255U) << shift;
}

void writePixel(char* destination, unsigned long pixel, int bytesPerPixel, int byteOrder) noexcept {
    for (int byte = 0; byte < bytesPerPixel; ++byte) {
        const int shift = byteOrder == LSBFirst ? byte * 8 : (bytesPerPixel - 1 - byte) * 8;
        destination[byte] = static_cast<char>((pixel >> shift) & 0xffU);
    }
}

} // namespace

LinuxPlatform::LinuxPlatform() : implementation_(std::make_unique<Implementation>()) {}

LinuxPlatform::~LinuxPlatform() {
    if (!implementation_) { return; }
    if (implementation_->graphicsContext != nullptr) {
        XFreeGC(implementation_->display, implementation_->graphicsContext);
    }
    if (implementation_->window != 0) { XDestroyWindow(implementation_->display, implementation_->window); }
    if (implementation_->display != nullptr) { XCloseDisplay(implementation_->display); }
}

bool LinuxPlatform::initialize() {
    auto& state = *implementation_;
    state.display = XOpenDisplay(nullptr);
    if (state.display == nullptr) { log(LogLevel::error, "could not open X11 display"); return false; }
    state.screen = DefaultScreen(state.display);
    const Window root = RootWindow(state.display, state.screen);
    state.window = XCreateSimpleWindow(state.display, root, 0, 0,
        core::GameMetrics::logicalWidth * 2, core::GameMetrics::logicalHeight * 2, 0,
        BlackPixel(state.display, state.screen), BlackPixel(state.display, state.screen));
    if (state.window == 0) { log(LogLevel::error, "could not create X11 window"); return false; }
    XStoreName(state.display, state.window, "Dungeon Underworld");
    state.closeAtom = XInternAtom(state.display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(state.display, state.window, &state.closeAtom, 1);
    XSelectInput(state.display, state.window,
                 ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask | FocusChangeMask);
    state.graphicsContext = XCreateGC(state.display, state.window, 0, nullptr);
    XMapWindow(state.display, state.window);
    XFlush(state.display);
    state.clientWidth = core::GameMetrics::logicalWidth * 2;
    state.clientHeight = core::GameMetrics::logicalHeight * 2;
    state.start = std::chrono::steady_clock::now();
    state.running = true;
    return true;
}

void LinuxPlatform::pollEvents() {
    auto& state = *implementation_;
    while (state.display != nullptr && XPending(state.display) != 0) {
        XEvent event{};
        XNextEvent(state.display, &event);
        if (event.type == ClientMessage && static_cast<Atom>(event.xclient.data.l[0]) == state.closeAtom) {
            state.running = false;
        } else if (event.type == DestroyNotify) {
            state.running = false;
        } else if (event.type == ConfigureNotify) {
            state.clientWidth = event.xconfigure.width;
            state.clientHeight = event.xconfigure.height;
            state.minimized = state.clientWidth <= 0 || state.clientHeight <= 0;
        } else if (event.type == FocusOut) {
            clearInput(state);
        } else if (event.type == KeyPress || event.type == KeyRelease) {
            const KeySym key = XLookupKeysym(&event.xkey, 0);
            if (event.type == KeyRelease && keyIsDown(key) && XPending(state.display) != 0) {
                XEvent next{};
                XPeekEvent(state.display, &next);
                if (next.type == KeyPress && next.xkey.time == event.xkey.time &&
                    next.xkey.keycode == event.xkey.keycode) { continue; }
            }
            updateKey(state, key, event.type == KeyPress);
        }
    }
}

bool LinuxPlatform::isRunning() const noexcept { return implementation_->running; }
bool LinuxPlatform::isMinimized() const noexcept { return implementation_->minimized; }

void LinuxPlatform::waitForEvents() {
    if (implementation_->display == nullptr || !implementation_->running) { return; }
    XEvent event{};
    XNextEvent(implementation_->display, &event);
    XPutBackEvent(implementation_->display, &event);
}

double LinuxPlatform::nowSeconds() const noexcept {
    if (implementation_->start.time_since_epoch().count() == 0) { return 0.0; }
    const auto elapsed = std::chrono::steady_clock::now() - implementation_->start;
    return std::chrono::duration<double>(elapsed).count();
}

bool LinuxPlatform::present(core::PixelBufferView surface) {
    auto& state = *implementation_;
    if (state.display == nullptr || state.window == 0 || !surface.isValid() ||
        surface.format != core::PixelFormat::rgba8) { return false; }
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(state.display, state.window, &attributes) == 0) { return false; }
    state.clientWidth = attributes.width;
    state.clientHeight = attributes.height;
    const auto destination = calculatePresentationRect(state.clientWidth, state.clientHeight,
                                                       surface.width, surface.height);
    if (!destination.isDrawable()) { return true; }

    const Visual* visual = DefaultVisual(state.display, state.screen);
    const int depth = DefaultDepth(state.display, state.screen);
    XImage* image = XCreateImage(state.display, const_cast<Visual*>(visual), depth, ZPixmap, 0,
                                 nullptr, static_cast<unsigned>(destination.width),
                                 static_cast<unsigned>(destination.height), 32, 0);
    if (image == nullptr || image->bits_per_pixel != 32 || image->bytes_per_line <= 0) {
        if (image != nullptr) { XDestroyImage(image); }
        return false;
    }
    const auto imageBytes = static_cast<std::size_t>(image->bytes_per_line) *
                            static_cast<std::size_t>(destination.height);
    image->data = static_cast<char*>(std::calloc(imageBytes, 1));
    if (image->data == nullptr) { XDestroyImage(image); return false; }
    for (int y = 0; y < destination.height; ++y) {
        const int sourceY = y * surface.height / destination.height;
        const auto* source = reinterpret_cast<const std::uint8_t*>(surface.pixels) +
                             static_cast<std::size_t>(sourceY) * surface.strideBytes;
        for (int x = 0; x < destination.width; ++x) {
            const int sourceX = x * surface.width / destination.width;
            const auto* pixel = source + static_cast<std::size_t>(sourceX) * 4U;
            const unsigned long packed = packChannel(pixel[0], visual->red_mask) |
                                         packChannel(pixel[1], visual->green_mask) |
                                         packChannel(pixel[2], visual->blue_mask);
            writePixel(image->data + static_cast<std::size_t>(y) * image->bytes_per_line +
                       static_cast<std::size_t>(x) * 4U, packed, 4, image->byte_order);
        }
    }
    static_cast<void>(XPutImage(state.display, state.window, state.graphicsContext, image, 0, 0,
                                destination.x, destination.y, destination.width, destination.height));
    XDestroyImage(image);
    XFlush(state.display);
    return true;
}

void LinuxPlatform::log(LogLevel level, std::string_view message) const {
    const char* prefix = level == LogLevel::error ? "[error] " :
                         level == LogLevel::warning ? "[warning] " : "[info] ";
    std::clog << prefix << message << '\n';
}

ImageDecoder& LinuxPlatform::imageDecoder() noexcept { return implementation_->decoder; }

std::filesystem::path LinuxPlatform::executableDirectory() const {
    std::vector<char> buffer(256);
    for (;;) {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1U);
        if (length < 0) { throw std::runtime_error("could not determine Linux executable path"); }
        if (static_cast<std::size_t>(length) < buffer.size() - 1U) {
            buffer[static_cast<std::size_t>(length)] = '\0';
            return std::filesystem::path(buffer.data()).parent_path();
        }
        buffer.resize(buffer.size() * 2U);
    }
}

InputState LinuxPlatform::consumeInputState() noexcept {
    InputState result = implementation_->input;
    implementation_->actionEdges.applyNext(result);
    return result;
}

DebugInputState LinuxPlatform::consumeDebugInput() noexcept {
    DebugInputState result = implementation_->debugInput;
    implementation_->debugInput = {};
    return result;
}

} // namespace underworld::platform::linux
