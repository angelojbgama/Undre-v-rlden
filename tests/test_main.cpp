#include "engine/core/color_rgba8.h"
#include "engine/core/fixed_timestep.h"
#include "engine/core/game_metrics.h"
#include "engine/platform/presentation.h"
#include "engine/render/framebuffer.h"

#ifdef _WIN32
#include "engine/platform/win32/win32_clock.h"
#endif

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

int failures = 0;
int checks = 0;

void expect(bool condition, std::string_view description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

bool approximately(double left, double right, double epsilon = 1.0e-9) {
    return std::abs(left - right) <= epsilon;
}

void testMetrics() {
    using underworld::core::GameMetrics;
    expect(GameMetrics::logicalWidth == 272, "logical width is centralized");
    expect(GameMetrics::logicalHeight == 224, "logical height is centralized");
    expect(GameMetrics::tileSize == 16, "tile size is centralized");
    expect(GameMetrics::tickRate == 60, "tick rate is centralized");
    expect(approximately(GameMetrics::fixedDt, 1.0 / 60.0), "fixedDt is derived");
}

void testFramebuffer() {
    using underworld::core::ColorRGBA8;
    using underworld::core::PixelFormat;
    underworld::render::Framebuffer framebuffer(272, 224);

    expect(framebuffer.width() == 272, "framebuffer width");
    expect(framebuffer.height() == 224, "framebuffer height");
    expect(framebuffer.stride() == 272U * 4U, "framebuffer stride");
    expect(framebuffer.byteSize() == 272U * 224U * 4U, "framebuffer memory size");

    constexpr ColorRGBA8 diagnostic{0x11, 0x22, 0x33, 0x44};
    framebuffer.clear(diagnostic);
    const auto view = framebuffer.view();
    expect(view.isValid(), "pixel buffer view is valid");
    expect(view.width == framebuffer.width() && view.height == framebuffer.height(),
           "view dimensions match owner");
    expect(view.strideBytes == framebuffer.stride(), "view stride matches owner");
    expect(view.format == PixelFormat::rgba8, "view format is RGBA8");
    expect(view.pixels == reinterpret_cast<const std::byte*>(framebuffer.pixels().data()),
           "view points at owned framebuffer memory");

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(view.pixels);
    expect(bytes[0] == 0x11, "RGBA memory byte 0 is red");
    expect(bytes[1] == 0x22, "RGBA memory byte 1 is green");
    expect(bytes[2] == 0x33, "RGBA memory byte 2 is blue");
    expect(bytes[3] == 0x44, "RGBA memory byte 3 is alpha");
    const auto last = framebuffer.pixels().back();
    expect(last.r == diagnostic.r && last.g == diagnostic.g &&
               last.b == diagnostic.b && last.a == diagnostic.a,
           "clear covers the entire framebuffer");

    bool rejectedInvalidSize = false;
    try {
        [[maybe_unused]] underworld::render::Framebuffer invalid(0, 224);
    } catch (const std::invalid_argument&) {
        rejectedInvalidSize = true;
    }
    expect(rejectedInvalidSize, "invalid framebuffer dimensions are rejected");
}

void testPresentationRect() {
    using underworld::platform::calculatePresentationRect;

    const auto exact = calculatePresentationRect(544, 448, 272, 224);
    expect(exact.x == 0 && exact.y == 0 && exact.width == 544 && exact.height == 448 &&
               exact.integerScale == 2,
           "exact 2x presentation");

    const auto letterboxed = calculatePresentationRect(600, 500, 272, 224);
    expect(letterboxed.x == 28 && letterboxed.y == 26 && letterboxed.width == 544 &&
               letterboxed.height == 448 && letterboxed.integerScale == 2,
           "2x presentation is centered with letterbox");

    const auto triple = calculatePresentationRect(816, 672, 272, 224);
    expect(triple.integerScale == 3 && triple.width == 816 && triple.height == 672,
           "exact 3x presentation");

    expect(!calculatePresentationRect(271, 224, 272, 224).isDrawable(),
           "client narrower than logical surface is not drawable");
    expect(!calculatePresentationRect(0, 0, 272, 224).isDrawable(),
           "zero client area is safe");
    expect(!calculatePresentationRect(600, 500, 0, 224).isDrawable(),
           "zero logical dimension is safe");
}

void testFixedStepAccumulator() {
    using underworld::core::FixedStepAccumulator;
    using underworld::core::FixedStepConfig;

    std::uint32_t ticks = 0;
    FixedStepAccumulator regular(FixedStepConfig{1.0 / 60.0, 0.25, 5});
    for (int frame = 0; frame < 120; ++frame) {
        regular.advance(1.0 / 120.0, [&ticks] { ++ticks; });
    }
    expect(ticks == 60, "60 fixed ticks execute across one synthetic second");
    expect(regular.accumulatorSeconds() >= 0.0 &&
               regular.accumulatorSeconds() < (1.0 / 60.0),
           "regular accumulator keeps only a fractional tick");

    std::uint32_t catchUpTicks = 0;
    FixedStepAccumulator protectedLoop(FixedStepConfig{1.0 / 60.0, 0.25, 5});
    const auto result = protectedLoop.advance(1.0, [&catchUpTicks] { ++catchUpTicks; });
    expect(result.frameDeltaClamped, "large frame delta is clamped");
    expect(result.catchUpLimited, "catch-up is limited");
    expect(result.ticksExecuted == 5 && catchUpTicks == 5, "catch-up executes at most five ticks");
    expect(result.discardedSeconds > 0.9, "discarded timing is reported");
    expect(result.interpolationAlpha >= 0.0 && result.interpolationAlpha < 1.0,
           "interpolation alpha remains normalized");

    FixedStepAccumulator negative(FixedStepConfig{1.0 / 60.0, 0.25, 5});
    const auto negativeResult = negative.advance(-1.0, [] {});
    expect(negativeResult.ticksExecuted == 0 && negativeResult.frameDeltaClamped,
           "negative delta cannot run simulation backwards");
}

void testWin32Clock() {
#ifdef _WIN32
    underworld::platform::win32::Win32Clock clock;
    expect(clock.initialize(), "QueryPerformanceFrequency initializes");
    expect(clock.frequency() > 0, "QPC frequency is positive");
    const double first = clock.nowSeconds();
    const double second = clock.nowSeconds();
    expect(first > 0.0 && second >= first, "QPC time is positive and monotonic");
#endif
}

} // namespace

int main() {
    try {
        testMetrics();
        testFramebuffer();
        testPresentationRect();
        testFixedStepAccumulator();
        testWin32Clock();
    } catch (const std::exception& exception) {
        ++failures;
        std::cerr << "UNEXPECTED EXCEPTION: " << exception.what() << '\n';
    } catch (...) {
        ++failures;
        std::cerr << "UNEXPECTED NON-STANDARD EXCEPTION\n";
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks << " checks\n";
        return 0;
    }
    std::cerr << "FAILED: " << failures << " of " << checks << " checks\n";
    return 1;
}
