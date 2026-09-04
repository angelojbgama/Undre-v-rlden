#include "engine/core/color_rgba8.h"
#include "engine/core/coordinates.h"
#include "engine/core/fixed_timestep.h"
#include "engine/core/game_metrics.h"
#include "engine/core/image_data.h"
#include "engine/assets/asset_manager.h"
#include "engine/platform/image_decoder.h"
#include "engine/platform/action_edge_buffer.h"
#include "engine/platform/input_state.h"
#include "engine/platform/presentation.h"
#include "engine/render/animation.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/camera_2d.h"
#include "engine/render/framebuffer.h"
#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"
#include "engine/render/sprite.h"
#include "engine/simulation/player_command.h"
#include "engine/simulation/entity_handle.h"
#include "engine/simulation/events.h"
#include "engine/simulation/persistent_id.h"
#include "engine/serialization/byte_io.h"
#include "engine/world/collision.h"
#include "engine/world/collision_grid.h"
#include "engine/world/runtime_map.h"
#include "engine/world/tile.h"
#include "engine/world/tile_layer.h"
#include "editor/editor_commands.h"
#include "editor/editor_document.h"
#include "game/command_builder.h"
#include "game/game_content.h"
#include "game/game_view_model.h"
#include "game/actor_render_order.h"
#include "game/combat_debug.h"
#include "game/effect_system.h"
#include "game/enemy_visual.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/combat_system.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/items.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/world_pickups.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/projectile_system.h"
#include "game/gameplay/player.h"
#include "game/player_visual.h"
#include "game/training_puppet.h"
#include "game/maps/dmap.h"
#include "game/maps/demo_maps.h"
#include "game/maps/map_catalog.h"
#include "game/maps/runtime_world.h"
#include "game/save/save_data.h"

#ifdef _WIN32
#include "engine/platform/win32/win32_clock.h"
#include "engine/platform/win32/win32_image_decoder.h"
#endif

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

underworld::core::ImageData makeImageData(
    int width, int height, const std::vector<underworld::core::ColorRGBA8>& colors,
    std::size_t paddingBytes = 0) {
    const std::size_t stride = static_cast<std::size_t>(width) * 4U + paddingBytes;
    underworld::core::ImageData data{width, height, stride,
                                     std::vector<std::uint8_t>(stride *
                                                               static_cast<std::size_t>(height))};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto color = colors[static_cast<std::size_t>(y) *
                                      static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(x)];
            const std::size_t offset = static_cast<std::size_t>(y) * stride +
                                       static_cast<std::size_t>(x) * 4U;
            data.pixels[offset] = color.r;
            data.pixels[offset + 1U] = color.g;
            data.pixels[offset + 2U] = color.b;
            data.pixels[offset + 3U] = color.a;
        }
    }
    return data;
}

underworld::core::ColorRGBA8 framebufferPixel(const underworld::render::Framebuffer& framebuffer,
                                               int x, int y) {
    return framebuffer.pixels()[static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(framebuffer.width()) +
                                static_cast<std::size_t>(x)];
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

void testRendererPrimitives() {
    using underworld::core::ColorRGBA8;
    underworld::render::Framebuffer framebuffer(5, 4);
    underworld::render::Renderer2D renderer(framebuffer);
    constexpr ColorRGBA8 black{0, 0, 0, 255};
    constexpr ColorRGBA8 red{255, 0, 0, 255};
    constexpr ColorRGBA8 green{0, 255, 0, 255};
    framebuffer.clear(black);

    renderer.setPixel(2, 1, red);
    renderer.setPixel(0, 0, green);
    renderer.setPixel(4, 3, green);
    renderer.setPixel(-1, 0, red);
    renderer.setPixel(5, 3, red);
    expect(framebufferPixel(framebuffer, 2, 1) == red, "setPixel writes an interior pixel");
    expect(framebufferPixel(framebuffer, 0, 0) == green, "setPixel writes the top-left corner");
    expect(framebufferPixel(framebuffer, 4, 3) == green, "setPixel writes the bottom-right corner");
    expect(framebufferPixel(framebuffer, 1, 0) == black, "out-of-bounds setPixel is ignored");

    framebuffer.clear(black);
    renderer.fillRect({1, 1, 3, 2}, red);
    expect(framebufferPixel(framebuffer, 1, 1) == red &&
               framebufferPixel(framebuffer, 3, 2) == red,
           "fillRect fills its inclusive-start exclusive-end area");
    expect(framebufferPixel(framebuffer, 0, 1) == black &&
               framebufferPixel(framebuffer, 4, 2) == black,
           "fillRect does not escape its normal area");

    framebuffer.clear(black);
    renderer.fillRect({-2, 1, 4, 2}, green);
    expect(framebufferPixel(framebuffer, 0, 1) == green &&
               framebufferPixel(framebuffer, 1, 2) == green &&
               framebufferPixel(framebuffer, 2, 1) == black,
           "fillRect clips its left edge");
    renderer.fillRect({4, 0, 4, 1}, red);
    expect(framebufferPixel(framebuffer, 4, 0) == red,
           "fillRect clips its right edge");
    renderer.fillRect({2, -3, 1, 4}, red);
    expect(framebufferPixel(framebuffer, 2, 0) == red,
           "fillRect clips its top edge");
    renderer.fillRect({3, 3, 1, 8}, red);
    expect(framebufferPixel(framebuffer, 3, 3) == red,
           "fillRect clips its bottom edge");
    renderer.fillRect({20, 20, 5, 5}, red);
    renderer.fillRect({0, 0, 0, 3}, red);
    renderer.fillRect({0, 0, 3, -1}, red);
    expect(framebufferPixel(framebuffer, 4, 3) == black,
           "fully outside and non-positive rectangles are no-ops");
}

void testImagesAndBlits() {
    using underworld::core::ColorRGBA8;
    constexpr ColorRGBA8 red{255, 0, 0, 255};
    constexpr ColorRGBA8 green{0, 255, 0, 255};
    constexpr ColorRGBA8 blue{0, 0, 255, 255};
    constexpr ColorRGBA8 white{255, 255, 255, 255};
    constexpr ColorRGBA8 black{0, 0, 0, 255};
    underworld::render::Image image(makeImageData(3, 2, {red, green, blue, white, red, green}, 5));
    expect(image.width() == 3 && image.height() == 2, "Image owns decoded dimensions");
    expect(image.stride() == 17, "Image preserves decoder stride");
    expect(image.pixel(2, 1) == green, "Image reads RGBA through padded stride");

    underworld::render::Framebuffer framebuffer(4, 3);
    underworld::render::Renderer2D renderer(framebuffer);
    framebuffer.clear(black);
    renderer.drawImageRegion(image, {1, 0, 2, 2}, 1, 1);
    expect(framebufferPixel(framebuffer, 1, 1) == green &&
               framebufferPixel(framebuffer, 2, 1) == blue &&
               framebufferPixel(framebuffer, 1, 2) == red &&
               framebufferPixel(framebuffer, 2, 2) == green,
           "drawImageRegion copies the selected source rectangle");

    framebuffer.clear(black);
    renderer.drawImageRegion(image, {0, 0, 3, 2}, -1, -1);
    expect(framebufferPixel(framebuffer, 0, 0) == red &&
               framebufferPixel(framebuffer, 1, 0) == green,
           "drawImageRegion clips destination top and left while advancing source");
    framebuffer.clear(black);
    renderer.drawImage(image, 3, 2);
    expect(framebufferPixel(framebuffer, 3, 2) == red,
           "drawImage clips destination right and bottom");

    bool invalidSourceRejected = false;
    try {
        renderer.drawImageRegion(image, {2, 0, 2, 1}, 0, 0);
    } catch (const std::out_of_range&) {
        invalidSourceRejected = true;
    }
    expect(invalidSourceRejected, "source rectangle outside the image is rejected");

    framebuffer.clear(black);
    renderer.drawImageRegionFlipX(image, {0, 0, 3, 1}, 0, 0);
    expect(framebufferPixel(framebuffer, 0, 0) == blue &&
               framebufferPixel(framebuffer, 1, 0) == green &&
               framebufferPixel(framebuffer, 2, 0) == red,
           "horizontal flip reverses source pixels without changing channels");

    bool invalidImageRejected = false;
    try {
        [[maybe_unused]] underworld::render::Image invalid({2, 2, 7, {}});
    } catch (const std::invalid_argument&) {
        invalidImageRejected = true;
    }
    expect(invalidImageRejected, "invalid stride and storage are rejected by Image");
    underworld::core::ImageData overflowCandidate{
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 0, {}};
    expect(!overflowCandidate.isValid(), "invalid external dimensions and stride are safe");

    underworld::render::Image nonsquare(makeImageData(
        2, 3, {red, green, blue, white, black, ColorRGBA8{255, 255, 0, 255}}));
    underworld::render::Framebuffer rotated(3, 2);
    underworld::render::Renderer2D rotationRenderer(rotated);
    rotationRenderer.drawImageRegionQuarterTurn(
        nonsquare, {0, 0, 2, 3}, 0, 0, underworld::render::QuarterTurn::r90);
    expect(framebufferPixel(rotated, 0, 0) == black &&
               framebufferPixel(rotated, 1, 0) == blue &&
               framebufferPixel(rotated, 2, 0) == red &&
               framebufferPixel(rotated, 0, 1) == ColorRGBA8{255, 255, 0, 255} &&
               framebufferPixel(rotated, 2, 1) == green,
           "90-degree pixel rotation handles a non-square source without interpolation");
    rotationRenderer.drawImageRegionQuarterTurn(
        nonsquare, {0, 0, 2, 3}, 0, 0, underworld::render::QuarterTurn::r270);
    expect(framebufferPixel(rotated, 0, 0) == green &&
               framebufferPixel(rotated, 2, 1) == black,
           "270-degree pixel rotation preserves orientation and channels");
}

void testAlphaBlending() {
    using underworld::core::ColorRGBA8;
    underworld::render::Framebuffer framebuffer(1, 1);
    underworld::render::Renderer2D renderer(framebuffer);
    constexpr ColorRGBA8 blue{0, 0, 255, 255};
    framebuffer.clear(blue);
    renderer.setPixel(0, 0, {255, 0, 0, 0});
    expect(framebufferPixel(framebuffer, 0, 0) == blue, "alpha 0 preserves destination");
    renderer.setPixel(0, 0, {255, 0, 0, 255});
    expect(framebufferPixel(framebuffer, 0, 0) == ColorRGBA8{255, 0, 0, 255},
           "alpha 255 copies RGBA exactly");
    framebuffer.clear(blue);
    renderer.setPixel(0, 0, {255, 0, 0, 128});
    expect(framebufferPixel(framebuffer, 0, 0) == ColorRGBA8{128, 0, 127, 255},
           "alpha 128 blends straight-alpha red over opaque blue with rounding");
    framebuffer.clear({0, 0, 0, 0});
    renderer.setPixel(0, 0, {10, 20, 30, 128});
    expect(framebufferPixel(framebuffer, 0, 0) == ColorRGBA8{10, 20, 30, 128},
           "straight-alpha composition preserves color over transparent destination");
}

std::shared_ptr<const underworld::render::AnimationClip> makeTestClip(
    std::string id, bool loop) {
    using underworld::core::ColorRGBA8;
    auto image = std::make_shared<const underworld::render::Image>(
        makeImageData(3, 1, {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}}));
    auto sheet = std::make_shared<const underworld::render::SpriteSheet>(image);
    std::vector<underworld::render::AnimationFrame> frames{
        {{{0, 0, 1, 1}, {0, 0}, {0, 0}, false}, 2, {}},
        {{{1, 0, 1, 1}, {0, 0}, {0, 0}, false}, 3, {"future.marker"}},
        {{{2, 0, 1, 1}, {0, 0}, {0, 0}, false}, 1, {}}};
    return std::make_shared<const underworld::render::AnimationClip>(
        std::move(id), std::move(sheet), std::move(frames), loop);
}

void testAnimationAndSpriteAnchor() {
    underworld::render::Animator animator;
    const auto looping = makeTestClip("test.loop", true);
    animator.play(looping);
    expect(animator.frameIndex() == 0 && animator.isPlaying(), "Animator starts at frame zero");
    animator.updateTicks(1);
    expect(animator.frameIndex() == 0 && animator.elapsedFrameTicks() == 1,
           "Animator retains partial integer ticks");
    animator.updateTicks(1);
    expect(animator.frameIndex() == 1 && animator.elapsedFrameTicks() == 0,
           "Animator advances at exact frame duration");
    animator.updateTicks(3);
    expect(animator.frameIndex() == 2, "Animator respects different frame durations");
    animator.updateTicks(1);
    expect(animator.frameIndex() == 0 && animator.isPlaying(), "looping clip wraps to frame zero");
    animator.updateTicks(12);
    expect(animator.frameIndex() == 0, "Animator handles multiple complete loops in one update");

    const auto once = makeTestClip("test.once", false);
    animator.play(once);
    animator.updateTicks(6);
    expect(animator.frameIndex() == 2 && !animator.isPlaying(),
           "non-looping clip holds its final frame and stops");
    animator.play(looping);
    animator.updateTicks(2);
    animator.play(looping, false);
    expect(animator.frameIndex() == 1 && animator.isPlaying(),
           "same clip can resume without restarting");
    animator.setPlaying(false);
    animator.updateTicks(20);
    expect(animator.frameIndex() == 1, "paused Animator does not consume ticks");

    using underworld::core::ColorRGBA8;
    auto image = std::make_shared<const underworld::render::Image>(
        makeImageData(4, 1, {{255, 0, 0, 255}, {0, 255, 0, 255},
                             {0, 0, 255, 255}, {255, 255, 255, 255}}));
    underworld::render::SpriteSheet sheet(image);
    underworld::render::Framebuffer framebuffer(8, 3);
    framebuffer.clear({0, 0, 0, 255});
    underworld::render::Renderer2D renderer(framebuffer);
    const underworld::render::SpriteFrame frame{{0, 0, 4, 1}, {1, 0}, {0, 0}, false};
    underworld::render::drawSprite(renderer, sheet, frame, {3, 1});
    expect(framebufferPixel(framebuffer, 2, 1) == ColorRGBA8{255, 0, 0, 255},
           "sprite anchor positions the unflipped source");
    framebuffer.clear({0, 0, 0, 255});
    underworld::render::drawSprite(renderer, sheet, frame, {3, 1}, true);
    expect(framebufferPixel(framebuffer, 0, 1) == ColorRGBA8{255, 255, 255, 255} &&
               framebufferPixel(framebuffer, 1, 1) == ColorRGBA8{0, 0, 255, 255} &&
               framebufferPixel(framebuffer, 3, 1) == ColorRGBA8{255, 0, 0, 255},
           "flipped sprite transforms its anchor without duplicating the image");

    auto markerImage = std::make_shared<const underworld::render::Image>(
        makeImageData(4, 1, {ColorRGBA8{}, ColorRGBA8{}, ColorRGBA8{}, ColorRGBA8{}}));
    auto markerSheet = std::make_shared<const underworld::render::SpriteSheet>(markerImage);
    std::vector<underworld::render::AnimationFrame> markerFrames{
        {{{0, 0, 1, 1}, {}, {}, false}, 2, {}},
        {{{1, 0, 1, 1}, {}, {}, false}, 3, {"attack_on"}},
        {{{2, 0, 1, 1}, {}, {}, false}, 1, {"middle"}},
        {{{3, 0, 1, 1}, {}, {}, false}, 2, {"attack_off"}}};
    auto markerClip = std::make_shared<const underworld::render::AnimationClip>(
        "markers", markerSheet, std::move(markerFrames), false);
    underworld::render::Animator markerAnimator;
    markerAnimator.play(markerClip);
    std::vector<underworld::render::AnimationMarkerEvent> markerEvents;
    markerAnimator.updateTicks(1, markerEvents);
    expect(markerEvents.empty(), "animation marker is not repeated during a frame");
    markerAnimator.updateTicks(1, markerEvents);
    expect(markerEvents.size() == 1 && markerEvents[0].marker == "attack_on",
           "animation marker emits exactly once when entering its frame");
    markerEvents.clear();
    markerAnimator.updateTicks(4, markerEvents);
    expect(markerEvents.size() == 2 && markerEvents[0].marker == "middle" &&
               markerEvents[1].marker == "attack_off",
           "multi-frame tick advance preserves every intermediate marker in order");
}

void testBitmapFontMapping() {
    using underworld::core::ColorRGBA8;
    const std::vector<ColorRGBA8> transparent(182U * 27U, ColorRGBA8{0, 0, 0, 0});
    auto image = std::make_shared<const underworld::render::Image>(
        makeImageData(182, 27, transparent));
    underworld::render::BitmapFont font(image);
    expect(font.glyphSource('A') == underworld::core::RectI{0, 0, 7, 9},
           "font explicitly maps uppercase A");
    expect(font.glyphSource('Z') == underworld::core::RectI{175, 0, 7, 9},
           "font explicitly maps uppercase Z");
    expect(font.glyphSource('a') == underworld::core::RectI{0, 9, 7, 9},
           "font explicitly maps lowercase a");
    expect(font.glyphSource('9') == underworld::core::RectI{63, 18, 7, 9},
           "font explicitly maps digits");
    expect(font.glyphSource('_') == underworld::core::RectI{98, 18, 7, 9},
           "font explicitly maps known punctuation");
    expect(font.glyphSource('@') == font.glyphSource('?'),
           "unknown glyph falls back to question mark");
    expect(font.advance() == 7 && font.lineHeight() == 9,
           "font uses fixed 7x9 metrics");
}

class CountingDecoder final : public underworld::platform::ImageDecoder {
public:
    underworld::core::ImageData decode(const std::filesystem::path&) override {
        ++calls;
        return makeImageData(1, 1, {{1, 2, 3, 255}});
    }
    int calls{};
};

void testAssetCache() {
    CountingDecoder decoder;
    underworld::assets::AssetManager assets;
    const auto first = assets.loadImage("image.one", "folder/../image.png", decoder);
    const auto second = assets.loadImage("image.one", "image.png", decoder);
    expect(first == second && decoder.calls == 1,
           "same asset id and normalized path decode only once");
    expect(assets.getImage("image.one") == first, "loaded image is retrievable by stable asset id");
    bool conflictRejected = false;
    try {
        [[maybe_unused]] const auto conflict =
            assets.loadImage("image.one", "different.png", decoder);
    } catch (const std::logic_error&) {
        conflictRejected = true;
    }
    expect(conflictRejected && decoder.calls == 1,
           "duplicate asset id with conflicting path is rejected before decoding");
    bool missingRejected = false;
    try {
        [[maybe_unused]] const auto missing = assets.getImage("missing");
    } catch (const std::out_of_range&) {
        missingRejected = true;
    }
    expect(missingRejected, "unknown asset id produces a diagnostic exception");
}

void testWicDecoder() {
#ifdef _WIN32
    constexpr std::array<std::uint8_t, 74> png{
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0xF4, 0x22, 0x7F, 0x8A, 0x00, 0x00, 0x00,
        0x11, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x10, 0x54, 0x32, 0x76,
        0x09, 0x4D, 0x2B, 0xEF, 0x00, 0x00, 0x08, 0x01, 0x02, 0x65, 0xB5, 0x0B,
        0xF3, 0x4F, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42,
        0x60, 0x82};
    const auto path = std::filesystem::temp_directory_path() /
                      "underworld_phase2_wic_synthetic_test.png";
    std::error_code cleanupError;
    std::filesystem::remove(path, cleanupError);
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(png.data()),
                     static_cast<std::streamsize>(png.size()));
        if (!output) {
            throw std::runtime_error("could not create temporary WIC test PNG");
        }
    }
    underworld::platform::win32::Win32ImageDecoder decoder;
    const auto decoded = decoder.decode(path);
    expect(decoded.width == 2 && decoded.height == 1 && decoded.strideBytes == 8,
           "WIC decodes synthetic PNG dimensions and RGBA stride");
    expect(decoded.pixels.size() == 8 && decoded.pixels[0] == 0x11 &&
               decoded.pixels[1] == 0x22 && decoded.pixels[2] == 0x33 &&
               decoded.pixels[3] == 0x44 && decoded.pixels[4] == 0x55 &&
               decoded.pixels[5] == 0x66 && decoded.pixels[6] == 0x77 &&
               decoded.pixels[7] == 0x88,
           "WIC explicitly outputs R,G,B,A bytes without channel swapping");
    std::filesystem::remove(path, cleanupError);
    expect(!std::filesystem::exists(path), "temporary synthetic PNG is removed after WIC test");

    bool missingFileRejected = false;
    try {
        [[maybe_unused]] const auto missing = decoder.decode(path);
    } catch (const std::runtime_error& error) {
        missingFileRejected = std::string_view(error.what()).find("not found") !=
                              std::string_view::npos;
    }
    expect(missingFileRejected, "WIC missing-file error includes useful context");
#endif
}

void testWorldCoordinates() {
    using underworld::core::floorDiv;
    using underworld::core::floorMod;
    expect(floorDiv(0, 16) == 0 && floorDiv(15, 16) == 0,
           "world floor division keeps pixels 0 through 15 in tile zero");
    expect(floorDiv(16, 16) == 1 && floorDiv(17, 16) == 1,
           "world floor division advances at the positive tile boundary");
    expect(floorDiv(-1, 16) == -1 && floorDiv(-15, 16) == -1,
           "negative world pixels floor into tile minus one");
    expect(floorDiv(-16, 16) == -1 && floorDiv(-17, 16) == -2,
           "negative exact and crossed tile boundaries use mathematical floor");
    expect(floorMod(0, 16) == 0 && floorMod(15, 16) == 15 &&
               floorMod(16, 16) == 0 && floorMod(17, 16) == 1,
           "positive floor modulo identifies the pixel inside a tile");
    expect(floorMod(-1, 16) == 15 && floorMod(-15, 16) == 1 &&
               floorMod(-16, 16) == 0 && floorMod(-17, 16) == 15,
           "negative floor modulo remains in the range zero through tileSize minus one");
    expect(underworld::core::worldToTile({0, 0}, 16) == underworld::core::TileCoord{0, 0} &&
               underworld::core::worldToTile({17, -17}, 16) ==
                   underworld::core::TileCoord{1, -2},
           "worldToTile uses explicit floor conversion on both axes");
    expect(underworld::core::tileToWorld({3, -2}, 16) ==
               underworld::core::WorldPointI{48, -32},
           "tileToWorld returns the tile top-left in world pixels");
    bool badDivisorRejected = false;
    try {
        [[maybe_unused]] const auto result = floorDiv(1, 0);
    } catch (const std::invalid_argument&) {
        badDivisorRejected = true;
    }
    expect(badDivisorRejected, "coordinate conversion rejects a non-positive tile size");
    bool coordinateOverflowRejected = false;
    try {
        [[maybe_unused]] const auto result = underworld::core::tileToWorld(
            {std::numeric_limits<int>::max(), 0}, 16);
    } catch (const std::overflow_error&) {
        coordinateOverflowRejected = true;
    }
    expect(coordinateOverflowRejected, "tileToWorld rejects coordinate overflow");
}

void testTilesAndLayers() {
    using underworld::world::TileAtlasLayout;
    using underworld::world::TileFlags;
    using underworld::world::TileRef;
    TileAtlasLayout atlas(304, 192, 16);
    expect(atlas.columns() == 19 && atlas.rows() == 12 && atlas.tileCount() == 228,
           "audited tile atlas layout is 19 by 12");
    expect(atlas.sourceRect(0) == underworld::core::RectI{0, 0, 16, 16},
           "tile index zero remains a usable atlas cell");
    expect(atlas.sourceRect(20) == underworld::core::RectI{16, 16, 16, 16},
           "tile index converts to source row and column centrally");
    expect(atlas.sourceRect(227) == underworld::core::RectI{288, 176, 16, 16},
           "last valid atlas tile resolves to the final cell");
    bool badTileIndexRejected = false;
    try {
        [[maybe_unused]] const auto source = atlas.sourceRect(228);
    } catch (const std::out_of_range&) {
        badTileIndexRejected = true;
    }
    expect(badTileIndexRejected, "tile source index outside atlas is rejected");
    bool misalignedAtlasRejected = false;
    try {
        [[maybe_unused]] TileAtlasLayout invalid(303, 192, 16);
    } catch (const std::invalid_argument&) {
        misalignedAtlasRejected = true;
    }
    expect(misalignedAtlasRejected, "atlas dimensions must align to tile size");

    underworld::world::TileLayer layer("ground", 3, 2);
    expect(layer.width() == 3 && layer.height() == 2 && layer.visible(),
           "valid tile layer preserves dimensions and visibility");
    expect(!layer.cell(0, 0).has_value() && !layer.cell(2, 1).has_value(),
           "new tile layer cells are explicitly empty");
    const TileRef tileZero{{1, 0}, TileFlags::flipX};
    layer.set(2, 1, tileZero);
    expect(layer.cell(2, 1) == tileZero &&
               underworld::world::hasFlag(layer.cell(2, 1)->flags, TileFlags::flipX),
           "tile layer stores TileRef including tile zero and flip flag");
    layer.set(2, 1, std::nullopt);
    expect(!layer.cell(2, 1), "tile layer can clear a cell back to empty");
    layer.setVisible(false);
    expect(!layer.visible(), "tile layer visibility is independent data");
    bool layerBoundsRejected = false;
    try {
        [[maybe_unused]] const auto& outside = layer.cell(3, 0);
    } catch (const std::out_of_range&) {
        layerBoundsRejected = true;
    }
    expect(layerBoundsRejected, "tile layer public access checks bounds");
    bool badLayerDimensionsRejected = false;
    try {
        [[maybe_unused]] underworld::world::TileLayer invalid("bad", 0, 2);
    } catch (const std::invalid_argument&) {
        badLayerDimensionsRejected = true;
    }
    expect(badLayerDimensionsRejected, "tile layer rejects invalid dimensions");
    bool hugeLayerRejected = false;
    try {
        [[maybe_unused]] underworld::world::TileLayer huge(
            "huge", std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
    } catch (const std::length_error&) {
        hugeLayerRejected = true;
    }
    expect(hugeLayerRejected, "tile layer rejects impossible storage before allocation");
}

void testRuntimeMap() {
    underworld::world::RuntimeMap map(64, 48, 16);
    expect(map.widthTiles() == 64 && map.heightTiles() == 48 && map.tileSize() == 16,
           "RuntimeMap stores tile dimensions and explicit tile size");
    expect(map.worldWidthPixels() == 1024 && map.worldHeightPixels() == 768,
           "RuntimeMap safely derives world pixel dimensions");
    const auto ground = map.addLayer("ground");
    const auto foreground = map.addLayer("foreground", false);
    expect(map.layerCount() == 2 && map.layer(ground).name() == "ground" &&
               !map.layer(foreground).visible(),
           "RuntimeMap owns separately named tile layers");

    const underworld::world::TileRef wall{{1, 49}, underworld::world::TileFlags::none};
    map.layer(ground).set(4, 5, wall);
    expect(!map.collision().isSolid(4, 5),
           "changing a visual tile does not change collision");
    map.collision().setSolid(4, 5, true);
    expect(map.layer(ground).cell(4, 5) == wall,
           "changing collision does not change the visual tile");

    bool mapLayerBoundsRejected = false;
    try {
        [[maybe_unused]] const auto& outside = map.layer(2);
    } catch (const std::out_of_range&) {
        mapLayerBoundsRejected = true;
    }
    expect(mapLayerBoundsRejected, "RuntimeMap checks layer indices");
    bool invalidMapRejected = false;
    try {
        [[maybe_unused]] underworld::world::RuntimeMap invalid(0, 4, 16);
    } catch (const std::invalid_argument&) {
        invalidMapRejected = true;
    }
    expect(invalidMapRejected, "RuntimeMap rejects invalid dimensions");
    bool mapExtentOverflowRejected = false;
    try {
        [[maybe_unused]] underworld::world::RuntimeMap huge(
            std::numeric_limits<int>::max(), 1, 16);
    } catch (const std::length_error&) {
        mapExtentOverflowRejected = true;
    }
    expect(mapExtentOverflowRejected, "RuntimeMap rejects world pixel extent overflow");
}

void testCameraAndCulling() {
    using underworld::render::Camera2D;
    using underworld::render::VisibleTileRange;
    Camera2D camera(272, 224);
    expect(camera.position() == underworld::core::WorldPointI{0, 0},
           "camera starts at world origin");
    expect(camera.worldToLogical({20, 30}) == underworld::core::LogicalPointI{20, 30},
           "camera at zero preserves world coordinates");
    camera.setPosition({5, 7});
    expect(camera.worldToLogical({20, 30}) == underworld::core::LogicalPointI{15, 23} &&
               camera.logicalToWorld({15, 23}) == underworld::core::WorldPointI{20, 30},
           "world and logical camera transforms are inverse");

    camera.setPosition({-20, -30});
    camera.clampToWorld(1024, 768);
    expect(camera.position() == underworld::core::WorldPointI{0, 0},
           "camera clamps left and top boundaries");
    camera.setPosition({5000, 5000});
    camera.clampToWorld(1024, 768);
    expect(camera.position() == underworld::core::WorldPointI{752, 544},
           "camera clamps right and bottom boundaries");
    camera.setPosition({100, 100});
    camera.clampToWorld(100, 100);
    expect(camera.position() == underworld::core::WorldPointI{0, 0},
           "map smaller than viewport deterministically fixes camera at origin");

    camera.setPosition({0, 0});
    expect(camera.visibleTiles(1000, 1000, 16) == VisibleTileRange{0, 0, 16, 13},
           "aligned camera sees exactly 17 by 14 tiles");
    camera.setPosition({1, 1});
    expect(camera.visibleTiles(1000, 1000, 16) == VisibleTileRange{0, 0, 17, 14},
           "one-pixel camera offset includes partial tiles on every edge");
    camera.setPosition({15, 15});
    expect(camera.visibleTiles(1000, 1000, 16) == VisibleTileRange{0, 0, 17, 14},
           "camera at pixel 15 remains smoothly off-grid");
    camera.setPosition({16, 16});
    expect(camera.visibleTiles(1000, 1000, 16) == VisibleTileRange{1, 1, 17, 14},
           "camera at tile boundary advances first visible tile without a gap");
    camera.setPosition({17, 17});
    expect(camera.visibleTiles(1000, 1000, 16) == VisibleTileRange{1, 1, 18, 15},
           "camera after tile boundary includes new partial far-edge tiles");

    camera.setPosition({1, 1});
    const auto largeMapRange = camera.visibleTiles(1024, 1024, 16);
    expect(largeMapRange.tileCount() == 270,
           "large-map culling consults only 18 by 15 visible cells");
    expect(largeMapRange.tileCount() < 1024U * 1024U,
           "visible range cost is independent of total map cell count");
    camera.setPosition({1024 * 16 - 272, 1024 * 16 - 224});
    const auto farEdge = camera.visibleTiles(1024, 1024, 16);
    expect(farEdge.lastX == 1023 && farEdge.lastY == 1023,
           "visible range remains inside map at far boundary");
}

void testCollisionGridAndAabb() {
    using underworld::world::AabbI;
    underworld::world::CollisionGrid grid(10, 10);
    expect(!grid.isSolid(0, 0) && !grid.isSolid(9, 9),
           "new collision grid cells are empty");
    expect(grid.isSolid(-1, 0) && grid.isSolid(10, 0) && grid.isSolid(0, -1),
           "collision policy treats outside the map as solid");
    grid.setSolid(2, 1, true);
    expect(grid.isSolid(2, 1), "collision grid stores a solid cell");

    const auto free = underworld::world::querySolidTiles(grid, {16, 16, 10, 10}, 16);
    expect(!free.collides && free.cellsTested == 1,
           "AABB fully in free space checks only its covered cell");
    const auto solid = underworld::world::querySolidTiles(grid, {32, 16, 10, 10}, 16);
    expect(solid.collides, "AABB directly over a solid tile collides");
    const auto partial = underworld::world::querySolidTiles(grid, {26, 16, 10, 10}, 16);
    expect(partial.collides, "AABB partially overlapping a solid tile collides");
    const auto touching = underworld::world::querySolidTiles(grid, {22, 16, 10, 10}, 16);
    expect(!touching.collides, "half-open AABB touching a wall without overlap is free");
    const auto outside = underworld::world::querySolidTiles(grid, {-1, 16, 10, 10}, 16);
    expect(outside.collides && outside.cellsTested == 1,
           "AABB outside world is rejected without scanning the grid");
    const auto fourCells = underworld::world::querySolidTiles(grid, {15, 31, 2, 2}, 16);
    expect(fourCells.cellsTested <= 4,
           "AABB broad phase visits only covered neighboring cells");
    bool collisionBoundsRejected = false;
    try {
        grid.setSolid(10, 0, true);
    } catch (const std::out_of_range&) {
        collisionBoundsRejected = true;
    }
    expect(collisionBoundsRejected, "collision mutation checks map bounds");
}

void testCollisionMovement() {
    using underworld::world::AabbI;
    using underworld::world::CollisionGrid;
    CollisionGrid verticalWall(8, 8);
    for (int y = 0; y < 8; ++y) {
        verticalWall.setSolid(3, y, true);
    }
    AabbI bodyX{32, 32, 10, 10};
    const auto xResult = underworld::world::moveAgainstSolidTiles(
        verticalWall, bodyX, 20, 0, 16);
    expect(bodyX.x == 38 && xResult.movedX == 6 && xResult.blockedX,
           "axis resolution stops X movement exactly against a wall");

    CollisionGrid horizontalWall(8, 8);
    for (int x = 0; x < 8; ++x) {
        horizontalWall.setSolid(x, 3, true);
    }
    AabbI bodyY{32, 32, 10, 10};
    const auto yResult = underworld::world::moveAgainstSolidTiles(
        horizontalWall, bodyY, 0, 20, 16);
    expect(bodyY.y == 38 && yResult.movedY == 6 && yResult.blockedY,
           "axis resolution stops Y movement exactly against a wall");

    AabbI sliding{38, 20, 10, 10};
    const auto slideResult = underworld::world::moveAgainstSolidTiles(
        verticalWall, sliding, 5, 8, 16);
    expect(sliding.x == 38 && sliding.y == 28 && slideResult.blockedX &&
               !slideResult.blockedY,
           "separate-axis resolution slides along a wall");

    CollisionGrid corridorGrid(8, 5);
    for (int x = 0; x < 8; ++x) {
        corridorGrid.setSolid(x, 1, true);
        corridorGrid.setSolid(x, 3, true);
    }
    AabbI corridorBody{16, 35, 10, 10};
    const auto corridorResult = underworld::world::moveAgainstSolidTiles(
        corridorGrid, corridorBody, 64, 0, 16);
    expect(corridorBody.x == 80 && corridorBody.y == 35 &&
               !corridorResult.blockedX && !corridorResult.blockedY,
           "body smaller than a tile passes through a one-tile corridor");

    CollisionGrid cornerGrid(8, 8);
    for (int y = 0; y < 8; ++y) {
        cornerGrid.setSolid(3, y, true);
    }
    for (int x = 0; x < 8; ++x) {
        cornerGrid.setSolid(x, 3, true);
    }
    AabbI corner{38, 38, 10, 10};
    const auto cornerResult = underworld::world::moveAgainstSolidTiles(
        cornerGrid, corner, 3, 3, 16);
    expect(corner == AabbI{38, 38, 10, 10} && cornerResult.blockedX &&
               cornerResult.blockedY,
           "axis resolution cannot tunnel diagonally through a solid corner");

    CollisionGrid boundary(4, 4);
    AabbI edge{1, 1, 10, 10};
    const auto edgeResult = underworld::world::moveAgainstSolidTiles(
        boundary, edge, -20, -20, 16);
    expect(edge.x == 0 && edge.y == 0 && edgeResult.blockedX && edgeResult.blockedY,
           "movement respects solid outer world boundary");
}

void testInputAndPlayerCommands() {
    using underworld::platform::InputState;
    using underworld::game::CommandBuilder;
    constexpr underworld::simulation::PlayerId playerId{7};
    CommandBuilder builder;

    const auto up = builder.build(10, playerId, InputState{true, false, false, false});
    expect(up.tick == 10 && up.playerId == playerId && up.sequence == 0 &&
               up.movement == underworld::simulation::MovementIntent{0, -1},
           "CommandBuilder maps up intent and preserves tick, player id, and sequence");
    const auto down = builder.build(11, playerId, InputState{false, true, false, false});
    expect(down.movement == underworld::simulation::MovementIntent{0, 1},
           "CommandBuilder maps down intent");
    const auto left = builder.build(12, playerId, InputState{false, false, true, false});
    const auto right = builder.build(13, playerId, InputState{false, false, false, true});
    expect(left.movement == underworld::simulation::MovementIntent{-1, 0} &&
               right.movement == underworld::simulation::MovementIntent{1, 0},
           "CommandBuilder maps left and right without physical key codes");
    const auto diagonal = builder.build(14, playerId, InputState{true, false, false, true});
    expect(diagonal.movement == underworld::simulation::MovementIntent{1, -1},
           "CommandBuilder preserves simultaneous up-right intent");
    const auto upLeft = builder.build(15, playerId, InputState{true, false, true, false});
    const auto downRight = builder.build(16, playerId, InputState{false, true, false, true});
    const auto downLeft = builder.build(17, playerId, InputState{false, true, true, false});
    expect(upLeft.movement == underworld::simulation::MovementIntent{-1, -1} &&
               downRight.movement == underworld::simulation::MovementIntent{1, 1} &&
               downLeft.movement == underworld::simulation::MovementIntent{-1, 1},
           "CommandBuilder maps every diagonal movement combination");
    const auto opposed = builder.build(18, playerId, InputState{true, true, true, true});
    expect(opposed.movement == underworld::simulation::MovementIntent{0, 0},
           "opposite directions cancel deterministically on both axes");
    expect(diagonal.sequence == 4 && opposed.sequence == 8 && builder.nextSequence() == 9,
           "command sequence advances once per built fixed-tick command");

    InputState held{true, false, true, false};
    held.clear();
    expect(held == InputState{},
           "neutral InputState clears every held key for focus-loss handling");
}

underworld::simulation::PlayerCommand movementCommand(
    std::uint64_t tick, int x, int y,
    underworld::simulation::PlayerId playerId = {0}) {
    return {tick, playerId, static_cast<std::uint32_t>(tick), {x, y}};
}

underworld::simulation::PlayerCommand actionCommand(
    std::uint64_t tick, bool primary, bool secondary, int x = 0, int y = 0,
    underworld::simulation::PlayerId playerId = {0}) {
    return {tick, playerId, static_cast<std::uint32_t>(tick), {x, y},
            {primary, secondary}};
}

void testPlayerMovementAndFacing() {
    namespace gameplay = underworld::game::gameplay;
    underworld::world::CollisionGrid openGrid(256, 256);
    gameplay::Player right({0}, {0, 1}, {1000, 1000});
    const auto start = right.subpixelPosition();
    right.update(movementCommand(1, 1, 0), openGrid, 16);
    expect(right.subpixelPosition().x - start.x ==
               gameplay::PlayerMovementConfig::cardinalSpeedSubpixelsPerTick &&
               right.subpixelPosition().y == start.y,
           "Player moves right by the fixed cardinal subpixel velocity");
    expect(right.facing() == gameplay::FacingDirection::right &&
               right.motionState() == gameplay::PlayerMotionState::walk,
           "right movement selects right-facing walk state");
    right.update(movementCommand(2, 0, 0), openGrid, 16);
    expect(right.facing() == gameplay::FacingDirection::right &&
               right.motionState() == gameplay::PlayerMotionState::idle,
           "stopping selects idle while preserving the last facing direction");

    gameplay::Player directions({0}, {0, 1}, {1000, 1000});
    directions.update(movementCommand(1, -1, 0), openGrid, 16);
    expect(directions.facing() == gameplay::FacingDirection::left,
           "left intent selects left facing");
    directions.update(movementCommand(2, 0, -1), openGrid, 16);
    expect(directions.facing() == gameplay::FacingDirection::up,
           "up intent selects up facing");
    directions.update(movementCommand(3, 0, 1), openGrid, 16);
    expect(directions.facing() == gameplay::FacingDirection::down,
           "down intent selects down facing");
    directions.update(movementCommand(4, 1, -1), openGrid, 16);
    expect(directions.facing() == gameplay::FacingDirection::up,
           "vertical direction has deterministic priority for diagonal facing");

    gameplay::Player freeLeft({0}, {0, 1}, {1000, 1000});
    gameplay::Player freeUp({0}, {1, 1}, {1000, 1000});
    gameplay::Player freeDown({0}, {2, 1}, {1000, 1000});
    freeLeft.update(movementCommand(1, -1, 0), openGrid, 16);
    freeUp.update(movementCommand(1, 0, -1), openGrid, 16);
    freeDown.update(movementCommand(1, 0, 1), openGrid, 16);
    expect(freeLeft.subpixelPosition().x < start.x &&
               freeLeft.subpixelPosition().y == start.y,
           "Player moves freely to the left");
    expect(freeUp.subpixelPosition().y < start.y &&
               freeUp.subpixelPosition().x == start.x,
           "Player moves freely upward");
    expect(freeDown.subpixelPosition().y > start.y &&
               freeDown.subpixelPosition().x == start.x,
           "Player moves freely downward");

    gameplay::Player oneSecond({0}, {0, 1}, {1000, 1000});
    for (std::uint64_t tick = 1; tick <= 60; ++tick) {
        oneSecond.update(movementCommand(tick, 1, 0), openGrid, 16);
    }
    expect(oneSecond.feetPosition().x == 1090 && oneSecond.feetPosition().y == 1000,
           "60 fixed ticks move the Player exactly 90 world pixels");

    gameplay::Player cardinal({0}, {0, 1}, {1000, 1000});
    gameplay::Player diagonal({0}, {1, 1}, {1000, 1000});
    for (std::uint64_t tick = 1; tick <= 600; ++tick) {
        cardinal.update(movementCommand(tick, 1, 0), openGrid, 16);
        diagonal.update(movementCommand(tick, 1, 1), openGrid, 16);
    }
    const auto cardinalDelta = cardinal.subpixelPosition().x -
                               static_cast<std::int64_t>(1000) * 256;
    const auto diagonalPosition = diagonal.subpixelPosition();
    const double diagonalX = static_cast<double>(diagonalPosition.x -
                              static_cast<std::int64_t>(1000) * 256);
    const double diagonalY = static_cast<double>(diagonalPosition.y -
                              static_cast<std::int64_t>(1000) * 256);
    const double diagonalDistance = std::sqrt(diagonalX * diagonalX + diagonalY * diagonalY);
    expect(std::abs(diagonalDistance - static_cast<double>(cardinalDelta)) < 512.0,
           "600-tick diagonal distance matches cardinal distance within two pixels");

    const auto body = gameplay::Player({0}, {0, 1}, {100, 80}).collisionBody();
    expect(body == underworld::world::AabbI{95, 72, 10, 8},
           "Player collision body is 10x8 and offset -5,-8 from the feet");

    bool wrongPlayerRejected = false;
    try {
        right.update(movementCommand(3, 1, 0, {9}), openGrid, 16);
    } catch (const std::invalid_argument&) {
        wrongPlayerRejected = true;
    }
    expect(wrongPlayerRejected, "Player rejects commands addressed to another PlayerId");
}

void testPlayerCollision() {
    namespace gameplay = underworld::game::gameplay;
    underworld::world::CollisionGrid verticalWall(16, 16);
    for (int y = 0; y < verticalWall.height(); ++y) {
        verticalWall.setSolid(3, y, true);
    }
    gameplay::Player againstWall({0}, {0, 1}, {43, 40});
    againstWall.update(movementCommand(1, 1, 0), verticalWall, 16);
    expect(againstWall.feetPosition() == underworld::core::WorldPointI{43, 40} &&
               againstWall.lastMovement().blockedX,
           "Player collision body stops exactly against a vertical wall");

    gameplay::Player sliding({0}, {0, 1}, {43, 40});
    sliding.update(movementCommand(1, 1, 1), verticalWall, 16);
    expect(sliding.feetPosition().x == 43 && sliding.feetPosition().y > 40 &&
               sliding.lastMovement().blockedX && !sliding.lastMovement().blockedY,
           "Player slides along a wall using separate-axis resolution");

    underworld::world::CollisionGrid horizontalWall(16, 16);
    for (int x = 0; x < horizontalWall.width(); ++x) {
        horizontalWall.setSolid(x, 3, true);
    }
    gameplay::Player aboveWall({0}, {0, 1}, {43, 48});
    aboveWall.update(movementCommand(1, 0, 1), horizontalWall, 16);
    expect(aboveWall.feetPosition() == underworld::core::WorldPointI{43, 48} &&
               aboveWall.lastMovement().blockedY,
           "Player collision body stops exactly against a horizontal wall");

    underworld::world::CollisionGrid cornerGrid(16, 16);
    for (int y = 0; y < cornerGrid.height(); ++y) {
        cornerGrid.setSolid(3, y, true);
    }
    for (int x = 0; x < cornerGrid.width(); ++x) {
        cornerGrid.setSolid(x, 3, true);
    }
    gameplay::Player corner({0}, {0, 1}, {43, 48});
    corner.update(movementCommand(1, 1, 1), cornerGrid, 16);
    expect(corner.feetPosition() == underworld::core::WorldPointI{43, 48} &&
               corner.lastMovement().blockedX && corner.lastMovement().blockedY,
           "Player cannot pass diagonally through a solid corner");

    underworld::world::CollisionGrid corridorGrid(16, 8);
    for (int x = 0; x < corridorGrid.width(); ++x) {
        corridorGrid.setSolid(x, 1, true);
        corridorGrid.setSolid(x, 3, true);
    }
    gameplay::Player corridor({0}, {0, 1}, {21, 43});
    for (std::uint64_t tick = 1; tick <= 40; ++tick) {
        corridor.update(movementCommand(tick, 1, 0), corridorGrid, 16);
    }
    expect(corridor.feetPosition().x > 70 && !corridor.lastMovement().blockedX,
           "Player body traverses a one-tile corridor");

    underworld::world::CollisionGrid boundary(8, 8);
    gameplay::Player edge({0}, {0, 1}, {5, 8});
    edge.update(movementCommand(1, -1, -1), boundary, 16);
    expect(edge.feetPosition() == underworld::core::WorldPointI{5, 8} &&
               edge.lastMovement().blockedX && edge.lastMovement().blockedY,
           "outside-map solid policy keeps the Player inside world bounds");
}

void testPlayerVisualAndCameraFollow() {
    namespace gameplay = underworld::game::gameplay;
    underworld::game::PlayerVisual::DirectionalClips idle{
        makeTestClip("idle.down", true), makeTestClip("idle.up", true),
        makeTestClip("idle.side", true)};
    underworld::game::PlayerVisual::DirectionalClips walk{
        makeTestClip("walk.down", true), makeTestClip("walk.up", true),
        makeTestClip("walk.side", true)};
    underworld::game::PlayerVisual visual(std::move(idle), std::move(walk));
    visual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::down, 1);
    visual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::down, 1);
    expect(visual.animator().frameIndex() == 1,
           "unchanged Player visual selection does not restart its clip every tick");
    visual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::right, 0);
    expect(visual.animator().clip().id() == "idle.side" && visual.flipX(),
           "right idle reuses and flips the side-left clip");
    visual.update(gameplay::PlayerMotionState::walk, gameplay::FacingDirection::right, 0);
    expect(visual.animator().clip().id() == "walk.side" &&
               visual.animator().frameIndex() == 0 && visual.flipX(),
           "idle-to-walk changes clip once and preserves right-facing flip");
    visual.update(gameplay::PlayerMotionState::walk, gameplay::FacingDirection::left, 0);
    expect(visual.animator().clip().id() == "walk.side" && !visual.flipX(),
           "left and right share the same side clip without duplicating image data");
    visual.update(gameplay::PlayerMotionState::walk, gameplay::FacingDirection::up, 0);
    expect(visual.animator().clip().id() == "walk.up" && !visual.flipX(),
           "up-facing walk selects the dedicated audited sheet row");

    underworld::game::PlayerVisual::DirectionalClips sword{
        makeTestClip("sword.down", false), makeTestClip("sword.up", false),
        makeTestClip("sword.side", false)};
    underworld::game::PlayerVisual::DirectionalClips bow{
        makeTestClip("bow.down", false), makeTestClip("bow.up", false),
        makeTestClip("bow.side", false)};
    underworld::game::PlayerVisual combatVisual(
        {makeTestClip("idle2.down", true), makeTestClip("idle2.up", true),
         makeTestClip("idle2.side", true)},
        {makeTestClip("walk2.down", true), makeTestClip("walk2.up", true),
         makeTestClip("walk2.side", true)},
        std::move(sword), std::move(bow));
    combatVisual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::right,
                        gameplay::PlayerActionState::swordAttack, 2);
    expect(combatVisual.animator().clip().id() == "sword.side" && combatVisual.flipX(),
           "right sword attack reuses the side-left clip with horizontal flip");
    expect(combatVisual.consumeMarkerEvents().size() == 1,
           "PlayerVisual forwards attack animation markers exactly once");
    combatVisual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::up,
                        gameplay::PlayerActionState::bowAttack, 0);
    expect(combatVisual.animator().clip().id() == "bow.up" &&
               combatVisual.animator().frameIndex() == 0,
           "changing attack action starts the selected bow clip once at frame zero");

    underworld::render::Camera2D camera(272, 224);
    camera.centerOn({500, 400});
    camera.clampToWorld(1024, 768);
    expect(camera.position() == underworld::core::WorldPointI{364, 288},
           "camera centers its logical viewport on the Player feet");
    camera.centerOn({20, 20});
    camera.clampToWorld(1024, 768);
    expect(camera.position() == underworld::core::WorldPointI{0, 0},
           "camera follow clamps at left and top world edges");
    camera.centerOn({1000, 740});
    camera.clampToWorld(1024, 768);
    expect(camera.position() == underworld::core::WorldPointI{752, 544},
           "camera follow clamps at right and bottom world edges");
}

void testActionCommandsAndPlayerAttackState() {
    namespace gameplay = underworld::game::gameplay;
    underworld::game::CommandBuilder builder;
    underworld::platform::InputState input{};
    input.primaryAttackPressed = true;
    auto primary = builder.build(30, {4}, input);
    expect(primary.tick == 30 && primary.playerId == underworld::simulation::PlayerId{4} &&
               primary.sequence == 0 && primary.actions.primaryAttackPressed &&
               !primary.actions.secondaryAttackPressed,
           "primary action edge survives the command boundary with tick/id/sequence");
    input.primaryAttackPressed = false;
    auto heldWithoutEdge = builder.build(31, {4}, input);
    expect(!heldWithoutEdge.actions.primaryAttackPressed,
           "held input without a new edge does not repeat an attack command");
    input.secondaryAttackPressed = true;
    auto secondary = builder.build(32, {4}, input);
    expect(secondary.actions.secondaryAttackPressed && secondary.sequence == 2,
           "secondary action edge is preserved and sequence remains deterministic");
    input.primaryAttackPressed = true;
    auto simultaneous = builder.build(33, {4}, input);
    expect(simultaneous.actions.primaryAttackPressed &&
               !simultaneous.actions.secondaryAttackPressed,
           "primary attack deterministically wins simultaneous primary/secondary input");

    underworld::platform::ActionEdgeBuffer edges;
    edges.pushPrimary(); // A down/up pair can complete before the next fixed tick.
    underworld::platform::InputState sampled{};
    edges.applyNext(sampled);
    expect(sampled.primaryAttackPressed && edges.pendingPrimary() == 0,
           "short action edge remains queued until exactly one fixed tick consumes it");
    sampled = {};
    edges.applyNext(sampled);
    expect(!sampled.primaryAttackPressed,
           "consumed edge does not repeat while the physical key remains without transition");
    edges.pushPrimary();
    edges.pushPrimary();
    sampled = {};
    edges.applyNext(sampled);
    expect(sampled.primaryAttackPressed && edges.pendingPrimary() == 1,
           "multiple physical transitions are queued one per fixed tick");
    edges.pushSecondary();
    edges.clear();
    sampled = {};
    edges.applyNext(sampled);
    expect(!sampled.primaryAttackPressed && !sampled.secondaryAttackPressed,
           "focus-loss clear removes all pending action edges and prevents ghost attacks");

    underworld::world::CollisionGrid grid(64, 64);
    gameplay::Player player({0}, {0, 1}, {100, 100});
    player.update(actionCommand(1, true, false, 1, 0), grid, 16);
    const auto lockedPosition = player.subpixelPosition();
    const auto firstAttack = player.attackInstance();
    expect(player.actionState() == gameplay::PlayerActionState::swordAttack && firstAttack != 0,
           "primary command starts a sword attack instance");
    expect(player.subpixelPosition() == lockedPosition &&
               player.facing() == gameplay::FacingDirection::down,
           "attack start locks movement and captures the existing facing");
    player.update(actionCommand(2, true, false, 0, -1), grid, 16);
    expect(player.attackInstance() == firstAttack &&
               player.facing() == gameplay::FacingDirection::down &&
               player.subpixelPosition() == lockedPosition,
           "active attack ignores retrigger, direction changes, and movement");
    player.finishAttack();
    player.update(actionCommand(3, false, false, 1, 0), grid, 16);
    expect(player.actionState() == gameplay::PlayerActionState::none &&
               player.motionState() == gameplay::PlayerMotionState::walk &&
               player.subpixelPosition().x > lockedPosition.x,
           "movement resumes after attack recovery completes");
    player.update(actionCommand(4, false, true), grid, 16);
    expect(player.actionState() == gameplay::PlayerActionState::bowAttack &&
               player.attackInstance() != firstAttack,
           "secondary command starts a distinct bow attack instance");
    expect(player.hurtbox().bounds == underworld::world::AabbI{94, 78, 14, 22} &&
               player.collisionBody() == underworld::world::AabbI{96, 92, 10, 8},
           "Player Hurtbox and CollisionBody have independent dimensions and offsets");
    expect(player.interactionArea().bounds.width == 22 &&
               player.interactionArea().bounds != player.hurtbox().bounds,
           "InteractionArea is distinct from damage and physical boxes");
}

void testEntityHandlesAndActorOrder() {
    underworld::simulation::EntityHandlePool pool;
    const auto first = pool.create();
    const auto second = pool.create();
    expect(pool.valid(first) && pool.valid(second) && first != second,
           "created runtime entity handles are valid and distinct");
    expect(pool.destroy(first) && !pool.valid(first),
           "destroyed entity handle becomes invalid");
    const auto reused = pool.create();
    expect(reused.index == first.index && reused.generation != first.generation &&
               pool.valid(reused) && !pool.valid(first),
           "reused slot changes generation and never revives the stale handle");
    expect(underworld::game::actorRendersBefore({100, reused}, {120, second}),
           "actor with lower feet Y renders first");
    const bool stableTie = reused.index < second.index
        ? underworld::game::actorRendersBefore({100, reused}, {100, second})
        : underworld::game::actorRendersBefore({100, second}, {100, reused});
    expect(stableTie, "equal-Y actor ordering uses a stable EntityHandle tie-breaker");

    underworld::game::CombatDebugVisibility debug;
    underworld::platform::DebugInputState hurtOnly{};
    hurtOnly.toggleHurtboxPressed = true;
    debug.apply(hurtOnly);
    expect(debug.hurtbox && !debug.hitbox && !debug.collisionBody && !debug.interaction,
           "Hurtbox debug visibility toggles independently from every other box");
    underworld::platform::DebugInputState hitOnly{};
    hitOnly.toggleHitboxPressed = true;
    debug.apply(hitOnly);
    expect(debug.hurtbox && debug.hitbox && !debug.collisionBody && !debug.interaction,
           "Hitbox debug visibility toggles without changing Hurtbox visibility");
}

struct TestCombatActor final {
    underworld::game::gameplay::CombatantState combatant;
    underworld::core::WorldPointI feet{};
    underworld::game::gameplay::CollisionBody collisionBody{};
    underworld::game::gameplay::Hurtbox hurtbox{};

    [[nodiscard]] underworld::game::gameplay::CombatTargetRef target() noexcept {
        hurtbox.enabled = !combatant.health.depleted();
        return {combatant, hurtbox};
    }

    void apply(const underworld::game::gameplay::CombatResolution& resolution,
               const underworld::world::CollisionGrid& collision, int tileSize) {
        underworld::world::AabbI body = collisionBody.bounds;
        const auto movement = underworld::world::moveAgainstSolidTiles(
            collision, body, resolution.requestedKnockbackX,
            resolution.requestedKnockbackY, tileSize);
        feet.x += movement.movedX;
        feet.y += movement.movedY;
        collisionBody.bounds = body;
        hurtbox.bounds.x += movement.movedX;
        hurtbox.bounds.y += movement.movedY;
    }
};

TestCombatActor makeCombatTarget(
    underworld::simulation::EntityHandle handle, underworld::core::WorldPointI feet,
    underworld::game::gameplay::Faction faction, int health) {
    return {{handle, faction, underworld::game::gameplay::Health{health}, 0, false}, feet,
            {{feet.x - 5, feet.y - 8, 10, 8}},
            {{feet.x - 7, feet.y - 22, 14, 22}, true}};
}

void testCombatSystem() {
    namespace gameplay = underworld::game::gameplay;
    namespace simulation = underworld::simulation;
    underworld::world::CollisionGrid grid(32, 32);
    simulation::EntityHandlePool pool;
    const auto attacker = pool.create();
    const auto targetHandle = pool.create();
    auto target = makeCombatTarget(targetHandle, {100, 100}, gameplay::Faction::enemy, 5);
    gameplay::CombatSystem combat;
    simulation::EventBuffer events;
    gameplay::Hitbox swing{{90, 78, 24, 24}, {attacker, 1}, gameplay::Faction::player,
                           {1, 8}, 8, 0, true};
    const auto firstResolution = combat.resolve(swing, target.target(), events);
    target.apply(firstResolution, grid, 16);
    expect(firstResolution.damaged && target.combatant.health.current == 4,
           "valid hit applies damage through CombatSystem");
    expect(target.feet.x == 108 && events.events().size() == 1 &&
               std::holds_alternative<simulation::EntityDamaged>(events.events()[0]),
           "successful damage emits EntityDamaged and deterministic knockback");
    expect(!combat.resolve(swing, target.target(), events).damaged &&
               target.combatant.health.current == 4,
           "same attack instance cannot damage the same target twice");
    gameplay::Hitbox secondSwing = swing;
    secondSwing.attack.localInstance = 2;
    expect(!combat.resolve(secondSwing, target.target(), events).damaged,
           "new attack is ignored during target invulnerability");
    for (std::uint32_t tick = 0; tick < gameplay::CombatSystem::invulnerabilityDurationTicks; ++tick) {
        gameplay::tickInvulnerability(target.combatant);
    }
    secondSwing.bounds = target.hurtbox.bounds;
    expect(combat.resolve(secondSwing, target.target(), events).damaged &&
               target.combatant.health.current == 3,
           "new attack damages after invulnerability expires");

    auto sameFaction = makeCombatTarget(pool.create(), {100, 100}, gameplay::Faction::player, 3);
    expect(!combat.resolve(swing, sameFaction.target(), events).damaged &&
               sameFaction.combatant.health.current == 3,
           "Player faction cannot damage Player faction");
    gameplay::Hitbox enemySwing = swing;
    enemySwing.attack.owner = targetHandle;
    enemySwing.faction = gameplay::Faction::enemy;
    auto enemyTarget = makeCombatTarget(pool.create(), {100, 100}, gameplay::Faction::enemy, 3);
    expect(!combat.resolve(enemySwing, enemyTarget.target(), events).damaged,
           "Enemy faction cannot damage Enemy faction");
    auto ownerTarget = makeCombatTarget(attacker, {100, 100}, gameplay::Faction::enemy, 3);
    expect(!combat.resolve(swing, ownerTarget.target(), events).damaged,
           "attack owner can never damage itself");

    auto defeated = makeCombatTarget(pool.create(), {160, 100}, gameplay::Faction::enemy, 1);
    gameplay::Hitbox lethal{{150, 78, 24, 24}, {attacker, 10}, gameplay::Faction::player,
                            {1, 0}, 0, 0, true};
    simulation::EventBuffer lethalEvents;
    expect(combat.resolve(lethal, defeated.target(), lethalEvents).defeated &&
               defeated.combatant.health.current == 0 && !defeated.target().hurtbox.enabled,
           "lethal damage clamps Health to zero and disables Hurtbox");
    const auto defeatedCount = std::count_if(
        lethalEvents.events().begin(), lethalEvents.events().end(),
        [](const simulation::SimulationEvent& event) {
            return std::holds_alternative<simulation::EntityDefeated>(event);
        });
    expect(defeatedCount == 1, "EntityDefeated emits exactly once");
    expect(!combat.resolve(lethal, defeated.target(), lethalEvents).damaged &&
               lethalEvents.events().size() == 2,
           "defeated target cannot emit damage or defeat again");

    underworld::world::CollisionGrid wallGrid(16, 16);
    wallGrid.setSolid(7, 6, true);
    auto nearWall = makeCombatTarget(pool.create(), {107, 104}, gameplay::Faction::enemy, 3);
    gameplay::Hitbox wallKnock{{95, 80, 20, 24}, {attacker, 20}, gameplay::Faction::player,
                               {1, 16}, 16, 0, true};
    const auto wallHit = combat.resolve(wallKnock, nearWall.target(), events);
    nearWall.apply(wallHit, wallGrid, 16);
    expect(nearWall.collisionBody.bounds.x + nearWall.collisionBody.bounds.width <= 112,
           "knockback reuses tile collision and cannot cross a wall");

    auto knockedUp = makeCombatTarget(pool.create(), {200, 200}, gameplay::Faction::enemy, 3);
    gameplay::Hitbox upward{{190, 175, 20, 25}, {attacker, 21}, gameplay::Faction::player,
                            {1, 7}, 0, -7, true};
    const int beforeUp = knockedUp.feet.y;
    const auto upwardResult = combat.resolve(upward, knockedUp.target(), events);
    knockedUp.apply(upwardResult, grid, 16);
    expect(upwardResult.damaged &&
               knockedUp.feet.y == beforeUp - 7,
           "upward sword knockback displaces target in facing direction");

    const gameplay::CollisionBody collisionBefore = target.collisionBody;
    target.hurtbox.bounds.width += 3;
    expect(target.collisionBody.bounds == collisionBefore.bounds,
           "changing Hurtbox does not alter CollisionBody");
    gameplay::Hitbox independent = swing;
    independent.bounds.width += 4;
    expect(independent.bounds != target.hurtbox.bounds,
           "Hitbox data is independent from Hurtbox data");

    const auto sword = gameplay::makePlayerSwordAttackDefinition();
    const auto bow = gameplay::makePlayerBowAttackDefinition();
    expect(sword.meleeHitboxes->forFacing(gameplay::FacingDirection::down).at({100, 100}) ==
               underworld::world::AabbI{90, 99, 20, 18} &&
               sword.meleeHitboxes->forFacing(gameplay::FacingDirection::up).at({100, 100}) ==
               underworld::world::AabbI{90, 73, 20, 19} &&
               sword.meleeHitboxes->forFacing(gameplay::FacingDirection::left).at({100, 100}) ==
               underworld::world::AabbI{73, 82, 21, 18} &&
               sword.meleeHitboxes->forFacing(gameplay::FacingDirection::right).at({100, 100}) ==
               underworld::world::AabbI{106, 82, 21, 18},
           "sword Hitboxes are explicit per facing and anchored to feet");
    expect(sword.totalTicks == 24 && bow.totalTicks == 16 &&
               sword.kind == gameplay::AttackKind::meleeHitbox &&
               bow.kind == gameplay::AttackKind::projectile,
           "Sword and Bow share small immutable AttackDefinition data");
}

void testProjectilesAndEffects() {
    namespace gameplay = underworld::game::gameplay;
    namespace simulation = underworld::simulation;
    simulation::EntityHandlePool pool;
    const auto owner = pool.create();
    gameplay::CombatSystem combat;
    const gameplay::ProjectileDefinition arrowDefinition =
        gameplay::makePlayerArrowProjectileDefinition();
    gameplay::ProjectileCatalog projectileCatalog;
    projectileCatalog.add(arrowDefinition);
    gameplay::ProjectileSystem projectiles(pool, projectileCatalog);
    underworld::world::CollisionGrid grid(64, 64);
    simulation::EventBuffer events;
    auto target = makeCombatTarget(pool.create(), {120, 100}, gameplay::Faction::enemy, 3);
    [[maybe_unused]] const auto targetProjectile = projectiles.spawn(
        {owner, 100}, gameplay::Faction::player, arrowDefinition.id, {100, 90},
        gameplay::FacingDirection::right, {1, 6});
    std::array<gameplay::CombatTargetRef, 1> targets{target.target()};
    std::vector<gameplay::CombatResolution> resolutions;
    for (int tick = 0; tick < 8 && !projectiles.projectiles().empty(); ++tick) {
        projectiles.update(grid, 16, targets, combat, events, resolutions);
    }
    expect(projectiles.projectiles().empty() && target.combatant.health.current == 2,
           "projectile hits a valid Hurtbox once and is destroyed");
    expect(std::any_of(events.events().begin(), events.events().end(),
                       [](const simulation::SimulationEvent& event) {
                           const auto* impact = std::get_if<simulation::ProjectileImpact>(&event);
                           return impact && impact->kind == simulation::ProjectileImpactKind::target;
                       }),
           "projectile target impact is emitted for VFX observers");

    gameplay::ProjectileSystem wallProjectiles(pool, projectileCatalog);
    underworld::world::CollisionGrid wall(64, 64);
    wall.setSolid(7, 6, true);
    [[maybe_unused]] const auto wallProjectile = wallProjectiles.spawn(
        {owner, 101}, gameplay::Faction::player, arrowDefinition.id, {108, 100},
        gameplay::FacingDirection::right, {1, 6});
    std::array<gameplay::CombatTargetRef, 0> noTargets{};
    wallProjectiles.update(wall, 16, noTargets, combat, events, resolutions);
    expect(wallProjectiles.projectiles().empty(),
           "pixel-substepped fast projectile cannot tunnel through a solid tile");

    gameplay::ProjectileSystem ownerFiltered(pool, projectileCatalog);
    auto ownerAsTarget = makeCombatTarget(owner, {104, 100}, gameplay::Faction::enemy, 3);
    [[maybe_unused]] const auto filteredProjectile = ownerFiltered.spawn(
        {owner, 102}, gameplay::Faction::player, arrowDefinition.id, {100, 90},
        gameplay::FacingDirection::down, {1, 6});
    std::array<gameplay::CombatTargetRef, 1> ownerTarget{ownerAsTarget.target()};
    ownerFiltered.update(grid, 16, ownerTarget, combat, events, resolutions);
    expect(ownerAsTarget.combatant.health.current == 3,
           "projectile owner filtering prevents self-hit regardless of faction");

    gameplay::ProjectileSystem expiring(pool, projectileCatalog);
    [[maybe_unused]] const auto expiringProjectile = expiring.spawn(
        {owner, 103}, gameplay::Faction::player, arrowDefinition.id, {500, 500},
        gameplay::FacingDirection::right, {1, 0});
    for (std::uint32_t tick = 0; tick < arrowDefinition.lifetimeTicks; ++tick) {
        expiring.update(grid, 16, noTargets, combat, events, resolutions);
    }
    expect(expiring.projectiles().empty(), "projectile expires and releases its handle at TTL");

    auto impactClip = makeTestClip("impact.test", false);
    underworld::game::EffectSystem effects(impactClip);
    effects.spawnImpact({10, 20});
    expect(effects.effects().size() == 1, "transient VFX spawns independently of gameplay");
    effects.update(impactClip->durationTicks());
    expect(effects.effects().empty(), "finished transient VFX removes itself automatically");

    expect(gameplay::directionVector(gameplay::FacingDirection::up) ==
               underworld::core::WorldPointI{0, -1} &&
               gameplay::directionVector(gameplay::FacingDirection::down) ==
               underworld::core::WorldPointI{0, 1} &&
               gameplay::directionVector(gameplay::FacingDirection::left) ==
               underworld::core::WorldPointI{-1, 0} &&
               gameplay::directionVector(gameplay::FacingDirection::right) ==
               underworld::core::WorldPointI{1, 0},
           "projectile direction vectors cover all four facings");

    gameplay::ProjectileSystem directions(pool, projectileCatalog);
    const std::array<gameplay::FacingDirection, 4> facings{
        gameplay::FacingDirection::up, gameplay::FacingDirection::down,
        gameplay::FacingDirection::left, gameplay::FacingDirection::right};
    for (std::size_t index = 0; index < facings.size(); ++index) {
        [[maybe_unused]] const auto projectile = directions.spawn(
            {owner, 200 + index}, gameplay::Faction::player, arrowDefinition.id,
            {400, 400}, facings[index], {1, 0});
    }
    directions.update(grid, 16, noTargets, combat, events, resolutions);
    const auto& moved = directions.projectiles();
    expect(moved[0].position == underworld::core::WorldPointI{400, 396} &&
               moved[1].position == underworld::core::WorldPointI{400, 404} &&
               moved[2].position == underworld::core::WorldPointI{396, 400} &&
               moved[3].position == underworld::core::WorldPointI{404, 400},
           "ProjectileSystem moves Up/Down/Left/Right by configured speed per tick");

    gameplay::ProjectileSystem invulnerableImpact(pool, projectileCatalog);
    auto invulnerable = makeCombatTarget(pool.create(), {120, 100}, gameplay::Faction::enemy, 3);
    invulnerable.combatant.invulnerabilityTicks = 5;
    [[maybe_unused]] const auto invulnerableArrow = invulnerableImpact.spawn(
        {owner, 300}, gameplay::Faction::player, arrowDefinition.id, {108, 90},
        gameplay::FacingDirection::right, {1, 6});
    std::array<gameplay::CombatTargetRef, 1> invulnerableTarget{invulnerable.target()};
    invulnerableImpact.update(grid, 16, invulnerableTarget, combat, events, resolutions);
    expect(invulnerableImpact.projectiles().empty() &&
               invulnerable.combatant.health.current == 3,
           "arrow impacts a valid invulnerable Hurtbox without applying damage or piercing");
}

void testPhase6CombatGeneralization() {
    namespace gameplay = underworld::game::gameplay;
    namespace simulation = underworld::simulation;

    const simulation::DefinitionId soldierAttack{"attack.soldier.sword"};
    expect(soldierAttack == simulation::DefinitionId{"attack.soldier.sword"} &&
               soldierAttack != simulation::DefinitionId{"attack.skull.arrow"},
           "DefinitionId equality depends on stable text rather than object address");

    gameplay::AttackCatalog attacks;
    auto sword = gameplay::makePlayerSwordAttackDefinition();
    attacks.add(sword);
    expect(attacks.find(gameplay::playerSwordAttackId()) != nullptr &&
               attacks.require(gameplay::playerSwordAttackId()).id == sword.id,
           "AttackCatalog resolves a stable DefinitionId");
    expect(attacks.find(simulation::DefinitionId{"attack.missing"}) == nullptr,
           "AttackCatalog reports a missing DefinitionId without pointer identity");
    bool duplicateAttackRejected = false;
    try {
        attacks.add(gameplay::makePlayerSwordAttackDefinition());
    } catch (const std::logic_error&) {
        duplicateAttackRejected = true;
    }
    expect(duplicateAttackRejected, "AttackCatalog rejects duplicate stable ids");

    gameplay::ProjectileCatalog projectiles;
    auto arrow = gameplay::makePlayerArrowProjectileDefinition();
    projectiles.add(arrow);
    expect(projectiles.require(gameplay::playerArrowProjectileId()).canonicalFacing ==
               gameplay::FacingDirection::up,
           "ProjectileDefinition records canonical visual orientation");

    simulation::EntityHandlePool pool;
    const auto ownerA = pool.create();
    const auto ownerB = pool.create();
    auto target = makeCombatTarget(pool.create(), {100, 100}, gameplay::Faction::player, 5);
    gameplay::CombatSystem combat;
    simulation::EventBuffer events;
    gameplay::Hitbox attackA{{90, 78, 24, 24}, {ownerA, 1}, gameplay::Faction::enemy,
                             {1, 0}, 0, 0, true};
    gameplay::Hitbox attackB = attackA;
    attackB.attack.owner = ownerB;
    expect(combat.resolve(attackA, target.target(), events).damaged,
           "owner A local attack 1 damages the target");
    for (std::uint32_t tick = 0; tick < gameplay::CombatSystem::invulnerabilityDurationTicks;
         ++tick) {
        gameplay::tickInvulnerability(target.combatant);
    }
    expect(combat.resolve(attackB, target.target(), events).damaged &&
               target.combatant.health.current == 3,
           "owner B local attack 1 is independent from owner A local attack 1");
    for (std::uint32_t tick = 0; tick < gameplay::CombatSystem::invulnerabilityDurationTicks;
         ++tick) {
        gameplay::tickInvulnerability(target.combatant);
    }
    combat.finishAttack(attackA.attack);
    expect(!combat.resolve(attackB, target.target(), events).damaged,
           "finishing owner A attack does not erase owner B hit deduplication");

    const auto playerHandle = pool.create();
    gameplay::Player player({9}, playerHandle, {200, 200});
    gameplay::Hitbox enemyHit{{190, 178, 24, 24}, {ownerA, 2}, gameplay::Faction::enemy,
                              {1, 6}, 6, 0, true};
    const auto playerHit = combat.resolve(enemyHit, player.combatTarget(), events);
    underworld::world::CollisionGrid openGrid(64, 64);
    player.applyKnockback(playerHit.requestedKnockbackX, playerHit.requestedKnockbackY,
                          openGrid, 16);
    expect(playerHit.damaged && player.health().current == 4 &&
               player.feetPosition().x == 206,
           "Player receives generic CombatSystem damage and applies requested knockback");
    expect(player.entityHandle() == playerHandle && player.id() == simulation::PlayerId{9},
           "PlayerId command identity remains distinct from runtime EntityHandle");
}

void testCreatureDefinitionsAndBehavior() {
    namespace creatures = underworld::game::gameplay::creatures;
    namespace gameplay = underworld::game::gameplay;
    namespace simulation = underworld::simulation;

    const simulation::DefinitionId behaviorId{"behavior.test.melee"};
    const simulation::DefinitionId visualId{"visual.enemy.test"};
    const simulation::DefinitionId enemyId{"enemy.test.melee"};
    const simulation::DefinitionId fastEnemyId{"enemy.test.fast_melee"};
    const simulation::DefinitionId attackId{"attack.test.melee"};

    gameplay::DirectionalBoxes boxes{{{
        {-8, -2, 16, 12}, {-8, -18, 16, 12},
        {-18, -12, 14, 12}, {4, -12, 14, 12},
    }}};
    gameplay::AttackCatalog attacks;
    attacks.add({attackId, gameplay::AttackKind::meleeHitbox, {1, 4}, 12, 20,
                 0, 24, simulation::DefinitionId{"visual.attack.test"}, boxes,
                 std::nullopt});
    gameplay::ProjectileCatalog projectiles;

    creatures::BehaviorCatalog behaviors;
    const creatures::BehaviorProfile profile{
        behaviorId, 80, 112, 3, 4};
    behaviors.add(profile);
    expect(behaviors.find(behaviorId) != nullptr &&
               behaviors.require(behaviorId).disengageRangePixels == 112,
           "BehaviorCatalog resolves immutable profiles by DefinitionId");
    bool duplicateBehaviorRejected = false;
    try { behaviors.add(profile); }
    catch (const std::logic_error&) { duplicateBehaviorRejected = true; }
    expect(duplicateBehaviorRejected, "BehaviorCatalog rejects duplicate ids");

    creatures::EnemyCatalog enemies;
    const creatures::EnemyDefinition definition{
        enemyId, visualId, behaviorId, gameplay::Faction::enemy, 3, 256,
        {-5, -8, 10, 8}, {-7, -22, 14, 22}, {attackId}};
    enemies.add(definition);
    enemies.add({fastEnemyId, visualId, behaviorId, gameplay::Faction::enemy, 7, 512,
                 {-5, -8, 10, 8}, {-7, -22, 14, 22}, {attackId}});
    expect(enemies.find(enemyId) != nullptr &&
               enemies.require(enemyId).maximumHealth == 3,
           "EnemyCatalog stores reusable definitions rather than runtime state");
    bool duplicateEnemyRejected = false;
    try { enemies.add(definition); }
    catch (const std::logic_error&) { duplicateEnemyRejected = true; }
    expect(duplicateEnemyRejected, "EnemyCatalog rejects duplicate ids");

    simulation::EntityHandlePool pool;
    const std::array availableVisuals{visualId};
    creatures::EnemyFactory factory(
        pool, enemies, behaviors, attacks, projectiles, availableVisuals);
    auto first = factory.create(enemyId, {200, 200});
    auto second = factory.create(enemyId, {240, 200}, gameplay::FacingDirection::left);
    expect(first.handle() != second.handle() &&
               &first.definition() == &second.definition(),
           "two enemy instances share one definition but own distinct handles");
    static_cast<void>(first.combatant().health.applyDamage(1));
    expect(first.combatant().health.current == 2 &&
               second.combatant().health.current == 3 &&
               first.feetPosition() != second.feetPosition(),
           "enemy health and position remain independent runtime state");
    auto synthetic = factory.create(fastEnemyId, {280, 200});
    expect(synthetic.combatant().health.maximum == 7 &&
               synthetic.definition().movementSpeedSubpixelsPerTick == 512,
           "a third synthetic enemy variant is created through data only");

    bool unknownEnemyRejected = false;
    try { [[maybe_unused]] auto invalid = factory.create(
              simulation::DefinitionId{"enemy.missing"}, {100, 100}); }
    catch (const std::out_of_range&) { unknownEnemyRejected = true; }
    expect(unknownEnemyRejected, "EnemyFactory rejects an unknown enemy definition");

    creatures::EnemyCatalog brokenEnemies;
    brokenEnemies.add({simulation::DefinitionId{"enemy.bad.visual"},
                       simulation::DefinitionId{"visual.missing"}, behaviorId,
                       gameplay::Faction::enemy, 1, 256,
                       {-5, -8, 10, 8}, {-7, -22, 14, 22}, {attackId}});
    creatures::EnemyFactory brokenFactory(
        pool, brokenEnemies, behaviors, attacks, projectiles, availableVisuals);
    bool missingVisualRejected = false;
    try { [[maybe_unused]] auto invalid = brokenFactory.create(
              simulation::DefinitionId{"enemy.bad.visual"}, {100, 100}); }
    catch (const std::invalid_argument&) { missingVisualRejected = true; }
    expect(missingVisualRejected,
           "EnemyFactory rejects a definition whose visual set is unavailable");

    creatures::EnemyCatalog missingAttackEnemies;
    missingAttackEnemies.add({simulation::DefinitionId{"enemy.bad.attack"}, visualId,
                              behaviorId, gameplay::Faction::enemy, 1, 256,
                              {-5, -8, 10, 8}, {-7, -22, 14, 22},
                              {simulation::DefinitionId{"attack.missing"}}});
    creatures::EnemyFactory missingAttackFactory(
        pool, missingAttackEnemies, behaviors, attacks, projectiles, availableVisuals);
    bool missingAttackRejected = false;
    try { [[maybe_unused]] auto invalid = missingAttackFactory.create(
              simulation::DefinitionId{"enemy.bad.attack"}, {100, 100}); }
    catch (const std::out_of_range&) { missingAttackRejected = true; }
    expect(missingAttackRejected,
           "EnemyFactory rejects a definition whose attack is unavailable");

    underworld::world::CollisionGrid openGrid(64, 64);
    creatures::EnemyBehaviorSystem behavior;
    const auto playerHandle = pool.create();
    auto actor = factory.create(enemyId, {200, 200});
    static_cast<void>(behavior.update(actor, playerHandle, {500, 500}, true, profile,
                                      attacks, openGrid, 16));
    static_cast<void>(behavior.update(actor, playerHandle, {500, 500}, true, profile,
                                      attacks, openGrid, 16));
    const auto idleToWander = behavior.update(
        actor, playerHandle, {500, 500}, true, profile, attacks, openGrid, 16);
    expect(idleToWander.stateChanged && actor.state() == creatures::BehaviorState::wander,
           "Idle transitions to Wander when its deterministic timer expires");
    const auto wanderStart = actor.feetPosition();
    static_cast<void>(behavior.update(actor, playerHandle, {500, 500}, true, profile,
                                      attacks, openGrid, 16));
    expect(actor.feetPosition() != wanderStart,
           "Wander performs deterministic fixed-tick movement");

    simulation::EntityHandlePool deterministicPoolA;
    simulation::EntityHandlePool deterministicPoolB;
    creatures::EnemyFactory deterministicFactoryA(
        deterministicPoolA, enemies, behaviors, attacks, projectiles, availableVisuals);
    creatures::EnemyFactory deterministicFactoryB(
        deterministicPoolB, enemies, behaviors, attacks, projectiles, availableVisuals);
    auto sameInitial = deterministicFactoryA.create(enemyId, {320, 320});
    auto sameInitialCopy = deterministicFactoryB.create(enemyId, {320, 320});
    for (int tick = 0; tick < 4; ++tick) {
        static_cast<void>(behavior.update(sameInitial, playerHandle, {900, 900}, true,
                                          profile, attacks, openGrid, 16));
        static_cast<void>(behavior.update(sameInitialCopy, playerHandle, {900, 900}, true,
                                          profile, attacks, openGrid, 16));
    }
    expect(sameInitial.state() == sameInitialCopy.state() &&
               sameInitial.feetPosition() == sameInitialCopy.feetPosition(),
           "Wander is deterministic for equivalent initialized state");

    auto hunter = factory.create(enemyId, {200, 200});
    const auto detected = behavior.update(
        hunter, playerHandle, {250, 200}, true, profile, attacks, openGrid, 16);
    expect(detected.stateChanged && hunter.state() == creatures::BehaviorState::chase &&
               hunter.target() == playerHandle,
           "Idle detects a live Player and transitions to Chase");
    const int beforeChase = hunter.feetPosition().x;
    static_cast<void>(behavior.update(hunter, playerHandle, {250, 200}, true, profile,
                                      attacks, openGrid, 16));
    expect(hunter.feetPosition().x > beforeChase,
           "Chase moves toward the target in a free map");
    static_cast<void>(behavior.update(hunter, playerHandle, {300, 200}, true, profile,
                                      attacks, openGrid, 16));
    expect(hunter.state() == creatures::BehaviorState::chase,
           "perception hysteresis keeps Chase outside detection but inside disengage range");
    static_cast<void>(behavior.update(hunter, playerHandle, {400, 200}, true, profile,
                                      attacks, openGrid, 16));
    expect(hunter.state() == creatures::BehaviorState::idle && !hunter.target(),
           "Chase disengages only beyond the larger disengage range");

    auto attacker = factory.create(enemyId, {200, 200}, gameplay::FacingDirection::left);
    static_cast<void>(behavior.update(attacker, playerHandle, {214, 200}, true, profile,
                                      attacks, openGrid, 16));
    const auto attackStarted = behavior.update(
        attacker, playerHandle, {214, 200}, true, profile, attacks, openGrid, 16);
    expect(attackStarted.attackStarted &&
               attacker.state() == creatures::BehaviorState::attack &&
               attacker.activeAttack()->definition->id == attackId,
           "Chase selects the first available attack whose configured range matches");
    const auto lockedFeet = attacker.feetPosition();
    const auto lockedFacing = attacker.facing();
    attacker.move(-1, -1, openGrid, 16);
    expect(attacker.feetPosition() == lockedFeet && attacker.facing() == lockedFacing,
           "Attack state locks enemy movement and facing");
    behavior.finishAttack(attacker, profile);
    expect(attacker.state() == creatures::BehaviorState::chase &&
               attacker.cooldownFor(attackId)->remainingTicks == 20,
           "finishing an attack returns to Chase and starts definition cooldown");
    expect(behavior.selectAttack(attacker, {214, 200}, attacks) == nullptr,
           "an attack cannot be selected while its runtime cooldown is active");

    underworld::world::CollisionGrid wallGrid(64, 64);
    wallGrid.setSolid(13, 12, true);
    auto blocked = factory.create(enemyId, {204, 200});
    static_cast<void>(behavior.update(blocked, playerHandle, {250, 200}, true, profile,
                                      attacks, wallGrid, 16));
    const int blockedStart = blocked.feetPosition().x;
    for (int tick = 0; tick < 20; ++tick) {
        static_cast<void>(behavior.update(blocked, playerHandle, {250, 200}, true, profile,
                                          attacks, wallGrid, 16));
    }
    expect(blocked.feetPosition().x < 213 && blocked.feetPosition().x >= blockedStart,
           "Chase reuses tile collision and cannot cross a solid wall");

    const auto soldierAttack = creatures::makeSoldierSwordAttackDefinition();
    const auto skullAttack = creatures::makeSkullArrowAttackDefinition();
    const auto skullProjectile = creatures::makeSkullArrowProjectileDefinition();
    expect(soldierAttack.id == creatures::soldierSwordAttackId() &&
               soldierAttack.kind == gameplay::AttackKind::meleeHitbox &&
               soldierAttack.meleeHitboxes.has_value(),
           "Evil Soldier melee behavior is declared by reusable AttackDefinition data");
    expect(skullAttack.id == creatures::skullArrowAttackId() &&
               skullAttack.kind == gameplay::AttackKind::projectile &&
               skullAttack.projectileDefinitionId == creatures::skullArrowProjectileId() &&
               skullProjectile.canonicalFacing == gameplay::FacingDirection::right,
           "Skull ranged behavior references a reusable right-oriented projectile definition");
    expect(creatures::makeSoldierEnemyDefinition().attackIds.front() ==
               creatures::soldierSwordAttackId() &&
               creatures::makeSkullEnemyDefinition().attackIds.front() ==
               creatures::skullArrowAttackId(),
           "concrete enemy definitions select attacks without enemy-type branches");

    underworld::game::EnemyVisualCatalog visualCatalog;
    const auto idleClip = makeTestClip("enemy.idle", true);
    const auto walkClip = makeTestClip("enemy.walk", true);
    const auto deathClip = makeTestClip("enemy.death", false);
    const auto attackClip = makeTestClip("enemy.attack", false);
    underworld::game::DirectionalAnimationClips idleClips{idleClip, idleClip, idleClip};
    underworld::game::DirectionalAnimationClips walkClips{walkClip, walkClip, walkClip};
    underworld::game::DirectionalAnimationClips deathClips{deathClip, deathClip, deathClip};
    underworld::game::DirectionalAnimationClips attackClips{
        attackClip, attackClip, attackClip};
    underworld::game::EnemyVisualSet visualSet{
        visualId, idleClips, walkClips, deathClips,
        {{simulation::DefinitionId{"visual.attack.test"}, attackClips}}};
    visualCatalog.add(std::move(visualSet));
    expect(visualCatalog.find(visualId) != nullptr && visualCatalog.ids().size() == 1,
           "EnemyVisualCatalog exposes renderer data independently from EnemyDefinition");

    auto visualEnemy = factory.create(enemyId, {200, 200});
    underworld::game::EnemyVisualInstance enemyVisual(
        visualEnemy.handle(), visualCatalog.require(visualId));
    enemyVisual.update(visualEnemy, 0);
    expect(enemyVisual.animator().clip().id() == "enemy.idle",
           "enemy visual selects idle clip from BehaviorState");
    static_cast<void>(behavior.update(visualEnemy, playerHandle, {214, 200}, true,
                                      profile, attacks, openGrid, 16));
    static_cast<void>(behavior.update(visualEnemy, playerHandle, {214, 200}, true,
                                      profile, attacks, openGrid, 16));
    enemyVisual.update(visualEnemy, 0);
    expect(enemyVisual.animator().clip().id() == "enemy.attack",
           "enemy visual resolves attack clips through visualActionId");
    enemyVisual.update(visualEnemy, 2);
    const auto enemyMarkers = enemyVisual.consumeMarkerEvents();
    enemyVisual.update(visualEnemy, 1);
    expect(enemyMarkers.size() == 1 && enemyMarkers[0].marker == "future.marker" &&
               enemyVisual.consumeMarkerEvents().empty(),
           "enemy attack markers are emitted once through the shared Animator timeline");
    static_cast<void>(visualEnemy.combatant().health.applyDamage(99));
    static_cast<void>(behavior.update(visualEnemy, playerHandle, {214, 200}, true,
                                      profile, attacks, openGrid, 16));
    enemyVisual.update(visualEnemy, 0);
    expect(enemyVisual.animator().clip().id() == "enemy.death" &&
               visualEnemy.state() == creatures::BehaviorState::dead,
           "depleted enemy enters Dead and selects a non-looping death clip");
    enemyVisual.update(visualEnemy, deathClip->durationTicks());
    expect(enemyVisual.animator().finished(),
           "enemy death visual exposes completion for despawn and handle release");
}

void testCreatureCombatIntegration() {
    namespace creatures = underworld::game::gameplay::creatures;
    namespace gameplay = underworld::game::gameplay;
    namespace simulation = underworld::simulation;

    gameplay::AttackCatalog attacks;
    attacks.add(gameplay::makePlayerSwordAttackDefinition());
    attacks.add(creatures::makeSoldierSwordAttackDefinition());
    attacks.add(creatures::makeSkullArrowAttackDefinition());
    gameplay::ProjectileCatalog projectileDefinitions;
    projectileDefinitions.add(gameplay::makePlayerArrowProjectileDefinition());
    projectileDefinitions.add(creatures::makeSkullArrowProjectileDefinition());
    creatures::BehaviorCatalog behaviors;
    behaviors.add(creatures::makeSoldierBehaviorProfile());
    behaviors.add(creatures::makeSkullBehaviorProfile());
    creatures::EnemyCatalog definitions;
    definitions.add(creatures::makeSoldierEnemyDefinition());
    definitions.add(creatures::makeSkullEnemyDefinition());
    const std::array visuals{creatures::soldierVisualId(), creatures::skullVisualId()};
    simulation::EntityHandlePool handles;
    const auto playerHandle = handles.create();
    gameplay::Player player({0}, playerHandle, {200, 200});
    creatures::EnemyFactory factory(
        handles, definitions, behaviors, attacks, projectileDefinitions, visuals);
    auto soldier = factory.create(creatures::soldierEnemyId(), {220, 200},
                                  gameplay::FacingDirection::left);
    auto skull = factory.create(creatures::skullEnemyId(), {260, 200},
                                gameplay::FacingDirection::left);
    creatures::EnemyBehaviorSystem behavior;
    underworld::world::CollisionGrid grid(64, 64);
    simulation::EventBuffer events;
    gameplay::CombatSystem combat;

    const auto& soldierProfile = behaviors.require(creatures::soldierBehaviorId());
    static_cast<void>(behavior.update(soldier, playerHandle, player.feetPosition(), true,
                                      soldierProfile, attacks, grid, 16));
    const auto soldierAttack = behavior.update(
        soldier, playerHandle, player.feetPosition(), true,
        soldierProfile, attacks, grid, 16);
    expect(soldierAttack.attackStarted && soldier.activeAttack() &&
               soldier.activeAttack()->key.owner == soldier.handle() &&
               soldier.activeAttack()->key.localInstance == 1,
           "Soldier FSM begins a reusable owner-scoped melee attack");
    const auto& active = *soldier.activeAttack();
    const auto direction = gameplay::directionVector(active.lockedFacing);
    gameplay::Hitbox soldierHit{
        active.definition->meleeHitboxes->forFacing(active.lockedFacing).at(
            soldier.feetPosition()),
        active.key, gameplay::Faction::enemy, active.definition->damage,
        direction.x * active.definition->damage.knockbackPixels,
        direction.y * active.definition->damage.knockbackPixels, true};
    const auto playerResolution = combat.resolve(soldierHit, player.combatTarget(), events);
    player.applyKnockback(playerResolution.requestedKnockbackX,
                          playerResolution.requestedKnockbackY, grid, 16);
    expect(playerResolution.damaged && player.health().current == 4 &&
               player.feetPosition().x < 200,
           "Soldier melee damages and knocks back Player through the shared CombatSystem");

    for (std::uint32_t tick = 0; tick < gameplay::CombatSystem::invulnerabilityDurationTicks;
         ++tick) {
        gameplay::tickInvulnerability(soldier.combatant());
    }
    gameplay::Hitbox playerHit{
        soldier.hurtbox().bounds, {playerHandle, 1}, gameplay::Faction::player,
        attacks.require(gameplay::playerSwordAttackId()).damage, 4, 0, true};
    const auto soldierResolution = combat.resolve(playerHit, soldier.combatTarget(), events);
    soldier.applyKnockback(soldierResolution.requestedKnockbackX,
                           soldierResolution.requestedKnockbackY, grid, 16);
    expect(soldierResolution.damaged && soldier.combatant().health.current == 2 &&
               soldier.feetPosition().x == 224,
           "Player melee damages the same generic enemy combat target and applies knockback");

    for (std::uint32_t tick = 0; tick < gameplay::CombatSystem::invulnerabilityDurationTicks;
         ++tick) {
        gameplay::tickInvulnerability(player.combatant());
    }
    const auto& skullProfile = behaviors.require(creatures::skullBehaviorId());
    static_cast<void>(behavior.update(skull, playerHandle, player.feetPosition(), true,
                                      skullProfile, attacks, grid, 16));
    const auto skullAttack = behavior.update(
        skull, playerHandle, player.feetPosition(), true,
        skullProfile, attacks, grid, 16);
    expect(skullAttack.attackStarted && skull.activeAttack() &&
               skull.activeAttack()->definition->kind == gameplay::AttackKind::projectile,
           "Skull FSM selects ranged AttackDefinition without a Skull-specific system");
    gameplay::ProjectileSystem projectiles(handles, projectileDefinitions);
    const auto& skullProjectile = projectileDefinitions.require(
        *skull.activeAttack()->definition->projectileDefinitionId);
    const auto spawn = gameplay::addOffset(
        skull.feetPosition(), skullProjectile.spawnOffsets.forFacing(skull.facing()));
    [[maybe_unused]] const auto projectileHandle = projectiles.spawn(
        skull.activeAttack()->key, skull.combatant().faction, skullProjectile.id,
        spawn, skull.facing(), skull.activeAttack()->definition->damage);
    std::array<gameplay::CombatTargetRef, 2> projectileTargets{
        soldier.combatTarget(), player.combatTarget()};
    std::vector<gameplay::CombatResolution> projectileResolutions;
    for (int tick = 0; tick < 32 && !projectiles.projectiles().empty(); ++tick) {
        projectiles.update(grid, 16, projectileTargets, combat, events,
                           projectileResolutions);
    }
    expect(player.health().current == 3 && soldier.combatant().health.current == 2 &&
               projectiles.projectiles().empty(),
           "Skull projectile ignores Enemy faction targets, damages Player, and is removed");

    expect(gameplay::clockwiseQuarterTurns(gameplay::FacingDirection::right,
                                            gameplay::FacingDirection::right) == 0 &&
               gameplay::clockwiseQuarterTurns(gameplay::FacingDirection::right,
                                                gameplay::FacingDirection::down) == 1 &&
               gameplay::clockwiseQuarterTurns(gameplay::FacingDirection::right,
                                                gameplay::FacingDirection::left) == 2 &&
               gameplay::clockwiseQuarterTurns(gameplay::FacingDirection::right,
                                                gameplay::FacingDirection::up) == 3,
           "canonical-right Skull arrow maps deterministically to all four quarter turns");

    soldier.combatant().invulnerabilityTicks = 0;
    soldier.combatant().health.current = 1;
    playerHit.attack.localInstance = 2;
    const auto lethal = combat.resolve(playerHit, soldier.combatTarget(), events);
    static_cast<void>(behavior.update(soldier, playerHandle, player.feetPosition(), true,
                                      soldierProfile, attacks, grid, 16));
    expect(lethal.defeated && soldier.state() == creatures::BehaviorState::dead &&
               !soldier.hurtbox().enabled,
           "depleted Soldier enters Dead once and disables its Hurtbox");
    const auto soldierDefeats = std::count_if(
        events.events().begin(), events.events().end(),
        [&](const simulation::SimulationEvent& event) {
            const auto* defeated = std::get_if<simulation::EntityDefeated>(&event);
            return defeated != nullptr && defeated->target == soldier.handle();
        });
    expect(soldierDefeats == 1,
           "Soldier death emits the generic EntityDefeated event exactly once");
    const auto staleHandle = soldier.handle();
    expect(handles.destroy(staleHandle) && !handles.valid(staleHandle),
           "death lifecycle releases the runtime handle after visual completion");

    simulation::PlayerCommand defeatedCommand{};
    defeatedCommand.playerId = player.id();
    defeatedCommand.movement = {1, 0};
    player.combatant().health.current = 0;
    const auto defeatedFeet = player.feetPosition();
    player.update(defeatedCommand, grid, 16);
    expect(player.feetPosition() == defeatedFeet &&
               player.motionState() == gameplay::PlayerMotionState::idle,
           "depleted Player blocks movement and actions without introducing game-over UI");

    auto soldierA = factory.create(creatures::soldierEnemyId(), {450, 500});
    auto soldierB = factory.create(creatures::soldierEnemyId(), {550, 500});
    auto ranged = factory.create(creatures::skullEnemyId(), {500, 580});
    const auto stableA = soldierA.handle();
    const auto stableB = soldierB.handle();
    const auto stableRanged = ranged.handle();
    int attacksStarted = 0;
    for (int tick = 0; tick < 360; ++tick) {
        for (auto* enemy : {&soldierA, &soldierB, &ranged}) {
            const auto& runtimeProfile = behaviors.require(
                enemy->definition().behaviorProfileId);
            const auto result = behavior.update(
                *enemy, playerHandle, {500, 500}, true, runtimeProfile,
                attacks, grid, 16);
            if (result.attackStarted) {
                ++attacksStarted;
                behavior.finishAttack(*enemy, runtimeProfile);
            }
        }
    }
    expect(attacksStarted > 3 && handles.valid(stableA) && handles.valid(stableB) &&
               handles.valid(stableRanged),
           "two Soldiers and one Skull update for hundreds of deterministic ticks safely");
    expect(soldierA.cooldownFor(creatures::soldierSwordAttackId()) !=
               soldierB.cooldownFor(creatures::soldierSwordAttackId()),
           "multiple Soldiers own independent cooldown state despite sharing definitions");
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

void testItemsInventoryAndWallet() {
    using namespace underworld;
    using namespace game::gameplay;

    ItemCatalog catalog;
    catalog.add(makeLifePotionDefinition());
    const simulation::DefinitionId equipmentId{"item.test_sword"};
    catalog.add({equipmentId, simulation::DefinitionId{"visual.test_sword"},
                 ItemCategory::equipment, 1, std::nullopt});
    expect(catalog.require(lifePotionItemId()).stackLimit == 66 &&
               catalog.find(equipmentId) != nullptr,
           "item catalog finds immutable definitions by stable id");

    bool duplicateRejected = false;
    try { catalog.add(makeLifePotionDefinition()); }
    catch (const std::logic_error&) { duplicateRejected = true; }
    expect(duplicateRejected, "item catalog rejects duplicate ids");
    bool unknownRejected = false;
    try { static_cast<void>(catalog.require(simulation::DefinitionId{"item.unknown"})); }
    catch (const std::out_of_range&) { unknownRejected = true; }
    expect(unknownRejected, "item catalog rejects unknown ids");
    bool invalidStackRejected = false;
    try {
        catalog.add({simulation::DefinitionId{"item.invalid"},
                     simulation::DefinitionId{"visual.invalid"},
                     ItemCategory::misc, 0, std::nullopt});
    } catch (const std::invalid_argument&) { invalidStackRejected = true; }
    expect(invalidStackRejected, "item definition requires positive stack limit");
    bool invalidUseRejected = false;
    try {
        catalog.add({simulation::DefinitionId{"item.invalid_use"},
                     simulation::DefinitionId{"visual.invalid_use"},
                     ItemCategory::consumable, 2,
                     ItemUseDefinition{ItemUseKind::restoreHealth, 0}});
    } catch (const std::invalid_argument&) { invalidUseRejected = true; }
    expect(invalidUseRejected, "item definition rejects invalid use data");

    ItemContainer stacks(3, catalog);
    expect(stacks.add(lifePotionItemId(), 60).accepted == 60, "item add creates first stack");
    const auto split = stacks.add(lifePotionItemId(), 10);
    expect(split.accepted == 10 && split.remainder == 0 &&
               stacks.slot(0)->quantity == 66 && stacks.slot(1)->quantity == 4,
           "stack limit 66 merges first then fills the lowest empty slot");

    ItemContainer equipment(2, catalog);
    const auto equipmentAdd = equipment.add(equipmentId, 2);
    expect(equipmentAdd.accepted == 2 && equipment.slot(0)->quantity == 1 &&
               equipment.slot(1)->quantity == 1,
           "equipment stack limit one occupies separate slots");

    PlayerInventory inventory(catalog);
    expect(inventory.items().capacity() == 30, "PlayerInventory has exactly 30 slots");
    ItemContainer futureBank(50, catalog);
    expect(futureBank.capacity() == 50, "generic ItemContainer supports future capacity 50");
    expect(inventory.items().add(equipmentId, 30).accepted == 30,
           "all player inventory slots can be occupied");
    const auto full = inventory.items().add(lifePotionItemId(), 10);
    expect(full.accepted == 0 && full.remainder == 10,
           "full inventory preserves the complete add remainder");

    ItemContainer partial(1, catalog);
    static_cast<void>(partial.add(lifePotionItemId(), 63));
    const auto partialAdd = partial.add(lifePotionItemId(), 10);
    expect(partialAdd.accepted == 3 && partialAdd.remainder == 7,
           "partial add reports accepted and remainder without item loss");
    expect(partial.remove(lifePotionItemId(), 2) == 2 &&
               partial.slot(0)->quantity == 64,
           "remove subtracts a smaller requested quantity");
    expect(partial.remove(lifePotionItemId(), 64) == 64 && !partial.slot(0),
           "remove clears a slot instead of preserving a zero quantity stack");
    expect(partial.remove(lifePotionItemId(), 7) == 0,
           "remove beyond available quantity reports only what existed");

    ItemContainer source(2, catalog);
    ItemContainer destination(1, catalog);
    static_cast<void>(source.add(lifePotionItemId(), 20));
    static_cast<void>(destination.add(lifePotionItemId(), 62));
    const std::uint64_t before = source.count(lifePotionItemId()) +
                                 destination.count(lifePotionItemId());
    const auto transferred = source.transferTo(destination, lifePotionItemId(), 10);
    const std::uint64_t after = source.count(lifePotionItemId()) +
                                destination.count(lifePotionItemId());
    expect(transferred == 4 && source.count(lifePotionItemId()) == 16 &&
               destination.count(lifePotionItemId()) == 66 && before == after,
           "partial transfer conserves items and leaves remainder in the source");

    Wallet wallet;
    expect(wallet.gold() == 0 && wallet.addGold(1) == 0 && wallet.gold() == 1,
           "wallet credits gold outside inventory");
    expect(wallet.addGold(std::numeric_limits<std::uint64_t>::max()) == 1 &&
               wallet.gold() == std::numeric_limits<std::uint64_t>::max(),
           "wallet saturation reports overflow without wrapping");

    Health health(5);
    static_cast<void>(health.applyDamage(3));
    expect(health.restore(2) == 2 && health.current == 4,
           "health restore applies a positive amount");
    expect(health.restore(9) == 1 && health.current == health.maximum &&
               health.restore(2) == 0,
           "health restore clamps at maximum and reports no-op at full health");
}

void testPickupsQuickSlotsAndInventoryOverlay() {
    using namespace underworld;
    using namespace game::gameplay;
    ItemCatalog catalog;
    catalog.add(makeLifePotionDefinition());
    PlayerItems items(catalog);
    Health health(5);
    simulation::EntityHandlePool handles;
    const auto player = handles.create();
    simulation::EventBuffer events;

    const PickupDefinition heartDefinition{
        simulation::DefinitionId{"pickup.heart"}, simulation::DefinitionId{"visual.heart"},
        {-4, -4, 8, 8}, HealthPickup{2}};
    WorldPickup fullHeart(handles.create(), heartDefinition, {10, 10});
    auto collection = collectPickup(fullHeart, player, {6, 6, 8, 8}, health,
                                    items.inventory().items(), items.wallet(), handles, events);
    expect(!collection.collected && handles.valid(fullHeart.handle()),
           "heart at full health remains alive in the world");
    static_cast<void>(health.applyDamage(3));
    collection = collectPickup(fullHeart, player, {6, 6, 8, 8}, health,
                               items.inventory().items(), items.wallet(), handles, events);
    expect(collection.amount == 2 && health.current == 4 &&
               !handles.valid(fullHeart.handle()),
           "heart heals damaged player and invalidates its handle");

    const PickupDefinition moneyDefinition{
        simulation::DefinitionId{"pickup.money"}, simulation::DefinitionId{"visual.money"},
        {-4, -4, 8, 8}, CurrencyPickup{1}};
    WorldPickup money(handles.create(), moneyDefinition, {20, 10});
    const auto inventoryBefore = items.inventory().items().count(lifePotionItemId());
    collection = collectPickup(money, player, {16, 6, 8, 8}, health,
                               items.inventory().items(), items.wallet(), handles, events);
    expect(collection.amount == 1 && items.wallet().gold() == 1 &&
               items.inventory().items().count(lifePotionItemId()) == inventoryBefore,
           "money pickup credits Wallet and leaves inventory unchanged");

    const PickupDefinition potionDefinition{
        simulation::DefinitionId{"pickup.life_potion"},
        simulation::DefinitionId{"visual.life_potion"}, {-4, -4, 8, 8},
        ItemPickup{lifePotionItemId(), 10}};
    ItemContainer nearlyFull(1, catalog);
    static_cast<void>(nearlyFull.add(lifePotionItemId(), 62));
    WorldPickup potion(handles.create(), potionDefinition, {30, 10});
    collection = collectPickup(potion, player, {26, 6, 8, 8}, health, nearlyFull,
                               items.wallet(), handles, events);
    expect(collection.amount == 4 && std::get<ItemPickup>(potion.payload()).quantity == 6 &&
               handles.valid(potion.handle()),
           "partial world item pickup keeps its uncollected quantity and handle");
    const auto* pickupEvent = std::get_if<simulation::PickupCollected>(&events.events().back());
    expect(pickupEvent != nullptr && pickupEvent->amount == 4 && pickupEvent->itemId &&
               *pickupEvent->itemId == lifePotionItemId(),
           "PickupCollected reports the quantity actually collected");

    static_cast<void>(items.inventory().items().add(lifePotionItemId(), 3));
    items.quickSlots().bind(0, lifePotionItemId());
    items.quickSlots().bind(2, lifePotionItemId());
    expect(QuickSlotBindings::slotCount == 4 && items.quickSlots().binding(0) &&
               items.quickSlots().binding(2),
           "quick slots provide exactly four definition-id bindings");
    static_cast<void>(health.restore(99));
    const auto countAtFull = items.inventory().items().count(lifePotionItemId());
    expect(!items.useQuickSlot(0, catalog, health).applied &&
               items.inventory().items().count(lifePotionItemId()) == countAtFull,
           "quick slot does not consume an item when use has no effect");
    static_cast<void>(health.applyDamage(3));
    expect(items.useQuickSlot(0, catalog, health).healthRestored == 2 &&
               items.inventory().items().count(lifePotionItemId()) == countAtFull - 1,
           "quick slot finds, applies, and consumes its bound item");
    static_cast<void>(items.inventory().items().remove(lifePotionItemId(), 99));
    expect(!items.useQuickSlot(0, catalog, health).applied && items.quickSlots().binding(0),
           "out-of-stock quick slot remains bound and is a safe no-op");

    platform::ActionEdgeBuffer edges;
    platform::InputState input;
    edges.pushQuickSlot(0);
    edges.applyNext(input);
    expect(input.quickSlot1Pressed, "quick slot edge is delivered once");
    edges.applyNext(input);
    expect(!input.quickSlot1Pressed, "held/repeated ticks do not recreate quick slot edges");
    edges.pushQuickSlot(0);
    edges.pushQuickSlot(2);
    edges.applyNext(input);
    game::CommandBuilder commands;
    const auto command = commands.build(1, {0}, input);
    expect(command.actions.quickSlotPressed == 0,
           "lowest quick slot index wins when multiple edges share a tick");
    edges.pushInteract();
    edges.pushToggleInventory();
    edges.pushQuickSlot(3);
    edges.clear();
    input = {};
    edges.applyNext(input);
    expect(!input.interactPressed && !input.toggleInventoryPressed &&
               !input.quickSlot4Pressed,
           "focus-loss clear removes interaction inventory and quick-slot ghost actions");

    InventoryOverlayState overlay;
    overlay.toggle();
    overlay.moveSelection(-1, -1);
    expect(overlay.open() && overlay.selection() == 0,
           "inventory selection clamps at top-left");
    for (int index = 0; index < 20; ++index) { overlay.moveSelection(1, 0); }
    expect(overlay.selection() == 9, "inventory selection clamps at right edge");
    overlay.moveSelection(0, 1);
    expect(overlay.selection() == 19, "inventory selection moves down by ten");
    overlay.moveSelection(0, 1);
    overlay.moveSelection(0, 1);
    expect(overlay.selection() == 29, "inventory selection clamps at bottom edge");
    overlay.moveSelection(-1, 0);
    expect(overlay.selection() == 28, "inventory selection moves left within its row");

    InventoryOverlayState routedOverlay;
    PlayerItems routedItems(catalog);
    world::CollisionGrid emptyGrid(8, 8);
    Player routedPlayer({3}, handles.create(), {32, 32});
    simulation::PlayerCommand openCommand{};
    openCommand.playerId = routedPlayer.id();
    openCommand.movement = {1, 0};
    openCommand.actions.primaryAttackPressed = true;
    openCommand.actions.toggleInventoryPressed = true;
    const auto beforeFeet = routedPlayer.feetPosition();
    const bool blocked = routeInventoryCommand(routedOverlay, openCommand, routedItems,
                                                catalog, routedPlayer.health());
    if (!blocked) { routedPlayer.update(openCommand, emptyGrid, 16); }
    expect(blocked && routedPlayer.feetPosition() == beforeFeet &&
               routedPlayer.actionState() == PlayerActionState::none,
           "open inventory routes movement and attack away from Player gameplay");
}

void testViewModelAndWorldObjects() {
    using namespace underworld;
    using namespace game::gameplay;
    ItemCatalog items;
    items.add(makeLifePotionDefinition());
    PlayerItems playerItems(items);
    static_cast<void>(playerItems.inventory().items().add(lifePotionItemId(), 5));
    static_cast<void>(playerItems.wallet().addGold(7));
    playerItems.quickSlots().bind(0, lifePotionItemId());
    simulation::EntityHandlePool handles;
    Player player({0}, handles.create(), {10, 10});
    static_cast<void>(player.health().applyDamage(2));
    InventoryOverlayState overlay;
    overlay.toggle();
    overlay.moveSelection(1, 1);
    auto view = game::buildGameViewModel(player, playerItems, items, overlay);
    expect(view.playerHealth == 3 && view.playerMaximumHealth == 5 && view.gold == 7 &&
               view.quickSlots[0].quantity == 5 && view.inventory[0].quantity == 5 &&
               view.inventoryOpen && view.inventorySelection == 11,
           "GameViewModel snapshots health wallet quick slots and 10x3 inventory state");
    view.gold = 999;
    view.inventory[0].quantity = 999;
    expect(playerItems.wallet().gold() == 7 &&
               playerItems.inventory().items().count(lifePotionItemId()) == 5,
           "mutating UI snapshot cannot mutate gameplay ownership");

    WorldObjectCatalog objects;
    const simulation::DefinitionId chestId{"object.chest"};
    const simulation::DefinitionId crateId{"object.crate"};
    const simulation::DefinitionId strongCrateId{"object.test_strong_crate"};
    objects.add({chestId, simulation::DefinitionId{"visual.object.chest"},
                 ObjectInteractionDefinition{{-12, -12, 24, 24}},
                 ObjectContainerDefinition{5}, std::nullopt});
    objects.add({crateId, simulation::DefinitionId{"visual.object.crate"}, std::nullopt,
                 std::nullopt, ObjectDestructibleDefinition{2, {-8, -24, 16, 24}}});
    objects.add({strongCrateId, simulation::DefinitionId{"visual.object.strong_crate"},
                 std::nullopt, std::nullopt,
                 ObjectDestructibleDefinition{7, {-8, -24, 16, 24}}});
    expect(objects.require(strongCrateId).destructible->maximumHealth == 7,
           "second destructible object is configured only through definition data");
    bool objectDuplicateRejected = false;
    try {
        objects.add({crateId, simulation::DefinitionId{"visual.duplicate"}, std::nullopt,
                     std::nullopt, ObjectDestructibleDefinition{1, {0, 0, 1, 1}}});
    } catch (const std::logic_error&) { objectDuplicateRejected = true; }
    expect(objectDuplicateRejected, "world object catalog rejects duplicate ids");

    WorldObjectFactory factory(handles, objects, items);
    const std::array<ItemStack, 1> contents{{{lifePotionItemId(), 10}}};
    std::vector<WorldObjectInstance> chests;
    chests.push_back(factory.create(chestId, {30, 10}, contents));
    ItemContainer destination(1, items);
    static_cast<void>(destination.add(lifePotionItemId(), 62));
    auto interaction = interactNearest({30, 10}, {25, 5, 10, 10}, destination, chests);
    expect(interaction.object == chests[0].handle() && interaction.itemsTransferred == 4 &&
               chests[0].state() == WorldObjectState::opened &&
               chests[0].contents()->count(lifePotionItemId()) == 6,
           "chest opens and preserves contents during partial transfer to full inventory");
    ItemContainer emptyDestination(2, items);
    interaction = interactNearest({0, 0}, {-5, -5, 10, 10}, emptyDestination, chests);
    expect(!interaction.object && chests[0].contents()->count(lifePotionItemId()) == 6,
           "chest interaction out of range has no effect");

    std::vector<WorldObjectInstance> tied;
    tied.push_back(factory.create(chestId, {9, 10}));
    tied.push_back(factory.create(chestId, {11, 10}));
    const auto expectedTie = tied[0].handle().index < tied[1].handle().index
                                 ? tied[0].handle() : tied[1].handle();
    interaction = interactNearest({10, 10}, {0, 0, 20, 20}, emptyDestination, tied);
    expect(interaction.object == expectedTie,
           "nearest interaction uses EntityHandle as deterministic distance tie-break");

    auto crate = factory.create(crateId, {50, 50});
    expect(crate.combatant() && crate.combatant()->faction == Faction::environment &&
               factionsCanDamage(Faction::player, Faction::environment) &&
               !factionsCanDamage(Faction::enemy, Faction::environment) &&
               !factionsCanDamage(Faction::player, Faction::player) &&
               !factionsCanDamage(Faction::enemy, Faction::enemy),
           "environment damage policy allows only Player attacks and preserves friendly fire rules");
    CombatSystem combat;
    simulation::EventBuffer events;
    Hitbox hit{{42, 26, 16, 24}, {player.entityHandle(), 1}, Faction::player,
               {1, 0}, 0, 0, true};
    auto resolution = combat.resolve(hit, crate.combatTarget(), events);
    expect(resolution.damaged && crate.combatant()->health.current == 1,
           "crate receives Player damage through CombatSystem");
    combat.finishAttack(hit.attack);
    for (std::uint32_t tick = 0; tick < CombatSystem::invulnerabilityDurationTicks; ++tick) {
        tickInvulnerability(*crate.combatant());
    }
    hit.attack.localInstance = 2;
    resolution = combat.resolve(hit, crate.combatTarget(), events);
    const auto defeatCount = std::count_if(events.events().begin(), events.events().end(),
        [&](const simulation::SimulationEvent& event) {
            const auto* defeated = std::get_if<simulation::EntityDefeated>(&event);
            return defeated && defeated->target == crate.handle();
        });
    expect(resolution.defeated && defeatCount == 1 && crate.syncDestructionState() &&
               !crate.hurtbox().enabled && crate.state() == WorldObjectState::destroying,
           "crate defeat emits once, disables hurtbox, and enters destruction lifecycle");
    const auto crateHandle = crate.handle();
    expect(crate.completeDestruction(handles) && !handles.valid(crateHandle) &&
               crate.state() == WorldObjectState::destroyed,
           "completed object destruction invalidates runtime handle");
}

underworld::game::maps::MapData makePhase8TestMap(
    std::string mapName = "map.test.room_a", std::string target = "map.test.room_b") {
    namespace gameplay = underworld::game::gameplay;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;
    maps::MapData map;
    map.id = simulation::MapId{std::move(mapName)};
    map.width = 4; map.height = 3; map.tileSize = 16;
    map.tileReferences.push_back({simulation::DefinitionId{"tileset.dungeon"}, 10,
                                  underworld::world::TileFlags::none});
    maps::MapTileLayer layer{"ground", true, std::vector<std::optional<std::uint32_t>>(12)};
    layer.cells[0] = 0; layer.cells[5] = 0; map.layers.push_back(std::move(layer));
    map.collision.assign(12, 0); map.collision[3] = 1;
    map.playerSpawns.push_back({simulation::SpawnId{"entry.start"}, {16, 24},
                                gameplay::FacingDirection::down});
    map.playerSpawns.push_back({simulation::SpawnId{"entry.return"}, {32, 24},
                                gameplay::FacingDirection::left});
    map.enemies.push_back({{1}, gameplay::creatures::soldierEnemyId(), {40, 24},
                           gameplay::FacingDirection::left});
    map.objects.push_back({{2}, simulation::DefinitionId{"object.chest"}, {48, 24},
                           {{gameplay::lifePotionItemId(), 2}}});
    map.objects.push_back({{3}, simulation::DefinitionId{"object.crate"}, {56, 24}, {}});
    map.pickups.push_back({{4}, simulation::DefinitionId{"pickup.money"},
        simulation::DefinitionId{"visual.pickup.money"}, {24, 24}, {-5,-5,10,10},
        gameplay::CurrencyPickup{1}});
    map.pickups.push_back({{5}, simulation::DefinitionId{"pickup.life_potion"},
        simulation::DefinitionId{"visual.item.life_potion"}, {30, 24}, {-5,-5,10,10},
        gameplay::ItemPickup{gameplay::lifePotionItemId(), 4}});
    map.links.push_back({"exit", {55, 0, 9, 48}, simulation::MapId{std::move(target)},
                         simulation::SpawnId{"entry.return"}});
    return map;
}

void testPhase8PersistentMapsAndSave() {
    namespace gameplay = underworld::game::gameplay;
    namespace creatures = underworld::game::gameplay::creatures;
    namespace maps = underworld::game::maps;
    namespace save = underworld::game::save;
    namespace serialization = underworld::serialization;
    namespace simulation = underworld::simulation;

    const simulation::PersistentInstanceId invalid{};
    const simulation::PersistentInstanceId first{1}, second{2};
    expect(!invalid && first && first != second, "PersistentInstanceId reserves zero and compares strongly");
    const simulation::PersistentEntityKey roomAOne{simulation::MapId{"map.a"}, first};
    const simulation::PersistentEntityKey roomBOne{simulation::MapId{"map.b"}, first};
    expect(roomAOne != roomBOne && simulation::PersistentEntityKeyHash{}(roomAOne) !=
               simulation::PersistentEntityKeyHash{}(roomBOne),
           "persistent entity identity combines MapId and local instance id");

    serialization::ByteWriter writer;
    writer.writeU8(0x12); writer.writeU16(0x3456); writer.writeU32(0x789abcdeU);
    writer.writeU64(0x0123456789abcdefULL); writer.writeI32(-123456); writer.writeString("map.test");
    const auto bytes = writer.bytes();
    expect(bytes[1] == 0x56 && bytes[2] == 0x34 && bytes[3] == 0xde,
           "ByteWriter uses explicit little-endian encoding");
    serialization::ByteReader reader(bytes); std::uint8_t u8{}; std::uint16_t u16{};
    std::uint32_t u32{}; std::uint64_t u64{}; std::int32_t i32{}; std::string text;
    expect(reader.readU8(u8) && reader.readU16(u16) && reader.readU32(u32) &&
               reader.readU64(u64) && reader.readI32(i32) && reader.readString(text, 32) &&
               reader.remaining() == 0 && u8 == 0x12 && u16 == 0x3456 &&
               u32 == 0x789abcdeU && u64 == 0x0123456789abcdefULL && i32 == -123456 &&
               text == "map.test", "ByteReader roundtrips every Phase 8 primitive at exact boundary");
    expect(!reader.readU8(u8) && reader.failed(), "ByteReader rejects reads beyond the exact boundary");
    const std::array<std::uint8_t, 3> badString{{0xff,0xff,0xff}};
    serialization::ByteReader truncated(badString);
    expect(!truncated.readString(text, 8), "ByteReader rejects truncated and oversized string lengths without access violation");

    gameplay::ItemCatalog items; items.add(gameplay::makeLifePotionDefinition());
    gameplay::WorldObjectCatalog objects;
    objects.add({simulation::DefinitionId{"object.chest"}, simulation::DefinitionId{"visual.object.chest"},
                 gameplay::ObjectInteractionDefinition{{-12,-12,24,24}},
                 gameplay::ObjectContainerDefinition{5}, std::nullopt});
    objects.add({simulation::DefinitionId{"object.crate"}, simulation::DefinitionId{"visual.object.crate"},
                 std::nullopt, std::nullopt, gameplay::ObjectDestructibleDefinition{2,{-8,-24,16,24}}});
    gameplay::AttackCatalog attacks; attacks.add(creatures::makeSoldierSwordAttackDefinition());
    attacks.add(creatures::makeSkullArrowAttackDefinition());
    gameplay::ProjectileCatalog projectiles;
    projectiles.add(creatures::makeSkullArrowProjectileDefinition());
    creatures::BehaviorCatalog behaviors; behaviors.add(creatures::makeSoldierBehaviorProfile());
    behaviors.add(creatures::makeSkullBehaviorProfile());
    creatures::EnemyCatalog enemies; enemies.add(creatures::makeSoldierEnemyDefinition());
    enemies.add(creatures::makeSkullEnemyDefinition());
    maps::MapValidationCatalogs validation{&enemies,&objects,&items};

    auto map = makePhase8TestMap();
    expect(maps::validateMapData(map,&validation).valid,
           "MapData validates tiles collision spawns enemies objects pickups links and catalogs before runtime");
    auto duplicate = map; duplicate.pickups[0].id = duplicate.objects[0].id;
    expect(!maps::validateMapData(duplicate,&validation),
           "persistent ids use one unique namespace across all placement kinds");
    auto invalidReference = map; invalidReference.objects[0].initialContents[0].itemId = simulation::DefinitionId{"item.missing"};
    expect(!maps::validateMapData(invalidReference,&validation),
           "MapData rejects unknown item definition references before construction");
    auto invalidDimensions = map; invalidDimensions.width = 0;
    expect(!maps::validateMapData(invalidDimensions), "MapData rejects zero dimensions before allocation");

    const auto encoded = maps::serializeDmap(map);
    const auto decoded = maps::deserializeDmap(encoded,&validation);
    expect(decoded && maps::semanticallyEqual(map,decoded.data),
           "DMAP v1 provides a semantic MapData roundtrip");
    expect(encoded == maps::serializeDmap(decoded.data),
           "DMAP output is deterministic byte-for-byte after roundtrip");
    auto corrupt = encoded; corrupt[0] = 'X';
    expect(!maps::deserializeDmap(corrupt), "DMAP rejects wrong magic");
    corrupt = encoded; corrupt[4] = 2;
    expect(!maps::deserializeDmap(corrupt), "DMAP rejects unsupported major version");
    corrupt.assign(encoded.begin(), encoded.begin()+10);
    expect(!maps::deserializeDmap(corrupt), "DMAP rejects truncated headers");
    corrupt = encoded; corrupt.resize(corrupt.size()-1);
    expect(!maps::deserializeDmap(corrupt), "DMAP rejects declared-size and truncated chunk corruption");
    corrupt = encoded; corrupt[20] = 'Z'; corrupt[21] = 'Z'; corrupt[22] = 'Z'; corrupt[23] = 'Z';
    expect(!maps::deserializeDmap(corrupt), "DMAP rejects a missing required singleton chunk");
    corrupt = encoded; corrupt.insert(corrupt.end(), {'F','U','T','R',0,0,0,0,0,0,0,0});
    const std::uint64_t newSize = corrupt.size(); for(unsigned i=0;i<8;++i) corrupt[12+i]=static_cast<std::uint8_t>(newSize>>(i*8U));
    expect(static_cast<bool>(maps::deserializeDmap(corrupt)),
           "DMAP safely skips an unknown size-bounded future chunk");

    simulation::EntityHandlePool handles;
    const std::array visuals{creatures::soldierVisualId(), creatures::skullVisualId()};
    creatures::EnemyFactory enemyFactory(handles,enemies,behaviors,attacks,projectiles,visuals);
    gameplay::WorldObjectFactory objectFactory(handles,objects,items);
    maps::RuntimeWorldBuilder builder(validation,enemyFactory,objectFactory,handles,
        {{simulation::DefinitionId{"tileset.dungeon"},1}});
    auto runtime=builder.build(decoded.data,simulation::SpawnId{"entry.start"});
    expect(runtime && runtime.world->map().layerCount()==1 && runtime.world->enemies().size()==1 &&
               runtime.world->objects().size()==2 && runtime.world->pickups().size()==2 &&
               runtime.world->spawn().position==underworld::core::WorldPointI{16,24},
           "validated DMAP data transactionally constructs RuntimeMap and factory-backed entities with new handles");
    expect(runtime.world->objects()[0].persistentId==simulation::PersistentInstanceId{2} &&
               runtime.world->objects()[0].instance.handle(),
           "runtime entities keep persistent identity separate from generated EntityHandle");
    expect(!builder.build(decoded.data,simulation::SpawnId{"missing"}),
           "runtime builder fails clearly instead of silently spawning at zero");

    save::SaveData saved;
    saved.player.currentMapId=map.id; saved.player.position={20,22};
    saved.player.facing=gameplay::FacingDirection::up; saved.player.health=3;
    saved.player.inventory[0]=gameplay::ItemStack{gameplay::lifePotionItemId(),7};
    saved.player.inventory[29]=gameplay::ItemStack{gameplay::lifePotionItemId(),1};
    saved.player.gold=99; saved.player.quickSlots[0]=gameplay::lifePotionItemId();
    saved.world.set(save::ObjectDelta{{map.id,{2}},true,false,{{gameplay::lifePotionItemId(),1}}});
    saved.world.set(save::ObjectDelta{{map.id,{3}},false,true,{}});
    saved.world.set(save::PickupDelta{{map.id,{4}},true,std::nullopt});
    saved.world.set(save::PickupDelta{{map.id,{5}},false,2});
    save::SaveValidationCatalogs saveCatalogs{&items,{&map}};
    expect(save::validateSaveData(saved,saveCatalogs).empty(),
           "save validation accepts player state and explicit chest crate and pickup deltas");
    const auto saveBytes=save::serializeSave(saved);const auto loaded=save::deserializeSave(saveBytes,saveCatalogs);
    expect(loaded && loaded.data.player.health==3 && loaded.data.player.gold==99 &&
               loaded.data.player.inventory[0]->quantity==7 && !loaded.data.player.inventory[1] &&
               loaded.data.player.quickSlots[0] && loaded.data.world.objects.size()==2 &&
               loaded.data.world.pickups.size()==2,
           "DSAV v1 roundtrips Health ordered 30-slot inventory Wallet QuickSlots and world deltas");
    expect(saveBytes==save::serializeSave(loaded.data), "DSAV output is deterministic for equivalent state");
    gameplay::PlayerItems restoredItems(items);simulation::EntityHandlePool playerHandles;
    gameplay::Player restoredPlayer({7},playerHandles.create(),{1,1});
    std::string restoreError;
    expect(save::applyPlayer(loaded.data.player,restoredPlayer,restoredItems,items,restoreError)&&
               restoredPlayer.feetPosition()==underworld::core::WorldPointI{20,22}&&
               restoredPlayer.facing()==gameplay::FacingDirection::up&&restoredPlayer.health().current==3&&
               restoredItems.inventory().items().slot(0)->quantity==7&&
               restoredItems.inventory().items().slot(29)->quantity==1&&
               restoredItems.wallet().gold()==99&&restoredItems.quickSlots().binding(0),
           "DSAV player state restores exact slots Health position facing Wallet and QuickSlots");
    const auto recaptured=save::capturePlayer(restoredPlayer,restoredItems,map.id);
    expect(recaptured.inventory[29]->quantity==1&&recaptured.currentMapId==map.id,
           "runtime Player state can be captured back into the explicit save DTO");
    auto badSave=saved;badSave.player.inventory[0]->quantity=67;
    expect(!save::deserializeSave(save::serializeSave(badSave),saveCatalogs),
           "save load rejects inventory quantities above ItemDefinition stack limit");
    badSave=saved;badSave.world.pickups.push_back(badSave.world.pickups[0]);
    expect(!save::deserializeSave(save::serializeSave(badSave),saveCatalogs),
           "save load rejects duplicate persistent delta keys");

    std::string applyError;
    expect(save::applyWorldState(saved.world,*runtime.world,handles,items,applyError) &&
               runtime.world->objects().size()==1 && runtime.world->objects()[0].instance.state()==gameplay::WorldObjectState::opened &&
               runtime.world->objects()[0].instance.contents()->count(gameplay::lifePotionItemId())==1 &&
               runtime.world->pickups().size()==1 &&
               std::get<gameplay::ItemPickup>(runtime.world->pickups()[0].instance.payload()).quantity==2,
           "SessionWorldState reapplies opened chest destroyed crate collected and partial pickup deltas");

    const auto temporary=std::filesystem::temp_directory_path()/"underworld_phase8_save_test.sav";
    std::error_code ec;std::filesystem::remove(temporary,ec);std::filesystem::remove(temporary.wstring()+L".bak",ec);
    std::string fileError;
    expect(save::writeSaveAtomic(temporary,saved,fileError) && save::readSave(temporary,saveCatalogs),
           "atomic save writes through a temporary file and reloads from a test-only path");
    auto secondSave=saved;secondSave.player.gold=100;
    expect(save::writeSaveAtomic(temporary,secondSave,fileError) &&
               std::filesystem::exists(temporary.wstring()+L".bak"),
           "a second atomic save preserves one backup of the prior valid save");
    std::filesystem::remove(temporary,ec);std::filesystem::remove(temporary.wstring()+L".bak",ec);

    auto roomB=makePhase8TestMap("map.test.room_b","map.test.room_a");
    const auto dmapA=std::filesystem::temp_directory_path()/"underworld_room_a.dmap";
    const auto dmapB=std::filesystem::temp_directory_path()/"underworld_room_b.dmap";
    expect(maps::writeDmap(dmapA,map,fileError)&&maps::writeDmap(dmapB,roomB,fileError),
           "DMAP filesystem boundary writes deterministic authoring data separately from decoding");
    maps::MapCatalog mapCatalog;mapCatalog.add(map.id,dmapA);mapCatalog.add(roomB.id,dmapB);
    expect(mapCatalog.validateLinks(&validation).empty(),
           "MapCatalog resolves MapId resources and validates cross-map destination spawns");
    save::SessionWorldState sessionState=saved.world;
    maps::MapSession session(mapCatalog,validation,builder,handles,sessionState);
    const auto initial=session.activate(map.id,simulation::SpawnId{"entry.start"});
    expect(initial.changed&&session.world()&&session.world()->id()==map.id&&
               session.world()->objects().size()==1&&session.world()->pickups().size()==1,
           "MapSession applies in-memory deltas while transactionally activating its initial room");
    session.beginTick();
    expect(session.requestTransition({56,8,4,4})&&session.pending().has_value(),
           "map overlap queues a PendingMapTransition instead of mutating world during iteration");
    const auto toB=session.commitPending();
    expect(toB.changed&&session.world()->id()==roomB.id&&toB.spawn.id==simulation::SpawnId{"entry.return"},
           "pending transition validates and builds target before swapping to its explicit spawn");
    expect(!session.requestTransition({56,8,4,4}),
           "transition latch prevents an immediate return loop on the activation tick");
    session.beginTick();expect(session.requestTransition({56,8,4,4})&&session.commitPending().changed&&
                                   session.world()->id()==map.id&&session.world()->objects().size()==1,
           "Room A to B to A rebuilds original DMAP and reapplies the same SessionWorldState deltas");
    std::filesystem::remove(dmapA,ec);std::filesystem::remove(dmapB,ec);

    auto demoA = maps::makeDemoRoomA();
    auto demoB = maps::makeDemoRoomB();
    expect(maps::validateMapData(demoA, &validation) &&
               maps::validateMapData(demoB, &validation),
           "both deterministic demo rooms are complete validated MapData documents");
    const auto demoBytes = maps::serializeDmap(demoA);
    const auto demoDecoded = maps::deserializeDmap(demoBytes, &validation);
    auto demoRuntime = builder.build(demoDecoded.data, simulation::SpawnId{"entry.start"});
    expect(demoDecoded && demoRuntime && demoRuntime.world->objects().size() == 2 &&
               demoRuntime.world->pickups().size() == 2 &&
               demoRuntime.world->enemies()[0].instance.feetPosition() ==
                   demoA.enemies[0].position,
           "playable Room A is sourced through the real DMAP decode and runtime builder chain");
    demoA.enemies[0].position = {300, 222};
    auto movedRuntime = builder.build(demoA, simulation::SpawnId{"entry.start"});
    expect(movedRuntime && movedRuntime.world->enemies()[0].instance.feetPosition() ==
                               underworld::core::WorldPointI{300, 222},
           "runtime entity placement follows MapData without hard-coded spawn logic");

    auto& runtimeObjects = demoRuntime.world->objects();
    runtimeObjects[0].instance.open();
    static_cast<void>(runtimeObjects[0].instance.contents()->remove(
        gameplay::lifePotionItemId(), 1));
    runtimeObjects[1].instance.combatant()->health.current = 0;
    static_cast<void>(runtimeObjects[1].instance.syncDestructionState());
    static_cast<void>(runtimeObjects[1].instance.completeDestruction(handles));
    runtimeObjects.erase(runtimeObjects.begin() + 1);
    auto& runtimePickups = demoRuntime.world->pickups();
    static_cast<void>(handles.destroy(runtimePickups[0].instance.handle()));
    runtimePickups.erase(runtimePickups.begin());
    std::get<gameplay::ItemPickup>(runtimePickups[0].instance.payload()).quantity = 2;
    save::SessionWorldState captured;
    save::captureWorldState(demoDecoded.data, *demoRuntime.world, captured);
    expect(captured.findObject({demoDecoded.data.id, {1}})->opened &&
               captured.findObject({demoDecoded.data.id, {1}})->remainingContents[0].quantity == 1,
           "runtime mutation capture records opened Chest and exact remaining contents");
    expect(captured.findObject({demoDecoded.data.id, {2}})->destroyed,
           "runtime mutation capture records a removed destructible as destroyed");
    expect(captured.findPickup({demoDecoded.data.id, {3}})->collected &&
               *captured.findPickup({demoDecoded.data.id, {4}})->remainingQuantity == 2,
           "runtime mutation capture records collected and partially collected pickups");

    const auto demoPathA = std::filesystem::temp_directory_path()/"underworld_demo_room_a.dmap";
    const auto demoPathB = std::filesystem::temp_directory_path()/"underworld_demo_room_b.dmap";
    expect(maps::writeDmap(demoPathA, maps::makeDemoRoomA(), fileError) &&
               maps::writeDmap(demoPathB, demoB, fileError),
           "playable demo resources are generated deterministically through the DMAP writer");
    maps::MapCatalog demoCatalog;
    demoCatalog.add(maps::demoRoomAId(), demoPathA);
    demoCatalog.add(maps::demoRoomBId(), demoPathB);
    save::SessionWorldState automaticState;
    maps::MapSession demoSession(demoCatalog, validation, builder, handles, automaticState);
    expect(demoSession.activate(maps::demoRoomAId(), simulation::SpawnId{"entry.start"}).changed,
           "MapSession activates playable Room A by MapId");
    const auto oldEnemyHandle = demoSession.world()->enemies()[0].instance.handle();
    const auto oldChestHandle = demoSession.world()->objects()[0].instance.handle();
    demoSession.world()->objects()[0].instance.open();
    static_cast<void>(demoSession.world()->objects()[0].instance.contents()->remove(
        gameplay::lifePotionItemId(), 1));
    auto& sessionObjects = demoSession.world()->objects();
    sessionObjects[1].instance.combatant()->health.current = 0;
    static_cast<void>(sessionObjects[1].instance.syncDestructionState());
    static_cast<void>(sessionObjects[1].instance.completeDestruction(handles));
    sessionObjects.erase(sessionObjects.begin() + 1);
    auto& sessionPickups = demoSession.world()->pickups();
    static_cast<void>(handles.destroy(sessionPickups[0].instance.handle()));
    sessionPickups.erase(sessionPickups.begin());
    demoSession.beginTick();
    expect(demoSession.requestTransition({960, 144, 16, 16}) &&
               demoSession.commitPending().changed &&
               demoSession.world()->id() == maps::demoRoomBId(),
           "Room A queues and commits a safe transition to playable Room B");
    expect(!handles.valid(oldEnemyHandle) && !handles.valid(oldChestHandle),
           "world swap invalidates every old map-local runtime handle");
    demoSession.beginTick();
    expect(demoSession.requestTransition({32, 144, 16, 16}) &&
               demoSession.commitPending().changed &&
               demoSession.world()->id() == maps::demoRoomAId() &&
               demoSession.world()->objects().size() == 1 &&
               demoSession.world()->objects()[0].instance.state() ==
                   gameplay::WorldObjectState::opened &&
               demoSession.world()->objects()[0].instance.contents()->count(
                   gameplay::lifePotionItemId()) == 1 &&
               demoSession.world()->pickups().size() == 1,
           "A to B to A automatically captures and reapplies Chest Crate and Pickup deltas");
    const auto* retainedWorld = demoSession.world();
    const auto failedRestore = demoSession.restore(
        simulation::MapId{"map.missing"}, automaticState);
    expect(!failedRestore.changed && demoSession.world() == retainedWorld &&
               demoSession.world()->id() == maps::demoRoomAId(),
           "failed transactional restore retains the active RuntimeWorld unchanged");

    demoSession.beginTick();
    expect(demoSession.requestTransition({960, 144, 16, 16}) &&
               demoSession.commitPending().changed,
           "session can leave modified Room A again before a cross-map save");
    save::SaveData crossMapSave;
    crossMapSave.player.currentMapId = maps::demoRoomBId();
    crossMapSave.player.position = {80, 160};
    crossMapSave.player.health = gameplay::Player::maximumHealth;
    crossMapSave.world = automaticState;
    save::SaveValidationCatalogs demoSaveCatalogs{&items, {&demoA, &demoB}};
    const auto crossMapReload = save::deserializeSave(
        save::serializeSave(crossMapSave), demoSaveCatalogs);
    expect(crossMapReload && crossMapReload.data.player.currentMapId == maps::demoRoomBId() &&
               crossMapReload.data.world.findObject({maps::demoRoomAId(), {1}}) &&
               crossMapReload.data.world.findPickup({maps::demoRoomAId(), {3}}),
           "saving in Room B retains the complete session delta set for modified Room A");

    gameplay::ProjectileSystem transientProjectiles(handles, projectiles);
    gameplay::CombatSystem transientCombat;
    const auto transientHandle = transientProjectiles.spawn(
        {{777, 1}, 1}, gameplay::Faction::enemy, creatures::skullArrowProjectileId(),
        {100, 100}, gameplay::FacingDirection::left, {1, 0});
    transientProjectiles.clear(transientCombat);
    expect(transientProjectiles.projectiles().empty() && !handles.valid(transientHandle),
           "map transient cleanup removes projectiles and invalidates their handles");

    underworld::platform::ActionEdgeBuffer persistenceEdges;
    underworld::platform::InputState persistenceInput;
    persistenceEdges.pushSaveGame(); persistenceEdges.pushLoadGame();
    persistenceEdges.applyNext(persistenceInput);
    expect(persistenceInput.saveGamePressed && persistenceInput.loadGamePressed,
           "logical F5 and F9 actions are buffered as one-shot fixed-tick edges");
    persistenceInput.clear(); persistenceEdges.applyNext(persistenceInput);
    expect(!persistenceInput.saveGamePressed && !persistenceInput.loadGamePressed,
           "held save and load keys cannot repeat without a new physical edge");
    persistenceEdges.pushSaveGame(); persistenceEdges.pushLoadGame(); persistenceEdges.clear();
    persistenceEdges.applyNext(persistenceInput);
    expect(!persistenceInput.saveGamePressed && !persistenceInput.loadGamePressed,
           "focus-loss clearing removes pending save and load actions");
    std::filesystem::remove(demoPathA, ec); std::filesystem::remove(demoPathB, ec);
}

void testPhase9EditorFoundation() {
    namespace editor = underworld::editor;
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;

    game::GameContentRegistry content;
    expect(content.enemies().find(gameplay::creatures::soldierEnemyId()) &&
               content.enemies().find(gameplay::creatures::skullEnemyId()) &&
               content.objects().find(simulation::DefinitionId{"object.chest"}) &&
               content.objects().find(simulation::DefinitionId{"object.crate"}) &&
               content.items().find(gameplay::lifePotionItemId()) &&
               content.pickup(simulation::DefinitionId{"pickup.money"}),
           "shared GameContentRegistry resolves runtime and editor definitions from one registration");
    expect(content.authoringDescriptors(game::AuthoringCategory::enemy).size() == 2 &&
               content.authoringDescriptors(game::AuthoringCategory::object).size() == 2 &&
               content.authoringDescriptors(game::AuthoringCategory::pickup).size() == 3,
           "authoring palette is derived from shared content descriptors");

    auto document = editor::EditorDocument::newMap(simulation::MapId{"map.editor.test"}, 4, 3);
    expect(document.dirty() && document.data().layers.size() == 1 &&
               document.data().layers[0].name == "Ground" && document.data().collision.size() == 12,
           "New Map creates Ground and collision authoring data without arbitrary content");
    document.markSaved();
    std::string error;
    const maps::MapTileReference tile{simulation::DefinitionId{"tileset.dungeon"}, 17,
                                      underworld::world::TileFlags::flipX};
    expect(document.execute(std::make_unique<editor::PaintTilesCommand>(0,
               std::vector<editor::TileCoordinate>{{0,0},{1,0},{1,0},{99,99}}, tile), error) &&
               document.data().layers[0].cells[0] && document.data().layers[0].cells[1] &&
               document.dirty() && document.history().size() == 1,
           "one tile brush stroke paints unique in-bounds cells as one command");
    expect(document.undo() && !document.data().layers[0].cells[0] &&
               document.data().tileReferences.empty(),
           "tile stroke undo restores cells and removes its unused tile reference");
    expect(document.redo(error) && document.data().layers[0].cells[1],
           "tile stroke redo restores the same authored tile");
    expect(document.undo(), "editor history can return before a stroke");
    expect(document.execute(std::make_unique<editor::SetCollisionCommand>(
               std::vector<editor::TileCoordinate>{{2,1}}, true), error) &&
               !document.history().canRedo(),
           "new edit after undo discards redo branch");
    expect(document.undo() && document.data().collision[6] == 0 && document.redo(error) &&
               document.data().collision[6] == 1,
           "collision paint supports undo and redo");

    const auto rectangle = editor::rectangleCells(-2, -1, 1, 1, document.data());
    expect(rectangle.size() == 4, "rectangle authoring clips to map bounds without resizing");
    auto compound = std::make_unique<editor::CompoundEditorCommand>("collision rectangle");
    compound->add(std::make_unique<editor::SetCollisionCommand>(rectangle, true));
    compound->add(std::make_unique<editor::SetCollisionCommand>(
        std::vector<editor::TileCoordinate>{{0,0}}, false));
    expect(document.execute(std::move(compound), error) && document.data().collision[0] == 0 &&
               document.data().collision[1] == 1 && document.undo() &&
               document.data().collision[1] == 0,
           "compound command applies atomically and reverts children in reverse order");
    expect(editor::collisionFloodCells(document.data(), 0, 0).size() == 11,
           "collision flood fill is iterative and finds a complete connected area");
    expect(editor::tileFloodCells(document.data(), 0, 0, 0).size() == 12,
           "tile flood fill is iterative and preserves empty-cell semantics");

    auto placementDocument = editor::EditorDocument(makePhase8TestMap());
    const auto firstNew = placementDocument.allocatePersistentId();
    expect(firstNew.value > 5, "persistent id allocation starts above every placement namespace id");
    expect(placementDocument.execute(std::make_unique<editor::DeleteEntityCommand>(
               editor::SelectionKind::pickup, simulation::PersistentInstanceId{5}), error),
           "delete placement command removes an existing entity");
    const auto afterDelete = placementDocument.allocatePersistentId();
    expect(afterDelete.value > firstNew.value && afterDelete.value != 5,
           "persistent id allocator is monotonic and never reuses deleted ids in-session");
    expect(placementDocument.undo() && placementDocument.data().pickups.back().id.value == 5 &&
               std::get<gameplay::ItemPickup>(placementDocument.data().pickups.back().payload).quantity == 4,
           "delete undo restores the same id definition payload and position");

    const auto before = placementDocument.data().enemies[0].position;
    const underworld::core::WorldPointI after{80, 64};
    expect(placementDocument.execute(std::make_unique<editor::MoveEntityCommand>(
               editor::SelectionKind::enemy, simulation::PersistentInstanceId{1}, before, after), error) &&
               placementDocument.data().enemies[0].position == after && placementDocument.undo() &&
               placementDocument.data().enemies[0].position == before && placementDocument.redo(error) &&
               placementDocument.data().enemies[0].position == after,
           "entity move commits once and roundtrips position through undo redo");
    const auto duplicateId = placementDocument.allocatePersistentId();
    const auto duplicate = editor::duplicatePlacement(placementDocument,
        editor::SelectionKind::object, simulation::PersistentInstanceId{2}, duplicateId, 16);
    expect(duplicate && std::get<maps::ObjectPlacement>(*duplicate).id == duplicateId &&
               std::get<maps::ObjectPlacement>(*duplicate).definitionId ==
                   placementDocument.data().objects[0].definitionId &&
               placementDocument.execute(std::make_unique<editor::PlaceEntityCommand>(*duplicate), error),
           "duplicate placement preserves authored data with a fresh persistent id and offset");

    const auto schemas = editor::propertySchemasFor(content, gameplay::creatures::soldierEnemyId());
    expect(schemas.size() == 2 && schemas[0].id.value() == "enemy.detection_range",
           "enemy authoring descriptor resolves typed schemas without sprite-specific inspector branches");
    expect(placementDocument.execute(std::make_unique<editor::SetPropertyCommand>(
               simulation::PersistentInstanceId{1}, schemas[0], editor::PropertyValue{std::int64_t{160}},
               content), error) &&
               std::get<std::int64_t>(placementDocument.propertyOverrides().at(1).at(schemas[0].id)) == 160,
           "valid typed property override commits through EditorCommand");
    expect(!placementDocument.execute(std::make_unique<editor::SetPropertyCommand>(
               simulation::PersistentInstanceId{1}, schemas[0], editor::PropertyValue{std::int64_t{-500}},
               content), error),
           "property command rejects integers below schema minimum before document mutation");
    editor::PropertySchema facing{editor::PropertyId{"test.facing"}, "Facing",
        editor::PropertyType::enumeration, {}, {}, {"Down","Up","Left","Right"}, {},
        editor::EnumPropertyValue{"Down"}};
    expect(!editor::validatePropertyValue(facing, editor::EnumPropertyValue{"Diagonal"},
               content, placementDocument, error),
           "enum property validation rejects values outside its explicit domain");
    editor::PropertySchema itemRef{editor::PropertyId{"test.item"}, "Item",
        editor::PropertyType::definitionReference, {}, {}, {}, game::AuthoringCategory::pickup,
        editor::DefinitionReference{simulation::DefinitionId{"pickup.money"},
                                    game::AuthoringCategory::pickup}};
    expect(!editor::validatePropertyValue(itemRef,
               editor::DefinitionReference{gameplay::creatures::soldierEnemyId(),
                                           game::AuthoringCategory::enemy},
               content, placementDocument, error),
           "DefinitionRef validation rejects a reference from the wrong category");
    expect(placementDocument.execute(std::make_unique<editor::SetPropertyCommand>(
               simulation::PersistentInstanceId{1}, schemas[0], std::nullopt, content), error) &&
               !placementDocument.propertyOverrides().contains(1) && placementDocument.undo() &&
               placementDocument.propertyOverrides().contains(1),
           "Reset to Default removes an override and undo restores its previous typed value");

    auto regionDocument = editor::EditorDocument::newMap(simulation::MapId{"map.region"}, 8, 8);
    const auto regionId = regionDocument.allocatePersistentId();
    expect(regionDocument.execute(std::make_unique<editor::PlaceEntityCommand>(
               editor::RegionPlacement{regionId, "arena", {16,16,32,32}}), error) &&
               regionDocument.execute(std::make_unique<editor::ResizeRegionCommand>(
                   regionId, underworld::world::AabbI{16,16,32,32},
                   underworld::world::AabbI{8,8,48,40}), error) &&
               regionDocument.regions()[0].bounds.width == 48 && regionDocument.undo() &&
               regionDocument.regions()[0].bounds.width == 32,
           "experimental region create resize and undo use persistent authoring identity");
    expect(!regionDocument.saveAs(std::filesystem::temp_directory_path() / "region-not-written.dmap",
               content, error),
           "DMAP 1.0 save explicitly blocks experimental regions instead of silently dropping them");

    auto roundtripMap = makePhase8TestMap("map.editor.roundtrip", "map.other");
    const auto temporary = std::filesystem::temp_directory_path() / "underworld_editor_roundtrip.dmap";
    editor::EditorDocument roundtrip(roundtripMap);
    expect(roundtrip.saveAs(temporary, content, error) && !roundtrip.dirty(),
           "EditorDocument Save As validates and writes through the existing DMAP writer");
    const auto reopened = editor::EditorDocument::open(temporary, content, error);
    expect(reopened && maps::semanticallyEqual(roundtripMap, reopened->data()) &&
               !reopened->dirty() && reopened->filePath() == temporary,
           "DMAP v1 open save reopen preserves semantic MapData equality");
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
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
        testRendererPrimitives();
        testImagesAndBlits();
        testAlphaBlending();
        testAnimationAndSpriteAnchor();
        testBitmapFontMapping();
        testAssetCache();
        testWicDecoder();
        testWorldCoordinates();
        testTilesAndLayers();
        testRuntimeMap();
        testCameraAndCulling();
        testCollisionGridAndAabb();
        testCollisionMovement();
        testInputAndPlayerCommands();
        testPlayerMovementAndFacing();
        testPlayerCollision();
        testPlayerVisualAndCameraFollow();
        testActionCommandsAndPlayerAttackState();
        testEntityHandlesAndActorOrder();
        testCombatSystem();
        testProjectilesAndEffects();
        testPhase6CombatGeneralization();
        testCreatureDefinitionsAndBehavior();
        testCreatureCombatIntegration();
        testItemsInventoryAndWallet();
        testPickupsQuickSlotsAndInventoryOverlay();
        testViewModelAndWorldObjects();
        testPhase8PersistentMapsAndSave();
        testPhase9EditorFoundation();
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
