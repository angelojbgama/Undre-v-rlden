#include "engine/core/color_rgba8.h"
#include "engine/core/coordinates.h"
#include "engine/core/fixed_timestep.h"
#include "engine/core/game_metrics.h"
#include "engine/core/image_data.h"
#include "engine/assets/asset_manager.h"
#include "engine/platform/image_decoder.h"
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
#include "engine/world/collision.h"
#include "engine/world/collision_grid.h"
#include "engine/world/runtime_map.h"
#include "engine/world/tile.h"
#include "engine/world/tile_layer.h"
#include "game/command_builder.h"
#include "game/gameplay/player.h"
#include "game/player_visual.h"

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

void testPlayerMovementAndFacing() {
    namespace gameplay = underworld::game::gameplay;
    underworld::world::CollisionGrid openGrid(256, 256);
    gameplay::Player right({0}, {1000, 1000});
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

    gameplay::Player directions({0}, {1000, 1000});
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

    gameplay::Player freeLeft({0}, {1000, 1000});
    gameplay::Player freeUp({0}, {1000, 1000});
    gameplay::Player freeDown({0}, {1000, 1000});
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

    gameplay::Player oneSecond({0}, {1000, 1000});
    for (std::uint64_t tick = 1; tick <= 60; ++tick) {
        oneSecond.update(movementCommand(tick, 1, 0), openGrid, 16);
    }
    expect(oneSecond.feetPosition().x == 1090 && oneSecond.feetPosition().y == 1000,
           "60 fixed ticks move the Player exactly 90 world pixels");

    gameplay::Player cardinal({0}, {1000, 1000});
    gameplay::Player diagonal({0}, {1000, 1000});
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

    const auto body = gameplay::Player({0}, {100, 80}).collisionBody();
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
    gameplay::Player againstWall({0}, {43, 40});
    againstWall.update(movementCommand(1, 1, 0), verticalWall, 16);
    expect(againstWall.feetPosition() == underworld::core::WorldPointI{43, 40} &&
               againstWall.lastMovement().blockedX,
           "Player collision body stops exactly against a vertical wall");

    gameplay::Player sliding({0}, {43, 40});
    sliding.update(movementCommand(1, 1, 1), verticalWall, 16);
    expect(sliding.feetPosition().x == 43 && sliding.feetPosition().y > 40 &&
               sliding.lastMovement().blockedX && !sliding.lastMovement().blockedY,
           "Player slides along a wall using separate-axis resolution");

    underworld::world::CollisionGrid horizontalWall(16, 16);
    for (int x = 0; x < horizontalWall.width(); ++x) {
        horizontalWall.setSolid(x, 3, true);
    }
    gameplay::Player aboveWall({0}, {43, 48});
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
    gameplay::Player corner({0}, {43, 48});
    corner.update(movementCommand(1, 1, 1), cornerGrid, 16);
    expect(corner.feetPosition() == underworld::core::WorldPointI{43, 48} &&
               corner.lastMovement().blockedX && corner.lastMovement().blockedY,
           "Player cannot pass diagonally through a solid corner");

    underworld::world::CollisionGrid corridorGrid(16, 8);
    for (int x = 0; x < corridorGrid.width(); ++x) {
        corridorGrid.setSolid(x, 1, true);
        corridorGrid.setSolid(x, 3, true);
    }
    gameplay::Player corridor({0}, {21, 43});
    for (std::uint64_t tick = 1; tick <= 40; ++tick) {
        corridor.update(movementCommand(tick, 1, 0), corridorGrid, 16);
    }
    expect(corridor.feetPosition().x > 70 && !corridor.lastMovement().blockedX,
           "Player body traverses a one-tile corridor");

    underworld::world::CollisionGrid boundary(8, 8);
    gameplay::Player edge({0}, {5, 8});
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
