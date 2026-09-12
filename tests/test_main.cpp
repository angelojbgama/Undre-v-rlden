#include "engine/core/color_rgba8.h"
#include "engine/core/coordinates.h"
#include "engine/core/fixed_timestep.h"
#include "engine/core/game_metrics.h"
#include "engine/core/image_data.h"
#include "engine/assets/asset_manager.h"
#include "engine/platform/image_decoder.h"
#include "engine/platform/action_edge_buffer.h"
#include "engine/platform/input_state.h"
#include "engine/platform/headless/headless_audit_platform.h"
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
#include "editor/editor_icons.h"
#include "editor/editor_playtest.h"
#include "editor/editor_app.h"
#include "editor/content_workspace_document.h"
#include "editor/visual_preview.h"
#include "game/command_builder.h"
#include "game/audit/audit_session.h"
#include "game/audit/audit_snapshot.h"
#include "game/audit/bmp_writer.h"
#include "game/game_content.h"
#include "game/gameplay/rpg/rewards.h"
#include "game/gameplay/rpg/reward_grants.h"
#include "game/gameplay/rpg/shops.h"
#include "game/gameplay/bank_overlay.h"
#include "game/gameplay/shop_overlay.h"
#include "game/gameplay/scenes/scene_controller.h"
#include "game/content/builtin_content.h"
#include "game/content/content_compiler.h"
#include "game/content/content_validation.h"
#include "game/content/content_json.h"
#include "game/content/content_workspace.h"
#include "game/content/content_source.h"
#include "editor/editor_launch.h"
#include "editor/editor_localization.h"
#include "editor/editor_preferences.h"
#include "editor/editor_ui.h"
#include "editor/editor_layout.h"
#include "editor/editor_text_layout.h"
#include "editor/asset_browser.h"
#include "editor/content_collection.h"
#include "editor/content_reference_tools.h"
#include "editor/visual_authoring.h"
#include "editor/scene_timeline.h"
#include "editor/world_project_document.h"
#include "engine/data/json.h"
#include "engine/core/utf8.h"
#include "game/game_session.h"
#include "game/game_view_model.h"
#include "game/actor_render_order.h"
#include "game/combat_debug.h"
#include "game/effect_system.h"
#include "game/enemy_visual.h"
#include "game/game_launch.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/attack_shapes.h"
#include "game/gameplay/combat_system.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/dialogue/dialogue_model.h"
#include "game/gameplay/dialogue/dialogue_session.h"
#include "game/gameplay/items.h"
#include "game/gameplay/npcs/npc_engine.h"
#include "game/gameplay/quests/quest_model.h"
#include "game/gameplay/quests/quest_state.h"
#include "game/gameplay/quests/quest_system.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/world_pickups.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_logic.h"
#include "game/gameplay/encounter_system.h"
#include "game/gameplay/projectile_system.h"
#include "game/gameplay/player.h"
#include "game/player_visual.h"
#include "game/runtime_visual_sync.h"
#include "game/training_puppet.h"
#include "game/world_object_visual.h"
#include "game/maps/dmap.h"
#include "game/maps/map_catalog.h"
#include "game/maps/official_maps.h"
#include "game/maps/map_composition.h"
#include "game/maps/reachability.h"
#include "game/maps/runtime_world.h"
#include "game/maps/authored_map.h"
#include "game/maps/authored_world.h"
#include "game/maps/region_tracker.h"
#include "game/save/save_data.h"
#include "game/presentation/presentation_effect_renderer.h"
#include "game/presentation/presentation_feedback_controller.h"
#include "game/presentation/presentation_effects.h"
#include "game/presentation/visual_content_loader.h"
#include "tools/map_compile_options.h"

#ifdef _WIN32
#include "engine/platform/win32/win32_clock.h"
#include "engine/platform/win32/win32_image_decoder.h"
#endif

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

const underworld::game::gameplay::rpg::PlayerProgressionDefinition& testProgression() {
    static const underworld::game::gameplay::rpg::PlayerProgressionDefinition definition{
        {"progression.player.default"}, {5}, {0, 100, 250}};
    return definition;
}

void expect(bool condition, std::string_view description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}

void removeSaveChunk(std::vector<std::uint8_t>& bytes, std::string_view tag) {
    const auto found = std::search(bytes.begin() + 20, bytes.end(), tag.begin(), tag.end());
    if (found == bytes.end()) return;
    const auto position = static_cast<std::size_t>(found - bytes.begin());
    std::uint64_t payloadSize{};
    for (unsigned index = 0; index < 8; ++index) {
        payloadSize |= static_cast<std::uint64_t>(bytes[position + 4 + index]) << (index * 8U);
    }
    const auto chunkSize = static_cast<std::size_t>(12 + payloadSize);
    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(position),
                bytes.begin() + static_cast<std::ptrdiff_t>(position + chunkSize));
    const auto newSize = static_cast<std::uint64_t>(bytes.size());
    for (unsigned index = 0; index < 8; ++index) {
        bytes[12 + index] = static_cast<std::uint8_t>(newSize >> (index * 8U));
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
    expect(font.glyphSource('@') != font.glyphSource('?'),
           "printable ASCII glyphs use a mapped source cell");
    expect(font.usesProceduralGlyph('@') && !font.usesProceduralGlyph('A'),
           "missing printable ASCII punctuation uses software bitmap glyphs");
    expect(font.glyphSource(0x2603U) == font.glyphSource('?'),
           "unknown Unicode glyph falls back to question mark");
    expect(font.glyphSource(0x00e3U) == font.glyphSource('a') &&
               font.glyphSource(0x00c7U) == font.glyphSource('C'),
           "bitmap font resolves Portuguese accented codepoints to extended glyph rendering");
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
    gameplay::Player right({0}, {0, 1}, {1000, 1000}, 5);
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

    gameplay::Player directions({0}, {0, 1}, {1000, 1000}, 5);
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

    gameplay::Player freeLeft({0}, {0, 1}, {1000, 1000}, 5);
    gameplay::Player freeUp({0}, {1, 1}, {1000, 1000}, 5);
    gameplay::Player freeDown({0}, {2, 1}, {1000, 1000}, 5);
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

    gameplay::Player oneSecond({0}, {0, 1}, {1000, 1000}, 5);
    for (std::uint64_t tick = 1; tick <= 60; ++tick) {
        oneSecond.update(movementCommand(tick, 1, 0), openGrid, 16);
    }
    expect(oneSecond.feetPosition().x == 1090 && oneSecond.feetPosition().y == 1000,
           "60 fixed ticks move the Player exactly 90 world pixels");

    gameplay::Player cardinal({0}, {0, 1}, {1000, 1000}, 5);
    gameplay::Player diagonal({0}, {1, 1}, {1000, 1000}, 5);
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

    gameplay::Player footprintPlayer({0}, {0, 1}, {100, 80}, 5);
    expect(footprintPlayer.collisionBody() ==
               underworld::world::AabbI{92, 72, 16, 8},
           "Player DOWN movement footprint is 16x8 and centered at the feet");
    footprintPlayer.relocate({100, 80}, gameplay::FacingDirection::left);
    expect(footprintPlayer.collisionBody() ==
               underworld::world::AabbI{90, 72, 18, 8},
           "Player LEFT movement footprint extends toward the facing side");
    footprintPlayer.relocate({100, 80}, gameplay::FacingDirection::right);
    expect(footprintPlayer.collisionBody() ==
               underworld::world::AabbI{92, 72, 18, 8},
           "Player RIGHT movement footprint extends toward the facing side");

    bool wrongPlayerRejected = false;
    try {
        right.update(movementCommand(3, 1, 0, {9}), openGrid, 16);
    } catch (const std::invalid_argument&) {
        wrongPlayerRejected = true;
    }
    expect(wrongPlayerRejected, "Player rejects commands addressed to another PlayerId");
}

void testGameSessionCommandBoundary() {
    using underworld::game::GameSession;
    GameSession first({0}, testProgression(), {1000, 1000});
    GameSession second({0}, testProgression(), {1000, 1000});
    const auto initial = first.player().subpixelPosition();
    const auto firstCommand = movementCommand(1, 1, 0);
    const auto secondCommand = movementCommand(2, 0, 0);
    first.tick(firstCommand);
    second.tick(firstCommand);
    first.tick(secondCommand);
    second.tick(secondCommand);
    expect(first.player().subpixelPosition() == initial &&
               first.player().subpixelPosition() == second.player().subpixelPosition() &&
               first.player().facing() == second.player().facing(),
           "uninitialized Sessions remain deterministic without a logical world");
}

void testPlayerCollision() {
    namespace gameplay = underworld::game::gameplay;
    underworld::world::CollisionGrid verticalWall(16, 16);
    for (int y = 0; y < verticalWall.height(); ++y) {
        verticalWall.setSolid(3, y, true);
    }
    gameplay::Player againstWall({0}, {0, 1}, {38, 40}, 5);
    againstWall.update(movementCommand(1, 1, 0), verticalWall, 16);
    expect(againstWall.feetPosition() == underworld::core::WorldPointI{38, 40} &&
               againstWall.lastMovement().blockedX,
           "Player directional collision body stops exactly against a vertical wall");

    gameplay::Player sliding({0}, {0, 1}, {40, 40}, 5);
    sliding.update(movementCommand(1, 1, 1), verticalWall, 16);
    expect(sliding.feetPosition().x == 40 && sliding.feetPosition().y > 40 &&
               sliding.lastMovement().blockedX && !sliding.lastMovement().blockedY,
           "Player diagonal input still slides along a wall by separate-axis resolution");

    underworld::world::CollisionGrid horizontalWall(16, 16);
    for (int x = 0; x < horizontalWall.width(); ++x) {
        horizontalWall.setSolid(x, 3, true);
    }
    gameplay::Player aboveWall({0}, {0, 1}, {43, 48}, 5);
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
    gameplay::Player corner({0}, {0, 1}, {43, 48}, 5);
    corner.update(movementCommand(1, 1, 1), cornerGrid, 16);
    expect(corner.feetPosition() == underworld::core::WorldPointI{43, 48} &&
               corner.lastMovement().blockedX && corner.lastMovement().blockedY,
           "Player cannot pass diagonally through a solid corner");

    underworld::world::CollisionGrid corridorGrid(16, 8);
    for (int x = 0; x < corridorGrid.width(); ++x) {
        corridorGrid.setSolid(x, 1, true);
        corridorGrid.setSolid(x, 3, true);
    }
    gameplay::Player corridor({0}, {0, 1}, {21, 43}, 5);
    for (std::uint64_t tick = 1; tick <= 40; ++tick) {
        corridor.update(movementCommand(tick, 1, 0), corridorGrid, 16);
    }
    expect(corridor.feetPosition().x > 70 && !corridor.lastMovement().blockedX,
           "Player body traverses a one-tile corridor");

    underworld::world::CollisionGrid boundary(8, 8);
    gameplay::Player edge({0}, {0, 1}, {5, 8}, 5);
    edge.update(movementCommand(1, -1, -1), boundary, 16);
    expect(edge.feetPosition() == underworld::core::WorldPointI{5, 8} &&
               edge.lastMovement().blockedX && edge.lastMovement().blockedY,
           "outside-map solid policy keeps the Player inside world bounds");
}


void testPlayerAuthoredMovementCollision() {
    namespace gameplay = underworld::game::gameplay;

    gameplay::DirectionalActorCollisionShapes shapes;
    gameplay::ActorCollisionShapeDefinition split;
    split.regions = {
        {-4, -4, 2, 4},
        {2, -4, 2, 4},
    };
    shapes.values = {split, split, split, split};

    gameplay::PlayerMovementConfig config;
    config.collisionShapes = shapes;
    config.cornerSlideMaxProbePixels = 0;

    underworld::world::CollisionGrid openGrid(16, 16);
    gameplay::Player player({0}, {0, 1}, {40, 40}, 5, config);
    const auto regions = player.collisionRegions();
    const std::array<underworld::world::AabbI, 1> gapObstacle{{
        {39, 35, 2, 4},
    }};

    expect(regions.size() == 2,
           "authored Player movement collision keeps compact regions");
    expect(!underworld::world::querySolidWorld(
                openGrid,
                std::span<const underworld::world::AabbI>{
                    regions.data(), regions.size()},
                16, gapObstacle).collides,
           "compound Player collision preserves holes between authored regions");
    expect(underworld::world::querySolidWorld(
                openGrid, player.collisionBody(), 16, gapObstacle).collides,
           "Player collisionBody remains a broad debug envelope");

    player.update(movementCommand(1, 0, -1), openGrid, 16, gapObstacle);
    expect(player.feetPosition().y < 40 && !player.lastMovement().blockedY,
           "Player movement resolves against exact compound authored regions");
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
    underworld::game::PlayerVisual::DirectionalClips hurt{
        makeTestClip("hurt.down", false), makeTestClip("hurt.up", false),
        makeTestClip("hurt.side", false)};
    underworld::game::PlayerVisual combatVisual(
        {makeTestClip("idle2.down", true), makeTestClip("idle2.up", true),
         makeTestClip("idle2.side", true)},
        {makeTestClip("walk2.down", true), makeTestClip("walk2.up", true),
         makeTestClip("walk2.side", true)},
        std::move(sword), std::move(bow), std::move(hurt));
    combatVisual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::right,
                        gameplay::PlayerActionState::swordAttack, 2);
    expect(combatVisual.animator().clip().id() == "sword.side" && !combatVisual.flipX(),
           "right sword attack preserves the authored attack sheet orientation");
    combatVisual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::left,
                        gameplay::PlayerActionState::swordAttack, 0);
    expect(combatVisual.animator().clip().id() == "sword.side" && combatVisual.flipX(),
           "left sword attack mirrors the authored right-facing attack sheet");
    expect(combatVisual.consumeMarkerEvents().size() == 1,
           "PlayerVisual forwards attack animation markers exactly once");
    combatVisual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::right,
                        gameplay::PlayerActionState::bowAttack, 0);
    expect(combatVisual.animator().clip().id() == "bow.side" && combatVisual.flipX(),
           "right bow attack mirrors the authored left-facing attack sheet");
    combatVisual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::left,
                        gameplay::PlayerActionState::bowAttack, 0);
    expect(combatVisual.animator().clip().id() == "bow.side" && !combatVisual.flipX(),
           "left bow attack preserves the authored bow sheet orientation");
    combatVisual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::up,
                        gameplay::PlayerActionState::bowAttack, 0);
    expect(combatVisual.animator().clip().id() == "bow.up" &&
               combatVisual.animator().frameIndex() == 0,
           "changing attack action starts the selected bow clip once at frame zero");
    combatVisual.update(gameplay::PlayerMotionState::idle, gameplay::FacingDirection::right,
                        gameplay::PlayerActionState::hurt, 0);
    expect(combatVisual.animator().clip().id() == "hurt.side" && combatVisual.flipX(),
           "Player damage selects the directional hurt visual and mirrors its side view");

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
    gameplay::Player player({0}, {0, 1}, {100, 100}, 5);
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
    player.beginHurt();
    const auto hurtPosition = player.subpixelPosition();
    player.update(actionCommand(5, false, false, 1, 0), grid, 16);
    expect(player.actionState() == gameplay::PlayerActionState::hurt &&
               player.subpixelPosition() == hurtPosition,
           "damaged Player enters hurt state and cannot move while hurt animation plays");
    expect(player.hurtbox().bounds == underworld::world::AabbI{94, 78, 14, 22} &&
               player.collisionBody() == underworld::world::AabbI{93, 92, 18, 8},
           "Player Hurtbox and directional movement footprint remain independent");
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

void testAttackTimeline() {
    namespace gameplay = underworld::game::gameplay;
    namespace simulation = underworld::simulation;

    gameplay::AttackDefinition definition;
    definition.id = simulation::DefinitionId{"attack.test.timeline"};
    definition.kind = gameplay::AttackKind::meleeHitbox;
    definition.damage = {1, 2};
    definition.totalTicks = 6;
    definition.visualActionId = simulation::DefinitionId{"visual.test.attack"};
    definition.meleeHitboxes = gameplay::DirectionalBoxes{};
    for (auto& box : definition.meleeHitboxes->values) { box = {0, 0, 1, 1}; }
    definition.timeline = {{2, gameplay::AttackTimelineEventKind::activateHitbox},
                           {4, gameplay::AttackTimelineEventKind::deactivateHitbox}};
    gameplay::AttackCatalog catalog;
    catalog.add(definition);
    simulation::EntityHandlePool handles;
    gameplay::AttackExecution execution{
        &catalog.require(definition.id), {handles.create(), 1},
        gameplay::FacingDirection::right};
    std::vector<gameplay::AttackTimelineEvent> events;
    execution.advance(events);
    expect(events.empty() && !execution.meleeHitboxActive,
           "attack timeline does not emit before its event tick");
    execution.advance(events);
    expect(events.size() == 1 && events[0].tick == 2 && execution.meleeHitboxActive,
           "attack timeline activates a melee hitbox at the declared tick");
    events.clear();
    execution.advance(events);
    expect(events.empty() && execution.meleeHitboxActive,
           "attack timeline keeps the melee hitbox active between events");
    execution.advance(events);
    expect(events.size() == 1 && events[0].tick == 4 && !execution.meleeHitboxActive,
           "attack timeline deactivates a melee hitbox at the declared tick");
    events.clear();
    execution.advance(events);
    execution.advance(events);
    expect(events.empty() && execution.finished,
           "attack timeline finishes exactly at totalTicks without repeating events");

    gameplay::AttackDefinition projectileDefinition;
    projectileDefinition.id = simulation::DefinitionId{"attack.test.projectile.timeline"};
    projectileDefinition.kind = gameplay::AttackKind::projectile;
    projectileDefinition.damage = {1, 1};
    projectileDefinition.totalTicks = 5;
    projectileDefinition.visualActionId = simulation::DefinitionId{"visual.test.projectile"};
    projectileDefinition.projectileDefinitionId =
        simulation::DefinitionId{"projectile.test"};
    projectileDefinition.timeline = {{3, gameplay::AttackTimelineEventKind::spawnProjectile}};
    catalog.add(projectileDefinition);
    gameplay::AttackExecution projectileExecution{
        &catalog.require(projectileDefinition.id), {handles.create(), 2},
        gameplay::FacingDirection::down};
    events.clear();
    projectileExecution.advance(events);
    projectileExecution.advance(events);
    expect(events.empty(), "projectile timeline does not spawn before its event tick");
    projectileExecution.advance(events);
    expect(events.size() == 1 &&
               events[0].kind == gameplay::AttackTimelineEventKind::spawnProjectile,
           "projectile timeline emits exactly one spawn event at its declared tick");
    projectileExecution.advance(events);
    projectileExecution.advance(events);
    expect(events.size() == 1, "projectile timeline does not repeat a spawn event");

    bool rejected = false;
    try {
        auto invalid = definition;
        invalid.timeline = {{6, gameplay::AttackTimelineEventKind::activateHitbox}};
        catalog.add(std::move(invalid));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "attack catalog rejects timeline events at or after totalTicks");
}

void testCombatSystem() {
    namespace gameplay = underworld::game::gameplay;
    namespace creatures = underworld::game::gameplay::creatures;
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
               bow.kind == gameplay::AttackKind::projectile &&
               sword.timeline == std::vector<gameplay::AttackTimelineEvent>{
                   {6, gameplay::AttackTimelineEventKind::activateHitbox},
                   {18, gameplay::AttackTimelineEventKind::deactivateHitbox}} &&
               bow.timeline == std::vector<gameplay::AttackTimelineEvent>{
                   {8, gameplay::AttackTimelineEventKind::spawnProjectile}},
           "Sword and Bow define gameplay timing independently from visuals");
    const auto soldierAttack = creatures::makeSoldierSwordAttackDefinition();
    const auto skullAttack = creatures::makeSkullArrowAttackDefinition();
    expect(soldierAttack.timeline == sword.timeline &&
               skullAttack.timeline == bow.timeline,
           "Soldier and Skull preserve the characterized gameplay attack timing");
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
    gameplay::Player player({9}, playerHandle, {200, 200}, 5);
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
    gameplay::Player player({0}, playerHandle, {200, 200}, 5);
    creatures::EnemyFactory factory(
        handles, definitions, behaviors, attacks, projectileDefinitions, visuals);
    auto soldier = factory.create(creatures::soldierEnemyId(), {220, 200},
                                  gameplay::FacingDirection::left);
    auto skull = factory.create(creatures::skullEnemyId(), {260, 200},
                                gameplay::FacingDirection::left);
    auto rearSoldier = factory.create(creatures::soldierEnemyId(), {194, 200},
                                      gameplay::FacingDirection::right);
    creatures::EnemyBehaviorSystem behavior;
    underworld::world::CollisionGrid grid(64, 64);
    simulation::EventBuffer events;
    gameplay::CombatSystem combat;

    const auto& playerSword = attacks.require(gameplay::playerSwordAttackId());
    const gameplay::Hitbox forwardSword{
        playerSword.meleeHitboxes->forFacing(gameplay::FacingDirection::right).at(
            player.feetPosition()),
        {playerHandle, 1}, gameplay::Faction::player, playerSword.damage,
        playerSword.damage.knockbackPixels, 0, true};
    const auto rearResolution = combat.resolve(
        forwardSword, rearSoldier.combatTarget(), events);
    expect(gameplay::overlaps(player.collisionBody(), rearSoldier.hurtbox().bounds) &&
               !rearResolution.damaged && rearSoldier.combatant().health.current ==
                   rearSoldier.combatant().health.maximum,
           "Player body overlap behind an enemy does not deal damage; only sword hitbox does");

    auto swordTarget = factory.create(creatures::soldierEnemyId(), {220, 200},
                                      gameplay::FacingDirection::left);
    const auto swordStart = swordTarget.feetPosition();
    const gameplay::Hitbox swordHit{
        playerSword.meleeHitboxes->forFacing(gameplay::FacingDirection::right).at(
            player.feetPosition()),
        {playerHandle, 2}, gameplay::Faction::player, playerSword.damage,
        playerSword.damage.knockbackPixels, 0, true};
    const auto swordResolution = combat.resolve(
        swordHit, swordTarget.combatTarget(), events);
    swordTarget.applyKnockback(swordResolution.requestedKnockbackX,
                               swordResolution.requestedKnockbackY, grid, 16);
    expect(swordResolution.damaged && swordResolution.requestedKnockbackX == 32 &&
               swordTarget.feetPosition().x == swordStart.x + 32,
           "Player sword damage applies the configured knockback to the enemy");

    const auto contactPlayerHandle = handles.create();
    gameplay::Player contactPlayer({1}, contactPlayerHandle, {500, 500}, 5);
    auto contactEnemy = factory.create(creatures::soldierEnemyId(), {494, 500},
                                       gameplay::FacingDirection::right);
    const gameplay::Hitbox contactHit{
        contactEnemy.collisionBody(),
        {contactEnemy.handle(), 2}, gameplay::Faction::enemy, {1, 32}, 32, 0, true};
    const auto contactResolution = combat.resolve(
        contactHit, contactPlayer.combatTarget(), events);
    contactPlayer.applyDamageKnockback(contactResolution.requestedKnockbackX,
                                       contactResolution.requestedKnockbackY,
                                       grid, 16);
    const auto contactStart = contactPlayer.feetPosition();
    contactPlayer.update(movementCommand(1, 0, 0, {1}), grid, 16);
    const auto contactMidpoint = contactPlayer.feetPosition();
    for (std::uint64_t tick = 2; tick <= 8; ++tick) {
        contactPlayer.update(movementCommand(tick, 0, 0, {1}), grid, 16);
    }
    expect(contactResolution.damaged && contactPlayer.health().current ==
               contactPlayer.health().maximum - 1 &&
               contactResolution.requestedKnockbackX == 32 &&
               contactResolution.requestedKnockbackY == 0 &&
               contactMidpoint.x > contactStart.x &&
               contactPlayer.feetPosition().x == contactStart.x + 32,
           "All Player damage uses a shared smooth 32-pixel knockback rule");

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
                 ItemCategory::equipment, 1, std::nullopt,
                 rpg::EquipmentDefinition{rpg::EquipmentSlot::armor, {}}});
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
    overlay.moveSelection(0, 1);
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
    Player routedPlayer({3}, handles.create(), {32, 32}, 5);
    simulation::PlayerCommand openCommand{};
    openCommand.playerId = routedPlayer.id();
    openCommand.movement = {1, 0};
    openCommand.actions.primaryAttackPressed = true;
    openCommand.actions.toggleInventoryPressed = true;
    const auto beforeFeet = routedPlayer.feetPosition();
    const bool blocked = routeInventoryCommand(routedOverlay, openCommand, routedItems,
                                                catalog, routedPlayer.health()).consumedTick;
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
    Player player({0}, handles.create(), {10, 10}, 5);
    static_cast<void>(player.health().applyDamage(2));
    InventoryOverlayState overlay;
    overlay.toggle();
    overlay.moveSelection(1, 1);
    auto view = game::buildGameViewModel(player, playerItems, items, overlay,
                                         game::gameplay::BankOverlayState{}, {5, 0});
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
    const auto destructionTicks = crate.definition().destructible->destructionDurationTicks;
    for (std::uint32_t tick = 0; tick + 1 < destructionTicks; ++tick) {
        crate.advanceDestructionTick();
    }
    expect(!crate.destructionComplete(),
           "crate remains logically destroying until its fixed destruction duration elapses");
    crate.advanceDestructionTick();
    expect(crate.destructionComplete() && destructionTicks == 28,
           "crate destruction uses the authored 7-frame by 4-tick logical duration");
    const auto crateHandle = crate.handle();
    expect(crate.completeDestruction(handles) && !handles.valid(crateHandle) &&
               crate.state() == WorldObjectState::destroyed,
           "completed object destruction invalidates runtime handle");
}

underworld::game::maps::MapData makeSyntheticMap(
    std::string mapName = "map.test.alpha", std::string target = "map.test.beta") {
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

std::optional<std::vector<std::uint8_t>> makeDmapV12WithoutObjectPersistence(
    std::vector<std::uint8_t> bytes) {
    constexpr std::size_t dmapHeaderSize = 20;
    constexpr std::size_t chunkHeaderSize = 12;
    constexpr std::size_t objectPrefixSize = 20; // id, definition, position

    const auto readU32 = [](const std::vector<std::uint8_t>& source, std::size_t offset,
                            std::uint32_t& value) {
        if (offset + 4 > source.size()) return false;
        value = static_cast<std::uint32_t>(source[offset]) |
                (static_cast<std::uint32_t>(source[offset + 1]) << 8U) |
                (static_cast<std::uint32_t>(source[offset + 2]) << 16U) |
                (static_cast<std::uint32_t>(source[offset + 3]) << 24U);
        return true;
    };
    const auto readU64 = [](const std::vector<std::uint8_t>& source, std::size_t offset,
                            std::uint64_t& value) {
        if (offset + 8 > source.size()) return false;
        value = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(source[offset + index]) << (index * 8U);
        }
        return true;
    };
    const auto writeU64 = [](std::vector<std::uint8_t>& target, std::size_t offset,
                             std::uint64_t value) {
        for (std::size_t index = 0; index < 8; ++index) {
            target[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }
    };

    if (bytes.size() < dmapHeaderSize || bytes[0] != 'D' || bytes[1] != 'M' ||
        bytes[2] != 'A' || bytes[3] != 'P') {
        return std::nullopt;
    }
    std::uint16_t headerSize = static_cast<std::uint16_t>(bytes[10]) |
                               (static_cast<std::uint16_t>(bytes[11]) << 8U);
    if (headerSize < dmapHeaderSize || headerSize > bytes.size()) return std::nullopt;

    for (std::size_t chunk = headerSize; chunk + chunkHeaderSize <= bytes.size();) {
        std::uint64_t payloadSize{};
        if (!readU64(bytes, chunk + 4, payloadSize) ||
            payloadSize > bytes.size() - chunk - chunkHeaderSize) return std::nullopt;
        const auto payload = chunk + chunkHeaderSize;
        if (bytes[chunk] != 'E' || bytes[chunk + 1] != 'N' || bytes[chunk + 2] != 'T' ||
            bytes[chunk + 3] != 'S') {
            chunk = payload + static_cast<std::size_t>(payloadSize);
            continue;
        }

        const std::size_t payloadEnd = payload + static_cast<std::size_t>(payloadSize);
        std::size_t cursor = payload;
        std::uint32_t enemyCount{};
        if (!readU32(bytes, cursor, enemyCount)) return std::nullopt;
        cursor += 4;
        constexpr std::size_t enemyRecordSize = 21;
        if (enemyCount > (payloadEnd - cursor) / enemyRecordSize) return std::nullopt;
        cursor += static_cast<std::size_t>(enemyCount) * enemyRecordSize;

        std::uint32_t objectCount{};
        if (!readU32(bytes, cursor, objectCount)) return std::nullopt;
        cursor += 4;
        std::vector<std::uint8_t> legacy;
        legacy.insert(legacy.end(), bytes.begin() + static_cast<std::ptrdiff_t>(payload),
                      bytes.begin() + static_cast<std::ptrdiff_t>(cursor));
        for (std::uint32_t index = 0; index < objectCount; ++index) {
            if (cursor + objectPrefixSize + 1 + 4 > payloadEnd) return std::nullopt;
            legacy.insert(legacy.end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                          bytes.begin() + static_cast<std::ptrdiff_t>(cursor + objectPrefixSize));
            cursor += objectPrefixSize + 1; // Drop the v1.5 persistence-policy byte.
            std::uint32_t stackCount{};
            if (!readU32(bytes, cursor, stackCount) ||
                stackCount > (payloadEnd - cursor - 4) / 8) return std::nullopt;
            const auto recordEnd = cursor + 4 + static_cast<std::size_t>(stackCount) * 8;
            legacy.insert(legacy.end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                          bytes.begin() + static_cast<std::ptrdiff_t>(recordEnd));
            cursor = recordEnd;
        }
        legacy.insert(legacy.end(), bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                      bytes.begin() + static_cast<std::ptrdiff_t>(payloadEnd));
        bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(payload),
                    bytes.begin() + static_cast<std::ptrdiff_t>(payloadEnd));
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(payload), legacy.begin(), legacy.end());
        writeU64(bytes, chunk + 4, legacy.size());
        bytes[6] = 2;
        bytes[7] = 0;
        for (std::size_t legacyChunk = headerSize;
             legacyChunk + chunkHeaderSize <= bytes.size();) {
            std::uint64_t legacyPayloadSize{};
            if (!readU64(bytes, legacyChunk + 4, legacyPayloadSize) ||
                legacyPayloadSize > bytes.size() - legacyChunk - chunkHeaderSize) {
                return std::nullopt;
            }
            if (bytes[legacyChunk] == 'S' && bytes[legacyChunk + 1] == 'C' &&
                bytes[legacyChunk + 2] == 'N' && bytes[legacyChunk + 3] == 'E') {
                bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(legacyChunk),
                            bytes.begin() + static_cast<std::ptrdiff_t>(
                                legacyChunk + chunkHeaderSize + legacyPayloadSize));
                break;
            }
            legacyChunk += chunkHeaderSize + static_cast<std::size_t>(legacyPayloadSize);
        }
        writeU64(bytes, 12, bytes.size());
        return bytes;
    }
    return std::nullopt;
}

void testBreakableProps() {
    namespace content = underworld::game::content;
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace maps = underworld::game::maps;
    namespace presentation = underworld::game::presentation;
    namespace simulation = underworld::simulation;
    namespace world = underworld::world;

    const auto authored = content::makeBuiltinAuthoredContent();
    const auto findObject = [&](std::string_view id) {
        return std::find_if(authored.objects.begin(), authored.objects.end(),
            [&](const auto& value) { return value.id.value() == id; });
    };
    const auto findVisual = [&](std::string_view id) {
        return std::find_if(authored.objectVisuals.begin(), authored.objectVisuals.end(),
            [&](const auto& value) { return value.id.value() == id; });
    };
    const auto findImage = [&](std::string_view id) {
        return std::find_if(authored.visualImages.begin(), authored.visualImages.end(),
            [&](const auto& value) { return value.id.value() == id; });
    };
    const auto findAnimation = [&](std::string_view id) {
        return std::find_if(authored.animations.begin(), authored.animations.end(),
            [&](const auto& value) { return value.id.value() == id; });
    };
    const auto crate = findObject("object.crate");
    const auto vase = findObject("object.vase");
    const auto stone = findObject("object.stone_block");
    const auto stoneVariant = findObject("object.stone_block_2");
    const auto fire = findObject("object.fire_block");
    expect(crate != authored.objects.end() && vase != authored.objects.end() &&
               stone != authored.objects.end() && stoneVariant != authored.objects.end() &&
               fire != authored.objects.end(),
           "builtin authored content exposes the crate vase stone variants and fire block");
    expect(findImage("image.object.crate") != authored.visualImages.end() &&
               findImage("image.object.breaking_crate") != authored.visualImages.end() &&
               findImage("image.object.vase") != authored.visualImages.end() &&
               findImage("image.object.breaking_vase") != authored.visualImages.end() &&
               findImage("image.object.stone_block") != authored.visualImages.end() &&
               findImage("image.object.stone_block_2") != authored.visualImages.end() &&
               findImage("image.object.block_destroyed") != authored.visualImages.end() &&
               findImage("image.object.fire_block") != authored.visualImages.end() &&
               findImage("image.object.fire_block_with_fire") != authored.visualImages.end() &&
               findImage("image.object.fire_block_destroyed") != authored.visualImages.end(),
           "breakable props use the real authored asset paths");
    expect(crate != authored.objects.end() && crate->destructible &&
               crate->destructible->destructionDurationTicks == 28 &&
               vase != authored.objects.end() && vase->destructible &&
               vase->destructible->destructionDurationTicks == 24 &&
               stone != authored.objects.end() && stone->destructible &&
               stoneVariant != authored.objects.end() && stoneVariant->destructible &&
               fire != authored.objects.end() && fire->destructible && fire->activation &&
               fire->activation->mode == gameplay::ObjectActivationMode::interactToggle,
           "breakable health timing and fire activation remain authored data");
    const auto crateVisual = findVisual("visual.object.crate");
    const auto vaseVisual = findVisual("visual.object.vase");
    const auto stoneVisual = findVisual("visual.object.stone_block");
    const auto fireVisual = findVisual("visual.object.fire_block");
    expect(crateVisual != authored.objectVisuals.end() &&
               crateVisual->destroyingAnimationId && vaseVisual != authored.objectVisuals.end() &&
               vaseVisual->destroyingAnimationId && stoneVisual != authored.objectVisuals.end() &&
               stoneVisual->destroyedAnimationId && fireVisual != authored.objectVisuals.end() &&
               fireVisual->activationInactiveAnimationId && fireVisual->activationActiveAnimationId &&
               fireVisual->destroyedAnimationId,
           "breakable visual profiles author destroying activation and destroyed states generically");
    expect(findAnimation("anim.object.crate.destroying") != authored.animations.end() &&
               findAnimation("anim.object.crate.destroying")->frames.size() == 7 &&
               findAnimation("anim.object.vase.destroying") != authored.animations.end() &&
               findAnimation("anim.object.vase.destroying")->frames.size() == 6 &&
               findAnimation("anim.object.fire_block.active") != authored.animations.end() &&
               findAnimation("anim.object.fire_block.active")->frames.size() == 4,
           "real breakable strips use their audited frame counts rather than assumed 16px frames");

    const auto json = content::encodeAuthoredContentJson(authored);
    const auto decoded = content::decodeAuthoredContentJson(json);
    expect(decoded.content && decoded.diagnostics.empty() &&
               content::encodeAuthoredContentJson(*decoded.content) == json,
           "breakable visual bindings survive canonical content JSON roundtrip");
    if (decoded.content) {
        const auto decodedVisual = std::find_if(decoded.content->objectVisuals.begin(),
            decoded.content->objectVisuals.end(), [&](const auto& value) {
                return value.id.value() == "visual.object.stone_block";
            });
        expect(decodedVisual != decoded.content->objectVisuals.end() &&
                   decodedVisual->destroyedAnimationId &&
                   decodedVisual->destroyedAnimationId->value() == "anim.object.stone_block.destroyed",
               "destroyedAnimationId is retained as an optional authored object visual field");
    }
    auto invalidVisual = authored;
    invalidVisual.objectVisuals.front().destroyedAnimationId =
        simulation::DefinitionId{"animation.missing.destroyed"};
    const auto invalidResult = content::compileContent(invalidVisual);
    expect(!invalidResult && std::any_of(invalidResult.report.diagnostics.begin(),
               invalidResult.report.diagnostics.end(), [](const auto& diagnostic) {
                   return diagnostic.kind == content::ContentKind::objectVisual &&
                          diagnostic.code == "unknown_reference" &&
                          diagnostic.field == "destroyedAnimationId";
               }),
           "content validation rejects an unknown generic destroyed visual reference");

    const auto compiled = content::compileBuiltinContentOrThrow();
    const auto& objects = compiled.objects();
    expect(objects.require({"object.vase"}).destructible->maximumHealth == 1 &&
               objects.require({"object.stone_block"}).destructible->maximumHealth == 2 &&
               objects.require({"object.stone_block_2"}).visualSetId ==
                   simulation::DefinitionId{"visual.object.stone_block_2"},
           "compiled vase and stone object definitions remain data-driven and distinct by visual set");

    class WideSyntheticDecoder final : public underworld::platform::ImageDecoder {
    public:
        underworld::core::ImageData decode(const std::filesystem::path& path) override {
            paths.push_back(path);
            constexpr int width = 304;
            constexpr int height = 192;
            return {width, height, static_cast<std::size_t>(width) * 4U,
                    std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4U,
                                              255U)};
        }
        std::vector<std::filesystem::path> paths;
    } decoder;
    presentation::VisualContentLoader loader(decoder);
    const auto loadedVisuals = loader.load(
        compiled, {std::filesystem::path{"game-assets"}, std::nullopt});
    expect(loadedVisuals && loadedVisuals.content->objects.require(
               {"visual.object.stone_block"}).destroyed &&
               loadedVisuals.content->objects.require(
                   {"visual.object.fire_block"}).activationActive &&
               loadedVisuals.content->objects.require(
                   {"visual.object.fire_block"}).destroyed,
           "visual loader publishes generic destroyed and fire activation clips");

    simulation::EntityHandlePool handles;
    gameplay::WorldObjectFactory factory(handles, objects, compiled.items());
    auto vaseInstance = factory.create({"object.vase"}, {32, 32});
    game::WorldObjectVisualSet vaseSet;
    vaseSet.id = {"visual.object.vase"};
    vaseSet.idle = makeTestClip("object.vase.idle", true);
    vaseSet.destroying = makeTestClip("object.vase.destroying", false);
    game::WorldObjectVisualInstance vaseVisualInstance(vaseInstance.handle(), vaseSet);
    const auto attacker = handles.create();
    gameplay::CombatSystem combat;
    simulation::EventBuffer events;
    const gameplay::Hitbox vaseHit{{24, 8, 16, 24}, {attacker, 1}, gameplay::Faction::player,
                                    {1, 0}, 0, 0, true};
    const auto vaseResolution = combat.resolve(vaseHit, vaseInstance.combatTarget(), events);
    expect(vaseResolution.damaged && vaseResolution.defeated &&
               vaseInstance.syncDestructionState() &&
               vaseInstance.state() == gameplay::WorldObjectState::destroying,
           "vase receives fatal damage through the existing CombatSystem and enters destroying");
    vaseVisualInstance.update(vaseInstance, 0);
    expect(vaseVisualInstance.animator().clip().id() == "object.vase.destroying",
           "vase selects its authored breaking animation without a vase-specific system");
    const auto vaseDuration = vaseInstance.definition().destructible->destructionDurationTicks;
    for (std::uint32_t tick = 0; tick < vaseDuration; ++tick) vaseInstance.advanceDestructionTick();
    expect(vaseInstance.destructionComplete() && vaseInstance.completeDestruction(handles) &&
               vaseInstance.state() == gameplay::WorldObjectState::destroyed,
           "vase completes its authored destruction duration and invalidates only its live handle");

    auto fireInstance = factory.create({"object.fire_block"}, {64, 32});
    game::WorldObjectVisualSet fireSet;
    fireSet.id = {"visual.object.fire_block"};
    fireSet.idle = makeTestClip("fire.inactive", true);
    fireSet.activationInactive = fireSet.idle;
    fireSet.activationActive = makeTestClip("fire.active", true);
    fireSet.destroyed = makeTestClip("fire.destroyed", true);
    game::WorldObjectVisualInstance fireVisualInstance(fireInstance.handle(), fireSet);
    fireVisualInstance.update(fireInstance, 0);
    expect(fireVisualInstance.animator().clip().id() == "fire.inactive",
           "fire block starts with its inactive visual state");
    expect(fireInstance.toggleActivation() && fireInstance.activationActive(),
           "fire block uses the existing interact-toggle activation capability");
    fireVisualInstance.update(fireInstance, 0);
    expect(fireVisualInstance.animator().clip().id() == "fire.active",
           "fire block activation selects the authored active animation");
    static_cast<void>(fireInstance.combatant()->health.applyDamage(2));
    expect(fireInstance.syncDestructionState(), "fire block enters the shared destruction lifecycle");
    fireInstance.advanceDestructionTick();
    expect(fireInstance.destructionComplete() && fireInstance.completeDestruction(handles),
           "fire block completes destruction through the shared WorldObject lifecycle");
    fireVisualInstance.update(fireInstance, 0);
    expect(fireVisualInstance.animator().clip().id() == "fire.destroyed",
           "destroyed visual has deterministic priority over an active fire state");

    maps::RuntimeWorld residueWorld(
        simulation::MapId{"map.test.breakables"}, world::RuntimeMap(4, 4, 16),
        {simulation::SpawnId{"entry.start"}, {16, 16}, gameplay::FacingDirection::down});
    game::WorldObjectVisualCatalog visualCatalog;
    game::WorldObjectVisualSet stoneSet;
    stoneSet.id = {"visual.object.stone_block"};
    stoneSet.idle = makeTestClip("stone.idle", true);
    stoneSet.destroyed = makeTestClip("stone.destroyed", true);
    visualCatalog.add(stoneSet);
    residueWorld.addDestroyedObjectResidue({99}, {"visual.object.stone_block"}, {32, 48});
    game::EnemyVisualCatalog enemyVisualCatalog;
    std::vector<game::EnemyVisualInstance> enemyVisuals;
    std::vector<game::WorldObjectVisualInstance> objectVisuals;
    std::vector<game::WorldObjectResidueVisualInstance> residueVisuals;
    const auto synchronized = game::synchronizeRuntimeWorldVisuals(
        residueWorld, enemyVisualCatalog, enemyVisuals, visualCatalog, objectVisuals,
        residueVisuals);
    expect(synchronized && objectVisuals.empty() && residueVisuals.size() == 1 &&
               residueVisuals.front().persistentId() == simulation::PersistentInstanceId{99} &&
               residueVisuals.front().animator().clip().id() == "stone.destroyed",
           "destroyed props render through a passive residue without retaining a live EntityHandle");
}

void testPhase8PersistentMapsAndSave() {
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace simulation = underworld::simulation;
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
    game::TilesetCatalog tilesets;
    tilesets.add({simulation::DefinitionId{"tileset.dungeon"}, "Dungeon", "Tileset/tileset.png", 16, 19, 12});
    maps::MapValidationCatalogs validation{&enemies,&objects,&items,&tilesets};

    auto map = makeSyntheticMap();
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
    const game::RuntimeTilesetCatalog runtimeTilesets(tilesets);
    maps::RuntimeWorldBuilder builder(validation,enemyFactory,objectFactory,handles,
        runtimeTilesets);
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
    saved.world.set(save::ObjectDelta{{map.id,{2}},true,false,{{gameplay::lifePotionItemId(),1}},std::nullopt});
    saved.world.set(save::ObjectDelta{{map.id,{3}},false,true,{},std::nullopt});
    saved.world.set(save::PickupDelta{{map.id,{4}},true,std::nullopt});
    saved.world.set(save::PickupDelta{{map.id,{5}},false,2});
    static_cast<void>(saved.dialogueFlags.set(simulation::DefinitionId{"dialogue.flag.zeta"}));
    static_cast<void>(saved.dialogueFlags.set(simulation::DefinitionId{"dialogue.flag.alpha"}));
    gameplay::rpg::PlayerProgressionCatalog progressions;
    progressions.add(testProgression());
    save::SaveValidationCatalogs saveCatalogs{&items,{&map},nullptr,&progressions};
    expect(save::validateSaveData(saved,saveCatalogs).empty(),
           "save validation accepts player state and explicit chest crate and pickup deltas");
    const auto saveBytes=save::serializeSave(saved);const auto loaded=save::deserializeSave(saveBytes,saveCatalogs);
    expect(loaded && loaded.data.player.health==3 && loaded.data.player.gold==99 &&
               loaded.data.player.inventory[0]->quantity==7 && !loaded.data.player.inventory[1] &&
               loaded.data.player.quickSlots[0] && loaded.data.world.objects.size()==2 &&
               loaded.data.world.pickups.size()==2 &&
               loaded.data.dialogueFlags.isSet(simulation::DefinitionId{"dialogue.flag.alpha"}) &&
               loaded.data.dialogueFlags.isSet(simulation::DefinitionId{"dialogue.flag.zeta"}),
           "DSAV v1.2 roundtrips player state world deltas and persistent dialogue flags");
    expect(saveBytes==save::serializeSave(loaded.data), "DSAV output is deterministic for equivalent state");
    gameplay::PlayerItems restoredItems(items);simulation::EntityHandlePool playerHandles;
    gameplay::Player restoredPlayer({7},playerHandles.create(),{1,1}, 5);
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
    auto legacySave = saved;
    legacySave.dialogueFlags.clearAll();
    legacySave.world.objects.clear();
    legacySave.world.pickups.clear();
    auto legacyBytes = save::serializeSave(legacySave);
    removeSaveChunk(legacyBytes, "PROG");
    removeSaveChunk(legacyBytes, "EQIP");
    removeSaveChunk(legacyBytes, "BANK");
    removeSaveChunk(legacyBytes, "WRLD");
    removeSaveChunk(legacyBytes, "ENCT");
    legacyBytes[6] = 0;
    legacyBytes[7] = 0;
    const auto legacyLoaded = save::deserializeSave(legacyBytes, saveCatalogs);
    expect(legacyLoaded && legacyLoaded.data.dialogueFlags.values().empty(),
           "DSAV 1.0 saves without dialogue flags remain backward compatible");
    auto incompatibleFlags = save::serializeSave(saved);
    incompatibleFlags[6] = 0;
    incompatibleFlags[7] = 0;
    expect(!save::deserializeSave(incompatibleFlags, saveCatalogs),
           "DSAV 1.0 rejects a future FLGS chunk instead of silently dropping flags");
    auto corruptFlags = save::serializeSave(saved);
    const std::array<std::uint8_t, 4> flagsTag{'F', 'L', 'G', 'S'};
    const auto flagsPosition = std::search(corruptFlags.begin(), corruptFlags.end(),
                                           flagsTag.begin(), flagsTag.end());
    bool malformedFlagsRejected = false;
    if (flagsPosition != corruptFlags.end()) {
        const auto countOffset = static_cast<std::size_t>(flagsPosition - corruptFlags.begin()) + 12;
        for (std::size_t index = 0; index < sizeof(std::uint32_t); ++index) {
            corruptFlags[countOffset + index] = 0xff;
        }
        malformedFlagsRejected = !save::deserializeSave(corruptFlags, saveCatalogs);
    }
    expect(malformedFlagsRejected, "DSAV rejects an oversized persistent dialogue flag chunk");

    std::string applyError;
    expect(save::applyWorldState(saved.world,*runtime.world,handles,items,applyError) &&
               runtime.world->objects().size()==1 && runtime.world->objects()[0].instance.state()==gameplay::WorldObjectState::opened &&
               runtime.world->objects()[0].instance.contents()->count(gameplay::lifePotionItemId())==1 &&
               runtime.world->destroyedObjectResidues().size() == 1 &&
               runtime.world->destroyedObjectResidues()[0].persistentId == simulation::PersistentInstanceId{3} &&
               runtime.world->destroyedObjectResidues()[0].visualSetId == simulation::DefinitionId{"visual.object.crate"} &&
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

    auto roomB=makeSyntheticMap("map.test.beta","map.test.alpha");
    const auto dmapA=std::filesystem::temp_directory_path()/"underworld_test_alpha.dmap";
    const auto dmapB=std::filesystem::temp_directory_path()/"underworld_test_beta.dmap";
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
    std::filesystem::remove(dmapA, ec); std::filesystem::remove(dmapB, ec);
}

void testSceneRuntimeController() {
    namespace gameplay = underworld::game::gameplay;
    namespace scenes = underworld::game::gameplay::scenes;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;

    scenes::SceneDefinition scene;
    scene.id = simulation::DefinitionId{"scene.runtime.controller"};
    scene.durationTicks = 5;
    scene.actors.push_back({"hero", scenes::SceneActorKind::player, {}});
    scenes::SceneTrack actorTrack;
    actorTrack.kind = scenes::SceneTrackKind::actor;
    actorTrack.actorSlot = "hero";
    // Deliberately reverse the authored collection order. Runtime evaluation
    // remains chronological, derived only from tick values.
    actorTrack.clips = {
        {scenes::SceneClipKind::move, "hero", 3, 2, {20, 0}},
        {scenes::SceneClipKind::move, "hero", 0, 3, {10, 0}},
        {scenes::SceneClipKind::face, "hero", 5, 0, {}, gameplay::FacingDirection::up},
        {scenes::SceneClipKind::emote, "hero", 3, 2, {}, gameplay::FacingDirection::down,
         scenes::SceneEmoteKind::surprise},
        {scenes::SceneClipKind::hop, "hero", 3, 2, {}, gameplay::FacingDirection::down,
         scenes::SceneEmoteKind::surprise, 5},
    };
    scene.tracks.push_back(actorTrack);
    scenes::SceneTrack effects;
    effects.kind = scenes::SceneTrackKind::presentation;
    effects.clips.push_back({scenes::SceneClipKind::presentationEffect, {}, 0, 0, {},
                             gameplay::FacingDirection::down, scenes::SceneEmoteKind::surprise,
                             0, {}, false, simulation::DefinitionId{"effect.scene.test"}});
    scene.tracks.push_back(std::move(effects));
    scenes::SceneTrack world;
    world.kind = scenes::SceneTrackKind::world;
    scenes::SceneClip worldEvent;
    worldEvent.kind = scenes::SceneClipKind::worldEvent;
    worldEvent.startTick = 4;
    worldEvent.worldAction.kind = maps::WorldActionKind::setFlag;
    worldEvent.worldAction.definitionTarget = simulation::DefinitionId{"flag.scene.test"};
    world.clips.push_back(std::move(worldEvent));
    scene.tracks.push_back(std::move(world));

    expect(scenes::validateScene(scene, 64, 64, {}, {}).valid,
           "scene validation accepts one deterministic actor track with typed global tracks");
    auto duplicateTrackScene = scene;
    duplicateTrackScene.tracks.push_back(actorTrack);
    expect(!scenes::validateScene(duplicateTrackScene, 64, 64, {}, {}).valid,
           "scene validation rejects ambiguous duplicate actor tracks");

    simulation::EventBuffer events;
    scenes::SceneController controller;
    gameplay::FacingDirection facing = gameplay::FacingDirection::down;
    underworld::core::WorldPointI position{};
    int effectRequests = 0;
    int worldActions = 0;
    scenes::SceneRuntimeHooks hooks{
        [&](const scenes::SceneActorBinding&) -> std::optional<scenes::SceneActorSnapshot> {
            return scenes::SceneActorSnapshot{{0, 0}, gameplay::FacingDirection::down};
        },
        [&](const scenes::SceneActorBinding&, underworld::core::WorldPointI value) {
            position = value;
            return true;
        },
        [&](const scenes::SceneActorBinding&, gameplay::FacingDirection value) {
            facing = value;
            return true;
        },
        {}, {}, {},
        [&](const simulation::DefinitionId&, simulation::EventBuffer&) { ++effectRequests; },
        [&](const maps::WorldAction&, simulation::EventBuffer&) { ++worldActions; return true; },
    };
    std::string error;
    expect(controller.start(simulation::MapId{"map.scene.test"}, scene, hooks, events, error) &&
               controller.active() && controller.tick() == 0 && effectRequests == 1 &&
               std::holds_alternative<simulation::SceneStarted>(events.eventAt(0)),
           "scene starts at tick zero before firing its tick-zero presentation events");
    controller.advance(events);
    controller.advance(events);
    controller.advance(events);
    expect(controller.tick() == 3 && position == underworld::core::WorldPointI{10, 0},
           "scene move reaches the first target independent of clip storage order");
    controller.advance(events);
    expect(controller.tick() == 4 && position == underworld::core::WorldPointI{15, 0} &&
               worldActions == 1 && !controller.presentation().empty() &&
               controller.presentation().actors.front().offsetY < 0 &&
               controller.presentation().actors.front().emote == scenes::SceneEmoteKind::surprise,
           "scene applies deterministic movement, world events, hop and emote presentation together");
    controller.advance(events);
    expect(!controller.active() && controller.finished() && position == underworld::core::WorldPointI{20, 0} &&
               facing == gameplay::FacingDirection::up && controller.presentation().empty(),
           "scene completion preserves final actor state and clears transient presentation state");

    scenes::SceneDefinition dialogueScene;
    dialogueScene.id = simulation::DefinitionId{"scene.runtime.dialogue"};
    dialogueScene.durationTicks = 3;
    scenes::SceneTrack dialogueTrack;
    dialogueTrack.kind = scenes::SceneTrackKind::dialogue;
    dialogueTrack.clips.push_back({scenes::SceneClipKind::dialogue, {}, 1, 0, {},
                                   gameplay::FacingDirection::down, scenes::SceneEmoteKind::surprise,
                                   0, simulation::DefinitionId{"dialogue.scene.test"}, true});
    dialogueScene.tracks.push_back(std::move(dialogueTrack));
    bool dialogueOpen = false;
    scenes::SceneRuntimeHooks dialogueHooks{
        [](const scenes::SceneActorBinding&) -> std::optional<scenes::SceneActorSnapshot> {
            return std::nullopt;
        }, {}, {},
        [&](const simulation::DefinitionId&, std::string&) { dialogueOpen = true; return true; },
        [&]() { return dialogueOpen; },
        [&]() { dialogueOpen = false; },
        {}, {},
    };
    simulation::EventBuffer dialogueEvents;
    scenes::SceneController dialogueController;
    expect(dialogueController.start(simulation::MapId{"map.scene.test"}, dialogueScene,
                                    dialogueHooks, dialogueEvents, error),
           "scene with a dialogue clip starts without actor bindings");
    dialogueController.advance(dialogueEvents);
    dialogueController.advance(dialogueEvents);
    expect(dialogueController.tick() == 1 && dialogueController.waitingForDialogue() && dialogueOpen,
           "blocking dialogue freezes the scene clock until the dialogue closes");
    dialogueOpen = false;
    dialogueController.advance(dialogueEvents);
    expect(dialogueController.tick() == 2 && !dialogueController.waitingForDialogue(),
           "scene resumes on the next fixed tick after its blocking dialogue closes");

    scenes::SceneDefinition missingActorScene;
    missingActorScene.id = simulation::DefinitionId{"scene.runtime.missing"};
    missingActorScene.durationTicks = 1;
    missingActorScene.actors.push_back({"missing", scenes::SceneActorKind::npc, {99}});
    simulation::EventBuffer abortEvents;
    scenes::SceneController missingActorController;
    expect(!missingActorController.start(simulation::MapId{"map.scene.test"}, missingActorScene,
                                         dialogueHooks, abortEvents, error) &&
               !missingActorController.active() && abortEvents.size() == 1 &&
               std::holds_alternative<simulation::SceneAborted>(abortEvents.eventAt(0)),
           "missing scene actors abort safely without leaving an active scene");
}

void testSceneTimelineAuthoring() {
    namespace editor = underworld::editor;
    namespace gameplay = underworld::game::gameplay;
    namespace scenes = underworld::game::gameplay::scenes;
    namespace simulation = underworld::simulation;

    scenes::SceneDefinition scene;
    scene.id = simulation::DefinitionId{"scene.editor.timeline"};
    scene.durationTicks = 120;
    scene.actors.push_back({"hero", scenes::SceneActorKind::player, {}});
    scene.tracks.push_back({scenes::SceneTrackKind::actor, "hero", {}});
    auto& actorTrack = scene.tracks.front();
    actorTrack.clips.push_back({scenes::SceneClipKind::move, "hero", 0, 60, {60, 0}});
    actorTrack.clips.push_back({scenes::SceneClipKind::face, "hero", 60, 0, {},
                                gameplay::FacingDirection::up});
    actorTrack.clips.push_back({scenes::SceneClipKind::emote, "hero", 70, 20, {},
                                gameplay::FacingDirection::down, scenes::SceneEmoteKind::surprise});
    actorTrack.clips.push_back({scenes::SceneClipKind::hop, "hero", 70, 20, {},
                                gameplay::FacingDirection::down, scenes::SceneEmoteKind::surprise, 8});

    std::string error;
    expect(editor::snapSceneTick(13, {true, 5}) == 15 &&
               editor::addSceneClip(scene, 0,
                   {scenes::SceneClipKind::face, "hero", 13, 99, {}, gameplay::FacingDirection::left},
                   {true, 5}, error) && actorTrack.clips.back().startTick == 15 &&
               actorTrack.clips.back().durationTicks == 0,
           "scene timeline snaps event clips and keeps instant events durationless");
    const auto copiedIndex = actorTrack.clips.size();
    expect(editor::duplicateSceneClip(scene, {0, 0}, 0, 65, {true, 5}, error) &&
               editor::resizeSceneClip(scene, {0, copiedIndex}, 25, {true, 5}, error) &&
               editor::setSceneMoveTarget(scene, {0, copiedIndex}, {100, 0}, error) &&
               editor::removeSceneClip(scene, {0, copiedIndex}, error),
           "scene timeline supports duplicate, resize, map-target edit and delete without runtime state");
    expect(editor::addSceneMarker(scene, {"reaction", 72}, {true, 5}, error) &&
               editor::renameSceneMarker(scene, 0, "reaction.start", error) &&
               editor::jumpToSceneMarker(scene, "reaction.start") == std::uint32_t{70} &&
               editor::fitSceneDurationToContent(scene) == 90,
           "scene markers support snap, rename, jump and fit-duration navigation");

    const auto preview = editor::evaluateScenePreview(scene,
        {{"hero", {0, 0}, gameplay::FacingDirection::down}}, 80);
    expect(preview.tick == 80 && preview.actors.size() == 1 &&
               preview.actors.front().position == underworld::core::WorldPointI{60, 0} &&
               preview.actors.front().facing == gameplay::FacingDirection::up &&
               preview.actors.front().emote == scenes::SceneEmoteKind::surprise &&
               preview.actors.front().visualOffsetY == -8,
           "scene scrub preview deterministically rebuilds movement, facing and presentation from snapshots");
    expect(scene.tracks.front().clips.front().targetPosition == underworld::core::WorldPointI{60, 0},
           "scene preview leaves authored clips unchanged");

    const editor::SceneValidationContext context{128, 128, {}, {}};
    expect(editor::validateSceneTimeline(scene, context).valid,
           "scene editor validation bridge delegates map-aware authored diagnostics");
    const editor::SceneTimelineGeometry geometry{10, 200, 60, 2.0F};
    const auto ruler = editor::sceneTimelineRulerMarks(geometry, 5, 60);
    expect(editor::sceneTimelineXForTick(geometry, 70) == 30 &&
               editor::sceneTimelineTickForX(geometry, 31, {true, 5}) == 70 &&
               !ruler.empty() && ruler.front().tick >= 60 &&
               editor::sceneTimelineClipBounds(geometry, scene.tracks.front().clips.front()).width > 1,
           "scene timeline geometry exposes deterministic ruler, tick and clip layout helpers");

    auto document = editor::EditorDocument::newMap(simulation::MapId{"map.scene.history"}, 8, 8, 16);
    auto authoredScenes = document.scenes();
    authoredScenes.push_back(scene);
    std::string historyError;
    expect(document.execute(std::make_unique<editor::ReplaceScenesCommand>(
                                document.scenes(), authoredScenes), historyError) &&
               document.scenes().size() == 1 && document.scenes().front().id == scene.id,
           "scene authoring is recorded as one document command");
    expect(document.undo() && document.scenes().empty() && document.redo(historyError) &&
               document.scenes().size() == 1 && document.authoredSource().scenes == document.scenes(),
           "scene command undo/redo keeps authored source and runtime map view synchronized");
}

void testWorldObjectPersistencePolicies() {
    namespace content = underworld::game::content;
    namespace creatures = underworld::game::gameplay::creatures;
    namespace editor = underworld::editor;
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace maps = underworld::game::maps;
    namespace save = underworld::game::save;
    namespace simulation = underworld::simulation;
    namespace world = underworld::world;

    gameplay::ItemCatalog items;
    items.add(gameplay::makeLifePotionDefinition());
    gameplay::WorldObjectCatalog objects;
    objects.add({{"object.chest"}, {"visual.object.chest"},
                 gameplay::ObjectInteractionDefinition{{-8, -8, 16, 16}},
                 gameplay::ObjectContainerDefinition{4}, std::nullopt});
    objects.add({{"object.crate"}, {"visual.object.crate"}, std::nullopt,
                 std::nullopt, gameplay::ObjectDestructibleDefinition{2, {-8, -8, 16, 16}, 1}});
    objects.add({{"object.door"}, {"visual.object.door"},
                 gameplay::ObjectInteractionDefinition{{-8, -8, 16, 16}}, std::nullopt,
                 std::nullopt, std::nullopt,
                 gameplay::ObjectDoorDefinition{gameplay::DoorState::closed, {-8, -8, 16, 16}},
                 std::nullopt});
    objects.add({{"object.pressure"}, {"visual.object.pressure"},
                 gameplay::ObjectInteractionDefinition{{-8, -8, 16, 16}}, std::nullopt,
                 std::nullopt, std::nullopt, std::nullopt,
                 gameplay::ObjectActivationDefinition{
                     gameplay::ObjectActivationMode::playerPressure, false,
                     world::AabbI{0, 0, 16, 16}}});

    game::TilesetCatalog tilesets;
    tilesets.add({{"tileset.test"}, "Test", "test.png", 16, 1, 1});
    creatures::EnemyCatalog enemies;
    creatures::BehaviorCatalog behaviors;
    gameplay::AttackCatalog attacks;
    gameplay::ProjectileCatalog projectiles;
    const std::array visuals{creatures::soldierVisualId(), creatures::skullVisualId()};
    simulation::EntityHandlePool handles;
    creatures::EnemyFactory enemyFactory(handles, enemies, behaviors, attacks, projectiles, visuals);
    gameplay::WorldObjectFactory objectFactory(handles, objects, items);
    const game::RuntimeTilesetCatalog runtimeTilesets(tilesets);
    const maps::MapValidationCatalogs validation{&enemies, &objects, &items, &tilesets};
    maps::RuntimeWorldBuilder builder(validation, enemyFactory, objectFactory, handles,
                                      runtimeTilesets);

    const auto makeMap = [&](simulation::MapId id, simulation::SpawnId spawn) {
        maps::MapData map;
        map.id = std::move(id); map.width = 12; map.height = 4; map.tileSize = 16;
        map.tileReferences.push_back({{"tileset.test"}, 0, world::TileFlags::none});
        map.layers.push_back({"ground", true, std::vector<std::optional<std::uint32_t>>(48)});
        map.collision.assign(48, 0);
        map.playerSpawns.push_back({std::move(spawn), {16, 16}, gameplay::FacingDirection::down});
        return map;
    };
    auto mapA = makeMap(simulation::MapId{"map.persistence.a"},
                        simulation::SpawnId{"entry.start"});
    auto mapB = makeMap(simulation::MapId{"map.persistence.b"},
                        simulation::SpawnId{"entry.return"});
    mapA.objects.push_back({{1}, {"object.chest"}, {16, 16},
                             {{gameplay::lifePotionItemId(), 2}},
                             maps::ObjectPersistencePolicy::persistent});
    mapA.objects.push_back({{2}, {"object.chest"}, {32, 16},
                             {{gameplay::lifePotionItemId(), 2}},
                             maps::ObjectPersistencePolicy::resetOnMapEnter});
    mapA.objects.push_back({{3}, {"object.crate"}, {48, 16}, {},
                             maps::ObjectPersistencePolicy::persistent});
    mapA.objects.push_back({{4}, {"object.crate"}, {64, 16}, {},
                             maps::ObjectPersistencePolicy::resetOnMapEnter});
    mapA.objects.push_back({{5}, {"object.door"}, {80, 16}, {},
                             maps::ObjectPersistencePolicy::persistent});
    mapA.objects.push_back({{6}, {"object.door"}, {112, 16}, {},
                             maps::ObjectPersistencePolicy::resetOnMapEnter});
    mapA.objects.push_back({{7}, {"object.pressure"}, {144, 16}, {},
                             maps::ObjectPersistencePolicy::persistent});
    mapA.links.push_back({"to-b", {0, 0, 1, 1}, mapB.id,
                         simulation::SpawnId{"entry.return"}});
    mapB.links.push_back({"to-a", {0, 0, 1, 1}, mapA.id,
                         simulation::SpawnId{"entry.start"}});
    expect(maps::validateMapData(mapA, &validation).valid &&
               maps::validateMapData(mapB, &validation).valid,
           "object persistence policies validate as placement data without changing definitions");

    const auto initialSource = maps::authoredMapFromMapData(mapA);
    const auto authoredJson = maps::encodeAuthoredMapJson(initialSource);
    const auto authoredDecoded = maps::decodeAuthoredMapJson(authoredJson);
    expect(authoredDecoded.source && authoredDecoded.diagnostics.empty() &&
               authoredJson.find("\"version\": 4") != std::string::npos &&
               maps::semanticallyEqual(maps::mapDataFromAuthored(*authoredDecoded.source), mapA) &&
               authoredDecoded.source->geometry.objects[1].persistence ==
                   maps::ObjectPersistencePolicy::resetOnMapEnter,
           "UMAP v4 roundtrips persistent and reset-on-map-enter placement policies");
    auto legacyJson = authoredJson;
    const auto persistenceField = legacyJson.find("\"persistence\"");
    if (persistenceField != std::string::npos) {
        const auto comma = legacyJson.find(',', persistenceField);
        legacyJson.erase(persistenceField,
                         comma == std::string::npos ? std::string::npos : comma - persistenceField + 1);
    }
    const auto legacyDecoded = maps::decodeAuthoredMapJson(legacyJson);
    expect(legacyDecoded.source && legacyDecoded.source->geometry.objects.front().persistence ==
               maps::ObjectPersistencePolicy::persistent,
           "older UMAP object placements without a persistence field default to persistent");

    const auto dmapDecoded = maps::deserializeDmap(maps::serializeDmap(mapA));
    expect(dmapDecoded && dmapDecoded.data.objects[1].persistence ==
               maps::ObjectPersistencePolicy::resetOnMapEnter &&
               dmapDecoded.data.objects[0].persistence == maps::ObjectPersistencePolicy::persistent,
           "DMAP 1.5 roundtrips both object persistence policies");
    const auto legacyDmap = makeDmapV12WithoutObjectPersistence(maps::serializeDmap(mapA));
    const auto legacyDmapDecoded = legacyDmap ? maps::deserializeDmap(*legacyDmap)
                                              : maps::DmapLoadResult{};
    expect(legacyDmapDecoded &&
               std::all_of(legacyDmapDecoded.data.objects.begin(),
                           legacyDmapDecoded.data.objects.end(), [](const auto& object) {
                   return object.persistence == maps::ObjectPersistencePolicy::persistent;
               }),
           "DMAP 1.2 object records without persistence default to persistent");

    maps::MapCatalog catalog;
    catalog.addData(mapA); catalog.addData(mapB);
    save::SessionWorldState sessionState;
    maps::MapSession session(catalog, validation, builder, handles, sessionState);
    const auto activated = session.activate(mapA.id, simulation::SpawnId{"entry.start"});
    expect(activated.changed, "persistence test activates the authored source map");
    if (!activated.changed) { return; }
    const auto findObject = [&](simulation::PersistentInstanceId id) {
        return std::find_if(session.world()->objects().begin(), session.world()->objects().end(),
            [&](const auto& value) { return value.persistentId == id; });
    };
    auto persistentChest = findObject({1});
    auto resetChest = findObject({2});
    expect(persistentChest != session.world()->objects().end() &&
               resetChest != session.world()->objects().end(),
           "runtime creates both persistent and resettable container instances");
    if (persistentChest != session.world()->objects().end() &&
        resetChest != session.world()->objects().end()) {
        static_cast<void>(persistentChest->instance.open());
        static_cast<void>(persistentChest->instance.contents()->remove(
            gameplay::lifePotionItemId(), 2));
        static_cast<void>(resetChest->instance.open());
        static_cast<void>(resetChest->instance.contents()->remove(
            gameplay::lifePotionItemId(), 2));
    }
    const auto destroyObject = [&](simulation::PersistentInstanceId id) {
        auto& values = session.world()->objects();
        const auto found = std::find_if(values.begin(), values.end(),
            [&](const auto& value) { return value.persistentId == id; });
        if (found == values.end()) return;
        session.world()->addDestroyedObjectResidue(found->persistentId,
            found->instance.definition().visualSetId, found->instance.position());
        static_cast<void>(handles.destroy(found->instance.handle()));
        values.erase(found);
    };
    destroyObject({3}); destroyObject({4});
    expect(session.world()->setDoorState({5}, gameplay::DoorState::open) &&
               session.world()->setDoorState({6}, gameplay::DoorState::open),
           "persistent and resettable doors accept the same runtime state change");
    simulation::EventBuffer pressureEvents;
    session.world()->updatePressureActivations({148, 20}, pressureEvents);
    expect(session.world()->objectActivation({7}) == std::optional<bool>{true} &&
               pressureEvents.size() == 1,
           "playerPressure remains a derived runtime state and emits its transition event");

    expect(session.activate(mapB.id, simulation::SpawnId{"entry.return"}).changed &&
               session.activate(mapA.id, simulation::SpawnId{"entry.start"}).changed,
           "map unload and rebuild completes through the existing MapSession pipeline");
    const auto returnedObject = [&](simulation::PersistentInstanceId id) {
        return std::find_if(session.world()->objects().begin(), session.world()->objects().end(),
            [&](const auto& value) { return value.persistentId == id; });
    };
    const auto returnedPersistentChest = returnedObject({1});
    const auto returnedResetChest = returnedObject({2});
    const auto returnedPersistentCrate = returnedObject({3});
    const auto returnedResetCrate = returnedObject({4});
    expect(returnedPersistentChest != session.world()->objects().end() &&
               returnedPersistentChest->instance.state() == gameplay::WorldObjectState::opened &&
               returnedPersistentChest->instance.contents()->count(gameplay::lifePotionItemId()) == 0,
           "persistent chest remains opened and empty after A-to-B-to-A");
    expect(returnedResetChest != session.world()->objects().end() &&
               returnedResetChest->instance.state() == gameplay::WorldObjectState::idle &&
               returnedResetChest->instance.contents()->count(gameplay::lifePotionItemId()) == 2,
           "reset-on-map-enter container restores its authored initial contents");
    expect(returnedPersistentCrate == session.world()->objects().end() &&
               returnedResetCrate != session.world()->objects().end(),
           "persistent destructible stays absent while resettable destructible reappears");
    const auto returnedPersistentDoor = returnedObject({5});
    const auto returnedResetDoor = returnedObject({6});
    expect(returnedPersistentDoor != session.world()->objects().end() &&
               returnedPersistentDoor->instance.doorState() == gameplay::DoorState::open &&
               returnedResetDoor != session.world()->objects().end() &&
               returnedResetDoor->instance.doorState() == gameplay::DoorState::closed,
           "persistent and resettable doors restore their distinct authored policies");
    expect(session.world()->objectActivation({7}) == std::optional<bool>{false} &&
               sessionState.findObject({mapA.id, {7}}) == nullptr &&
               sessionState.findObject({mapA.id, {2}}) == nullptr,
           "playerPressure and resettable object state never become persistent deltas");
    expect(sessionState.findObject({mapA.id, {1}}) != nullptr &&
               sessionState.findObject({mapA.id, {3}}) != nullptr &&
               sessionState.findObject({mapA.id, {5}}) != nullptr,
           "persistent object changes are captured in the existing SessionWorldState");

    gameplay::rpg::PlayerProgressionCatalog progressions;
    progressions.add(testProgression());
    save::SaveData saveData;
    saveData.player.currentMapId = mapA.id; saveData.player.health = 1;
    saveData.progression = {testProgression().id, 0}; saveData.world = sessionState;
    const save::SaveValidationCatalogs saveCatalogs{
        &items, {&mapA, &mapB}, nullptr, &progressions, &objects};
    const auto saveBytes = save::serializeSave(saveData);
    const auto loadedSave = save::deserializeSave(saveBytes, saveCatalogs);
    expect(loadedSave && loadedSave.data.world.findObject({mapA.id, {2}}) == nullptr &&
               loadedSave.data.world.findObject({mapA.id, {1}}) != nullptr,
           "DSAV 1.8 stores persistent object deltas without resettable object state");
    if (loadedSave) {
        auto legacyState = loadedSave.data.world;
        legacyState.set(save::ObjectDelta{{mapA.id, {2}}, true, false, {}, std::nullopt});
        expect(session.restore(mapA.id, legacyState).changed,
               "save restore accepts a legacy resettable delta without applying it");
        const auto restoredResetChest = std::find_if(session.world()->objects().begin(),
            session.world()->objects().end(), [&](const auto& value) {
                return value.persistentId == simulation::PersistentInstanceId{2};
            });
        expect(restoredResetChest != session.world()->objects().end() &&
                   restoredResetChest->instance.state() == gameplay::WorldObjectState::idle &&
                   restoredResetChest->instance.contents()->count(gameplay::lifePotionItemId()) == 2,
               "legacy resettable deltas cannot override the new authored policy on load");
    }

    auto compiledContent = content::compileBuiltinContentOrThrow();
    editor::EditorDocument editorDocument(makeSyntheticMap("map.persistence.editor",
                                                            "map.persistence.editor"));
    editorDocument.markSaved();
    std::string editorError;
    expect(editorDocument.execute(std::make_unique<editor::SetObjectPersistenceCommand>(
                   simulation::PersistentInstanceId{3},
                   maps::ObjectPersistencePolicy::resetOnMapEnter), editorError) &&
               editorDocument.dirty() &&
               editorDocument.data().objects[0].persistence == maps::ObjectPersistencePolicy::persistent,
           "Content Studio changes object persistence through an undoable command and dirties UMAP");
    expect(editorDocument.undo() && editorDocument.data().objects[0].persistence ==
               maps::ObjectPersistencePolicy::persistent &&
               editorDocument.redo(editorError) &&
               editorDocument.data().objects[1].persistence ==
                   maps::ObjectPersistencePolicy::resetOnMapEnter,
           "object persistence command supports undo and redo without changing another placement");
    const auto editorPath = std::filesystem::temp_directory_path() /
                            "underworld_object_persistence_editor.umap";
    std::error_code removeError; std::filesystem::remove(editorPath, removeError);
    expect(editorDocument.saveAs(editorPath, compiledContent, editorError) &&
               !editorDocument.dirty(),
           "Content Studio saves the authored persistence policy and clears dirty state");
    const auto reopened = editor::EditorDocument::open(editorPath, compiledContent, editorError);
    expect(reopened && reopened->data().objects[1].persistence ==
               maps::ObjectPersistencePolicy::resetOnMapEnter,
           "reopened UMAP preserves the command-edited placement policy");
    std::filesystem::remove(editorPath, removeError);
    editor::EditorLocalization localization;
    expect(localization.text(editor::EditorTextId::persistent) == "Persistente" &&
               localization.text(editor::EditorTextId::resetOnMapEnter) ==
                   "Reiniciar ao entrar no mapa", "Content Studio localizes persistence in pt-BR");
    localization.setLanguage(editor::EditorLanguage::englishUnitedStates);
    expect(localization.text(editor::EditorTextId::persistent) == "Persistent" &&
               localization.text(editor::EditorTextId::resetOnMapEnter) == "Reset On Map Enter",
           "Content Studio localizes persistence in en-US");
}

void testPhase10NpcFoundation() {
    namespace editor = underworld::editor;
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace maps = underworld::game::maps;
    namespace npcs = underworld::game::gameplay::npcs;
    namespace simulation = underworld::simulation;

    auto content = game::content::compileBuiltinContentOrThrow();
    expect(content.npcs().find(npcs::guardNpcId()) &&
               content.npcs().find(npcs::scholarNpcId()) &&
               content.npcVisuals().find(simulation::DefinitionId{"visual.npc.guard"}) &&
               content.authoringDescriptors(game::AuthoringCategory::npc).size() == 3,
           "GameContentRegistry exposes reusable NPC definitions through the authoring catalog");

    auto map = makeSyntheticMap("map.test.npc", "map.test.npc.target");
    map.npcs.push_back({{6}, npcs::guardNpcId(), {72, 24}, gameplay::FacingDirection::left});
    map.npcs.push_back({{7}, npcs::scholarNpcId(), {88, 24}, gameplay::FacingDirection::right});
    const auto catalogs = game::mapValidationCatalogs(content);
    expect(static_cast<bool>(maps::validateMapData(map, &catalogs)),
           "NPC placements validate with definition IDs and the shared persistent ID namespace");

    const auto bytes = maps::serializeDmap(map);
    const auto decoded = maps::deserializeDmap(bytes, &catalogs);
    expect(decoded && decoded.data.npcs.size() == 2 &&
               maps::semanticallyEqual(map, decoded.data) && maps::dmapMinorVersion >= 1,
           "DMAP current minor roundtrips authored NPC placements without changing DSAV");
    auto unknown = map;
    unknown.npcs[0].definitionId = simulation::DefinitionId{"npc.missing"};
    expect(!maps::validateMapData(unknown, &catalogs),
           "unknown NPC definitions fail before runtime construction");
    expect(static_cast<bool>(maps::validateMapData(map, &catalogs)),
           "NPC map validation depends on logical definitions rather than visual availability");

    simulation::EntityHandlePool handles;
    const std::array visuals{gameplay::creatures::soldierVisualId(),
                             gameplay::creatures::skullVisualId()};
    gameplay::creatures::EnemyFactory enemies(handles, content.enemies(), content.behaviors(),
                                              content.attacks(), content.projectiles(), visuals);
    gameplay::WorldObjectFactory objects(handles, content.objects(), content.items());
    npcs::NpcFactory npcFactory(handles, content.npcs());
    game::RuntimeTilesetCatalog runtimeTilesets(content.tilesets());
    maps::RuntimeWorldBuilder builder(catalogs, enemies, objects, handles, runtimeTilesets,
                                      &npcFactory);
    const auto runtime = builder.build(decoded.data, simulation::SpawnId{"entry.start"});
    expect(runtime && runtime.world->npcs().size() == 2 &&
               runtime.world->npcs()[0].persistentId == simulation::PersistentInstanceId{6} &&
               runtime.world->npcs()[0].instance.definition().id == npcs::guardNpcId() &&
               runtime.world->npcs()[0].instance.facing() == gameplay::FacingDirection::left,
           "RuntimeWorldBuilder creates both NPC instances with persistent identity and facing");
    expect(runtime && runtime.world->npcs()[0].instance.interactionArea().enabled,
           "runtime NPC exposes the shared InteractionArea contract");
    const auto interaction = npcs::interactNearest(
        {72, 24}, {60, 8, 20, 20}, runtime.world->npcs());
    expect(interaction.npc == runtime.world->npcs()[0].instance.handle(),
           "player interaction resolves the nearest authored NPC deterministically");

    auto document = editor::EditorDocument::newMap(simulation::MapId{"map.editor.npc"}, 8, 8);
    std::string error;
    const auto placement = maps::NpcPlacement{{1}, npcs::guardNpcId(), {40, 40},
                                               gameplay::FacingDirection::up};
    expect(document.execute(std::make_unique<editor::PlaceEntityCommand>(placement), error) &&
               document.data().npcs.size() == 1,
           "Map Maker places an NPC through the generic EditorCommand pipeline");
    expect(document.execute(std::make_unique<editor::MoveEntityCommand>(
               editor::SelectionKind::npc, simulation::PersistentInstanceId{1},
               underworld::core::WorldPointI{40, 40},
               underworld::core::WorldPointI{56, 40}), error) &&
               document.data().npcs[0].position == underworld::core::WorldPointI{56, 40},
           "Map Maker moves NPC placements while preserving their persistent ID");
    expect(document.undo() && document.data().npcs[0].position ==
               underworld::core::WorldPointI{40, 40} && document.redo(error) &&
               document.data().npcs[0].position == underworld::core::WorldPointI{56, 40},
           "NPC move participates in editor undo and redo");
    expect(document.execute(std::make_unique<editor::DeleteEntityCommand>(
               editor::SelectionKind::npc, simulation::PersistentInstanceId{1}), error) &&
               document.data().npcs.empty() &&
               document.undo() && document.data().npcs.size() == 1,
           "Map Maker deletes and restores NPC placements through undo");
}

void testPhase10DialogueDataModel() {
    namespace dialogue = underworld::game::gameplay::dialogue;
    namespace game = underworld::game;

    const auto content = game::content::compileBuiltinContentOrThrow();
    const auto& guard = content.dialogues().require(dialogue::guardDialogueId());
    const auto& scholar = content.dialogues().require(dialogue::scholarDialogueId());
    expect(content.dialogues().size() == 3 &&
               content.npcs().require(underworld::game::gameplay::npcs::guardNpcId())
                       .defaultDialogueId == dialogue::guardDialogueId() &&
               content.npcs().require(underworld::game::gameplay::npcs::scholarNpcId())
                       .defaultDialogueId == dialogue::scholarDialogueId(),
           "GameContentRegistry connects reusable NPCs to data-driven dialogues");

    const auto& guardEntry = dialogue::requireNode(guard, guard.entryNodeId);
    const auto& guardResponse = dialogue::requireNode(guard, guardEntry.nextNodeId);
    expect(guardEntry.pages.size() == 2 && guardEntry.speaker == "Guard" &&
               guardEntry.nextNodeId == guardResponse.id && guardResponse.choices.empty(),
           "dialogue nodes support speaker, pagination and a linear next-node transition");

    const auto& scholarEntry = dialogue::requireNode(scholar, scholar.entryNodeId);
    expect(scholarEntry.pages.size() == 1 && scholarEntry.choices.size() == 3 &&
               dialogue::findNode(scholar, scholarEntry.choices[0].targetNodeId) != nullptr &&
               dialogue::findNode(scholar, scholarEntry.choices[1].targetNodeId) != nullptr,
           "dialogue nodes support data-driven choices targeting other nodes");

    bool rejectedUnknownTarget = false;
    try {
        auto invalid = dialogue::makeGuardDialogueDefinition();
        invalid.nodes[0].nextNodeId = underworld::simulation::DefinitionId{"dialogue.missing"};
        dialogue::DialogueCatalog catalog;
        catalog.add(std::move(invalid));
    } catch (const std::invalid_argument&) {
        rejectedUnknownTarget = true;
    }
    expect(rejectedUnknownTarget, "dialogue catalog rejects unknown next-node targets");

    bool rejectedAmbiguousTransition = false;
    try {
        auto invalid = dialogue::makeGuardDialogueDefinition();
        invalid.nodes[0].choices.push_back({"Continue", invalid.nodes[1].id, {}, {}});
        dialogue::DialogueCatalog catalog;
        catalog.add(std::move(invalid));
    } catch (const std::invalid_argument&) {
        rejectedAmbiguousTransition = true;
    }
    expect(rejectedAmbiguousTransition,
           "dialogue catalog rejects nodes with both next-node and choice transitions");

    bool rejectedEmptyPage = false;
    try {
        auto invalid = dialogue::makeScholarDialogueDefinition();
        invalid.nodes[0].pages.push_back({});
        dialogue::DialogueCatalog catalog;
        catalog.add(std::move(invalid));
    } catch (const std::invalid_argument&) {
        rejectedEmptyPage = true;
    }
    expect(rejectedEmptyPage, "dialogue catalog rejects empty pages");
}

void testPhase10DialogueSession() {
    namespace dialogue = underworld::game::gameplay::dialogue;
    namespace simulation = underworld::simulation;

    const auto content = underworld::game::content::compileBuiltinContentOrThrow();
    dialogue::DialogueFlagSet flags;
    dialogue::DialogueSession session(content.dialogues(), flags);
    std::string error;
    expect(session.begin(dialogue::guardDialogueId(), error) && session.isOpen() &&
               session.state() == dialogue::DialogueSessionState::text &&
               session.pageIndex() == 0 && session.pageCount() == 2 &&
               session.currentPage() == "Halt, traveler.",
           "DialogueSession opens at the catalogued entry node and first page");

    expect(session.handleCommand(actionCommand(1, false, false, 1, 0)) &&
               session.pageIndex() == 0,
           "DialogueSession consumes movement while text is open without advancing");
    expect(session.handleCommand(actionCommand(2, true, false)) && session.pageIndex() == 1,
           "DialogueSession advances paginated text through PlayerCommand");
    auto advanceCommand = actionCommand(3, false, false, 0, 0, {0});
    advanceCommand.actions.interactPressed = true;
    expect(session.handleCommand(advanceCommand) &&
               session.currentPage() == "Keep your blade ready." && session.pageIndex() == 0,
           "DialogueSession follows a linear next-node transition");
    advanceCommand = actionCommand(4, false, false);
    advanceCommand.actions.interactPressed = true;
    expect(session.handleCommand(advanceCommand) && !session.isOpen(),
           "DialogueSession closes after the final page of a terminal node");

    expect(session.begin(dialogue::scholarDialogueId(), error) &&
               session.handleCommand(actionCommand(5, true, false)) &&
               session.choicesVisible() && session.choiceCount() == 2 &&
               session.selectedChoice() == 0,
           "DialogueSession reveals choices after the final text page");
    expect(session.handleCommand(actionCommand(6, false, false, 0, 1)) &&
               session.selectedChoice() == 1,
           "DialogueSession moves the selected choice with logical movement");
    expect(session.handleCommand(actionCommand(7, true, false)) && !session.choicesVisible() &&
               session.currentPage() == "Then walk carefully, friend.",
           "DialogueSession activates the selected choice and enters its target node");
    expect(session.handleCommand(actionCommand(8, false, true)) && !session.isOpen(),
           "DialogueSession closes through the secondary logical action");

    expect(!session.begin(simulation::DefinitionId{"dialogue.missing"}, error) &&
               !error.empty() && !session.isOpen(),
           "DialogueSession reports an unknown dialogue without opening a broken overlay");

    expect(session.begin(dialogue::scholarDialogueId(), error) &&
               session.handleCommand(actionCommand(9, true, false)) &&
               session.handleCommand(actionCommand(10, true, false)) &&
               !session.choicesVisible(),
           "selected dialogue action is reported by the dialogue session");
    for (const auto& action : session.takeActions()) {
        if (action.kind == dialogue::DialogueActionKind::setFlag) {
            static_cast<void>(flags.set(action.targetId));
        }
    }
    expect(flags.isSet(dialogue::scholarAskedFlagId()),
           "gameplay owner applies selected dialogue flag actions");
    session.close();
    expect(session.begin(dialogue::scholarDialogueId(), error) &&
               session.handleCommand(actionCommand(11, true, false)) &&
               session.choiceCount() == 2 && session.choiceLabel(1) == "Recall the lesson",
           "dialogue conditions select choices from the current persistent flag state");
}

void testPhase11QuestDefinitions() {
    namespace quests = underworld::game::gameplay::quests;
    namespace simulation = underworld::simulation;

    const auto content = underworld::game::content::compileBuiltinContentOrThrow();
    const auto& scholarQuest = content.quests().require(quests::scholarQuestId());
    expect(content.quests().size() == 1 && scholarQuest.title == "The Scholar's Path" &&
               scholarQuest.objectives.size() == 2 &&
               scholarQuest.objectives[0].kind == quests::QuestObjectiveKind::kill &&
               scholarQuest.objectives[1].kind == quests::QuestObjectiveKind::pickup,
           "GameContentRegistry exposes a reusable multi-objective quest definition");

    expect(quests::findObjective(scholarQuest,
                                 simulation::DefinitionId{"quest.scholar.kill"}) != nullptr &&
               quests::requireObjective(scholarQuest,
                                         simulation::DefinitionId{"quest.scholar.pickup"})
                       .requiredCount == 1,
           "quest objectives are addressable by stable DefinitionId");

    quests::QuestCatalog catalog;
    quests::QuestDefinition allKinds{simulation::DefinitionId{"quest.test.all_kinds"},
                                     "All objective kinds", {}, {"test"}};
    const std::array kinds{quests::QuestObjectiveKind::talk,
                           quests::QuestObjectiveKind::kill,
                           quests::QuestObjectiveKind::pickup,
                           quests::QuestObjectiveKind::enter,
                           quests::QuestObjectiveKind::open,
                           quests::QuestObjectiveKind::deliver};
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        allKinds.objectives.push_back(
            {simulation::DefinitionId{"quest.test.objective." + std::to_string(index)},
             kinds[index], simulation::DefinitionId{"target.test." + std::to_string(index)},
             1, "Test objective"});
    }
    catalog.add(allKinds);
    expect(catalog.size() == 1 &&
               catalog.require(simulation::DefinitionId{"quest.test.all_kinds"}) == allKinds,
           "QuestCatalog accepts all declared objective kinds without runtime state");

    bool rejectedIncompleteQuest = false;
    try {
        catalog.add({simulation::DefinitionId{"quest.test.empty"}, "", {}, {}});
    } catch (const std::invalid_argument&) {
        rejectedIncompleteQuest = true;
    }
    expect(rejectedIncompleteQuest, "QuestCatalog rejects incomplete quest definitions");

    bool rejectedIncompleteObjective = false;
    try {
        catalog.add({simulation::DefinitionId{"quest.test.invalid_objective"}, "Invalid",
                     {{simulation::DefinitionId{"quest.test.objective"},
                       quests::QuestObjectiveKind::talk, {}, 0, ""}}, {}});
    } catch (const std::invalid_argument&) {
        rejectedIncompleteObjective = true;
    }
    expect(rejectedIncompleteObjective,
           "QuestCatalog rejects objectives without target IDs or required count");

    bool rejectedDuplicateObjective = false;
    try {
        auto invalid = allKinds;
        invalid.objectives.push_back(invalid.objectives.front());
        invalid.id = simulation::DefinitionId{"quest.test.duplicate_objective"};
        catalog.add(std::move(invalid));
    } catch (const std::logic_error&) {
        rejectedDuplicateObjective = true;
    }
    expect(rejectedDuplicateObjective, "QuestCatalog rejects duplicate objective IDs");

    bool rejectedDuplicateQuest = false;
    try {
        catalog.add(allKinds);
    } catch (const std::logic_error&) {
        rejectedDuplicateQuest = true;
    }
    expect(rejectedDuplicateQuest, "QuestCatalog rejects duplicate quest definition IDs");
}

void testPhase11QuestState() {
    namespace quests = underworld::game::gameplay::quests;
    namespace simulation = underworld::simulation;

    const auto content = underworld::game::content::compileBuiltinContentOrThrow();
    const auto& definition = content.quests().require(quests::scholarQuestId());
    quests::QuestStateStore state;

    expect(state.status(definition.id) == quests::QuestStatus::inactive &&
               state.find(definition.id) == nullptr,
           "unstarted quests expose the inactive status without a runtime record");
    expect(state.start(definition) && state.status(definition.id) == quests::QuestStatus::active &&
               state.require(definition.id).objectives.size() == definition.objectives.size() &&
               state.require(definition.id).objectives[0].currentCount == 0,
           "starting a quest creates independent zeroed objective progress");
    expect(!state.start(definition) && state.size() == 1,
           "a quest cannot be started twice while its progress is active");

    expect(state.setObjectiveProgress(definition,
                                      simulation::DefinitionId{"quest.scholar.pickup"}, 99) &&
               quests::findObjectiveProgress(state.require(definition.id),
                                              simulation::DefinitionId{"quest.scholar.pickup"})
                       ->currentCount == 1 &&
               state.status(definition.id) == quests::QuestStatus::active,
           "setting one objective clamps to the immutable required count");
    expect(state.advanceObjective(definition,
                                  simulation::DefinitionId{"quest.scholar.kill"}) &&
               quests::findObjectiveProgress(state.require(definition.id),
                                              simulation::DefinitionId{"quest.scholar.kill"})
                       ->currentCount == 1 &&
               state.status(definition.id) == quests::QuestStatus::completed,
           "advancing the final objective completes the runtime quest state");
    expect(!state.advanceObjective(definition,
                                   simulation::DefinitionId{"quest.scholar.missing"}) &&
               !state.setObjectiveProgress(definition,
                                           simulation::DefinitionId{"quest.scholar.missing"}, 1),
           "unknown objectives do not mutate quest state");
    expect(!state.advanceObjective(definition,
                                   simulation::DefinitionId{"quest.scholar.kill"}) &&
               state.status(definition.id) == quests::QuestStatus::completed,
           "completed quests reject further objective progress");
    expect(state.reset(definition.id) && state.status(definition.id) == quests::QuestStatus::inactive &&
               state.find(definition.id) == nullptr && !state.reset(definition.id),
           "reset removes runtime progress and returns a quest to inactive");
}

void testPhase11QuestEvents() {
    namespace quests = underworld::game::gameplay::quests;
    namespace simulation = underworld::simulation;

    const auto id = [](std::string value) { return simulation::DefinitionId{std::move(value)}; };
    quests::QuestDefinition definition{id("quest.test.events"), "Event progression", {}, {}};
    definition.objectives = {
        {id("quest.test.talk"), quests::QuestObjectiveKind::talk, id("npc.test"), 1, "Talk"},
        {id("quest.test.kill"), quests::QuestObjectiveKind::kill, id("enemy.test"), 1, "Kill"},
        {id("quest.test.pickup"), quests::QuestObjectiveKind::pickup, id("pickup.test"), 2, "Pick up"},
        {id("quest.test.enter"), quests::QuestObjectiveKind::enter, id("map.test"), 1, "Enter"},
        {id("quest.test.open"), quests::QuestObjectiveKind::open, id("object.test"), 1, "Open"},
        {id("quest.test.deliver"), quests::QuestObjectiveKind::deliver, id("item.test"), 2, "Deliver"},
    };
    quests::QuestCatalog catalog;
    catalog.add(definition);
    quests::QuestStateStore state;
    quests::QuestSystem system(catalog, state);

    expect(system.start(definition.id) && state.status(definition.id) == quests::QuestStatus::active,
           "QuestSystem starts a catalogued quest without polling world collections");

    simulation::EventBuffer events;
    events.emit(simulation::EntityDamaged{});
    events.emit(simulation::NpcTalked{{1, 1}, {2, 1}, id("npc.test")});
    events.emit(simulation::EntityDefeated{{1, 1}, {3, 1}, 4, id("enemy.test")});
    events.emit(simulation::PickupCollected{{1, 1}, {4, 1},
                                            simulation::PickupPayloadKind::item,
                                            id("item.test"), 2, id("pickup.test")});
    events.emit(simulation::MapEntered{simulation::MapId{"map.test"}});
    events.emit(simulation::ObjectOpened{{1, 1}, {5, 1}, id("object.test")});
    events.emit(simulation::ItemDelivered{{1, 1}, id("item.test"), 1});
    system.consume(events);
    expect(state.status(definition.id) == quests::QuestStatus::active &&
               quests::findObjectiveProgress(state.require(definition.id),
                                              id("quest.test.deliver"))->currentCount == 1,
           "QuestSystem consumes matching domain events and leaves partial objectives active");

    simulation::EventBuffer finalEvent;
    finalEvent.emit(simulation::ItemDelivered{{1, 1}, id("item.test"), 1});
    finalEvent.emit(simulation::ProjectileImpact{});
    system.consume(finalEvent.events());
    expect(state.status(definition.id) == quests::QuestStatus::completed &&
               state.require(definition.id).objectives.size() == definition.objectives.size(),
           "QuestSystem completes a multi-objective quest from event stream input");
    expect(!system.start(simulation::DefinitionId{"quest.missing"}),
           "QuestSystem rejects unknown quest IDs without mutating state");
}

void testPhase11QuestPersistence() {
    namespace quests = underworld::game::gameplay::quests;
    namespace save = underworld::game::save;
    namespace simulation = underworld::simulation;

    const auto content = underworld::game::content::compileBuiltinContentOrThrow();
    const auto& definition = content.quests().require(quests::scholarQuestId());
    quests::QuestStateStore state;
    expect(state.start(definition) &&
               state.advanceObjective(definition, simulation::DefinitionId{"quest.scholar.kill"}),
           "quest progress can be prepared for persistence without copying definitions");

    const auto map = makeSyntheticMap("map.test.quest.save", "map.test.quest.save");
    save::SaveData data;
    data.player.currentMapId = map.id;
    data.player.health = content.progressions().require(
        underworld::game::gameplay::rpg::defaultPlayerProgressionId()).baseStats.maximumHealth;
    data.progression = {underworld::game::gameplay::rpg::defaultPlayerProgressionId(), 137};
    data.bank.items[0] = underworld::game::gameplay::ItemStack{
        underworld::game::gameplay::lifePotionItemId(), 20};
    data.bank.gold = 500;
    data.quests = state;
    const save::SaveValidationCatalogs catalogs{&content.items(), {&map}, &content.quests(),
                                               &content.progressions()};
    expect(save::validateSaveData(data, catalogs).empty(),
           "save validation accepts active quest progress against the quest catalog");
    expect(!save::validateSaveData(data, {&content.items(), {&map}, nullptr,
                                          &content.progressions()}).empty(),
           "save validation rejects quest progress without a quest catalog");
    const auto encoded = save::serializeSave(data);
    expect(encoded.size() > 7 && encoded[6] == 8 && encoded[7] == 0,
           "world state persistence advances DSAV to minor version 8");
    const auto loaded = save::deserializeSave(encoded, catalogs);
    expect(loaded && loaded.data.progression.totalExperience == 137 &&
               loaded.data.bank.items[0] && loaded.data.bank.items[0]->quantity == 20 &&
               loaded.data.bank.gold == 500 &&
               loaded.data.quests.snapshot() == data.quests.snapshot() &&
               encoded == save::serializeSave(loaded.data),
           "DSAV QSTS roundtrips quest status and objective counters deterministically");

    auto legacy = data;
    static_cast<void>(legacy.quests.reset(definition.id));
    legacy.world.objects.clear();
    legacy.world.pickups.clear();
    auto legacyBytes = save::serializeSave(legacy);
    removeSaveChunk(legacyBytes, "PROG");
    removeSaveChunk(legacyBytes, "EQIP");
    removeSaveChunk(legacyBytes, "BANK");
    removeSaveChunk(legacyBytes, "WRLD");
    removeSaveChunk(legacyBytes, "ENCT");
    legacyBytes[6] = 1;
    legacyBytes[7] = 0;
    const auto legacyLoaded = save::deserializeSave(legacyBytes, catalogs);
    expect(legacyLoaded && legacyLoaded.data.progression.totalExperience == 0 &&
               legacyLoaded.data.quests.size() == 0,
           "DSAV 1.1 saves without QSTS remain backward compatible");

    auto incompatible = encoded;
    incompatible[6] = 1;
    incompatible[7] = 0;
    expect(!save::deserializeSave(incompatible, catalogs),
           "DSAV 1.1 rejects a future QSTS chunk instead of silently dropping quest progress");

    auto corrupt = encoded;
    const std::array<std::uint8_t, 4> qstsTag{'Q', 'S', 'T', 'S'};
    const auto qstsPosition = std::search(corrupt.begin(), corrupt.end(),
                                          qstsTag.begin(), qstsTag.end());
    bool malformedRejected = false;
    if (qstsPosition != corrupt.end()) {
        const auto countOffset = static_cast<std::size_t>(qstsPosition - corrupt.begin()) + 12;
        for (std::size_t index = 0; index < sizeof(std::uint32_t); ++index) {
            corrupt[countOffset + index] = 0xff;
        }
        malformedRejected = !save::deserializeSave(corrupt, catalogs);
    }
    expect(malformedRejected, "DSAV rejects an oversized persistent quest state chunk");

    quests::QuestProgress invalid{definition.id, quests::QuestStatus::active,
                                  {{definition.objectives[0].id, 99},
                                   {definition.objectives[1].id, 0}}};
    quests::QuestStateStore restored;
    expect(!restored.restore(std::span<const quests::QuestProgress>{&invalid, 1},
                             content.quests()),
           "quest state restore rejects counters above definition requirements");
}

void testNearestImageRegions() {
    using underworld::core::ColorRGBA8;
    constexpr ColorRGBA8 black{0, 0, 0, 255};
    constexpr ColorRGBA8 red{255, 0, 0, 255};
    constexpr ColorRGBA8 green{0, 255, 0, 255};
    constexpr ColorRGBA8 blue{0, 0, 255, 255};
    constexpr ColorRGBA8 white{255, 255, 255, 255};
    const underworld::render::Image image(makeImageData(2, 2, {red, green, blue, white}));

    underworld::render::Framebuffer framebuffer(4, 4);
    underworld::render::Renderer2D renderer(framebuffer);
    framebuffer.clear(ColorRGBA8{0, 0, 0, 255});
    renderer.drawImageRegionNearest(image, {0, 0, 2, 2}, {0, 0, 4, 4});
    expect(framebufferPixel(framebuffer, 0, 0) == red && framebufferPixel(framebuffer, 1, 1) == red &&
               framebufferPixel(framebuffer, 2, 0) == green && framebufferPixel(framebuffer, 0, 2) == blue &&
               framebufferPixel(framebuffer, 3, 3) == white,
           "drawImageRegionNearest uses nearest-neighbor scaling");

    framebuffer.clear(black);
    renderer.drawImageRegionNearest(image, {1, 0, 1, 2}, {0, 0, 2, 4});
    expect(framebufferPixel(framebuffer, 0, 0) == green && framebufferPixel(framebuffer, 1, 1) == green &&
               framebufferPixel(framebuffer, 0, 3) == white,
           "drawImageRegionNearest honors the selected source sub-region");

    framebuffer.clear(black);
    renderer.drawImageRegionNearest(image, {0, 0, 2, 2}, {-2, -2, 4, 4});
    expect(framebufferPixel(framebuffer, 0, 0) == white && framebufferPixel(framebuffer, 1, 1) == white &&
               framebufferPixel(framebuffer, 2, 2) == black,
           "drawImageRegionNearest clips destination without escaping the framebuffer");

    framebuffer.clear(black);
    renderer.drawImageRegionNearest(image, {0, 0, 2, 2}, {0, 0, 4, 4}, true);
    expect(framebufferPixel(framebuffer, 0, 0) == green && framebufferPixel(framebuffer, 3, 0) == red &&
               framebufferPixel(framebuffer, 0, 3) == white && framebufferPixel(framebuffer, 3, 3) == blue,
           "drawImageRegionNearest flips horizontally while preserving nearest sampling");

    const underworld::render::Image transparent(makeImageData(1, 1, {{255, 0, 0, 0}}));
    framebuffer.clear(blue);
    renderer.drawImageRegionNearest(transparent, {0, 0, 1, 1}, {0, 0, 2, 2});
    expect(framebufferPixel(framebuffer, 0, 0) == blue && framebufferPixel(framebuffer, 1, 1) == blue,
           "drawImageRegionNearest preserves alpha blending semantics");
}

void testPhase12AProgressionFoundation() {
    namespace rpg = underworld::game::gameplay::rpg;
    namespace content = underworld::game::content;
    static_assert(!std::is_same_v<content::AuthoredPlayerProgression,
                                  rpg::PlayerProgressionDefinition>);

    rpg::PlayerProgressionDefinition definition{{"progression.test"}, {5}, {0, 100, 250}};
    rpg::PlayerProgressionState state(definition);
    expect(state.level() == 1 && state.nextLevelExperienceThreshold() == 100 &&
               state.experienceNeededForNextLevel() == 100,
           "progression starts at level one with the first cumulative threshold");
    expect(state.grantExperience(99).newLevel == 1 &&
               state.experienceNeededForNextLevel() == 1,
           "progression remains below the exact level threshold");
    const auto levelTwo = state.grantExperience(1);
    expect(levelTwo.leveledUp() && levelTwo.previousLevel == 1 &&
               levelTwo.newLevel == 2 && state.level() == 2,
           "progression reaches level two at the exact cumulative threshold");
    expect(state.grantExperience(150).newLevel == 3 && state.level() == 3 &&
               !state.nextLevelExperienceThreshold() && !state.experienceNeededForNextLevel(),
           "progression supports multi-level gains and derives the maximum level");
    const auto zero = state.grantExperience(0);
    expect(zero.granted == 0 && zero.previousLevel == zero.newLevel &&
               state.totalExperience() == 250,
           "zero experience does not change progression state");
    rpg::PlayerProgressionState saturated(definition);
    static_cast<void>(saturated.restoreExperience(std::numeric_limits<std::uint64_t>::max() - 1));
    const auto saturatedGain = saturated.grantExperience(99);
    expect(saturatedGain.granted == 1 &&
               saturated.totalExperience() == std::numeric_limits<std::uint64_t>::max(),
           "experience accumulation saturates instead of overflowing");

    const auto compiled = content::compileBuiltinContentOrThrow();
    const auto& builtin = compiled.progressions().require(rpg::defaultPlayerProgressionId());
    expect(builtin.baseStats.maximumHealth == 5 &&
               builtin.cumulativeExperienceThresholds == std::vector<std::uint64_t>{0, 100, 250},
           "builtin player progression preserves the provisional health and curve");
    auto invalid = content::makeBuiltinAuthoredContent();
    invalid.playerProgressions.front().cumulativeExperienceThresholds = {0, 100, 100};
    const auto invalidResult = content::compileContent(invalid);
    expect(!invalidResult && std::any_of(invalidResult.report.diagnostics.begin(),
               invalidResult.report.diagnostics.end(), [](const auto& diagnostic) {
                   return diagnostic.kind == content::ContentKind::playerProgression &&
                          diagnostic.code == "invalid_curve";
               }),
           "content validation rejects a non-increasing progression curve");
}

void testPhase9EditorFoundation() {
    namespace editor = underworld::editor;
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;

    auto content = game::content::compileBuiltinContentOrThrow();
    expect(content.enemies().find(gameplay::creatures::soldierEnemyId()) &&
               content.enemies().find(gameplay::creatures::skullEnemyId()) &&
               content.objects().find(simulation::DefinitionId{"object.chest"}) &&
               content.objects().find(simulation::DefinitionId{"object.crate"}) &&
               content.items().find(gameplay::lifePotionItemId()) &&
               content.pickup(simulation::DefinitionId{"pickup.money"}),
           "shared GameContentRegistry resolves runtime and editor definitions from one registration");
    expect(content.authoringDescriptors(game::AuthoringCategory::enemy).size() == 2 &&
               content.authoringDescriptors(game::AuthoringCategory::object).size() == 7 &&
               content.authoringDescriptors(game::AuthoringCategory::pickup).size() == 3,
           "authoring palette is derived from shared content descriptors");

    auto document = editor::EditorDocument::newMap(simulation::MapId{"map.editor.test"}, 4, 3);
    expect(document.dirty() && document.data().layers.size() == 1 &&
               document.data().layers[0].name == "Ground" && document.data().collision.size() == 12,
           "New Map creates Ground and collision authoring data without arbitrary content");
    auto authoredDocument = editor::EditorDocument::newAuthoredMap(
        simulation::MapId{"map.editor.authored"}, 10, 8, 16, content);
    const auto& authoredData = authoredDocument.data();
    const auto authoredSurface = [&] {
        for (const auto& reference : authoredData.tileReferences) {
            if (const auto* semantic = content.authoringSemantics().findTile(
                    reference.tilesetId, reference.sourceIndex);
                semantic && semantic->id.value() == std::string_view{"tile.dungeon.masonry.39"}) {
                return true;
            }
        }
        return false;
    }();
    expect(authoredData.layers.size() == 5 && authoredData.layers[0].name == "ground" &&
               authoredData.layers[1].name == "walls" && authoredData.layers[0].cells[11].has_value() &&
               authoredData.collision[0] != 0 && authoredData.playerSpawns.size() == 1 &&
               authoredData.collision[static_cast<std::size_t>(4) * authoredData.width + 5U] == 0 &&
               authoredSurface,
           "authored New Map creates canonical layers, a dataset surface, collision boundary, and safe spawn");
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

    auto collisionDocument = editor::EditorDocument::newMap(
        simulation::MapId{"map.editor.collision"}, 4, 3, 16, false);
    const std::vector<editor::TileCoordinate> singleCell{{1, 1}};
    expect(collisionDocument.execute(std::make_unique<editor::SetCollisionCommand>(singleCell, true), error) &&
               collisionDocument.data().collision[5] == 1 && collisionDocument.undo() &&
               collisionDocument.data().collision[5] == 0 && collisionDocument.redo(error),
           "collision solid paint is one undoable command with redo");
    expect(collisionDocument.execute(std::make_unique<editor::SetCollisionCommand>(singleCell, false), error) &&
               collisionDocument.data().collision[5] == 0 && collisionDocument.undo() &&
               collisionDocument.data().collision[5] == 1,
           "collision erase restores its prior solid cell on undo");
    const auto collisionRectangle = editor::rectangleCells(0, 0, 1, 1, collisionDocument.data());
    expect(collisionDocument.execute(std::make_unique<editor::SetCollisionCommand>(collisionRectangle, true), error) &&
               collisionDocument.data().collision[0] == 1 && collisionDocument.undo() &&
               collisionDocument.data().collision[0] == 0,
           "collision solid rectangle is undoable");
    expect(collisionDocument.execute(std::make_unique<editor::SetCollisionCommand>(collisionRectangle, false), error) &&
               collisionDocument.data().collision[5] == 0 && collisionDocument.undo() &&
               collisionDocument.data().collision[5] == 1,
           "collision erase rectangle is undoable");
    const auto solidFill = editor::collisionFloodCells(collisionDocument.data(), 2, 2);
    expect(collisionDocument.execute(std::make_unique<editor::SetCollisionCommand>(solidFill, true), error) &&
               collisionDocument.data().collision[10] == 1 && collisionDocument.undo() &&
               collisionDocument.data().collision[10] == 0,
           "collision solid flood fill is iterative and undoable");
    const auto eraseFill = editor::collisionFloodCells(collisionDocument.data(), 1, 1);
    expect(collisionDocument.execute(std::make_unique<editor::SetCollisionCommand>(eraseFill, false), error) &&
               collisionDocument.data().collision[5] == 0 && collisionDocument.undo() &&
               collisionDocument.data().collision[5] == 1,
           "collision erase flood fill is iterative and undoable");
    const auto collisionHistory = collisionDocument.history().size();
    expect(!collisionDocument.execute(std::make_unique<editor::SetCollisionCommand>(
               std::vector<editor::TileCoordinate>{{99,99}}, true), error) &&
               collisionDocument.history().size() == collisionHistory &&
               collisionDocument.data().collision[0] == 0,
           "outside-map collision cells are safe no-ops without history");

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
    document.layerStates()[0].locked = true;
    expect(!document.execute(std::make_unique<editor::PaintTilesCommand>(0,
               std::vector<editor::TileCoordinate>{{0,0}}, tile), error) &&
               document.history().size() == 2,
           "locked layer rejects tile edits without creating history");
    expect(!document.execute(std::make_unique<editor::PaintTilesCommand>(0,
               std::vector<editor::TileCoordinate>{{0,0}}, std::nullopt), error) &&
               !document.execute(std::make_unique<editor::PaintTilesCommand>(0,
               editor::rectangleCells(0, 0, 1, 1, document.data()), tile), error) &&
               !document.execute(std::make_unique<editor::PaintTilesCommand>(0,
               editor::tileFloodCells(document.data(), 0, 0, 0), tile), error) &&
               document.history().size() == 2,
           "locked layer rejects erase rectangle and flood fill without document history");
    document.layerStates()[0].locked = false;
    expect(editor::worldPointToTile(document.data(), {-1,0}) == std::nullopt &&
               editor::worldPointToTile(document.data(), {0,-1}) == std::nullopt &&
               editor::worldPointToTile(document.data(), {15,15})->x == 0 &&
               editor::worldPointToTile(document.data(), {16,0})->x == 1 &&
               !editor::worldPointToTile(document.data(), {64,0}),
           "world-to-tile rejects negative and beyond-map coordinates without clamping");

    auto placementDocument = editor::EditorDocument(makeSyntheticMap());
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

    const auto spawnBefore = placementDocument.data().playerSpawns[0].position;
    const auto linkBefore = underworld::core::WorldPointI{placementDocument.data().links[0].trigger.x,
                                                          placementDocument.data().links[0].trigger.y};
    expect(placementDocument.execute(std::make_unique<editor::MoveEntityCommand>(
               editor::SelectionKind::playerSpawn, simulation::PersistentInstanceId{}, spawnBefore,
               underworld::core::WorldPointI{48,48}, std::string(placementDocument.data().playerSpawns[0].id.value())), error) &&
               placementDocument.undo() && placementDocument.data().playerSpawns[0].position == spawnBefore &&
               placementDocument.execute(std::make_unique<editor::MoveEntityCommand>(
               editor::SelectionKind::mapLink, simulation::PersistentInstanceId{}, linkBefore,
               underworld::core::WorldPointI{48,48}, placementDocument.data().links[0].id), error),
           "PlayerSpawn and MapLink moves are authored commands with stable string identities");
    const auto spawnCopy = editor::duplicateAuthoredPlacement(placementDocument,
        editor::SelectionKind::playerSpawn, placementDocument.data().playerSpawns[0].id.value(), 16);
    const auto linkCopy = editor::duplicateAuthoredPlacement(placementDocument,
        editor::SelectionKind::mapLink, placementDocument.data().links[0].id, 16);
    expect(spawnCopy && linkCopy && std::get<maps::PlayerSpawn>(*spawnCopy).id != placementDocument.data().playerSpawns[0].id &&
               std::get<maps::MapLink>(*linkCopy).id != placementDocument.data().links[0].id,
           "Spawn and MapLink duplication generates deterministic unique authored IDs");

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
    editor::PropertyOverrideSet placedOverrides;
    placedOverrides.emplace(schemas[0].id, std::int64_t{160});
    const simulation::PersistentInstanceId placedEnemyId{99};
    expect(placementDocument.execute(std::make_unique<editor::PlaceEntityCommand>(
               maps::EnemyPlacement{placedEnemyId, gameplay::creatures::soldierEnemyId(), {96, 64},
                                    gameplay::FacingDirection::down}, placedOverrides), error) &&
               placementDocument.propertyOverrides().contains(placedEnemyId.value) &&
               placementDocument.undo() && !placementDocument.propertyOverrides().contains(placedEnemyId.value) &&
               placementDocument.redo(error) && placementDocument.propertyOverrides().contains(placedEnemyId.value),
           "placing a persistent enemy registers overrides and undo redo preserves their lifecycle");
    expect(placementDocument.execute(std::make_unique<editor::PlaceEntityCommand>(
               maps::PlayerSpawn{simulation::SpawnId{"entry.editor"}, {96, 96},
                                 gameplay::FacingDirection::down}, placedOverrides), error) &&
               placementDocument.execute(std::make_unique<editor::PlaceEntityCommand>(
               maps::MapLink{"link.editor", {96, 112, 16, 16}, placementDocument.data().id,
                             simulation::SpawnId{"entry.editor"}}, placedOverrides), error) &&
               !placementDocument.propertyOverrides().contains(0) &&
               placementDocument.propertyOverrides().size() == 1,
           "PlayerSpawn and MapLink commands never treat textual IDs as persistent override keys");
    expect(placementDocument.execute(std::make_unique<editor::SetPropertyCommand>(
               simulation::PersistentInstanceId{1}, schemas[0], editor::PropertyValue{std::int64_t{160}},
               content), error) &&
               std::get<std::int64_t>(placementDocument.propertyOverrides().at(1).at(schemas[0].id)) == 160,
           "valid typed property override commits through EditorCommand");
    expect(placementDocument.execute(std::make_unique<editor::DeleteEntityCommand>(
               editor::SelectionKind::enemy, simulation::PersistentInstanceId{1}), error) &&
               !placementDocument.propertyOverrides().contains(1) && placementDocument.undo() &&
               placementDocument.propertyOverrides().contains(1),
           "delete and undo preserve an enemy's typed authoring overrides with its placement");
    const auto duplicateEnemyId = placementDocument.allocatePersistentId();
    const auto duplicateEnemy = editor::duplicatePlacement(placementDocument,
        editor::SelectionKind::enemy, simulation::PersistentInstanceId{1}, duplicateEnemyId, 16);
    expect(duplicateEnemy && placementDocument.execute(std::make_unique<editor::PlaceEntityCommand>(
               *duplicateEnemy, placementDocument.propertyOverrides().at(1)), error) &&
               placementDocument.propertyOverrides().contains(duplicateEnemyId.value) &&
               placementDocument.execute(std::make_unique<editor::SetPropertyCommand>(duplicateEnemyId,
               schemas[0], editor::PropertyValue{std::int64_t{161}}, content), error) &&
               std::get<std::int64_t>(placementDocument.propertyOverrides().at(1).at(schemas[0].id)) == 160 &&
               std::get<std::int64_t>(placementDocument.propertyOverrides().at(duplicateEnemyId.value).at(schemas[0].id)) == 161,
           "duplicate placement copies overrides to its new id without aliasing the original");
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

    auto roundtripMap = makeSyntheticMap("map.test.roundtrip", "map.test.other");
    const auto temporary = std::filesystem::temp_directory_path() / "underworld_editor_roundtrip.umap";
    const auto runtimeExport = std::filesystem::temp_directory_path() /
                               "underworld_editor_roundtrip.dmap";
    editor::EditorDocument roundtrip(roundtripMap);
    expect(roundtrip.saveAs(temporary, content, error) && !roundtrip.dirty() &&
               roundtrip.exportDmap(runtimeExport, content, error),
           "EditorDocument Save As writes authored UMAP and exposes explicit DMAP export");
    const auto reopened = editor::EditorDocument::open(temporary, content, error);
    expect(reopened && maps::semanticallyEqual(roundtripMap, reopened->data()) &&
               !reopened->dirty() && reopened->filePath() == temporary,
           "UMAP open save reopen preserves semantic MapData equality");
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    std::filesystem::remove(runtimeExport, removeError);

    auto playtestSource = makeSyntheticMap("map.test.playtest", "map.test.playtest");
    editor::EditorDocument playtestDocument(playtestSource);
    const auto beforePlaytest = playtestDocument.data();
    editor::EditorPlaytestSession playtest;
    expect(playtest.start(playtestDocument.data(), content, error) && playtest.active() &&
               playtest.sourceData() && maps::semanticallyEqual(*playtest.sourceData(), beforePlaytest) &&
               playtest.world() && playtest.world()->spawn().id == simulation::SpawnId{"entry.start"} &&
               playtest.world()->enemies().size() == playtestSource.enemies.size() &&
               playtest.world()->objects().size() == playtestSource.objects.size() &&
               playtest.world()->pickups().size() == playtestSource.pickups.size(),
           "editor playtest builds a temporary RuntimeWorld through the normal builder pipeline");
    expect(maps::semanticallyEqual(playtestDocument.data(), beforePlaytest),
           "editor playtest does not mutate the source EditorDocument");
    playtest.stop();
    expect(!playtest.active() && !playtest.sourceData(),
           "stopping editor playtest releases the temporary world and snapshot");

    editor::EditorDocument backupDocument(playtestSource);
    const auto authoredPath = std::filesystem::temp_directory_path() /
                              "underworld_editor_authored.umap";
    const auto backupPath = std::filesystem::temp_directory_path() /
                            "underworld_editor_authored.autosave.umap";
    std::filesystem::remove(authoredPath, removeError);
    std::filesystem::remove(backupPath, removeError);
    expect(backupDocument.saveAs(authoredPath, content, error) &&
               backupDocument.execute(std::make_unique<editor::SetCollisionCommand>(
                   std::vector<editor::TileCoordinate>{{1, 1}}, true), error) &&
               backupDocument.dirty() && backupDocument.saveBackup(backupPath, content, error) &&
               backupDocument.dirty() && backupDocument.filePath() == authoredPath,
           "editor autosave backup writes a validated sidecar without clearing dirty state or path");
    const auto backupLoaded = maps::readAuthoredMapFile(backupPath);
    expect(backupLoaded.source && maps::mapDataFromAuthored(*backupLoaded.source).collision[5] == 1 &&
               backupDocument.autosavePath() && *backupDocument.autosavePath() == backupPath,
           "editor autosave sidecar preserves the authored document through the official UMAP reader");
    expect(!backupDocument.saveBackup(authoredPath, content, error),
           "editor backup rejects replacing the authored document path");
    std::filesystem::remove(authoredPath, removeError);
    std::filesystem::remove(backupPath, removeError);
}

void testPhase12D2BankInterface() {
    using namespace underworld;
    using namespace game::gameplay;
    const auto content = game::content::compileBuiltinContentOrThrow();
    const auto& bankObject = content.objects().require({"object.bank_access"});
    expect(bankObject.bankAccess && bankObject.interactable && !bankObject.container &&
               !bankObject.destructible, "builtin bank access is an interactable capability");

    auto authored = game::content::makeBuiltinAuthoredContent();
    auto& authoredBank = authored.objects.back();
    authoredBank.interactable.reset();
    expect(game::content::ContentValidator{}.validate(authored).hasErrors(),
           "bank access without interaction is rejected by content validation");
    authored = game::content::makeBuiltinAuthoredContent();
    authored.objects.back().container = ObjectContainerDefinition{1};
    expect(game::content::ContentValidator{}.validate(authored).hasErrors(),
           "bank access combined with a container is rejected");

    ItemCatalog catalog;
    catalog.add(makeLifePotionDefinition());
    ItemContainer source(3, catalog);
    ItemContainer destination(1, catalog);
    static_cast<void>(source.add(lifePotionItemId(), 66));
    static_cast<void>(source.add(lifePotionItemId(), 20));
    static_cast<void>(destination.add(lifePotionItemId(), 56));
    expect(source.transferSlotTo(destination, 1, 20) == 10 &&
               source.slot(0)->quantity == 66 && source.slot(1)->quantity == 10 &&
               destination.slot(0)->quantity == 66,
           "slot-aware transfer uses the selected stack and preserves partial remainder");
    expect(source.transferSlotTo(destination, 2, 1) == 0 &&
               source.transferSlotTo(source, 1, 1) == 0 &&
               source.transferSlotTo(destination, 1, 0) == 0,
           "slot-aware transfer handles empty, same-container and zero requests");

    PlayerItems items(content.items());
    static_cast<void>(items.inventory().items().add(lifePotionItemId(), 4));
    BankOverlayState overlay;
    overlay.toggle();
    simulation::PlayerCommand command{};
    command.actions.primaryAttackPressed = true;
    expect(routeBankCommand(overlay, command, items).itemsMoved == 4 &&
               items.inventory().items().count(lifePotionItemId()) == 0 &&
               items.bank().items().count(lifePotionItemId()) == 4,
           "bank command deposits the complete selected inventory stack");
    command = {};
    command.movement.y = 1;
    for (int index = 0; index < 8; ++index) { overlay.moveSelection(0, 1); }
    expect(overlay.focus() == BankOverlayFocus::gold,
           "bank overlay reaches gold focus after the bottom storage row");
    command = {};
    command.actions.primaryAttackPressed = true;
    expect(overlay.goldSelection() == BankGoldSelection::carried &&
               routeBankCommand(overlay, command, items).goldMoved == 0,
           "empty carried gold deposit is a safe bank action");
}

void testSyntheticMapIntegrationFixture() {
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace creatures = underworld::game::gameplay::creatures;
    namespace maps = underworld::game::maps;
    namespace save = underworld::game::save;
    namespace simulation = underworld::simulation;

    const auto source = makeSyntheticMap("map.test.editor", "map.test.editor");
    auto content = game::content::compileBuiltinContentOrThrow();
    const auto validation = game::mapValidationCatalogs(content);
    expect(source.id == simulation::MapId{"map.test.editor"} && source.width == 4 && source.height == 3 &&
               source.tileSize == 16 && source.layers.size() >= 1 &&
               std::any_of(source.collision.begin(), source.collision.end(),
                           [](std::uint8_t value) { return value != 0; }),
           "synthetic editor fixture is a deterministic tiled map with collision");
    expect(static_cast<bool>(maps::validateMapData(source, &validation)),
           "synthetic editor fixture resolves only shared GameContentRegistry definitions");

    const auto bytes = maps::serializeDmap(source);
    const auto decoded = maps::deserializeDmap(bytes, &validation);
    expect(decoded && maps::semanticallyEqual(source, decoded.data),
           "synthetic fixture roundtrips through the official DMAP v1 reader and writer");
    expect(decoded.data.playerSpawns.size() == 2 && decoded.data.enemies.size() == 1 &&
               decoded.data.objects.size() == 2 && decoded.data.pickups.size() == 2 &&
               decoded.data.links.size() == 1 && decoded.data.enemies[0].id.value == 1 &&
               decoded.data.objects[0].id.value == 2 &&
               decoded.data.objects[1].id.value == 3 && decoded.data.pickups[0].id.value == 4 &&
               decoded.data.pickups[1].id.value == 5 &&
               decoded.data.enemies[0].definitionId == creatures::soldierEnemyId() &&
               decoded.data.objects[0].definitionId == simulation::DefinitionId{"object.chest"} &&
               decoded.data.objects[1].definitionId == simulation::DefinitionId{"object.crate"} &&
               decoded.data.pickups[0].definitionId == simulation::DefinitionId{"pickup.money"} &&
               decoded.data.pickups[1].definitionId == simulation::DefinitionId{"pickup.life_potion"} &&
               decoded.data.links[0].targetMapId == simulation::MapId{"map.test.editor"} &&
               decoded.data.links[0].targetSpawnId == simulation::SpawnId{"entry.return"} &&
               std::any_of(decoded.data.playerSpawns.begin(), decoded.data.playerSpawns.end(),
                           [&](const maps::PlayerSpawn& spawn) {
                               return spawn.id == decoded.data.links[0].targetSpawnId;
                           }),
           "DMAP persists synthetic placement identities without runtime handles");

    simulation::EntityHandlePool handles;
    const std::array visuals{creatures::soldierVisualId(), creatures::skullVisualId()};
    creatures::EnemyFactory enemyFactory(handles, content.enemies(), content.behaviors(),
                                         content.attacks(), content.projectiles(), visuals);
    gameplay::WorldObjectFactory objectFactory(handles, content.objects(), content.items());
    const game::RuntimeTilesetCatalog runtimeTilesets(content.tilesets());
    maps::RuntimeWorldBuilder builder(validation, enemyFactory, objectFactory, handles,
        runtimeTilesets);
    const auto runtime = builder.build(decoded.data, simulation::SpawnId{"entry.start"});
    expect(runtime && runtime.world->spawn().id == simulation::SpawnId{"entry.start"} &&
               runtime.world->enemies().size() == 1 && runtime.world->objects().size() == 2 &&
               runtime.world->pickups().size() == 2 && runtime.world->enemies()[0].persistentId.value == 1 &&
               runtime.world->objects()[0].persistentId.value == 2 &&
               runtime.world->objects()[1].persistentId.value == 3 &&
               runtime.world->pickups()[0].persistentId.value == 4 &&
               runtime.world->pickups()[1].persistentId.value == 5 &&
               runtime.world->map().collision().isSolid(3, 0) &&
               runtime.world->enemies()[0].instance.handle() && runtime.world->objects()[0].instance.handle() &&
               runtime.world->pickups()[0].instance.handle(),
           "RuntimeWorldBuilder creates the smoke fixture placements and allocates handles only at runtime");
}

void testOfficialGameplayMapAuthoringAsset() {
    namespace editor = underworld::editor;
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace creatures = underworld::game::gameplay::creatures;
    namespace npcs = underworld::game::gameplay::npcs;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;

    std::filesystem::path root = std::filesystem::current_path();
    std::filesystem::path gameplayMap;
    for (;;) {
        const auto candidate = root / "maps" / "gameplay" / "dungeon_01_entry.dmap";
        if (std::filesystem::exists(candidate)) {
            gameplayMap = candidate;
            break;
        }
        const auto parent = root.parent_path();
        if (parent == root) { break; }
        root = parent;
    }
    auto content = game::content::compileBuiltinContentOrThrow();
    const auto validation = game::mapValidationCatalogs(content);
    const auto loaded = gameplayMap.empty() ? maps::DmapLoadResult{}
                                            : maps::readDmap(gameplayMap, &validation);
    if (!loaded || loaded.data.id != simulation::MapId{"map.dungeon.01"} ||
        loaded.data.width != 24 || loaded.data.height != 18) {
        return;
    }
    expect(loaded && loaded.data.id == simulation::MapId{"map.dungeon.01"} &&
               loaded.data.width == 24 && loaded.data.height == 18 && loaded.data.tileSize == 16 &&
               !loaded.data.layers.empty() && !loaded.data.playerSpawns.empty(),
           "official gameplay asset is a readable DMAP v1 map with stable broad invariants");
    expect(loaded && maps::validateMapData(loaded.data, &validation),
           "official gameplay asset validates against shared game content definitions");
    expect(loaded && std::all_of(loaded.data.npcs.begin(), loaded.data.npcs.end(), [&](const auto& npc) {
               const auto x = npc.position.x / static_cast<int>(loaded.data.tileSize);
               const auto y = npc.position.y / static_cast<int>(loaded.data.tileSize);
               return x >= 0 && y >= 0 && static_cast<std::uint32_t>(x) < loaded.data.width &&
                      static_cast<std::uint32_t>(y) < loaded.data.height &&
                      loaded.data.collision[static_cast<std::size_t>(y) * loaded.data.width +
                                             static_cast<std::size_t>(x)] == 0;
           }),
           "official NPC placements are inside the authored map and outside collision");

    simulation::EntityHandlePool handles;
    const std::array visuals{creatures::soldierVisualId(), creatures::skullVisualId()};
    creatures::EnemyFactory enemyFactory(handles, content.enemies(), content.behaviors(),
                                         content.attacks(), content.projectiles(), visuals);
    gameplay::WorldObjectFactory objectFactory(handles, content.objects(), content.items());
    npcs::NpcFactory npcFactory(handles, content.npcs());
    const game::RuntimeTilesetCatalog runtimeTilesets(content.tilesets());
    maps::RuntimeWorldBuilder builder(validation, enemyFactory, objectFactory, handles,
        runtimeTilesets, &npcFactory);
    std::string startupError;
    const auto startupSpawn = loaded
        ? game::selectStartupSpawn(loaded.data, std::nullopt, startupError)
        : std::optional<simulation::SpawnId>{};
    const auto selectedRuntime = loaded && startupSpawn
        ? builder.build(loaded.data, *startupSpawn)
                                : maps::RuntimeWorldBuildResult{};
    expect(startupSpawn && *startupSpawn == loaded.data.playerSpawns.front().id,
           "authored map resolves a deterministic PlayerSpawn for game startup");
    expect(selectedRuntime && selectedRuntime.world->map().collision().width() == 24 &&
               selectedRuntime.world->map().collision().height() == 18,
           "official gameplay DMAP builds through RuntimeWorldBuilder with runtime-only handles");
    expect(selectedRuntime && selectedRuntime.world->enemies().size() == loaded.data.enemies.size() &&
               selectedRuntime.world->objects().size() == loaded.data.objects.size() &&
               selectedRuntime.world->pickups().size() == loaded.data.pickups.size() &&
               selectedRuntime.world->npcs().size() == loaded.data.npcs.size(),
           "official enemy object pickup and NPC placements become runtime instances");

    std::string error;
    auto document = gameplayMap.empty() ? std::optional<editor::EditorDocument>{}
                                        : editor::EditorDocument::open(gameplayMap, content, error);
    const auto copy = std::filesystem::temp_directory_path() / "underworld_gameplay_map_copy.umap";
    std::error_code removeError;
    std::filesystem::remove(copy, removeError);
    const bool savedCopy = document && !document->dirty() && !document->hasExperimentalData() &&
                           document->saveAs(copy, content, error);
    const auto reopened = savedCopy ? editor::EditorDocument::open(copy, content, error)
                                    : std::optional<editor::EditorDocument>{};
    expect(savedCopy && reopened && !reopened->dirty() &&
               maps::semanticallyEqual(document->data(), reopened->data()),
           "Map Editor imports an official DMAP and saves a persistable authored UMAP copy");
    std::filesystem::remove(copy, removeError);
}

void testOfficialGameplayMapSet() {
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace creatures = underworld::game::gameplay::creatures;
    namespace npcs = underworld::game::gameplay::npcs;
    namespace dialogue = underworld::game::gameplay::dialogue;
    namespace quests = underworld::game::gameplay::quests;
    namespace maps = underworld::game::maps;
    namespace save = underworld::game::save;
    namespace simulation = underworld::simulation;

    auto content = game::content::compileBuiltinContentOrThrow();
    const auto validation = game::mapValidationCatalogs(content);
    const auto manifest = maps::officialGameplayMaps();
    bool completeFixture = true;
    for (const auto& entry : manifest) {
        const auto path = maps::resolveOfficialGameplayMapPath(
            entry.relativePath, std::filesystem::current_path(), std::filesystem::current_path());
        if (!path || !maps::readDmap(*path, &validation)) {
            completeFixture = false;
            break;
        }
    }
    if (!completeFixture) { return; }
    expect(manifest.size() == 3, "official gameplay manifest contains exactly three maps");
    std::vector<maps::MapData> loadedMaps;
    std::vector<std::filesystem::path> paths;
    std::vector<std::string> coveredTiles;
    std::vector<std::string> coveredStamps;
    auto addUnique = [](std::vector<std::string>& values, std::string_view value) {
        if (std::find(values.begin(), values.end(), value) == values.end()) {
            values.emplace_back(value);
        }
    };
    auto tileAt = [&](const maps::MapData& map, std::size_t layer, int x, int y)
        -> const underworld::game::authoring::TileSemanticDefinition* {
        if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) >= map.width ||
            static_cast<std::uint32_t>(y) >= map.height) { return nullptr; }
        const auto cell = map.layers[layer].cells[static_cast<std::size_t>(y) * map.width +
                                                   static_cast<std::size_t>(x)];
        if (!cell || *cell >= map.tileReferences.size()) { return nullptr; }
        const auto& ref = map.tileReferences[*cell];
        return content.authoringSemantics().findTile(ref.tilesetId, ref.sourceIndex);
    };
    for (const auto& entry : manifest) {
        const auto path = maps::resolveOfficialGameplayMapPath(
            entry.relativePath, std::filesystem::current_path(), std::filesystem::current_path());
        const auto loaded = path ? maps::readDmap(*path, &validation) : maps::DmapLoadResult{};
        expect(path && loaded && loaded.data.id == entry.id && loaded.data.width <= 28 &&
                   loaded.data.height <= 20,
               "official gameplay DMAP reads with independent persistent MapId and small bounds");
        if (!loaded) { continue; }
        paths.push_back(*path);
        loadedMaps.push_back(loaded.data);
        expect(maps::validateMapData(loaded.data, &validation).valid,
               "official gameplay map passes structural validation");
        const auto semanticReport = game::authoring::MapSemanticValidator{}.validate(
            loaded.data, content.authoringSemantics());
        const bool semanticClean = std::none_of(
            semanticReport.issues.begin(), semanticReport.issues.end(), [](const auto& issue) {
                return issue.code == "unclassified_tile" || issue.code == "forbidden_flip_x" ||
                       issue.severity == game::authoring::SemanticIssueSeverity::warning;
            });
        expect(semanticClean, "official gameplay map has no unclassified or forbidden semantic tiles");
        for (const auto& layer : loaded.data.layers) {
            for (const auto cell : layer.cells) {
                if (!cell || *cell >= loaded.data.tileReferences.size()) { continue; }
                const auto& ref = loaded.data.tileReferences[*cell];
                const auto* tile = content.authoringSemantics().findTile(ref.tilesetId, ref.sourceIndex);
                expect(tile != nullptr && !underworld::world::hasFlag(
                    ref.flags, underworld::world::TileFlags::flipX),
                    "official gameplay tile references resolve without transparent or forbidden cells");
                if (tile) { addUnique(coveredTiles, tile->id.value()); }
            }
        }
        const auto entrySpawn = entry.id == simulation::MapId{"map.dungeon.01"}
            ? simulation::SpawnId{"entry.start"}
            : entry.id == simulation::MapId{"map.dungeon.02"}
                ? simulation::SpawnId{"entry.from_01"}
                : simulation::SpawnId{"entry.from_02"};
        const auto spawn = std::find_if(loaded.data.playerSpawns.begin(),
            loaded.data.playerSpawns.end(), [&](const auto& value) { return value.id == entrySpawn; });
        expect(spawn != loaded.data.playerSpawns.end(), "official gameplay entry spawn exists");
        if (spawn != loaded.data.playerSpawns.end()) {
            std::vector<underworld::core::TileCoord> targets;
            for (const auto& link : loaded.data.links) {
                targets.push_back({(link.trigger.x + link.trigger.width / 2) /
                                       static_cast<int>(loaded.data.tileSize),
                                   (link.trigger.y + link.trigger.height / 2) /
                                       static_cast<int>(loaded.data.tileSize)});
            }
            const auto start = underworld::core::worldToTile(spawn->position, loaded.data.tileSize);
            expect(maps::ReachabilityValidator{}.validate(loaded.data, start, targets).valid(),
                   "official gameplay map reaches every declared exit from its entry spawn");
        }
        for (std::size_t layerIndex = 0; layerIndex < loaded.data.layers.size(); ++layerIndex) {
            for (const auto& stamp : content.authoringSemantics().stamps()) {
                bool found = false;
                for (std::uint32_t y = 0; y < loaded.data.height && !found; ++y) {
                    for (std::uint32_t x = 0; x < loaded.data.width && !found; ++x) {
                        if (x + stamp.width > loaded.data.width || y + stamp.height > loaded.data.height) continue;
                        bool matches = true;
                        for (const auto& member : stamp.cells) {
                            matches = matches && tileAt(loaded.data, layerIndex,
                                static_cast<int>(x) + member.x, static_cast<int>(y) + member.y) != nullptr &&
                                tileAt(loaded.data, layerIndex, static_cast<int>(x) + member.x,
                                       static_cast<int>(y) + member.y)->id == member.tileId;
                        }
                        found = matches;
                    }
                }
                if (found) { addUnique(coveredStamps, stamp.id.value()); }
            }
        }
    }
    for (const auto& tile : content.authoringSemantics().tiles()) {
        expect(std::find(coveredTiles.begin(), coveredTiles.end(), tile.id.value()) != coveredTiles.end(),
               "all current Dungeon semantic tiles are represented by official gameplay maps");
    }
    for (const auto& stamp : content.authoringSemantics().stamps()) {
        expect(std::find(coveredStamps.begin(), coveredStamps.end(), stamp.id.value()) != coveredStamps.end(),
               "all registered Dungeon stamps occur as complete authored patterns");
    }
    expect(coveredTiles.size() == content.authoringSemantics().tiles().size() &&
               coveredStamps.size() == content.authoringSemantics().stamps().size(),
           "official gameplay content has complete live semantic and stamp coverage");
    for (std::size_t index = 0; index < paths.size(); ++index) {
        std::string editorError;
        auto document = underworld::editor::EditorDocument::open(
            paths[index], content, editorError);
        const auto copy = std::filesystem::temp_directory_path() /
            ("underworld_official_map_" + std::to_string(index) + ".umap");
        std::error_code removeError;
        std::filesystem::remove(copy, removeError);
        const bool roundtrip = document && !document->dirty() &&
            document->saveAs(copy, content, editorError);
        const auto reopened = roundtrip
            ? underworld::editor::EditorDocument::open(copy, content, editorError)
            : std::optional<underworld::editor::EditorDocument>{};
        expect(roundtrip && reopened && maps::semanticallyEqual(document->data(), reopened->data()),
               "Map Maker imports and roundtrips every official map through authored UMAP");
        std::filesystem::remove(copy, removeError);
    }
    maps::MapCatalog catalog;
    for (std::size_t index = 0; index < manifest.size() && index < paths.size(); ++index) {
        catalog.add(manifest[index].id, paths[index]);
    }
    expect(catalog.validateLinks(&validation).empty(),
           "official gameplay catalog resolves all bidirectional map links");
    std::vector<const maps::MapData*> saveMaps;
    for (const auto& map : loadedMaps) { saveMaps.push_back(&map); }
    for (const auto& map : loadedMaps) {
        save::SaveData saveData;
        saveData.player.currentMapId = map.id;
        saveData.player.health = 5;
        saveData.progression = {gameplay::rpg::defaultPlayerProgressionId(), 0};
        expect(save::validateSaveData(saveData, {&content.items(), saveMaps, nullptr,
                                                  &content.progressions()}).empty(),
               "save validation recognizes every official gameplay MapId");
    }
    simulation::EntityHandlePool handles;
    const std::array visuals{creatures::soldierVisualId(), creatures::skullVisualId()};
    creatures::EnemyFactory enemyFactory(handles, content.enemies(), content.behaviors(),
        content.attacks(), content.projectiles(), visuals);
    gameplay::WorldObjectFactory objectFactory(handles, content.objects(), content.items());
    npcs::NpcFactory npcFactory(handles, content.npcs());
    const game::RuntimeTilesetCatalog runtimeTilesets(content.tilesets());
    maps::RuntimeWorldBuilder builder(validation, enemyFactory, objectFactory, handles,
                                      runtimeTilesets, &npcFactory);
    for (const auto& map : loadedMaps) {
        const auto spawn = map.playerSpawns.front().id;
        const auto runtime = builder.build(map, spawn);
        expect(runtime && runtime.world->enemies().size() == map.enemies.size() &&
                   runtime.world->objects().size() == map.objects.size() &&
                   runtime.world->pickups().size() == map.pickups.size() &&
                   runtime.world->npcs().size() == map.npcs.size(),
               "official gameplay placements build through RuntimeWorldBuilder");
    }
    save::SessionWorldState sessionState;
    maps::MapSession session(catalog, validation, builder, handles, sessionState);
    expect(session.activate(simulation::MapId{"map.dungeon.01"},
                            simulation::SpawnId{"entry.start"}).changed,
           "official Map 01 activates at entry.start");
    session.beginTick();
    const auto to02 = session.requestTransition({23 * 16, 8 * 16, 16, 32})
        ? session.commitPending() : maps::TransitionResult{};
    expect(to02.changed && session.world()->id() == simulation::MapId{"map.dungeon.02"},
           "official map transition follows Map 01 east link to Map 02");
    session.beginTick();
    const auto to03 = session.requestTransition({23 * 16, 8 * 16, 16, 32})
        ? session.commitPending() : maps::TransitionResult{};
    expect(to03.changed && session.world()->id() == simulation::MapId{"map.dungeon.03"},
           "official map transition follows Map 02 east link to Map 03");
    session.beginTick();
    const auto back02 = session.requestTransition({0, 8 * 16, 16, 32})
        ? session.commitPending() : maps::TransitionResult{};
    expect(back02.changed && session.world()->id() == simulation::MapId{"map.dungeon.02"},
           "official map transition returns from Map 03 to Map 02");
    session.beginTick();
    const auto back01 = session.requestTransition({0, 8 * 16, 16, 32})
        ? session.commitPending() : maps::TransitionResult{};
    expect(back01.changed && session.world()->id() == simulation::MapId{"map.dungeon.01"},
           "official map transition returns from Map 02 to Map 01");

    game::GameSession gameSession({0}, testProgression());
    std::string sessionError;
    expect(gameSession.initializeMap(catalog, validation, builder,
                                     simulation::MapId{"map.dungeon.01"},
                                     simulation::SpawnId{"entry.start"}, sessionError) &&
               gameSession.world().id() == simulation::MapId{"map.dungeon.01"},
           "GameSession owns the active logical map without presentation dependencies");
    const auto sessionInitial = gameSession.player().subpixelPosition();
    gameSession.tick(movementCommand(1, 1, 0));
    expect(gameSession.player().subpixelPosition().x > sessionInitial.x,
           "GameSession resolves movement against active map collision");
    gameSession.relocatePlayer({23 * 16, 8 * 16 + 8},
                                            gameplay::FacingDirection::right);
    gameSession.tick(movementCommand(2, 0, 0));
    const bool enteredMap02 = std::any_of(
        gameSession.events().events().begin(), gameSession.events().events().end(),
        [](const simulation::SimulationEvent& event) {
            const auto* entered = std::get_if<simulation::MapEntered>(&event);
            return entered && entered->mapId == simulation::MapId{"map.dungeon.02"};
        });
    expect(enteredMap02 && gameSession.world().id() == simulation::MapId{"map.dungeon.02"},
           "GameSession performs map transition and emits typed MapEntered");

    const auto& sessionAttacks = content.attacks();
    const auto& sessionProjectiles = content.projectiles();
    creatures::EnemyFactory logicalEnemyFactory(handles, content.enemies(), content.behaviors(),
        sessionAttacks, sessionProjectiles);
    maps::RuntimeWorldBuilder logicalBuilder(validation, logicalEnemyFactory, objectFactory,
        handles, runtimeTilesets, &npcFactory);
    game::GameSession combatSession({0}, testProgression());
    combatSession.configureCombat(sessionAttacks, sessionProjectiles, content.behaviors(),
        sessionAttacks.require(gameplay::playerSwordAttackId()),
        sessionAttacks.require(gameplay::playerBowAttackId()));
    std::string combatError;
    expect(combatSession.initializeMap(catalog, validation, logicalBuilder,
        simulation::MapId{"map.dungeon.01"}, simulation::SpawnId{"entry.start"}, combatError),
        "GameSession combat fixture initializes without visual catalogs or presentation");
    if (combatSession.world().enemies().empty()) {
        expect(false, "GameSession combat fixture contains a logical enemy");
    } else {
        auto& enemy = const_cast<game::maps::RuntimeWorld&>(combatSession.world()).enemies().front().instance;
        enemy.combatant().health.current = 1;
        const auto enemyFeet = enemy.feetPosition();
        combatSession.relocatePlayer(
            {enemyFeet.x - 12, enemyFeet.y}, gameplay::FacingDirection::right);
        combatSession.tick(actionCommand(3, true, false));
        for (std::uint32_t tick = 4; tick <= 8; ++tick) {
            combatSession.tick(movementCommand(tick, 0, 0));
        }
        const bool damagedOrDefeated = std::any_of(
            combatSession.events().events().begin(), combatSession.events().events().end(),
            [](const simulation::SimulationEvent& event) {
                return std::holds_alternative<simulation::EntityDamaged>(event) ||
                       std::holds_alternative<simulation::EntityDefeated>(event);
            });
        expect(damagedOrDefeated,
               "headless GameSession advances Player sword combat without presentation");
    }

    game::GameSession narrativeSession({0}, testProgression());
    narrativeSession.configureItems(content.items());
    narrativeSession.configureNarrative(content.dialogues(), content.quests());
    std::string narrativeError;
    expect(narrativeSession.initializeMap(catalog, validation, logicalBuilder,
        simulation::MapId{"map.dungeon.02"}, simulation::SpawnId{"entry.from_01"},
        narrativeError), "headless GameSession narrative fixture initializes logically");
    const auto scholar = std::find_if(narrativeSession.world().npcs().begin(),
        narrativeSession.world().npcs().end(), [&](const auto& npc) {
            return npc.instance.definition().id == npcs::scholarNpcId();
        });
    if (scholar != narrativeSession.world().npcs().end()) {
        narrativeSession.relocatePlayer(
            scholar->instance.position(), gameplay::FacingDirection::down);
        auto interact = actionCommand(1, false, false);
        interact.actions.interactPressed = true;
        narrativeSession.tick(interact);
        expect(narrativeSession.dialogue().isOpen() &&
                   narrativeSession.dialogue().dialogueId() ==
                       dialogue::scholarDialogueId().value() &&
                   std::any_of(narrativeSession.events().events().begin(),
                               narrativeSession.events().events().end(), [](const auto& event) {
                                   return std::holds_alternative<simulation::NpcTalked>(event);
                               }),
               "GameSession opens dialogue and emits NpcTalked without presentation");
        narrativeSession.tick(actionCommand(2, true, false));
        expect(narrativeSession.dialogue().choicesVisible(),
               "GameSession routes dialogue navigation before world simulation");
        narrativeSession.tick(actionCommand(3, true, false));
        expect(narrativeSession.dialogueFlags().isSet(dialogue::scholarAskedFlagId()) &&
                   narrativeSession.questState().status(quests::scholarQuestId()) ==
                       quests::QuestStatus::active,
               "GameSession applies dialogue flag and starts Scholar quest action");
    } else {
        expect(false, "headless GameSession narrative fixture contains the Scholar");
    }

    game::GameSession itemSession({0}, testProgression());
    itemSession.configureItems(content.items());
    itemSession.configureNarrative(content.dialogues(), content.quests());
    std::string itemError;
    expect(itemSession.initializeMap(catalog, validation, builder,
        simulation::MapId{"map.dungeon.01"}, simulation::SpawnId{"entry.start"}, itemError),
        "GameSession item fixture initializes without presentation assets");
    const auto moneyPickup = std::find_if(
        itemSession.world().pickups().begin(), itemSession.world().pickups().end(),
        [](const auto& pickup) {
            return std::holds_alternative<gameplay::CurrencyPickup>(
                pickup.instance.payload());
        });
    if (moneyPickup != itemSession.world().pickups().end()) {
        const auto pickupPosition = moneyPickup->instance.position();
        const auto pickupCount = itemSession.world().pickups().size();
        itemSession.relocatePlayer(pickupPosition, gameplay::FacingDirection::down);
        itemSession.tick(movementCommand(1, 0, 0));
        const bool collected = std::any_of(
            itemSession.events().events().begin(), itemSession.events().events().end(),
            [](const simulation::SimulationEvent& event) {
                return std::holds_alternative<simulation::PickupCollected>(event);
            });
        expect(collected && itemSession.playerItems().wallet().gold() > 0 &&
                   itemSession.world().pickups().size() + 1 == pickupCount,
               "GameSession routes logical pickup collection without presentation");
        const auto saved = itemSession.captureSaveData();
        const auto savedPosition = itemSession.player().feetPosition();
        auto invalidSave = saved;
        invalidSave.player.quickSlots[0] = simulation::DefinitionId{"item.unknown"};
        std::string restoreError;
        expect(!itemSession.restoreSaveData(invalidSave, restoreError) &&
                   itemSession.player().feetPosition() == savedPosition &&
                   itemSession.playerItems().wallet().gold() == saved.player.gold,
               "GameSession rejects invalid save without partially restoring state");
        static_cast<void>(const_cast<gameplay::PlayerItems&>(itemSession.playerItems())
                              .inventory().items().add(simulation::DefinitionId{"item.training_armor"}, 1));
        auto& mutablePlayer = const_cast<gameplay::Player&>(itemSession.player());
        auto& mutableItems = const_cast<gameplay::PlayerItems&>(itemSession.playerItems());
        expect(mutableItems.equipment().equipFromInventory(
                   gameplay::rpg::EquipmentSlot::armor,
                   simulation::DefinitionId{"item.training_armor"},
                   mutableItems.inventory().items(), content.items()),
               "GameSession rollback fixture equips Training Armor");
        mutablePlayer.health().setMaximum(7);
        mutablePlayer.health().current = 7;
        const auto equippedSave = itemSession.captureSaveData();
        auto healthMismatch = equippedSave;
        healthMismatch.equipment.armor.reset();
        healthMismatch.player.health = 7;
        expect(!itemSession.restoreSaveData(healthMismatch, restoreError) &&
                   itemSession.playerItems().equipment().item(gameplay::rpg::EquipmentSlot::armor) ==
                       simulation::DefinitionId{"item.training_armor"} &&
                   itemSession.derivedPlayerStats().maximumHealth == 7 &&
                   itemSession.player().health().current == 7 &&
                   itemSession.player().health().maximum == 7,
               "GameSession rollback restores equipment before previous 7/7 health");
    } else {
        expect(false, "GameSession item fixture contains a logical pickup");
    }
}

void testPhase9StartupAndEditorPerformanceContracts() {
    namespace editor = underworld::editor;
    namespace game = underworld::game;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;

    const auto root = std::filesystem::temp_directory_path() /
                      "underworld_phase9_startup_contract";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::create_directories(root / "maps" / "gameplay");
    const auto canonical = root / "maps" / "gameplay" / "dungeon_01_entry.dmap";
    auto canonicalMap = makeSyntheticMap("map.test.canonical", "map.test.canonical");
    canonicalMap.links.clear();
    std::string canonicalError;
    static_cast<void>(maps::writeDmap(canonical, canonicalMap, canonicalError));

    const game::GameLaunchOptions defaults;
    const wchar_t* commandLine[] = {
        L"game.exe", L"--map", L"maps/authored.dmap", L"--spawn=entry.start"};
    std::string optionError;
    const auto parsed = game::parseGameLaunchOptions(4, commandLine, optionError);
    expect(parsed && parsed->mapPath &&
               parsed->mapPath->generic_string() == "maps/authored.dmap" &&
               parsed->spawnId && parsed->spawnId->value() == "entry.start",
           "game startup options parse authored map and spawn arguments");
    const wchar_t* auditCommandLine[] = {L"game.exe", L"--audit"};
    const auto audited = game::parseGameLaunchOptions(2, auditCommandLine, optionError);
    expect(audited && audited->auditEnabled,
           "game startup options enable manual audit mode");
    const char* linuxCommandLine[] = {"game", "--map", "maps/gameplay/dungeon_01_entry.dmap",
                                      "--map-root", "maps/gameplay", "--spawn=entry.start",
                                      "--asset-root", "assets", "--audit"};
    const auto linuxOptions = game::parseGameLaunchOptions(9, linuxCommandLine, optionError);
    expect(linuxOptions && linuxOptions->mapPath &&
               linuxOptions->mapPath->generic_string() == "maps/gameplay/dungeon_01_entry.dmap" &&
               linuxOptions->mapRoot &&
               linuxOptions->mapRoot->generic_string() == "maps/gameplay" &&
               linuxOptions->spawnId && linuxOptions->spawnId->value() == "entry.start" &&
               linuxOptions->assetRoot && linuxOptions->assetRoot->generic_string() == "assets" &&
               linuxOptions->auditEnabled,
           "Linux startup options parse map spawn and audit arguments");
    const char* contentCommandLine[] = {"game", "--content", "content", "--map", "map.dmap"};
    const auto contentOptions = game::parseGameLaunchOptions(5, contentCommandLine, optionError);
    expect(contentOptions && contentOptions->contentRoot && contentOptions->mapPath,
           "game startup options parse explicit content independently of map and assets");
    const char* duplicateContent[] = {"game", "--content=a", "--content", "b"};
    expect(!game::parseGameLaunchOptions(4, duplicateContent, optionError) &&
               optionError.find("duplicate") != std::string::npos,
           "game startup options reject duplicate content sources");
    const char* missingContent[] = {"game", "--content"};
    expect(!game::parseGameLaunchOptions(2, missingContent, optionError),
           "game startup options reject content without a value");
    const wchar_t* wideContent[] = {L"game", L"--content=external-content",
                                    L"--asset-root", L"assets"};
    const auto wideOptions = game::parseGameLaunchOptions(4, wideContent, optionError);
    expect(wideOptions && wideOptions->contentRoot &&
               wideOptions->contentRoot->generic_string() == "external-content" &&
               wideOptions->assetRoot && wideOptions->assetRoot->generic_string() == "assets",
           "wide game startup options parse content= and asset-root");
    const wchar_t* wideDuplicateContent[] = {L"game", L"--content", L"a",
                                             L"--content=b"};
    expect(!game::parseGameLaunchOptions(4, wideDuplicateContent, optionError) &&
               optionError.find("duplicate") != std::string::npos,
           "wide game startup options reject duplicate content sources");
    const wchar_t* wideMissingContent[] = {L"game", L"--content"};
    expect(!game::parseGameLaunchOptions(2, wideMissingContent, optionError),
           "wide game startup options reject content without a value");
    const char* editorContent[] = {"editor", "--content", "content", "--asset-root", "assets"};
    const auto editorOptions = editor::parseEditorLaunchOptions(5, editorContent, optionError);
    expect(editorOptions && editorOptions->contentRoot && editorOptions->assetRoot,
           "editor startup options parse content and asset roots");
    const char* duplicateEditor[] = {"editor", "--content=a", "--content=b"};
    expect(!editor::parseEditorLaunchOptions(3, duplicateEditor, optionError),
           "editor startup options reject duplicate content sources");
    const wchar_t* wideEditorContent[] = {L"editor", L"--content=external-content",
                                           L"--asset-root", L"assets"};
    const auto wideEditorOptions = editor::parseEditorLaunchOptions(
        4, wideEditorContent, optionError);
    expect(wideEditorOptions && wideEditorOptions->contentRoot &&
               wideEditorOptions->contentRoot->generic_string() == "external-content" &&
               wideEditorOptions->assetRoot,
           "wide editor startup options parse content= and asset-root");
    const wchar_t* wideEditorDuplicate[] = {L"editor", L"--content", L"a",
                                            L"--content=b"};
    expect(!editor::parseEditorLaunchOptions(4, wideEditorDuplicate, optionError) &&
               optionError.find("duplicate") != std::string::npos,
           "wide editor startup options reject duplicate content sources");
    const wchar_t* wideEditorMissing[] = {L"editor", L"--content"};
    expect(!editor::parseEditorLaunchOptions(2, wideEditorMissing, optionError),
           "wide editor startup options reject content without a value");
    const char* mapCompileContent[] = {"map_compile", "--content=content", "source.umap",
                                       "output.dmap"};
    const auto mapCompileOptions = underworld::tools::parseMapCompileOptions(
        4, mapCompileContent, optionError);
    expect(mapCompileOptions && mapCompileOptions->contentRoot &&
               mapCompileOptions->contentRoot->generic_string() == "content" &&
               mapCompileOptions->source == "source.umap" &&
               mapCompileOptions->output == "output.dmap",
           "map_compile parser accepts content= and authored source/output paths");
    const char* mapCompileMissingContent[] = {"map_compile", "--content"};
    expect(!underworld::tools::parseMapCompileOptions(
               2, mapCompileMissingContent, optionError) &&
               optionError.find("requires") != std::string::npos,
           "map_compile parser rejects a missing content directory value");
    const char* mapCompileDuplicate[] = {"map_compile", "--content=a", "--content=b",
                                         "source.umap", "output.dmap"};
    expect(!underworld::tools::parseMapCompileOptions(
               5, mapCompileDuplicate, optionError) &&
               optionError.find("duplicate") != std::string::npos,
           "map_compile parser rejects duplicate content sources");
    const char* mapCompileMissingPositionals[] = {"map_compile", "--content=content"};
    expect(!underworld::tools::parseMapCompileOptions(
               2, mapCompileMissingPositionals, optionError),
           "map_compile parser requires both source and output paths");
    const auto authored = game::selectStartupMap(defaults, root / "build" / "bin", root);
    expect(authored.source == game::StartupMapSource::officialGameplay &&
               authored.path == canonical,
           "the only discovered gameplay map is selected automatically");
    game::GameLaunchOptions explicitMap;
    explicitMap.mapPath = root / "explicit.dmap";
    const auto explicitSelection = game::selectStartupMap(
        explicitMap, root / "build" / "bin", root);
    expect(explicitSelection.source == game::StartupMapSource::explicitPath &&
               explicitSelection.path == *explicitMap.mapPath,
           "explicit map path overrides canonical authored startup");
    std::filesystem::remove(canonical, cleanupError);
    const auto fallback = game::selectStartupMap(defaults, root / "build" / "bin", root);
    expect(fallback.source == game::StartupMapSource::officialGameplay &&
               fallback.path == root / "maps" / "gameplay" / "startup.dmap",
           "empty gameplay discovery keeps a deterministic startup error path");

    const auto authoredMap = makeSyntheticMap("map.test.startup", "map.test.startup");
    std::string spawnError;
    const auto canonicalSpawn = game::selectStartupSpawn(authoredMap, std::nullopt, spawnError);
    expect(canonicalSpawn && *canonicalSpawn == simulation::SpawnId{"entry.start"},
           "startup spawn selection prefers the canonical entry.start spawn");
    const auto missingSpawn = game::selectStartupSpawn(
        authoredMap, simulation::SpawnId{"missing"}, spawnError);
    expect(!missingSpawn && spawnError == "requested player spawn does not exist",
           "explicit startup spawn rejects an unknown SpawnId");

    editor::EditorDocument document = editor::EditorDocument::newMap(
        simulation::MapId{"map.validation.cache"}, 8, 8);
    const auto initialRevision = document.revision();
    auto content = game::content::compileBuiltinContentOrThrow();
    editor::EditorValidationCache cache;
    cache.refreshIfNeeded(document, content);
    cache.refreshIfNeeded(document, content);
    expect(cache.recomputeCount() == 1 && cache.validatedRevision() == document.revision(),
           "unchanged editor documents reuse the validation cache");
    document.viewport().worldX = 12.0;
    document.viewport().zoomStep = 5;
    cache.refreshIfNeeded(document, content);
    expect(cache.recomputeCount() == 1,
           "pan and zoom do not invalidate semantic validation");
    auto externalAuthored = game::content::makeBuiltinAuthoredContent();
    const auto semanticIt = std::find_if(externalAuthored.tileSemantics.begin(),
                                         externalAuthored.tileSemantics.end(),
        [](const auto& value) { return value.id.value() == "tile.dungeon.masonry.39"; });
    expect(semanticIt != externalAuthored.tileSemantics.end(),
           "editor injection fixture finds the authored starter semantic");
    if (semanticIt != externalAuthored.tileSemantics.end()) {
        semanticIt->sourceIndex = 100;
        const auto externalCompiled = game::content::compileContent(externalAuthored);
        expect(externalCompiled.registry.has_value(),
               "editor injection fixture compiles an altered authored registry");
        if (externalCompiled.registry) {
            const auto builtinDocument = editor::EditorDocument::newAuthoredMap(
                simulation::MapId{"map.editor.builtin"}, 8, 8, 16, content, false);
            const auto externalDocument = editor::EditorDocument::newAuthoredMap(
                simulation::MapId{"map.editor.external"}, 8, 8, 16,
                *externalCompiled.registry, false);
            expect(!builtinDocument.data().tileReferences.empty() &&
                       !externalDocument.data().tileReferences.empty() &&
                       builtinDocument.data().tileReferences.back().sourceIndex !=
                           externalDocument.data().tileReferences.back().sourceIndex,
                   "editor authored document consumes the injected registry data");
        }
    }
    std::string error;
    expect(!document.execute(std::make_unique<editor::PaintTilesCommand>(0,
               std::vector<editor::TileCoordinate>{{99, 99}},
               maps::MapTileReference{simulation::DefinitionId{"tileset.dungeon"}, 10,
                                      underworld::world::TileFlags::none}), error) &&
               document.revision() == initialRevision,
           "failed editor commands do not create a revision");
    const auto tile = maps::MapTileReference{simulation::DefinitionId{"tileset.dungeon"}, 10,
                                             underworld::world::TileFlags::none};
    expect(document.execute(std::make_unique<editor::PaintTilesCommand>(0,
               std::vector<editor::TileCoordinate>{{1, 1}}, tile), error) &&
               document.revision() == initialRevision + 1,
           "successful authored mutation advances the document revision");
    cache.refreshIfNeeded(document, content);
    expect(cache.recomputeCount() == 2,
           "validation recomputes once after a successful mutation");
    expect(document.undo() && document.revision() == initialRevision + 2,
           "undo invalidates the revision-based validation cache");
    cache.refreshIfNeeded(document, content);
    expect(cache.recomputeCount() == 3, "undo refreshes validation on the next inspection");
    expect(document.redo(error) && document.revision() == initialRevision + 3,
           "redo advances the authored revision");

    maps::MapData large = document.data();
    large.width = 4096;
    large.height = 4096;
    large.layers[0].cells.assign(static_cast<std::size_t>(large.width) * large.height,
                                 std::nullopt);
    large.collision.assign(static_cast<std::size_t>(large.width) * large.height, 0);
    const auto range = editor::visibleTileRange(large, {0, 0, 272, 224}, 0.0, 0.0, 1.0, 0);
    expect(!range.empty() && range.firstX == 0 && range.firstY == 0 &&
               range.lastX < 32 && range.lastY < 32 &&
               range.tileCount() < static_cast<std::size_t>(large.width) * large.height,
           "editor tile culling visits only the viewport region of a large map");
    bool zoomRangesValid = true;
    for (const double zoom : std::array<double, 6>{0.25, 0.5, 1.0, 2.0, 4.0, 8.0}) {
        const auto zoomed = editor::visibleTileRange(
            large, {0, 0, 272, 224}, 128.0, 128.0, zoom);
        zoomRangesValid = zoomRangesValid && !zoomed.empty() && zoomed.firstX >= 0 &&
            zoomed.lastX < static_cast<int>(large.width) && zoomed.firstY >= 0 &&
            zoomed.lastY < static_cast<int>(large.height);
    }
    expect(zoomRangesValid, "editor visible tile bounds remain clamped at every supported zoom");
    std::filesystem::remove_all(root, cleanupError);
}

void testGameplayMapDiscovery() {
    namespace game = underworld::game;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;
    namespace gameplay = underworld::game::gameplay;
    const auto root = std::filesystem::temp_directory_path() /
                      "underworld_gameplay_map_discovery";
    std::error_code fsError;
    std::filesystem::remove_all(root, fsError);
    std::filesystem::create_directories(root / "maps" / "gameplay", fsError);
    expect(!fsError, "gameplay discovery fixture creates its isolated root");
    const auto writeMap = [&](const std::filesystem::path& path, maps::MapData map) {
        std::string error;
        return maps::writeDmap(path, map, error);
    };

    auto only = makeSyntheticMap("map.only", "map.missing");
    only.links.clear();
    expect(writeMap(root / "maps" / "gameplay" / "renamed_file.dmap", only),
           "gameplay discovery fixture writes one DMAP with an internal MapId");
    auto discovered = maps::discoverGameplayMaps(root / "bin", root);
    expect(discovered && discovered.maps.size() == 1 &&
               discovered.maps.front().id == simulation::MapId{"map.only"},
           "one gameplay DMAP is discovered by internal MapId rather than filename");
    std::string selectionError;
    const game::GameLaunchOptions defaults;
    const auto oneSelection = game::selectDiscoveredStartupMap(
        defaults, discovered.maps, selectionError);
    expect(oneSelection && oneSelection->path.filename() == "renamed_file.dmap",
           "one discovered gameplay map is selected automatically");

    std::filesystem::remove_all(root / "maps" / "gameplay", fsError);
    std::filesystem::create_directories(root / "maps" / "gameplay", fsError);
    discovered = maps::discoverGameplayMaps(root / "bin", root);
    expect(!discovered && discovered.error.find("no gameplay maps found") != std::string::npos,
           "empty gameplay directory produces a clear discovery error");

    auto mapA = makeSyntheticMap("map.a", "map.b");
    auto mapB = makeSyntheticMap("map.b", "map.a");
    expect(writeMap(root / "maps" / "gameplay" / "z.dmap", mapB) &&
               writeMap(root / "maps" / "gameplay" / "a.dmap", mapA),
           "gameplay discovery fixture writes two linked maps");
    discovered = maps::discoverGameplayMaps(root / "bin", root);
    expect(discovered && discovered.maps.size() == 2,
           "two linked gameplay DMAPs form a valid discovered catalog");

    auto broken = makeSyntheticMap("map.broken", "map.missing");
    expect(writeMap(root / "maps" / "gameplay" / "broken.dmap", broken),
           "gameplay discovery fixture writes a broken-link map");
    discovered = maps::discoverGameplayMaps(root / "bin", root);
    expect(!discovered && discovered.error.find("map.broken") != std::string::npos &&
               discovered.error.find("map.missing") != std::string::npos,
           "discovery rejects missing link targets with source and destination diagnostics");
    std::filesystem::remove(root / "maps" / "gameplay" / "broken.dmap", fsError);

    auto missingSpawn = makeSyntheticMap("map.spawn_source", "map.spawn_target");
    missingSpawn.links[0].targetSpawnId = simulation::SpawnId{"entry.missing"};
    auto spawnTarget = makeSyntheticMap("map.spawn_target", "map.spawn_source");
    expect(writeMap(root / "maps" / "gameplay" / "spawn_source.dmap", missingSpawn) &&
               writeMap(root / "maps" / "gameplay" / "spawn_target.dmap", spawnTarget),
           "gameplay discovery fixture writes a missing-spawn link pair");
    discovered = maps::discoverGameplayMaps(root / "bin", root);
    expect(!discovered && discovered.error.find("entry.missing") != std::string::npos,
           "discovery rejects links to missing destination spawns");
    std::filesystem::remove(root / "maps" / "gameplay" / "spawn_source.dmap", fsError);
    std::filesystem::remove(root / "maps" / "gameplay" / "spawn_target.dmap", fsError);

    auto duplicate = makeSyntheticMap("map.a", "map.b");
    duplicate.links.clear();
    expect(writeMap(root / "maps" / "gameplay" / "duplicate.dmap", duplicate),
           "gameplay discovery fixture writes a duplicate MapId");
    discovered = maps::discoverGameplayMaps(root / "bin", root);
    expect(!discovered && discovered.error.find("duplicate gameplay MapId 'map.a'") !=
               std::string::npos,
           "discovery rejects duplicate internal MapIds deterministically");

    maps::GameplayMapRecord first{simulation::MapId{"map.first"}, "first.dmap", only};
    maps::GameplayMapRecord second{simulation::MapId{"map.second"}, "second.dmap", only};
    first.data.playerSpawns.clear();
    first.data.playerSpawns.push_back({simulation::SpawnId{"entry.return"}, {16, 24},
                                       gameplay::FacingDirection::down});
    second.data.playerSpawns.clear();
    second.data.playerSpawns.push_back({simulation::SpawnId{"entry.start"}, {16, 24},
                                        gameplay::FacingDirection::down});
    std::vector<maps::GameplayMapRecord> candidates{first, second};
    const auto selected = game::selectDiscoveredStartupMap(defaults, candidates, selectionError);
    expect(selected && selected->path == "second.dmap",
           "multiple gameplay maps select the unique entry.start map");
    candidates[0].data.playerSpawns.push_back({simulation::SpawnId{"entry.start"}, {16, 24},
                                               gameplay::FacingDirection::down});
    expect(!game::selectDiscoveredStartupMap(defaults, candidates, selectionError) &&
               selectionError.find("ambiguous") != std::string::npos,
           "multiple entry.start spawns reject ambiguous automatic startup");
    candidates[0].data.playerSpawns.clear();
    candidates[1].data.playerSpawns.clear();
    expect(!game::selectDiscoveredStartupMap(defaults, candidates, selectionError) &&
               selectionError.find("--map") != std::string::npos,
           "multiple maps without entry.start require explicit --map");
    game::GameLaunchOptions explicitOptions;
    explicitOptions.mapPath = "explicit.dmap";
    expect(game::selectDiscoveredStartupMap(explicitOptions, candidates, selectionError) &&
               game::selectDiscoveredStartupMap(explicitOptions, candidates, selectionError)->path ==
                   "explicit.dmap",
           "explicit --map overrides automatic gameplay map selection");
    std::filesystem::remove_all(root, fsError);
}

void testRuntimeVisualSynchronization() {
    namespace creatures = underworld::game::gameplay::creatures;
    namespace gameplay = underworld::game::gameplay;
    namespace game = underworld::game;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;

    auto content = game::content::compileBuiltinContentOrThrow();
    const auto validation = game::mapValidationCatalogs(content);
    simulation::EntityHandlePool handles;
    const std::array visuals{creatures::soldierVisualId(), creatures::skullVisualId()};
    creatures::EnemyFactory enemyFactory(handles, content.enemies(), content.behaviors(),
        content.attacks(), content.projectiles(), visuals);
    gameplay::WorldObjectFactory objectFactory(handles, content.objects(), content.items());
    const game::RuntimeTilesetCatalog runtimeTilesets(content.tilesets());
    maps::RuntimeWorldBuilder builder(validation, enemyFactory, objectFactory, handles,
                                      runtimeTilesets);
    const auto authored = makeSyntheticMap("map.test.visuals", "map.test.visuals");
    const auto runtime = builder.build(authored, authored.playerSpawns.front().id);

    game::EnemyVisualCatalog enemyCatalog;
    const auto clip = makeTestClip("sync.clip", true);
    const game::DirectionalAnimationClips directional{clip, clip, clip};
    enemyCatalog.add({creatures::soldierVisualId(), directional, directional, directional, {}});
    enemyCatalog.add({creatures::skullVisualId(), directional, directional, directional, {}});
    game::WorldObjectVisualCatalog objectCatalog;
    objectCatalog.add({simulation::DefinitionId{"visual.object.chest"}, clip, clip, clip});
    objectCatalog.add({simulation::DefinitionId{"visual.object.crate"}, clip, clip, clip});
    std::vector<game::EnemyVisualInstance> enemyVisuals;
    std::vector<game::WorldObjectVisualInstance> objectVisuals;
    const auto synchronized = game::synchronizeRuntimeWorldVisuals(
        *runtime.world, enemyCatalog, enemyVisuals, objectCatalog, objectVisuals);
    bool enemyIdentityMatches = synchronized && enemyVisuals.size() == runtime.world->enemies().size();
    for (std::size_t index = 0; enemyIdentityMatches && index < enemyVisuals.size(); ++index) {
        enemyIdentityMatches = enemyVisuals[index].handle() == runtime.world->enemies()[index].instance.handle() &&
            enemyVisuals[index].visualSetId() == runtime.world->enemies()[index].instance.definition().visualSetId;
    }
    bool objectIdentityMatches = synchronized && objectVisuals.size() == runtime.world->objects().size();
    for (std::size_t index = 0; objectIdentityMatches && index < objectVisuals.size(); ++index) {
        objectIdentityMatches = objectVisuals[index].handle() == runtime.world->objects()[index].instance.handle() &&
            objectVisuals[index].visualSetId() == runtime.world->objects()[index].instance.definition().visualSetId;
    }
    expect(synchronized && enemyIdentityMatches && objectIdentityMatches,
           "runtime world activation creates synchronized enemy and object visual instances");

    game::EnemyVisualCatalog incompleteEnemyCatalog;
    std::vector<game::EnemyVisualInstance> incompleteEnemyVisuals;
    std::vector<game::WorldObjectVisualInstance> incompleteObjectVisuals;
    const auto failedSynchronization = game::synchronizeRuntimeWorldVisuals(
        *runtime.world, incompleteEnemyCatalog, incompleteEnemyVisuals,
        objectCatalog, incompleteObjectVisuals);
    expect(!failedSynchronization &&
               failedSynchronization.error.find("enemy visual set") != std::string::npos,
           "missing runtime enemy visuals fail synchronization with a clear error");
}

void testMultiTilesetAuthoringAndRuntime() {
    namespace editor = underworld::editor;
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace creatures = underworld::game::gameplay::creatures;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;

    const simulation::DefinitionId tilesetA{"tileset.test_a"};
    const simulation::DefinitionId tilesetB{"tileset.test_b"};
    game::TilesetCatalog tilesets;
    tilesets.add({tilesetA, "Test A", "test_a.png", 16, 2, 1});
    tilesets.add({tilesetB, "Test B", "test_b.png", 16, 2, 1});
    expect(tilesets.find(tilesetA) && tilesets.require(tilesetB).tileCount() == 2 &&
               tilesets.definitions().size() == 2,
           "TilesetCatalog registers immutable metadata and reports tile counts");
    bool duplicateRejected = false;
    try { tilesets.add({tilesetA, "Duplicate", "duplicate.png", 16, 1, 1}); }
    catch (const std::logic_error&) { duplicateRejected = true; }
    expect(duplicateRejected, "TilesetCatalog rejects duplicate DefinitionIds");

    maps::MapData map;
    map.id = simulation::MapId{"map.test.multi_tileset"};
    map.width = 2; map.height = 1; map.tileSize = 16;
    map.tileReferences = {{tilesetA, 1, underworld::world::TileFlags::flipX},
                          {tilesetB, 0, underworld::world::TileFlags::none}};
    map.layers = {{"ground", true, {0U, 1U}}};
    map.collision = {0, 0};
    auto content = game::content::compileBuiltinContentOrThrow();
    const maps::MapValidationCatalogs validation{
        &content.enemies(), &content.objects(), &content.items(), &tilesets};
    expect(static_cast<bool>(maps::validateMapData(map, &validation)),
           "MapData accepts multiple known tilesets in the same layer");
    const auto bytes = maps::serializeDmap(map);
    const auto decoded = maps::deserializeDmap(bytes, &validation);
    expect(decoded && maps::dmapMajorVersion == 1 && maps::dmapMinorVersion >= 1 &&
               decoded.data.tileReferences[0] == map.tileReferences[0] &&
               decoded.data.tileReferences[1] == map.tileReferences[1],
           "DMAP v1 preserves multi-tileset DefinitionIds source indices and flags");
    auto missing = map; missing.tileReferences[1].tilesetId = simulation::DefinitionId{"tileset.missing"};
    auto invalidIndex = map; invalidIndex.tileReferences[0].sourceIndex = 2;
    auto sizeMismatch = map;
    game::TilesetCatalog wrongSize;
    wrongSize.add({tilesetA, "Test A", "test_a.png", 32, 1, 1});
    wrongSize.add({tilesetB, "Test B", "test_b.png", 16, 2, 1});
    const maps::MapValidationCatalogs wrongSizeValidation{
        &content.enemies(), &content.objects(), &content.items(), &wrongSize};
    expect(!maps::validateMapData(missing, &validation) &&
               !maps::validateMapData(invalidIndex, &validation) &&
               !maps::validateMapData(sizeMismatch, &wrongSizeValidation),
           "tileset validation rejects unknown IDs invalid source indices and tile-size mismatches");

    simulation::EntityHandlePool handles;
    const std::array visuals{creatures::soldierVisualId(), creatures::skullVisualId()};
    creatures::EnemyFactory enemyFactory(handles, content.enemies(), content.behaviors(),
                                         content.attacks(), content.projectiles(), visuals);
    gameplay::WorldObjectFactory objectFactory(handles, content.objects(), content.items());
    game::RuntimeTilesetCatalog runtimeTilesets(tilesets);
    maps::RuntimeWorldBuilder builder(validation, enemyFactory, objectFactory, handles, runtimeTilesets);
    const auto runtime = builder.build(map, simulation::SpawnId{});
    // Maps without a PlayerSpawn are intentionally rejected by the builder request; add one for runtime.
    map.playerSpawns.push_back({simulation::SpawnId{"entry.start"}, {8, 8},
                                gameplay::FacingDirection::down});
    const auto built = builder.build(map, simulation::SpawnId{"entry.start"});
    expect(!runtime && built && built.world->map().layer(0).cell(0, 0)->definition.tilesetId !=
               built.world->map().layer(0).cell(1, 0)->definition.tilesetId,
           "RuntimeWorldBuilder resolves distinct persistent tileset definitions to distinct runtime IDs");

    using underworld::core::ColorRGBA8;
    const std::vector<ColorRGBA8> pixelsA(32U * 16U, ColorRGBA8{240, 20, 20, 255});
    const std::vector<ColorRGBA8> pixelsB(32U * 16U, ColorRGBA8{20, 220, 40, 255});
    const underworld::render::Image imageA(makeImageData(32, 16, pixelsA));
    const underworld::render::Image imageB(makeImageData(32, 16, pixelsB));
    game::TilesetVisualCatalog tileVisuals;
    tileVisuals.add(runtimeTilesets.requireRuntimeId(tilesetA),
                    std::make_shared<const underworld::render::Image>(imageA), tilesets.require(tilesetA));
    tileVisuals.add(runtimeTilesets.requireRuntimeId(tilesetB),
                    std::make_shared<const underworld::render::Image>(imageB), tilesets.require(tilesetB));
    underworld::render::Framebuffer framebuffer(32, 16);
    underworld::render::Renderer2D renderer(framebuffer);
    framebuffer.clear(ColorRGBA8{0, 0, 0, 255});
    const auto* visualA = tileVisuals.find(runtimeTilesets.requireRuntimeId(tilesetA));
    const auto* visualB = tileVisuals.find(runtimeTilesets.requireRuntimeId(tilesetB));
    renderer.drawImageRegionNearest(*visualA->image, visualA->atlas.sourceRect(0), {0, 0, 16, 16}, true);
    renderer.drawImageRegionNearest(*visualB->image, visualB->atlas.sourceRect(0), {16, 0, 16, 16});
    expect(framebufferPixel(framebuffer, 0, 0) == ColorRGBA8{240, 20, 20, 255} &&
               framebufferPixel(framebuffer, 16, 0) == ColorRGBA8{20, 220, 40, 255},
           "loaded tileset visuals render independently with nearest sampling and flip support");

    editor::EditorDocument document = editor::EditorDocument::newMap(
        simulation::MapId{"map.test.editor_brush"}, 2, 1, 16);
    std::string error;
    expect(document.execute(std::make_unique<editor::PaintTilesCommand>(0,
               std::vector<editor::TileCoordinate>{{0, 0}}, map.tileReferences[0]), error) &&
               document.execute(std::make_unique<editor::PaintTilesCommand>(0,
               std::vector<editor::TileCoordinate>{{1, 0}}, map.tileReferences[1]), error) &&
               document.undo() && document.redo(error) &&
               document.data().tileReferences[*document.data().layers[0].cells[0]].tilesetId == tilesetA &&
               document.data().tileReferences[*document.data().layers[0].cells[1]].tilesetId == tilesetB,
           "editor tile commands preserve multi-tileset brush identity through undo and redo");
}

void testSemanticAuthoringFoundation() {
    using namespace underworld;
    namespace maps = game::maps;
    auto content = game::content::compileBuiltinContentOrThrow();
    const auto& semantics = content.authoringSemantics();
    expect(semantics.tiles().size() == 72, "all 72 visible Dungeon atlas cells have semantic definitions");
    expect(semantics.stamps().size() == 8, "confirmed Dungeon visual stamps are cataloged");
    expect(!content.tilesets().find(simulation::DefinitionId{"tileset.block"}) &&
               !content.tilesets().find(simulation::DefinitionId{"tileset.block_2"}) &&
               !content.tilesets().find(simulation::DefinitionId{"tileset.block_destroyed"}),
           "block object and destroyed-state sprites are not exposed as map tile atlases");
    const auto* frameNorthWest = semantics.findTile(simulation::DefinitionId{"tileset.dungeon"}, 2U + 2U * 19U);
    expect(frameNorthWest && frameNorthWest->id == simulation::DefinitionId{"tile.dungeon.frame.nw"} &&
               semantics.findTile(frameNorthWest->id) == frameNorthWest,
           "semantic tile catalog supports forward and reverse tile reference lookup");
    expect(semantics.edgesCompatible(game::authoring::EdgeProfile::masonry, game::authoring::EdgeProfile::masonry) &&
               !semantics.edgesCompatible(game::authoring::EdgeProfile::masonry, game::authoring::EdgeProfile::floor) &&
               semantics.edgesCompatible(game::authoring::EdgeProfile::unknown, game::authoring::EdgeProfile::floor),
           "edge compatibility distinguishes known mismatch from unknown evidence");

    editor::EditorDocument document = editor::EditorDocument::newMap(simulation::MapId{"map.semantic"}, 8, 8);
    std::string error;
    const auto* stamp = semantics.findStamp(simulation::DefinitionId{"stamp.dungeon.masonry_frame_3x3"});
    expect(stamp && document.execute(std::make_unique<editor::PlaceStampCommand>(0, *stamp,
               editor::TileCoordinate{2,2}, semantics), error) && document.history().size() == 1 &&
               document.data().layers[0].cells[2U + 2U * document.data().width].has_value(),
           "PlaceStampCommand writes its cells as one editor undo operation");
    expect(document.undo() && !document.data().layers[0].cells[2U + 2U * document.data().width] &&
               document.redo(error) && document.data().layers[0].cells[2U + 2U * document.data().width],
           "PlaceStampCommand undo and redo restore authored cells");
    const auto history = document.history().size();
    expect(!document.execute(std::make_unique<editor::PlaceStampCommand>(0, *stamp,
               editor::TileCoordinate{7,7}, semantics), error) && document.history().size() == history,
           "out-of-bounds stamps are rejected atomically without history");
    document.layerStates()[0].locked = true;
    expect(!document.execute(std::make_unique<editor::PlaceStampCommand>(0, *stamp,
               editor::TileCoordinate{1,1}, semantics), error) && document.history().size() == history,
           "locked layers reject stamps without history");

    game::authoring::MapSemanticValidator validator;
    maps::MapData semanticMap = document.data();
    semanticMap.layers[0].cells[2U + 3U * semanticMap.width].reset();
    const auto broken = validator.validate(semanticMap, semantics);
    expect(std::any_of(broken.issues.begin(), broken.issues.end(), [](const auto& issue) {
        return issue.code == "broken_atomic_stamp";
    }), "semantic validator reports a confidently incomplete atomic stamp as a warning");
    editor::EditorDocument sparseDocument = editor::EditorDocument::newMap(
        simulation::MapId{"map.semantic.sparse"}, 8, 8);
    const auto* frameNorth = semantics.findTile(
        simulation::DefinitionId{"tileset.dungeon"}, 3U + 2U * 19U);
    expect(sparseDocument.execute(std::make_unique<editor::PaintTilesCommand>(0,
               std::vector<editor::TileCoordinate>{{0, 0}},
               game::authoring::tileReferenceFor(*frameNorthWest)), error) && frameNorth &&
               sparseDocument.execute(std::make_unique<editor::PaintTilesCommand>(0,
                   std::vector<editor::TileCoordinate>{{1, 0}},
                   game::authoring::tileReferenceFor(*frameNorth)), error),
           "sparse semantic tiles can be authored for conservative validator coverage");
    const auto sparseReport = validator.validate(sparseDocument.data(), semantics);
    expect(!std::any_of(sparseReport.issues.begin(), sparseReport.issues.end(), [](const auto& issue) {
                       return issue.code == "broken_atomic_stamp";
                   }), "sparse reusable stamp members do not produce a broken-stamp warning");
    semanticMap.tileReferences.push_back({simulation::DefinitionId{"tileset.dungeon"}, 0, world::TileFlags::none});
    semanticMap.layers[0].cells[0] = static_cast<std::uint32_t>(semanticMap.tileReferences.size()-1);
    const auto unclassified = validator.validate(semanticMap, semantics);
    expect(std::any_of(unclassified.issues.begin(), unclassified.issues.end(), [](const auto& issue) {
        return issue.code == "unclassified_tile" && issue.severity == game::authoring::SemanticIssueSeverity::info;
    }), "unclassified but structurally valid tile is an informational semantic issue");
}

void testMapCompositionFoundation() {
    namespace authoring = underworld::game::authoring;
    namespace game = underworld::game;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;

    auto content = game::content::compileBuiltinContentOrThrow();
    maps::MapBlueprint blueprint;
    blueprint.id = simulation::MapId{"map.composition.basic"};
    blueprint.room.width = 10;
    blueprint.room.height = 8;
    maps::MapComposer composer;

    const auto basic = composer.compose(blueprint, content.authoringSemantics());
    bool basicInterior = basic && basic.map && basic.grid.width() == 10 &&
        basic.grid.height() == 8;
    bool basicBoundary = true;
    bool basicCollision = true;
    if (basic) {
        for (std::uint32_t y = 0; y < blueprint.room.height; ++y) {
            for (std::uint32_t x = 0; x < blueprint.room.width; ++x) {
                const bool boundary = x == 0 || y == 0 || x + 1U == blueprint.room.width ||
                    y + 1U == blueprint.room.height;
                const auto tile = underworld::core::TileCoord{
                    static_cast<int>(x), static_cast<int>(y)};
                basicBoundary = basicBoundary &&
                    basic.grid.cell(tile) == (boundary ? maps::RoomCellKind::boundary :
                                               maps::RoomCellKind::walkable);
                basicCollision = basicCollision &&
                    basic.map->collision[static_cast<std::size_t>(y) * blueprint.room.width + x] ==
                    static_cast<std::uint8_t>(boundary ? 1 : 0);
                basicInterior = basicInterior && (!boundary || basic.map->layers[0].cells[
                    static_cast<std::size_t>(y) * blueprint.room.width + x].has_value());
            }
        }
    }
    const auto* basicTile = basic && basic.map && !basic.map->tileReferences.empty()
        ? content.authoringSemantics().findTile(basic.map->tileReferences[0].tilesetId,
                                                basic.map->tileReferences[0].sourceIndex)
        : nullptr;
    const auto basicSemantic = basic ? authoring::MapSemanticValidator{}.validate(
        *basic.map, content.authoringSemantics()) : authoring::SemanticValidationReport{};
    expect(basic && basic.map->width == 10 && basic.map->height == 8 &&
               basic.map->layers.size() == 1 && basic.map->collision.size() == 80,
           "MapComposer creates a bounded rectangular MapData from a minimal room blueprint");
    expect(basicBoundary && basicCollision && basicInterior,
           "room composition distinguishes boundary and walkable cells and derives collision from intent");
    expect(basicTile && basicTile->role == authoring::TileRole::wall && basicSemantic.issues.empty(),
           "room composition resolves its boundary through AuthoringSemanticRegistry without atlas hardcoding");

    const auto repeated = composer.compose(blueprint, content.authoringSemantics());
    expect(repeated && maps::semanticallyEqual(*basic.map, *repeated.map),
           "identical MapBlueprint input produces deterministic semantic MapData");

    blueprint.room.openings = {
        {maps::RoomSide::north, 4, 1}, {maps::RoomSide::east, 3, 1},
        {maps::RoomSide::south, 4, 1}, {maps::RoomSide::west, 3, 1}};
    const auto fourOpenings = composer.compose(blueprint, content.authoringSemantics());
    const auto openingTiles = maps::openingCells(blueprint.room);
    bool openingsPass = fourOpenings && openingTiles.size() == 4;
    if (openingsPass) {
        for (const auto tile : openingTiles) {
            const auto index = static_cast<std::size_t>(tile.y) * blueprint.room.width +
                                 static_cast<std::size_t>(tile.x);
            openingsPass = openingsPass && fourOpenings.grid.cell(tile) == maps::RoomCellKind::opening &&
                !fourOpenings.map->layers[0].cells[index] && fourOpenings.map->collision[index] == 0;
        }
        openingsPass = openingsPass && fourOpenings.grid.cell({0, 0}) == maps::RoomCellKind::boundary &&
            fourOpenings.map->collision[0] == 1;
    }
    expect(openingsPass, "north east south and west openings interrupt only their boundary passages");

    const auto invalidOffset = [&] {
        auto value = blueprint;
        value.room.openings = {{maps::RoomSide::north, 0, 1}};
        return composer.compose(value, content.authoringSemantics());
    }();
    const auto zeroWidth = [&] {
        auto value = blueprint;
        value.room.openings = {{maps::RoomSide::south, 4, 0}};
        return composer.compose(value, content.authoringSemantics());
    }();
    const auto overlong = [&] {
        auto value = blueprint;
        value.room.openings = {{maps::RoomSide::east, 1, 7}};
        return composer.compose(value, content.authoringSemantics());
    }();
    const auto overlap = [&] {
        auto value = blueprint;
        value.room.openings = {{maps::RoomSide::north, 4, 2}, {maps::RoomSide::north, 5, 1}};
        return composer.compose(value, content.authoringSemantics());
    }();
    const auto hasIssue = [](const maps::MapCompositionResult& result, std::string_view code) {
        return std::any_of(result.report.issues.begin(), result.report.issues.end(),
                           [&](const auto& issue) { return issue.code == code; });
    };
    expect(!invalidOffset && hasIssue(invalidOffset, "invalid_opening") &&
               !zeroWidth && hasIssue(zeroWidth, "invalid_opening") &&
               !overlong && hasIssue(overlong, "invalid_opening") &&
               !overlap && hasIssue(overlap, "opening_overlap"),
           "invalid and overlapping openings fail with structured deterministic diagnostics");

    blueprint.room.openings = {{maps::RoomSide::south, 4, 1}, {maps::RoomSide::east, 3, 1}};
    blueprint.room.playerSpawn = maps::PlayerSpawnBlueprint{
        simulation::SpawnId{"entry.start"}, {4, 3},
        underworld::game::gameplay::FacingDirection::down};
    const auto withSpawn = composer.compose(blueprint, content.authoringSemantics());
    const auto structural = withSpawn && withSpawn.map
        ? maps::validateMapData(*withSpawn.map) : maps::MapValidationResult{};
    expect(withSpawn && withSpawn.map->playerSpawns.size() == 1 && structural,
           "optional blueprint player spawn becomes a structurally valid authored spawn");
    expect(withSpawn && withSpawn.map->collision[static_cast<std::size_t>(3) * 10U + 4U] == 0 &&
               withSpawn.map->playerSpawns[0].position == underworld::core::WorldPointI{72, 56},
           "composed player spawn resolves to the center of a walkable authored tile");
    const auto composedBytes = withSpawn ? maps::serializeDmap(*withSpawn.map)
                                         : std::vector<std::uint8_t>{};
    const auto composedRoundtrip = withSpawn
        ? maps::deserializeDmap(composedBytes) : maps::DmapLoadResult{};
    expect(composedRoundtrip && maps::semanticallyEqual(*withSpawn.map, composedRoundtrip.data),
           "composed MapData remains compatible with the existing deterministic DMAP v1 pipeline");

    const auto reachability = maps::ReachabilityValidator{}.validateRoomOpenings(
        *withSpawn.map, blueprint.room, {4, 3});
    expect(reachability.valid(), "spawn reachability reaches every composed opening through collision data");

    auto isolated = *withSpawn.map;
    const auto isolatedOpening = maps::openingCells(blueprint.room).front();
    const auto isolationCell = isolatedOpening.y == 0
        ? underworld::core::TileCoord{isolatedOpening.x, isolatedOpening.y + 1}
        : isolatedOpening.y + 1 == static_cast<int>(isolated.height)
            ? underworld::core::TileCoord{isolatedOpening.x, isolatedOpening.y - 1}
            : isolatedOpening.x == 0
                ? underworld::core::TileCoord{isolatedOpening.x + 1, isolatedOpening.y}
                : underworld::core::TileCoord{isolatedOpening.x - 1, isolatedOpening.y};
    isolated.collision[static_cast<std::size_t>(isolationCell.y) * isolated.width +
                       static_cast<std::size_t>(isolationCell.x)] = 1;
    const auto isolatedReport = maps::ReachabilityValidator{}.validateRoomOpenings(
        isolated, blueprint.room, {4, 3});
    expect(!isolatedReport.valid() && std::any_of(
               isolatedReport.issues.begin(), isolatedReport.issues.end(), [](const auto& issue) {
                   return issue.code == "unreachable_opening";
               }),
           "reachability reports an opening isolated by authored collision");

    auto invalidSpawnBlueprint = blueprint;
    invalidSpawnBlueprint.room.playerSpawn->tile = {0, 0};
    const auto invalidSpawn = composer.compose(invalidSpawnBlueprint, content.authoringSemantics());
    expect(!invalidSpawn && hasIssue(invalidSpawn, "spawn_not_walkable"),
           "spawn on a composed boundary is rejected instead of silently falling back");

    maps::MapCompositionProfile missingProfile;
    missingProfile.boundaryTileId = simulation::DefinitionId{"tile.missing"};
    maps::MapBlueprint validBlueprint;
    validBlueprint.id = simulation::MapId{"map.composition.missing_semantic"};
    validBlueprint.room.width = 4;
    validBlueprint.room.height = 4;
    const auto missingSemantic = maps::MapComposer{missingProfile}.compose(
        validBlueprint, content.authoringSemantics());
    expect(!missingSemantic && hasIssue(missingSemantic, "composition_missing_semantic"),
           "missing composition semantics fail clearly without inventing an atlas tile");
}

void testAuditSessionFoundation() {
    using namespace underworld;
    namespace audit = game::audit;

    expect(audit::escapeJsonString("quote\" slash\\ line\n tab\t") ==
               "quote\\\" slash\\\\ line\\n tab\\t",
           "audit JSON string escaping protects quotes backslashes and controls");

    const audit::AuditEvent event{
        812, "EntityDefeated",
        {{"map", std::string{"map.dungeon.02"}}, {"instance", std::uint64_t{102}},
         {"visible", true}}};
    expect(audit::serializeAuditEvent(event) ==
               "{\"tick\":812,\"type\":\"EntityDefeated\",\"map\":\"map.dungeon.02\","
               "\"instance\":102,\"visible\":true}",
           "audit events serialize as deterministic structured JSON lines");

    const auto root = std::filesystem::temp_directory_path() / "underworld_audit_session_test";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    expect(audit::makeAuditSessionDirectory(root, "unit_01") == root / "unit_01",
           "audit session directory uses an explicit safe session identifier");
    bool unsafeRejected = false;
    try {
        static_cast<void>(audit::makeAuditSessionDirectory(root, "../escape"));
    } catch (const std::invalid_argument&) {
        unsafeRejected = true;
    }
    expect(unsafeRejected, "audit session directory rejects traversal identifiers");

    audit::GameAuditSnapshot snapshot;
    snapshot.tick = 60;
    snapshot.currentMap = "map.dungeon.01";
    snapshot.currentSpawn = "entry.start";
    snapshot.playerX = 80;
    snapshot.playerY = 96;
    snapshot.playerFacing = "down";
    snapshot.playerHealth = 4;
    snapshot.playerMaximumHealth = 5;
    snapshot.gold = 17;
    snapshot.inventory.push_back({2, "item.life_potion", 3});
    snapshot.enemies.push_back({9, "enemy.evil_soldier", 128, 96, 5, 5, "chase"});
    snapshot.dialogue.active = true;
    snapshot.dialogue.dialogueId = "dialogue.guard";
    snapshot.dialogue.nodeId = "guard.entry";
    snapshot.quests.push_back({"quest.scholar", "active", {{"objective.talk", 1}}});
    snapshot.lastEvent = "SAVED\" checkpoint";
    const auto snapshotJson = audit::serializeAuditSnapshot(snapshot);
    expect(snapshotJson.find("\"map\":\"map.dungeon.01\"") != std::string::npos &&
               snapshotJson.find("enemy.evil_soldier") != std::string::npos &&
               snapshotJson.find("dialogue.guard") != std::string::npos &&
               snapshotJson.find("objective.talk") != std::string::npos &&
               snapshotJson.find("SAVED\\\" checkpoint") != std::string::npos,
           "audit snapshot serialization includes stable gameplay diagnostics and event context");

    audit::AuditSession session;
    audit::AuditSessionConfig config;
    config.outputRoot = root;
    config.sessionId = "unit_01";
    config.metadata.mode = "automated";
    config.metadata.scenario = "audit_foundation";
    config.metadata.initialMap = "map.dungeon.01";
    config.metadata.initialSpawn = "entry.start";
    config.metadata.commandLine = {"tests", "--audit"};
    config.stateCheckpointInterval = 60;
    std::string error;
    expect(session.open(config, error), "enabled audit session creates its output files");
    expect(session.recordEvent(event), "enabled audit session records structured events");
    expect(session.recordState(snapshot), "audit session records the first state checkpoint");
    snapshot.tick = 90;
    expect(!session.recordState(snapshot), "unchanged audit interval keeps state checkpoints cached");
    snapshot.tick = 120;
    expect(session.recordState(snapshot), "audit session records the next periodic checkpoint");
    snapshot.tick = 121;
    expect(session.recordState(snapshot, true), "audit session supports forced state checkpoints");
    expect(session.close("PASS"), "audit session flushes event state and summary files");
    expect(std::filesystem::exists(root / "unit_01" / "session.json") &&
               std::filesystem::exists(root / "unit_01" / "events.jsonl") &&
               std::filesystem::exists(root / "unit_01" / "state.jsonl") &&
               std::filesystem::exists(root / "unit_01" / "summary.txt") &&
               std::filesystem::is_directory(root / "unit_01" / "screenshots"),
           "audit session creates the documented artifact layout");
    std::ifstream summary(root / "unit_01" / "summary.txt");
    const std::string summaryText((std::istreambuf_iterator<char>(summary)), {});
    expect(summaryText.find("events: 1") != std::string::npos &&
               summaryText.find("state checkpoints: 3") != std::string::npos,
           "audit summary reports event and checkpoint counts");

    audit::AuditSession disabled;
    audit::AuditSessionConfig disabledConfig;
    disabledConfig.enabled = false;
    expect(disabled.open(disabledConfig, error) && !disabled.enabled() &&
               !disabled.recordEvent(event) && !disabled.recordState(snapshot) &&
               disabled.close(),
           "disabled audit mode performs no file-backed work");
    std::filesystem::remove_all(root, cleanupError);
}

void testAuditFramebufferScreenshots() {
    using namespace underworld;
    namespace audit = game::audit;
    using core::ColorRGBA8;

    render::Framebuffer framebuffer(2, 2);
    framebuffer.pixels()[0] = {255, 0, 0, 17};
    framebuffer.pixels()[1] = {0, 255, 0, 34};
    framebuffer.pixels()[2] = {0, 0, 255, 51};
    framebuffer.pixels()[3] = {255, 255, 255, 68};
    const auto path = std::filesystem::temp_directory_path() / "underworld_audit_test.bmp";
    std::string error;
    expect(audit::writeBmp32(path, framebuffer.view(), error),
           "BMP writer accepts a logical RGBA8 framebuffer");
    std::ifstream file(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    expect(bytes.size() == 70U && bytes[0] == 'B' && bytes[1] == 'M' &&
               bytes[18] == 2U && bytes[22] == 2U && bytes[28] == 32U,
           "BMP writer emits a 32-bit header with the source dimensions");
    expect(bytes.size() >= 70U && bytes[54] == 255U && bytes[55] == 0U &&
               bytes[56] == 0U && bytes[57] == 51U &&
               bytes[62] == 0U && bytes[63] == 0U && bytes[64] == 255U &&
               bytes[65] == 17U,
           "BMP writer preserves bottom-up pixel order and RGBA alpha bytes");

    core::PixelBufferView invalid{nullptr, 2, 2, 8, core::PixelFormat::rgba8};
    expect(!audit::writeBmp32(path, invalid, error) && !error.empty(),
           "BMP writer rejects invalid pixel buffers with a clear error");

    const auto root = std::filesystem::temp_directory_path() / "underworld_bmp_session_test";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    audit::AuditSession session;
    audit::AuditSessionConfig config;
    config.outputRoot = root;
    config.sessionId = "bmp";
    expect(session.open(config, error) && session.captureScreenshot("startup", framebuffer.view(), error) &&
               session.screenshotCount() == 1U && session.close(),
           "audit session captures the framebuffer into its screenshot directory");
    expect(std::filesystem::exists(root / "bmp" / "screenshots" / "startup.bmp"),
           "audit screenshot names resolve only inside the session screenshot directory");
    std::filesystem::remove(path, cleanupError);
    std::filesystem::remove_all(root, cleanupError);
}

void testHeadlessAuditPlatform() {
    using namespace underworld;

    class NullImageDecoder final : public platform::ImageDecoder {
    public:
        [[nodiscard]] core::ImageData decode(const std::filesystem::path&) override {
            return {};
        }
    } decoder;

    platform::HeadlessAuditPlatform headless(decoder, std::filesystem::path{"test/bin"});
    expect(headless.isRunning() && !headless.isMinimized() && headless.nowSeconds() == 0.0,
           "headless platform starts running with a deterministic zero clock");
    platform::InputState moveRight;
    moveRight.moveRight = true;
    headless.setInput(1, moveRight);
    headless.holdInput(2, 3, moveRight);
    platform::InputState attack;
    attack.primaryAttackPressed = true;
    headless.setInput(4, attack);
    platform::DebugInputState debug;
    debug.toggleCollisionPressed = true;
    debug.captureAuditSnapshotPressed = true;
    headless.setDebugInput(4, debug);
    expect(headless.consumeInputState() == moveRight &&
               headless.consumeInputState() == moveRight &&
               headless.consumeInputState() == moveRight,
           "headless platform schedules held logical input by simulation tick");
    expect(headless.consumeInputState() == attack &&
               headless.consumeDebugInput().toggleCollisionPressed &&
               headless.consumeDebugInput().captureAuditSnapshotPressed,
           "headless platform injects action edges and debug input without physical key codes");
    headless.advanceFixedTicks(4);
    expect(approximately(headless.nowSeconds(), 4.0 / 60.0),
           "headless clock advances by an exact fixed-tick duration");

    render::Framebuffer framebuffer(2, 1);
    framebuffer.pixels()[0] = {10, 20, 30, 40};
    framebuffer.pixels()[1] = {50, 60, 70, 80};
    expect(headless.present(framebuffer.view()), "headless platform receives the real framebuffer");
    const auto presented = headless.lastPresentedFrame();
    const auto* pixels = reinterpret_cast<const std::uint8_t*>(presented.pixels);
    expect(presented.width == 2 && presented.height == 1 && presented.strideBytes == 8U &&
               pixels != nullptr && pixels[0] == 10U && pixels[3] == 40U && pixels[4] == 50U,
           "headless platform retains a tightly packed logical frame for audit consumers");
    expect(!headless.present({nullptr, 2, 1, 8, core::PixelFormat::rgba8}),
           "headless platform rejects an invalid presented surface");
    headless.log(platform::LogLevel::info, "headless checkpoint");
    expect(headless.logs().size() == 1U && headless.logs()[0].message == "headless checkpoint",
           "headless platform retains operational log messages");
    headless.stop();
    expect(!headless.isRunning(), "headless platform has a controlled lifetime");
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

void testAuthoredContentBoundary() {
    namespace content = underworld::game::content;
    namespace simulation = underworld::simulation;

    static_assert(!std::is_same_v<content::AuthoredAttack,
                                  underworld::game::gameplay::AttackDefinition>);
    static_assert(!std::is_same_v<content::AuthoredEnemy,
                                  underworld::game::gameplay::creatures::EnemyDefinition>);
    static_assert(!std::is_same_v<content::AuthoredNpc,
                                  underworld::game::gameplay::npcs::NpcDefinition>);
    static_assert(!std::is_same_v<content::AuthoredDialogue,
                                  underworld::game::gameplay::dialogue::DialogueDefinition>);
    static_assert(!std::is_same_v<content::AuthoredQuest,
                                  underworld::game::gameplay::quests::QuestDefinition>);

    const content::AuthoredContentPack builtin = content::makeBuiltinAuthoredContent();
    expect(builtin.tileSemantics.size() == 72 && builtin.stamps.size() == 8,
           "builtin authored content carries dungeon semantics before compilation");
    const auto compiled = content::compileContent(builtin);
    expect(compiled && compiled.registry->attacks().find(
               underworld::game::gameplay::playerSwordAttackId()) != nullptr &&
               compiled.registry->enemies().find(
                   underworld::game::gameplay::creatures::soldierEnemyId()) != nullptr &&
               compiled.registry->authoringSemantics().tiles().size() == 72,
           "builtin authored content compiles into a complete headless registry");

    const underworld::game::GameContentRegistry emptyRegistry;
    expect(emptyRegistry.attacks().find(underworld::game::gameplay::playerSwordAttackId()) == nullptr,
           "GameContentRegistry no longer seeds concrete content in its constructor");

    auto invalid = builtin;
    invalid.enemies.front().behaviorProfileId = simulation::DefinitionId{"behavior.missing"};
    const auto invalidResult = content::compileContent(invalid);
    const auto hasDiagnostic = [](const content::ContentValidationReport& report,
                                  std::string_view code, std::string_view field) {
        return std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
            [&](const auto& diagnostic) {
                return diagnostic.code == code && diagnostic.field == field;
            });
    };
    expect(!invalidResult && hasDiagnostic(invalidResult.report, "unknown_reference",
                                           "behaviorProfileId"),
           "content validation reports unknown references before registry publication");
    auto invalidWeight = builtin;
    invalidWeight.tileSemantics.front().variantWeight = 0;
    const auto invalidWeightResult = content::compileContent(invalidWeight);
    expect(!invalidWeightResult && hasDiagnostic(invalidWeightResult.report, "invalid_variant_weight",
                                                  "variantWeight"),
           "content validation rejects a zero Smart Terrain variant weight");

    auto invalidStamp = builtin;
    invalidStamp.stamps.front().cells.front().tileId = simulation::DefinitionId{"tile.missing"};
    const auto stampResult = content::compileContent(invalidStamp);
    expect(!stampResult && hasDiagnostic(stampResult.report, "invalid_stamp_cell", "cells"),
           "content validation rejects stamps with unknown semantic tiles");

    expect(invalidResult.report.diagnostics == content::compileContent(invalid).report.diagnostics,
           "content diagnostics are deterministic for the same authored pack");
}

void testPhase12BRewards() {
    using RewardProfileDefinition = underworld::game::gameplay::rpg::RewardProfileDefinition;
    using RewardResolver = underworld::game::gameplay::rpg::RewardResolver;
    const auto builtin = underworld::game::content::compileBuiltinContentOrThrow();
    const auto& soldier = builtin.rewards().require({"reward.enemy.evil_soldier"});
    const auto& skull = builtin.rewards().require({"reward.enemy.skull"});
    expect(soldier.experience == 60 && skull.experience == 40 && soldier.loot.size() == 2,
           "builtin reward profiles contain provisional XP and loot");
    expect(!std::is_same_v<underworld::game::content::AuthoredRewardProfile,
                           RewardProfileDefinition>,
           "authored reward profile is distinct from runtime definition");
    RewardResolver resolver;
    const underworld::simulation::MapId map{"map.dungeon.01"};
    const underworld::simulation::PersistentInstanceId enemy{42};
    const auto first = resolver.resolve(soldier, {map, enemy});
    const auto second = resolver.resolve(soldier, {map, enemy});
    expect(first.experience == 60 && first.loot == second.loot,
           "reward resolution is deterministic for a persistent enemy");
    const RewardProfileDefinition guaranteed{{"reward.test"}, 50,
        {{{"pickup.money"}, 10000, 2, 4}, {{"pickup.heart"}, 0, 1, 1}}};
    const auto guaranteedResult = resolver.resolve(guaranteed, {map, enemy});
    expect(guaranteedResult.experience == 50 && guaranteedResult.loot.size() == 1 &&
               guaranteedResult.loot.front().count >= 2 && guaranteedResult.loot.front().count <= 4,
           "guaranteed loot honors an independent count range");
    const RewardProfileDefinition xpOnly{{"reward.xp_only"}, 50, {}};
    expect(resolver.resolve(xpOnly, {map, enemy}).loot.empty(),
           "XP-only reward produces no loot");
    auto invalid = underworld::game::content::makeBuiltinAuthoredContent();
    invalid.rewardProfiles.front().loot.front().chanceBasisPoints = 10001;
    const auto invalidResult = underworld::game::content::compileContent(invalid);
    expect(!invalidResult && std::any_of(invalidResult.report.diagnostics.begin(),
        invalidResult.report.diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.kind == underworld::game::content::ContentKind::rewardProfile &&
                   diagnostic.code == "invalid_range";
        }), "invalid reward chance is rejected before publication");
}

void testPhase12BRewardEventSnapshot() {
    underworld::simulation::EventBuffer events;
    const underworld::simulation::EntityDefeated first{{1, 1}, {2, 1}, 1, {"enemy.evil_soldier"}};
    const underworld::simulation::EntityDefeated second{{1, 1}, {3, 1}, 2, {"enemy.skull"}};
    events.emit(first);
    events.emit(second);
    std::vector<underworld::simulation::EntityDefeated> defeatEvents;
    for (const auto& event : events.events()) {
        if (const auto* defeated = std::get_if<underworld::simulation::EntityDefeated>(&event)) {
            defeatEvents.push_back(*defeated);
        }
    }
    for (const auto& defeated : defeatEvents) {
        events.emit(underworld::simulation::ExperienceGranted{
            {1, 1}, defeated.defeatedDefinitionId, 1, 1, 1, 1});
    }
    const auto defeatedCount = std::count_if(events.events().begin(), events.events().end(),
        [](const auto& event) { return std::holds_alternative<underworld::simulation::EntityDefeated>(event); });
    const auto grantedCount = std::count_if(events.events().begin(), events.events().end(),
        [](const auto& event) { return std::holds_alternative<underworld::simulation::ExperienceGranted>(event); });
    expect(defeatEvents.size() == 2 && defeatedCount == 2 && grantedCount == 2,
           "reward processing snapshots multiple defeat events before emitting results");
}

void testPhase12C1EquipmentTransactions() {
    using namespace underworld;
    using namespace game::gameplay;
    using namespace game::gameplay::rpg;
    ItemCatalog catalog;
    const auto potion = makeLifePotionDefinition();
    catalog.add(potion);
    const simulation::DefinitionId armorA{"item.test.armor_a"};
    const simulation::DefinitionId armorB{"item.test.armor_b"};
    catalog.add({armorA, {"visual.test.armor_a"}, ItemCategory::equipment, 1,
                 std::nullopt, EquipmentDefinition{EquipmentSlot::armor, {1, 0}}});
    catalog.add({armorB, {"visual.test.armor_b"}, ItemCategory::equipment, 1,
                 std::nullopt, EquipmentDefinition{EquipmentSlot::armor, {2, 0}}});
    std::vector<simulation::DefinitionId> filler;
    for (int index = 0; index < 29; ++index) {
        filler.emplace_back("item.test.filler." + std::to_string(index));
        catalog.add({filler.back(), {"visual.test.filler"}, ItemCategory::misc, 1,
                     std::nullopt});
    }
    PlayerItems items(catalog);
    items.equipment().restore(armorA, std::nullopt);
    static_cast<void>(items.inventory().items().add(armorB, 1));
    for (const auto& item : filler) static_cast<void>(items.inventory().items().add(item, 1));
    expect(items.inventory().items().count(armorB) == 1 &&
               items.inventory().items().capacity() == 30,
           "equipment replacement fixture starts with a full inventory");
    expect(items.equipment().equipFromInventory(EquipmentSlot::armor, armorB,
                                                 items.inventory().items(), catalog) &&
               items.equipment().item(EquipmentSlot::armor) == armorB &&
               items.inventory().items().count(armorA) == 1 &&
               items.inventory().items().count(armorB) == 0,
           "full inventory equipment replacement frees the incoming slot first");
    expect(items.inventory().items().count(armorA) + items.inventory().items().count(armorB) == 1,
           "equipment replacement does not duplicate or lose either armor");
    expect(!items.equipment().unequipToInventory(EquipmentSlot::armor,
                                                  items.inventory().items(), catalog) &&
               items.equipment().item(EquipmentSlot::armor) == armorB,
           "plain unequip still fails when inventory is full");
}

void testPhase12C1Equipment() {
    using namespace underworld;
    using namespace game::gameplay;
    using namespace game::content;
    using namespace game::gameplay::rpg;
    static_assert(!std::is_same_v<AuthoredEquipment, EquipmentDefinition>);
    const auto content = compileBuiltinContentOrThrow();
    const auto& armor = content.items().require(simulation::DefinitionId{"item.training_armor"});
    const auto& charm = content.items().require(simulation::DefinitionId{"item.power_charm"});
    expect(armor.equipment && armor.equipment->modifiers.maximumHealthBonus == 2 &&
               armor.stackLimit == 1 && charm.equipment &&
               charm.equipment->modifiers.playerAttackDamageBonus == 1,
           "builtin equipment compiles with typed slots and modifiers");
    expect(content.items().require(lifePotionItemId()).stackLimit == 66,
           "equipment boundary preserves life potion stack limit");
    PlayerItems items(content.items());
    static_cast<void>(items.inventory().items().add(armor.id, 1));
    static_cast<void>(items.inventory().items().add(charm.id, 1));
    expect(items.equipment().equipFromInventory(EquipmentSlot::armor, armor.id,
                                                items.inventory().items(), content.items()) &&
               items.inventory().items().count(armor.id) == 0 &&
               items.equipment().item(EquipmentSlot::armor) == armor.id,
           "equipping moves armor out of inventory");
    const auto derived = derivePlayerStats({5}, items.equipment(), content.items());
    expect(derived.maximumHealth == 7 && derived.playerAttackDamageBonus == 0,
           "training armor derives maximum health without attack bonus");
    expect(items.equipment().equipFromInventory(EquipmentSlot::accessory, charm.id,
                                                items.inventory().items(), content.items()) &&
               derivePlayerStats({5}, items.equipment(), content.items()).playerAttackDamageBonus == 1,
           "power charm derives player attack damage bonus");
    expect(items.equipment().unequipToInventory(EquipmentSlot::armor, items.inventory().items(),
                                                content.items()) &&
               !items.equipment().item(EquipmentSlot::armor) &&
               items.inventory().items().count(armor.id) == 1,
           "unequipping returns armor to inventory");
    Health health(5);
    health.setMaximum(7);
    expect(health.current == 5 && health.maximum == 7,
           "increasing derived maximum health does not heal");
    health.current = 7;
    health.setMaximum(5);
    expect(health.current == 5, "decreasing maximum health clamps current health");
}

void testPhase12DBank() {
    using namespace underworld;
    using namespace game::gameplay;
    ItemCatalog catalog;
    catalog.add(makeLifePotionDefinition());
    const simulation::DefinitionId filler{"item.test.bank_filler"};
    catalog.add({filler, {"visual.test.filler"}, ItemCategory::misc, 1, std::nullopt});

    PlayerInventory inventory(catalog);
    PlayerBank bank(catalog);
    expect(PlayerBank::slotCount == 50 && PlayerBank::rows == 5 && PlayerBank::columns == 10,
           "player bank has the fixed 5 by 10 capacity");
    static_cast<void>(inventory.items().add(lifePotionItemId(), 10));
    expect(bank.depositItem(inventory.items(), lifePotionItemId(), 7) == 7 &&
               inventory.items().count(lifePotionItemId()) == 3 &&
               bank.items().count(lifePotionItemId()) == 7,
           "bank deposit reuses ItemContainer stacking and moves the requested quantity");
    expect(bank.withdrawItem(inventory.items(), lifePotionItemId(), 4) == 4 &&
               inventory.items().count(lifePotionItemId()) == 7 &&
               bank.items().count(lifePotionItemId()) == 3,
           "bank withdrawal moves only the requested quantity back to inventory");

    for (int index = 0; index < 50; ++index) {
        const simulation::DefinitionId id{"item.test.bank_slot." + std::to_string(index)};
        catalog.add({id, {"visual.test.filler"}, ItemCategory::misc, 1, std::nullopt});
        static_cast<void>(bank.items().add(id, 1));
    }
    static_cast<void>(inventory.items().add(filler, 1));
    expect(bank.depositItem(inventory.items(), filler, 1) == 0 &&
               inventory.items().count(filler) == 1,
           "full bank leaves an item transfer source unchanged");

    Wallet wallet;
    wallet.restoreGold(100);
    expect(bank.depositGold(wallet, 40) == 40 && wallet.gold() == 60 && bank.gold() == 40,
           "bank gold deposit moves carried gold safely");
    expect(bank.withdrawGold(wallet, 25) == 25 && wallet.gold() == 85 && bank.gold() == 15,
           "bank gold withdrawal credits the wallet safely");
    expect(wallet.removeGold(200) == 85 && wallet.gold() == 0,
           "wallet gold removal saturates at the available balance");
    wallet.restoreGold(std::numeric_limits<std::uint64_t>::max() - 1);
    bank.restoreGold(10);
    expect(bank.withdrawGold(wallet, 10) == 1 &&
               wallet.gold() == std::numeric_limits<std::uint64_t>::max() && bank.gold() == 9,
           "bank gold withdrawal saturates wallet capacity without loss");
}

} // namespace

void testPhase12E1RewardGrants() {
    const auto content = underworld::game::content::compileBuiltinContentOrThrow();
    static_assert(!std::is_same_v<underworld::game::content::AuthoredRewardGrant, underworld::game::gameplay::rpg::RewardGrantDefinition>);
    const auto& grant = content.rewardGrants().require({"reward.quest.scholar.path"});
    expect(grant.experience == 40 && grant.gold == 25 && grant.items.size() == 2,
           "builtin quest reward grant contains provisional XP, gold and items");
    const auto& quest = content.quests().require({"quest.scholar.path"});
    expect(quest.rewardGrantId && *quest.rewardGrantId == grant.id,
           "scholar quest references its guaranteed reward grant");

    underworld::game::gameplay::PlayerItems items(content.items());
    underworld::game::gameplay::rpg::PlayerProgressionState progression(content.progressions().require({"progression.player.default"}));
    underworld::game::gameplay::rpg::RewardGrantService service;
    const auto applied = service.grant(grant, progression, items);
    expect(applied.applied && applied.experience.granted == 40 && items.wallet().gold() == 25 &&
               items.inventory().items().count({"item.life_potion"}) == 2 &&
               items.inventory().items().count({"item.training_armor"}) == 1 &&
               !items.equipment().item(underworld::game::gameplay::rpg::EquipmentSlot::armor),
           "guaranteed quest reward applies XP, gold and inventory items without auto-equipping");

    underworld::game::gameplay::quests::QuestStateStore quests;
    expect(quests.start(quest), "rewarded quest starts with pending claim state");
    const auto* progress = quests.find(quest.id);
    expect(progress && !progress->rewardClaimed, "active rewarded quest starts unclaimed");
    for (const auto& objective : quest.objectives) static_cast<void>(quests.setObjectiveProgress(quest, objective.id, objective.requiredCount));
    progress = quests.find(quest.id);
    expect(progress && progress->status == underworld::game::gameplay::quests::QuestStatus::completed && !progress->rewardClaimed,
           "quest completion is distinct from guaranteed reward delivery");
    expect(quests.pendingRewardQuestIds(content.quests()).size() == 1 && quests.markRewardClaimed(quest) &&
               !quests.markRewardClaimed(quest), "pending reward claim is deterministic and exactly once");

    underworld::game::gameplay::PlayerItems full(content.items());
    const auto filler = underworld::game::gameplay::lifePotionItemId();
    static_cast<void>(full.inventory().items().add(filler, 66));
    for (std::size_t i = 1; i < full.inventory().items().capacity(); ++i)
        static_cast<void>(full.inventory().items().add({"item.training_armor"}, 1));
    for (std::size_t i = 0; i < full.bank().items().capacity(); ++i)
        static_cast<void>(full.bank().items().add({"item.training_armor"}, 1));
    underworld::game::gameplay::rpg::RewardGrantDefinition blocked{{"reward.test.blocked"}, 40, 25,
                                                  {{{"item.life_potion"}, 2}, {{"item.training_armor"}, 1}}};
    underworld::game::gameplay::rpg::PlayerProgressionState blockedProgression(content.progressions().require({"progression.player.default"}));
    const auto blockedResult = service.grant(blocked, blockedProgression, full);
    expect(!blockedResult.applied && blockedResult.blockedByStorage && blockedProgression.totalExperience() == 0 &&
               full.wallet().gold() == 0 && full.bank().gold() == 0,
           "guaranteed reward is atomic when inventory and bank cannot store all items");
}

void testPhase12E2Shops() {
    const auto content = underworld::game::content::compileBuiltinContentOrThrow();
    static_assert(!std::is_same_v<underworld::game::content::AuthoredShop, underworld::game::gameplay::rpg::ShopDefinition>);
    const auto& shop = content.shops().require({"shop.development.general"});
    const auto& potion = *underworld::game::gameplay::rpg::findOffer(shop, {"item.life_potion"});
    expect(potion.playerBuyPrice == 25 && potion.playerSellPrice == 10 && shop.offers.size() == 3,
           "builtin development shop contains authored buy and sell prices");

    using namespace underworld::game;
    gameplay::PlayerItems items(content.items());
    gameplay::rpg::ShopTransactionService service;
    items.wallet().restoreGold(100);
    auto bought = service.buyOne(shop, {"item.life_potion"}, items);
    expect(bought && bought.quantity == 1 && bought.gold == 25 && items.wallet().gold() == 75 &&
               items.inventory().items().count({"item.life_potion"}) == 1,
           "shop buy uses carried wallet and inventory atomically");
    for (int i = 0; i < 2; ++i) static_cast<void>(service.buyOne(shop, {"item.life_potion"}, items));
    expect(items.wallet().gold() == 25 && items.inventory().items().count({"item.life_potion"}) == 3,
           "shop purchases are one-unit transactions and stack normally");

    items.wallet().restoreGold(50);
    const auto sold = service.sellOneFromSlot(shop, 0, items);
    expect(sold && sold.gold == 10 && items.wallet().gold() == 60 &&
               items.inventory().items().count({"item.life_potion"}) == 2,
           "shop sale removes exactly one item from the selected inventory slot");

    gameplay::PlayerItems full(content.items());
    full.wallet().restoreGold(200);
    for (std::size_t i = 0; i < full.inventory().items().capacity(); ++i)
        static_cast<void>(full.inventory().items().add({"item.training_armor"}, 1));
    const auto fullBuy = service.buyOne(shop, {"item.life_potion"}, full);
    expect(fullBuy.status == gameplay::rpg::ShopTransactionStatus::inventoryFull && full.wallet().gold() == 200,
           "full inventory rejects purchase without charging wallet or using bank");

    gameplay::PlayerItems rich(content.items());
    rich.wallet().restoreGold(std::numeric_limits<std::uint64_t>::max() - 5);
    static_cast<void>(rich.inventory().items().add({"item.training_armor"}, 1));
    const auto saturated = service.sellOneFromSlot(shop, 0, rich);
    expect(saturated.status == gameplay::rpg::ShopTransactionStatus::walletCapacityExceeded &&
               rich.inventory().items().count({"item.training_armor"}) == 1 &&
               rich.wallet().gold() == std::numeric_limits<std::uint64_t>::max() - 5,
           "wallet saturation rejects sale without removing the item");

    auto invalid = underworld::game::content::makeBuiltinAuthoredContent();
    invalid.shops.front().offers.front().playerBuyPrice.reset();
    invalid.shops.front().offers.front().playerSellPrice.reset();
    const auto invalidResult = underworld::game::content::compileContent(invalid);
    expect(!invalidResult, "shop offer without an operation is rejected by content validation");
}

void testPhase12E3ShopInterface() {
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace simulation = underworld::simulation;
    const auto content = game::content::compileBuiltinContentOrThrow();
    const auto& shop = content.shops().require({"shop.development.general"});
    const auto& merchant = content.npcs().require({"npc.merchant"});
    const auto& dialogue = content.dialogues().require({"dialogue.merchant.greeting"});
    expect(merchant.defaultDialogueId == dialogue.id && dialogue.nodes.front().choices.front().actions.front().kind == gameplay::dialogue::DialogueActionKind::openShop &&
               dialogue.nodes.front().choices.front().actions.front().targetId == shop.id,
           "merchant dialogue opens the authored development shop by definition ID");
    gameplay::ShopOverlayState overlay;
    overlay.open(shop);
    expect(overlay.open() && overlay.mode() == gameplay::ShopOverlayMode::buy && overlay.buySelection() == 0,
           "shop overlay opens in buy mode on the first purchasable offer");
    simulation::PlayerCommand command;
    command.movement.y = 1;
    gameplay::PlayerItems navigationItems(content.items());
    static_cast<void>(gameplay::routeShopCommand(overlay, command, shop, navigationItems, gameplay::rpg::ShopTransactionService{}));
    expect(overlay.buySelection() == 1, "shop buy navigation advances through purchasable offers");
    gameplay::PlayerItems items(content.items());
    items.wallet().restoreGold(100);
    command = {};
    command.actions.primaryAttackPressed = true;
    const auto buy = gameplay::routeShopCommand(overlay, command, shop, items, gameplay::rpg::ShopTransactionService{});
    expect(buy.transaction && buy.transaction->status == gameplay::rpg::ShopTransactionStatus::insufficientGold && items.wallet().gold() == 100,
           "shop command routing preserves typed atomic buy failure feedback");
    command = {};
    command.actions.secondaryAttackPressed = true;
    static_cast<void>(gameplay::routeShopCommand(overlay, command, shop, items, gameplay::rpg::ShopTransactionService{}));
    expect(overlay.mode() == gameplay::ShopOverlayMode::sell && !overlay.feedback(),
           "secondary switches shop mode and clears transaction feedback");
    overlay.close();
    expect(!overlay.open() && overlay.activeShopId().empty(), "closing shop clears transient active state");
}

void testPhase13AJsonFoundation() {
    using underworld::engine::data::parseJson;
    using underworld::game::content::decodeAuthoredContentJson;
    const auto parsed = parseJson(R"({"text":"Olá, viajante.","n":18446744073709551615,"unicode":"\uD83D\uDE00"})");
    expect(parsed.value && parsed.diagnostics.empty(), "strict JSON parser accepts UTF-8, escapes and uint64 lexemes");
    expect(parseJson(R"({"id":"a","id":"b"})").value == nullptr &&
               parseJson(R"([1,])").value == nullptr && parseJson("{\"x\":1} trailing").value == nullptr,
           "strict JSON rejects duplicate keys, trailing commas and trailing data");
    expect(parseJson(R"({"x":01})").value == nullptr && parseJson(R"({"x":1.})").value == nullptr &&
               parseJson(R"({"x":NaN})").value == nullptr,
           "strict JSON rejects malformed numbers");
    const auto decoded = decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":1,"items":[{"id":"item.x","visualId":"visual.x","category":"misc","stackLimit":3}],"shops":[]})");
    expect(decoded.content && decoded.content->items.size() == 1 && decoded.content->items.front().stackLimit == 3,
           "content JSON decodes typed item fields and optional category arrays");
    expect(!decodeAuthoredContentJson(R"({"format":"wrong","version":1})").content &&
               decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":2})").content &&
               decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":3})").content &&
               decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":4})").content &&
               decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":5})").content &&
               !decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":6})").content,
           "content JSON accepts v1-v5 and rejects wrong format identifiers and unsupported versions");
    const auto coreJson = R"({"format":"dungeon-underworld-content","version":1,"tilesets":[{"id":"tileset.decoder","displayName":"T","relativeAssetPath":"t.png","tileSize":16,"columns":2,"rows":3}],"behaviors":[{"id":"behavior.decoder","detectionRangePixels":12,"disengageRangePixels":18,"idleDurationTicks":7,"wanderDurationTicks":9}],"items":[{"id":"item.decoder","visualId":"visual.decoder","category":"consumable","stackLimit":66,"use":{"kind":"restoreHealth","amount":3}},{"id":"item.armor","visualId":"visual.armor","category":"equipment","stackLimit":1,"equipment":{"slot":"armor","modifiers":{"maximumHealthBonus":2,"playerAttackDamageBonus":0}}}],"npcVisuals":[{"id":"visual.decoder.npc","markerColor":{"r":1,"g":2,"b":3,"a":255}}],"playerProgressions":[{"id":"progression.decoder","baseStats":{"maximumHealth":5},"cumulativeExperienceThresholds":[0,100,18446744073709551615]}],"rewardProfiles":[{"id":"reward.decoder","experience":18446744073709551615,"loot":[]}],"rewardGrants":[{"id":"grant.decoder","experience":4,"gold":5,"items":[{"itemId":"item.decoder","quantity":100}]}],"shops":[{"id":"shop.decoder","offers":[{"itemId":"item.decoder","playerBuyPrice":0,"playerSellPrice":null},{"itemId":"item.armor","playerSellPrice":80}]}],"authoringDescriptors":[{"definitionId":"item.decoder","displayName":"Decoder","category":"item","tags":["test"]}]})";
    const auto roundtrip = decodeAuthoredContentJson(coreJson);
    expect(roundtrip.content && roundtrip.diagnostics.empty() && roundtrip.content->tilesets.size() == 1 &&
               roundtrip.content->behaviors.size() == 1 && roundtrip.content->items.size() == 2 &&
               roundtrip.content->items[0].use && roundtrip.content->items[1].equipment &&
               roundtrip.content->npcVisuals.size() == 1 && roundtrip.content->npcVisuals[0].markerColor.r == 1 &&
               roundtrip.content->playerProgressions.size() == 1 && roundtrip.content->playerProgressions[0].cumulativeExperienceThresholds.back() == UINT64_MAX &&
               roundtrip.content->rewardProfiles.size() == 1 && roundtrip.content->rewardProfiles[0].experience == UINT64_MAX &&
               roundtrip.content->rewardGrants.size() == 1 && roundtrip.content->rewardGrants[0].items[0].quantity == 100 &&
               roundtrip.content->shops.size() == 1 && roundtrip.content->shops[0].offers[0].playerBuyPrice == 0 && !roundtrip.content->shops[0].offers[0].playerSellPrice &&
               roundtrip.content->authoringDescriptors.size() == 1 && roundtrip.content->authoringDescriptors[0].category == underworld::game::content::AuthoringCategory::item,
           "core authored DTO decoder preserves all 13A1 categories and uint64 precision");
    expect(!decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":1,"items":[{"id":"x","visualId":"v","category":"misc","stackLmit":3}]})").content,
           "core decoder rejects unknown nested item fields");
    const auto doorV2 = decodeAuthoredContentJson(
        R"({"format":"dungeon-underworld-content","version":2,"objects":[{"id":"object.door.test","visualSetId":"visual.door.test","door":{"initialState":"closed","blockingBounds":{"x":0,"y":0,"width":16,"height":16}}}]})");
    expect(doorV2.content && doorV2.diagnostics.empty() && doorV2.content->objects.size() == 1 &&
               doorV2.content->objects.front().door &&
               doorV2.content->objects.front().door->initialState == underworld::game::gameplay::DoorState::closed,
           "content schema v2 decodes a typed door capability");
    const auto doorV2Json = doorV2.content ? encodeAuthoredContentJson(*doorV2.content) : std::string{};
    const auto doorV2Roundtrip = decodeAuthoredContentJson(doorV2Json);
    expect(doorV2Roundtrip.content && doorV2Roundtrip.diagnostics.empty() &&
               encodeAuthoredContentJson(*doorV2Roundtrip.content) == doorV2Json,
           "content schema v2 door encoding remains byte-identical after decode");
    expect(!decodeAuthoredContentJson(
               R"({"format":"dungeon-underworld-content","version":1,"objects":[{"id":"object.door.test","visualSetId":"visual.door.test","door":{"initialState":"closed","blockingBounds":{"x":0,"y":0,"width":16,"height":16}}}]})").content,
           "content schema v1 rejects the door capability instead of changing its meaning silently");
    const std::string invalidUtf8{"{\"x\":\xC0\x80}"};
    expect(parseJson(invalidUtf8).value == nullptr,
           "strict JSON rejects overlong raw UTF-8 sequences");

    const auto minimal = [&](std::string_view category, std::string_view entry) {
        return decodeAuthoredContentJson(std::string{"{\"format\":\"dungeon-underworld-content\",\"version\":1,\""} +
                                          std::string{category} + "\":[" + std::string{entry} + "]}");
    };
    const auto hasDiagnostic = [](const auto& result, std::string_view path, std::string_view message) {
        for (const auto& diagnostic : result.diagnostics) {
            if (diagnostic.path == path && diagnostic.message.find(message) != std::string::npos) return true;
        }
        return false;
    };
    const auto visual = minimal("npcVisuals", R"({"id":"visual.npc.decoder_test","markerColor":{"r":10,"g":20,"b":30,"a":255}})");
    expect(visual.content && visual.diagnostics.empty() && visual.content->npcVisuals.size() == 1 &&
               visual.content->npcVisuals[0].id.value() == "visual.npc.decoder_test" &&
               visual.content->npcVisuals[0].markerColor.r == 10 && visual.content->npcVisuals[0].markerColor.g == 20 &&
               visual.content->npcVisuals[0].markerColor.b == 30 && visual.content->npcVisuals[0].markerColor.a == 255,
           "npc visual decoder dispatches and preserves RGBA");
    expect(!minimal("npcVisuals", R"({"id":"v","markerColor":{"r":-1,"g":0,"b":0,"a":0}})").content &&
               !minimal("npcVisuals", R"({"id":"v","markerColor":{"r":256,"g":0,"b":0,"a":0}})").content &&
               !minimal("npcVisuals", R"({"id":"v","markerColor":{"r":1.5,"g":0,"b":0,"a":0}})").content &&
               !minimal("npcVisuals", R"({"id":"v","markerColor":{"r":"255","g":0,"b":0,"a":0}})").content &&
               !minimal("npcVisuals", R"({"id":"v","markerColor":{"r":0,"g":0,"b":0,"a":0,"alpha":1}})").content,
           "color decoder rejects range, fractional and unknown fields");
    expect(!minimal("items", R"({"id":"i","visualId":"v","stackLimit":1})").content &&
               hasDiagnostic(minimal("items", R"({"id":"i","visualId":"v","stackLimit":1})"), "items[0].category", "missing required field"),
           "item category is required with a precise diagnostic");
    expect(!minimal("items", R"({"id":"i","visualId":"v","category":"consumable","stackLimit":1,"use":{"amount":2}})").content &&
               hasDiagnostic(minimal("items", R"({"id":"i","visualId":"v","category":"consumable","stackLimit":1,"use":{"amount":2}})"), "items[0].use.kind", "missing required field") &&
               !minimal("items", R"({"id":"i","visualId":"v","category":"consumable","stackLimit":1,"use":{"kind":"restoreMana","amount":2}})").content,
           "item use requires and strictly decodes its kind");
    const auto missingModifier = minimal("items", R"({"id":"i","visualId":"v","category":"equipment","stackLimit":1,"equipment":{"modifiers":{"maximumHealthBonus":2}}})");
    expect(!missingModifier.content && hasDiagnostic(missingModifier, "items[0].equipment.slot", "missing required field") &&
               hasDiagnostic(missingModifier, "items[0].equipment.modifiers.playerAttackDamageBonus", "missing required field"),
           "equipment required fields are safe and diagnostic");
    expect(!minimal("items", R"({"id":"i","visualId":"v","category":"equipment","stackLimit":1,"equipment":{"slot":"armor"}})").content &&
               hasDiagnostic(minimal("items", R"({"id":"i","visualId":"v","category":"equipment","stackLimit":1,"equipment":{"slot":"armor"}})"), "items[0].equipment.modifiers", "missing required field"),
           "equipment modifiers are required");
    const auto multilineMissing = decodeAuthoredContentJson(R"({
  "format": "dungeon-underworld-content",
  "version": 1,
  "shops": [{
    "id": "shop.test"
  }]
})");
    const auto missingOffers = std::find_if(multilineMissing.diagnostics.begin(), multilineMissing.diagnostics.end(),
                                            [](const auto& diagnostic) {
                                                return diagnostic.path == "shops[0].offers" &&
                                                       diagnostic.message == "missing required field";
                                            });
    expect(!multilineMissing.content && missingOffers != multilineMissing.diagnostics.end() &&
               missingOffers->line == 4 && missingOffers->column == 13,
           "missing required field uses the parent object source span");
    expect(!minimal("playerProgressions", R"({"id":"p","cumulativeExperienceThresholds":[]})").content &&
               !minimal("rewardProfiles", R"({"id":"r","experience":0})").content &&
               !minimal("rewardGrants", R"({"id":"g","experience":0,"gold":0})").content &&
               !minimal("shops", R"({"id":"s"})").content &&
               !minimal("authoringDescriptors", R"({"definitionId":"d","displayName":"D","tags":[]})").content,
           "all core DTO required collections and objects are enforced");
    expect(minimal("items", R"({"id":"i","visualId":"v","category":"misc","stackLimit":1})").content &&
               !minimal("items", R"({"id":"i","visualId":"v","category":"misc","stackLimit":18446744073709551615})").content &&
               minimal("shops", R"({"id":"s","offers":[{"itemId":"i","playerBuyPrice":null,"playerSellPrice":0}]})").content,
           "numeric ranges and optional null shop prices are handled exactly");
    expect(!decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":1,"mysteryCategory":[]})").content &&
               !decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":1,"items":{}})").content &&
               !decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":1,"attacks":{}})").content &&
               !decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":1,"attacks":[{"id":"a"}]})").content,
           "top-level strictness rejects unknown, malformed and unsupported categories");
    const auto invalidVersionType = decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":"one"})");
    expect(!invalidVersionType.content && invalidVersionType.diagnostics.size() == 1 &&
               invalidVersionType.diagnostics[0].path == "version",
           "invalid schema version type emits one numeric diagnostic");
}

void testPhase13A2JsonDecoders() {
    using underworld::game::content::decodeAuthoredContentJson;
    using namespace underworld::game::content;
    namespace gameplay = underworld::game::gameplay;
    const auto document = [](std::string_view category, std::string_view entries) {
        return decodeAuthoredContentJson(std::string{"{\"format\":\"dungeon-underworld-content\",\"version\":1,\""} +
                                          std::string{category} + "\":[" + std::string{entries} + "]}");
    };
    const auto projectile = document("projectiles", R"({"id":"projectile.test","visualId":"visual.arrow","canonicalFacing":"left","speedPixelsPerTick":-2,"lifetimeTicks":12,"hitboxWidth":3,"hitboxHeight":4,"spawnOffsets":{"down":{"x":1,"y":2},"up":{"x":3,"y":4},"left":{"x":-5,"y":6},"right":{"x":7,"y":-8}}})");
    expect(projectile.content && projectile.content->projectiles.size() == 1 && projectile.content->projectiles[0].canonicalFacing == gameplay::FacingDirection::left &&
               projectile.content->projectiles[0].lifetimeTicks == 12 && projectile.content->projectiles[0].spawnOffsets.values[2].x == -5,
           "13A2 projectile decoder preserves facing, lifetime and directional offsets");
    const auto melee = document("attacks", R"({"id":"attack.melee.test","kind":"meleeHitbox","damage":{"amount":3,"knockbackPixels":2},"totalTicks":8,"cooldownTicks":10,"minimumRangePixels":0,"maximumRangePixels":16,"visualActionId":"visual.swing","meleeHitboxes":{"down":{"offsetX":1,"offsetY":2,"width":3,"height":4},"up":{"offsetX":-1,"offsetY":-2,"width":3,"height":4},"left":{"offsetX":1,"offsetY":0,"width":5,"height":6},"right":{"offsetX":1,"offsetY":0,"width":5,"height":6}},"timeline":[{"tick":2,"kind":"activateHitbox"},{"tick":5,"kind":"deactivateHitbox"}]})");
    const auto ranged = document("attacks", R"({"id":"attack.projectile.test","kind":"projectile","damage":{"amount":4,"knockbackPixels":1},"totalTicks":6,"cooldownTicks":9,"minimumRangePixels":2,"maximumRangePixels":30,"visualActionId":"visual.cast","projectileDefinitionId":"projectile.test","timeline":[{"tick":3,"kind":"spawnProjectile"}]})");
    expect(melee.content && melee.content->attacks[0].kind == gameplay::AttackKind::meleeHitbox && melee.content->attacks[0].damage.amount == 3 &&
               melee.content->attacks[0].meleeHitboxes && melee.content->attacks[0].meleeHitboxes->values[1].offsetX == -1 && melee.content->attacks[0].timeline.size() == 2 &&
               ranged.content && ranged.content->attacks[0].projectileDefinitionId && ranged.content->attacks[0].timeline[0].kind == gameplay::AttackTimelineEventKind::spawnProjectile,
           "13A2 melee and projectile attack decoders preserve complex fields");
    const auto enemy = document("enemies", R"({"id":"enemy.test","visualSetId":"visual.enemy","behaviorProfileId":"behavior.test","faction":"neutral","maximumHealth":20,"movementSpeedSubpixelsPerTick":-9223372036854775807,"collisionBody":{"offsetX":-2,"offsetY":-3,"width":8,"height":9},"hurtbox":{"offsetX":-1,"offsetY":-4,"width":10,"height":11},"attackIds":["attack.melee.test","attack.projectile.test"],"rewardProfileId":"reward.test"})");
    expect(enemy.content && enemy.content->enemies.size() == 1 && enemy.content->enemies[0].faction == gameplay::Faction::neutral &&
               enemy.content->enemies[0].movementSpeedSubpixelsPerTick == -9223372036854775807LL && enemy.content->enemies[0].collisionBody.offsetX == -2 &&
               enemy.content->enemies[0].attackIds.size() == 2 && enemy.content->enemies[0].rewardProfileId,
           "13A2 enemy decoder preserves faction, int64 speed, boxes and references");
    const auto object = document("objects", R"({"id":"object.test","visualSetId":"visual.chest","interactable":{"x":-1,"y":2,"width":16,"height":17},"container":{"capacity":50},"destructible":{"maximumHealth":8,"hurtbox":{"x":0,"y":1,"width":12,"height":13},"destructionDurationTicks":28},"bankAccess":{}})");
    expect(object.content && object.content->objects.size() == 1 && object.content->objects[0].interactable && object.content->objects[0].container->capacity == 50 &&
               object.content->objects[0].destructible->hurtbox.height == 13 && object.content->objects[0].bankAccess,
           "13A2 world object decoder preserves independent capabilities");
    const auto pickups = decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":1,"pickups":[{"id":"pickup.health.test","visualId":"visual.heart","collectionBounds":{"x":0,"y":0,"width":8,"height":8},"payload":{"kind":"health","amount":2}},{"id":"pickup.gold.test","visualId":"visual.gold","collectionBounds":{"x":1,"y":2,"width":8,"height":8},"payload":{"kind":"currency","amount":18446744073709551615}},{"id":"pickup.item.test","visualId":"visual.potion","collectionBounds":{"x":2,"y":3,"width":8,"height":8},"payload":{"kind":"item","itemId":"item.life_potion","quantity":3}}]})");
    expect(pickups.content && std::get<AuthoredHealthPickup>(pickups.content->pickups[0].payload).amount == 2 &&
               std::get<AuthoredCurrencyPickup>(pickups.content->pickups[1].payload).amount == UINT64_MAX && std::get<AuthoredItemPickup>(pickups.content->pickups[2].payload).quantity == 3,
           "13A2 pickup decoder preserves all tagged payload variants and uint64 currency");
    const auto npc = document("npcs", R"({"id":"npc.test","visualSetId":"visual.npc","interaction":{"x":-4,"y":-5,"width":12,"height":13},"interactionEnabled":false,"defaultDialogueId":"dialogue.test","tags":["test","friendly"]})");
    expect(npc.content && npc.content->npcs.size() == 1 && !npc.content->npcs[0].interaction.enabled && npc.content->npcs[0].interaction.bounds.x == -4 &&
               npc.content->npcs[0].defaultDialogueId.value() == "dialogue.test" && npc.content->npcs[0].tags.size() == 2,
           "13A2 NPC decoder preserves interaction, dialogue reference and tags");
    expect(!document("projectiles", R"({"id":"p","visualId":"v","canonicalFacing":"diagonal","speedPixelsPerTick":1,"lifetimeTicks":1,"hitboxWidth":1,"hitboxHeight":1,"spawnOffsets":{"down":{"x":0,"y":0},"up":{"x":0,"y":0},"left":{"x":0,"y":0},"right":{"x":0,"y":0}}})").content &&
               !document("attacks", R"({"id":"a","kind":"meleeHitbox","damage":{"amount":1,"knockbackPixels":0},"totalTicks":1,"cooldownTicks":1,"minimumRangePixels":0,"maximumRangePixels":1,"visualActionId":"v","timeline":[{"tick":0,"kind":"unknown"}]})").content &&
               !document("enemies", R"({"id":"e","visualSetId":"v","behaviorProfileId":"b","faction":"enemy","maximumHealth":1,"movementSpeedSubpixelsPerTick":0,"collisionBody":{"offsetX":0,"offsetY":0,"width":1,"height":1},"hurtbox":{"offsetX":0,"offsetY":0,"width":1,"height":1},"attackIds":{}})").content,
           "13A2 strict enum, timeline and enemy array validation rejects malformed input");
    expect(!document("objects", R"({"id":"o","visualSetId":"v","container":{"capacity":"50"}})").content &&
               !document("objects", R"({"id":"o","visualSetId":"v","bankAccess":{"unexpected":true}})").content &&
               !document("pickups", R"({"id":"p","visualId":"v","collectionBounds":{"x":0,"y":0,"width":1,"height":1},"payload":{"kind":"health","amount":1,"itemId":"wrong"}})").content &&
               !document("npcs", R"({"id":"n","visualSetId":"v","interaction":{"x":0,"y":0,"width":1,"height":1},"interactionEnabled":1,"defaultDialogueId":"d","tags":[]})").content,
           "13A2 capability, pickup variant and strict boolean validation rejects malformed input");
    const auto malformed = [&document](std::string_view category, std::string_view entry) {
        const auto result = document(category, entry);
        return !result.content && !result.diagnostics.empty();
    };
    expect(malformed("projectiles", R"({"id":"p","visualId":"v","canonicalFacing":"down","speedPixelsPerTick":1,"lifetimeTicks":1,"hitboxWidth":1,"hitboxHeight":1,"spawnOffsets":{"down":{"x":0,"y":0},"up":{"x":0,"y":0},"left":{"x":0,"y":0}}})") &&
               malformed("attacks", R"({"id":"a","kind":"meleeHitbox","totalTicks":1,"cooldownTicks":1,"minimumRangePixels":0,"maximumRangePixels":1,"visualActionId":"v","timeline":[]})") &&
               malformed("attacks", R"({"id":"a","kind":"meleeHitbox","damage":{"amount":1,"knockbackPixels":0},"totalTicks":1,"cooldownTicks":1,"minimumRangePixels":0,"maximumRangePixels":1,"visualActionId":"v"})") &&
               malformed("enemies", R"({"id":"e","visualSetId":"v","behaviorProfileId":"b","faction":"enemy","maximumHealth":1,"movementSpeedSubpixelsPerTick":0,"collisionBody":{"offsetX":0,"offsetY":0,"width":1},"hurtbox":{"offsetX":0,"offsetY":0,"width":1,"height":1},"attackIds":[]})"),
           "13A2 required projectile, attack and enemy fields reject invalid documents");
    expect(malformed("objects", R"({"id":"o","visualSetId":"v","interactable":{"x":0,"y":0,"width":1}})") &&
               malformed("pickups", R"({"id":"p","visualId":"v","collectionBounds":{"x":0,"y":0,"width":1,"height":1},"payload":{}})") &&
               malformed("pickups", R"({"id":"p","visualId":"v","collectionBounds":{"x":0,"y":0,"width":1,"height":1},"payload":{"kind":"item","quantity":1}})") &&
               malformed("npcs", R"({"id":"n","visualSetId":"v","interaction":{"x":0,"y":0,"width":1,"height":1},"interactionEnabled":true,"defaultDialogueId":"d","tags":[1]})"),
           "13A2 malformed world object, pickup and NPC nested fields reject invalid documents");
    const auto unsupported = decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":1,"dialogues":[{"id":"not-decoded"}]})");
    const auto wrongCategoryType = decodeAuthoredContentJson(R"({"format":"dungeon-underworld-content","version":1,"attacks":{}})");
    expect(!unsupported.content && std::any_of(unsupported.diagnostics.begin(), unsupported.diagnostics.end(), [](const auto& diagnostic) {
                   return diagnostic.path == "dialogues[0].nodes";
               }) &&
               !wrongCategoryType.content && wrongCategoryType.diagnostics.size() == 1 && wrongCategoryType.diagnostics[0].path == "attacks",
           "narrative malformed and wrong-type categories fail explicitly");
    const auto hasDiagnostic = [](const auto& result, std::string_view path) {
        return !result.content && std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [path](const auto& diagnostic) {
            return diagnostic.path == path;
        });
    };
    expect(hasDiagnostic(document("attacks", R"({"id":"a","kind":"unknown","damage":{"amount":1,"knockbackPixels":0},"totalTicks":1,"cooldownTicks":1,"minimumRangePixels":0,"maximumRangePixels":1,"visualActionId":"v","timeline":[]})"), "attacks[0].kind") &&
               hasDiagnostic(document("enemies", R"({"id":"e","visualSetId":"v","behaviorProfileId":"b","faction":"unknown","maximumHealth":1,"movementSpeedSubpixelsPerTick":0,"collisionBody":{"offsetX":0,"offsetY":0,"width":1,"height":1},"hurtbox":{"offsetX":0,"offsetY":0,"width":1,"height":1},"attackIds":[]})"), "enemies[0].faction"),
           "13A2 AttackKind and Faction unknown values are rejected at their fields");
    expect(hasDiagnostic(document("projectiles", R"({"id":"p","visualId":"v","canonicalFacing":"down","speedPixelsPerTick":1,"lifetimeTicks":1,"hitboxWidth":1,"hitboxHeight":1,"spawnOffsets":{"down":{"x":0},"up":{"x":0,"y":0},"left":{"x":0,"y":0},"right":{"x":0,"y":0}}})"), "projectiles[0].spawnOffsets.down.y") &&
               hasDiagnostic(document("projectiles", R"({"id":"p","visualId":"v","canonicalFacing":"down","speedPixelsPerTick":1,"lifetimeTicks":4294967296,"hitboxWidth":1,"hitboxHeight":1,"spawnOffsets":{"down":{"x":0,"y":0},"up":{"x":0,"y":0},"left":{"x":0,"y":0},"right":{"x":0,"y":0}}})"), "projectiles[0].lifetimeTicks"),
           "13A2 projectile directional and narrow unsigned validation is strict");
    expect(hasDiagnostic(document("attacks", R"({"id":"a","kind":"meleeHitbox","damage":{"knockbackPixels":0},"totalTicks":1,"cooldownTicks":1,"minimumRangePixels":0,"maximumRangePixels":1,"visualActionId":"v","timeline":[]})"), "attacks[0].damage.amount") &&
               hasDiagnostic(document("attacks", R"({"id":"a","kind":"meleeHitbox","damage":{"amount":1},"totalTicks":1,"cooldownTicks":1,"minimumRangePixels":0,"maximumRangePixels":1,"visualActionId":"v","timeline":[]})"), "attacks[0].damage.knockbackPixels") &&
               hasDiagnostic(document("attacks", R"({"id":"a","kind":"meleeHitbox","damage":{"amount":1,"knockbackPixels":0},"totalTicks":1,"cooldownTicks":1,"minimumRangePixels":0,"maximumRangePixels":1,"visualActionId":"v","timeline":[{"tick":1}]})"), "attacks[0].timeline[0].kind") &&
               hasDiagnostic(document("attacks", R"({"id":"a","kind":"meleeHitbox","damage":{"amount":1,"knockbackPixels":0},"totalTicks":1,"cooldownTicks":1,"minimumRangePixels":0,"maximumRangePixels":1,"visualActionId":"v","meleeHitboxes":{"down":{"offsetX":0,"offsetY":0,"width":4},"up":{"offsetX":0,"offsetY":0,"width":4,"height":4},"left":{"offsetX":0,"offsetY":0,"width":4,"height":4},"right":{"offsetX":0,"offsetY":0,"width":4,"height":4}},"timeline":[]})"), "attacks[0].meleeHitboxes.down.height"),
           "13A2 DamageSpec, timeline and melee box required fields are strict");
    const auto attackIdsElement = document("enemies", R"({"id":"e","visualSetId":"v","behaviorProfileId":"b","faction":"enemy","maximumHealth":1,"movementSpeedSubpixelsPerTick":-9223372036854775808,"collisionBody":{"offsetX":0,"offsetY":0,"width":1,"height":1},"hurtbox":{"offsetX":0,"offsetY":0,"width":1,"height":1},"attackIds":["attack.valid",123]})");
    expect(hasDiagnostic(attackIdsElement, "enemies[0].attackIds[1]"), "13A2 enemy attackIds rejects non-string elements");
    expect(hasDiagnostic(document("objects", R"({"id":"o","visualSetId":"v","destructible":{"maximumHealth":1,"hurtbox":{"x":0,"y":0,"width":1,"height":1}}})"), "objects[0].destructible.destructionDurationTicks") &&
               hasDiagnostic(document("objects", R"({"id":"o","visualSetId":"v","bankAccess":1})"), "objects[0].bankAccess") &&
               hasDiagnostic(document("objects", R"({"id":"o","visualSetId":"v","interactable":{"x":0,"y":0,"width":1}})"), "objects[0].interactable.height"),
           "13A2 world object capability shapes are strict");
    const auto maxCapacity = document("objects", R"({"id":"o","visualSetId":"v","container":{"capacity":18446744073709551615}})");
    expect(maxCapacity.content.has_value(), "13A2 size_t capacity accepts UINT64_MAX on the 64-bit host");
    expect(hasDiagnostic(document("pickups", R"({"id":"p","visualId":"v","collectionBounds":{"x":0,"y":0,"width":1,"height":1},"payload":{"amount":1}})"), "pickups[0].payload.kind") &&
               hasDiagnostic(document("pickups", R"({"id":"p","visualId":"v","collectionBounds":{"x":0,"y":0,"width":1,"height":1},"payload":{"kind":"mana","amount":1}})"), "pickups[0].payload.kind") &&
               hasDiagnostic(document("pickups", R"({"id":"p","visualId":"v","collectionBounds":{"x":0,"y":0,"width":1,"height":1},"payload":{"kind":"currency","amount":18446744073709551616}})"), "pickups[0].payload.amount") &&
               hasDiagnostic(document("pickups", R"({"id":"p","visualId":"v","collectionBounds":{"x":0,"y":0,"width":1,"height":1},"payload":{"kind":"item","quantity":1}})"), "pickups[0].payload.itemId") &&
               hasDiagnostic(document("pickups", R"({"id":"p","visualId":"v","collectionBounds":{"x":0,"y":0,"width":1,"height":1},"payload":{"kind":"item","itemId":"i","quantity":4294967296}})"), "pickups[0].payload.quantity"),
           "13A2 pickup kind, uint64 and uint32 boundaries are strict");
    expect(hasDiagnostic(document("npcs", R"({"id":"n","visualSetId":"v","interaction":{"x":0,"y":0,"width":1},"interactionEnabled":true,"defaultDialogueId":"d","tags":[]})"), "npcs[0].interaction.height") &&
               hasDiagnostic(document("npcs", R"({"id":"n","visualSetId":"v","interaction":{"x":0,"y":0,"width":1,"height":1},"interactionEnabled":true,"defaultDialogueId":"d","tags":{}})"), "npcs[0].tags"),
           "13A2 NPC interaction and tags shapes are strict");
}

void testPhase13A3JsonDecoders() {
    using namespace underworld::game::content;
    namespace gameplay = underworld::game::gameplay;
    const auto document = [](std::string_view category, std::string_view entries) {
        return decodeAuthoredContentJson(std::string{"{\"format\":\"dungeon-underworld-content\",\"version\":1,\""} +
                                          std::string{category} + "\":[" + std::string{entries} + "]}");
    };
    const auto dialogue = document("dialogues", R"({"id":"dialogue.test","entryNodeId":"node.start","nodes":[{"id":"node.start","speaker":"Olá, viajante.","pages":["Poção","Você encontrou a saída?"],"nextNodeId":"node.end","choices":[{"label":"Trade","targetNodeId":"node.end","conditions":[{"kind":"flagNotSet","flagId":"flag.test"}],"actions":[{"kind":"openShop","targetId":"shop.test"}]}]},{"id":"node.end","speaker":"Merchant","pages":["Até logo."],"nextNodeId":"node.end","choices":[]}]})");
    expect(dialogue.content && dialogue.content->dialogues[0].nodes[0].choices[0].conditions[0].kind == gameplay::dialogue::DialogueConditionKind::flagNotSet &&
               dialogue.content->dialogues[0].nodes[0].choices[0].actions[0].kind == gameplay::dialogue::DialogueActionKind::openShop &&
               dialogue.content->dialogues[0].nodes[0].pages[1] == "Você encontrou a saída?",
           "13A3 dialogue decoder preserves UTF-8 graph, condition and action");
    const auto quest = document("quests", R"({"id":"quest.test","title":"A small quest","objectives":[{"id":"objective.test","kind":"deliver","targetId":"item.test","requiredCount":2,"description":"Deliver two items"}],"tags":["test","story"],"rewardGrantId":"grant.test"})");
    expect(quest.content && quest.content->quests[0].objectives[0].kind == gameplay::quests::QuestObjectiveKind::deliver &&
               quest.content->quests[0].objectives[0].requiredCount == 2 && quest.content->quests[0].rewardGrantId,
           "13A3 quest decoder preserves objective, tags and reward reference");
    const auto semantic = document("tileSemantics", R"({"id":"tile.semantic.test","tilesetId":"tileset.test","sourceIndex":7,"family":"masonry","role":"corner","topology":"innerCorner","north":"masonry","east":"floor","south":"voidEdge","west":"terminal","preferredLayer":"walls","flipXAllowed":true,"visualConfidence":"confirmed","semanticConfidence":"probable","gameplayConfidence":"unverified","variantWeight":7})");
    expect(semantic.content && semantic.content->tileSemantics[0].role == underworld::game::authoring::TileRole::corner &&
               semantic.content->tileSemantics[0].topology == underworld::game::authoring::TileTopology::innerCorner &&
               semantic.content->tileSemantics[0].east == underworld::game::authoring::EdgeProfile::floor && semantic.content->tileSemantics[0].flipXAllowed &&
               semantic.content->tileSemantics[0].variantWeight == 7,
           "13A3 tile semantic decoder preserves enums, edges, bool and variant weight");
    const auto stamp = document("stamps", R"({"id":"stamp.test","displayName":"Test Stamp","width":2,"height":3,"cells":[{"x":-1,"y":2,"tileId":"tile.semantic.test"}],"anchor":{"x":-2,"y":1},"flipXAllowed":true,"atomic":false,"confidence":"probable"})");
    expect(stamp.content && stamp.content->stamps[0].width == 2 && stamp.content->stamps[0].cells[0].x == -1 &&
               stamp.content->stamps[0].anchor.x == -2 && stamp.content->stamps[0].atomic == false &&
               stamp.content->stamps[0].confidence == underworld::game::authoring::SemanticConfidence::probable,
           "13A3 stamp decoder preserves cells, signed anchor and flags");
    expect(!document("dialogues", R"({"id":"d","entryNodeId":"n","nodes":[{"id":"n","speaker":"s","pages":[],"nextNodeId":"n","choices":[{"label":"x","targetNodeId":"n","conditions":[{"kind":"bad","flagId":"f"}],"actions":[]}]}]})").content &&
               !document("quests", R"({"id":"q","title":"q","objectives":[{"id":"o","kind":"bad","targetId":"t","requiredCount":1,"description":"d"}],"tags":[]})").content &&
               !document("tileSemantics", R"({"id":"t","tilesetId":"ts","sourceIndex":4294967296,"family":"f","role":"bad","topology":"unknown","north":"unknown","east":"unknown","south":"unknown","west":"unknown","preferredLayer":"l","flipXAllowed":false,"visualConfidence":"confirmed","semanticConfidence":"unverified","gameplayConfidence":"unverified"})").content &&
               !document("stamps", R"({"id":"s","displayName":"s","width":4294967296,"height":1,"cells":[],"anchor":{"x":0,"y":0},"flipXAllowed":false,"atomic":false,"confidence":"bad"})").content,
           "13A3 invalid enum, range and shape documents return no content");
    const auto invalidPath = [&document](std::string_view category, std::string_view entry,
                                         std::string_view path) {
        const auto result = document(category, entry);
        return !result.content && std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
            [path](const auto& diagnostic) { return diagnostic.path == path; });
    };
    expect(invalidPath("dialogues", R"({"id":"d","entryNodeId":"n","nodes":[{"id":"n","speaker":"s","pages":{},"nextNodeId":"n","choices":[]}]})", "dialogues[0].nodes[0].pages") &&
               invalidPath("dialogues", R"({"id":"d","entryNodeId":"n","nodes":[{"id":"n","speaker":"s","pages":[1],"nextNodeId":"n","choices":[]}]})", "dialogues[0].nodes[0].pages[0]") &&
               invalidPath("dialogues", R"({"id":"d","entryNodeId":"n","nodes":[{"id":"n","speaker":"s","pages":[],"nextNodeId":"n","choices":{}}]})", "dialogues[0].nodes[0].choices"),
           "13A3 dialogue nested page and choice shapes are strict");
    expect(invalidPath("dialogues", R"({"id":"d","entryNodeId":"n","nodes":[{"id":"n","speaker":"s","pages":[],"nextNodeId":"n","choices":[{"label":"x","targetNodeId":"n","conditions":[{"kind":"flagSet"}],"actions":[]}] }]})", "dialogues[0].nodes[0].choices[0].conditions[0].flagId") &&
               invalidPath("dialogues", R"({"id":"d","entryNodeId":"n","nodes":[{"id":"n","speaker":"s","pages":[],"nextNodeId":"n","choices":[{"label":"x","targetNodeId":"n","conditions":[],"actions":[{"kind":"openShop"}]}] }]})", "dialogues[0].nodes[0].choices[0].actions[0].targetId"),
           "13A3 dialogue condition and action fields are required");
    expect(invalidPath("quests", R"({"id":"q","title":"q","objectives":[{"id":"o","kind":"kill","targetId":"e","description":"d"}],"tags":[]})", "quests[0].objectives[0].requiredCount") &&
               invalidPath("quests", R"({"id":"q","title":"q","objectives":[],"tags":{}})", "quests[0].tags") &&
               invalidPath("quests", R"({"id":"q","title":"q","objectives":[],"tags":[1]})", "quests[0].tags[0]") &&
               invalidPath("quests", R"({"id":"q","title":"q","objectives":[],"tags":[],"rewardGrantId":1})", "quests[0].rewardGrantId") &&
               invalidPath("quests", R"({"id":"q","title":"q","objectives":[{"id":"o","kind":"kill","targetId":"e","requiredCount":4294967296,"description":"d"}],"tags":[]})", "quests[0].objectives[0].requiredCount"),
           "13A3 quest nested fields and uint32 range are strict");
    expect(invalidPath("tileSemantics", R"({"id":"t","tilesetId":"ts","sourceIndex":4294967296,"family":"f","role":"floor","topology":"unknown","north":"unknown","east":"unknown","south":"unknown","west":"unknown","preferredLayer":"l","flipXAllowed":true,"visualConfidence":"confirmed","semanticConfidence":"probable","gameplayConfidence":"unverified"})", "tileSemantics[0].sourceIndex") &&
               invalidPath("tileSemantics", R"({"id":"t","tilesetId":"ts","sourceIndex":0,"family":"f","role":"floor","topology":"bad","north":"unknown","east":"unknown","south":"unknown","west":"unknown","preferredLayer":"l","flipXAllowed":true,"visualConfidence":"confirmed","semanticConfidence":"probable","gameplayConfidence":"unverified"})", "tileSemantics[0].topology") &&
               invalidPath("tileSemantics", R"({"id":"t","tilesetId":"ts","sourceIndex":0,"family":"f","role":"floor","topology":"unknown","north":"bad","east":"unknown","south":"unknown","west":"unknown","preferredLayer":"l","flipXAllowed":true,"visualConfidence":"confirmed","semanticConfidence":"probable","gameplayConfidence":"unverified"})", "tileSemantics[0].north") &&
               invalidPath("tileSemantics", R"({"id":"t","tilesetId":"ts","sourceIndex":0,"family":"f","role":"floor","topology":"unknown","north":"unknown","east":"unknown","south":"unknown","west":"unknown","preferredLayer":"l","flipXAllowed":1,"visualConfidence":"confirmed","semanticConfidence":"probable","gameplayConfidence":"unverified"})", "tileSemantics[0].flipXAllowed") &&
               invalidPath("tileSemantics", R"({"id":"t","tilesetId":"ts","sourceIndex":0,"family":"f","role":"floor","topology":"unknown","north":"unknown","east":"unknown","south":"unknown","west":"unknown","preferredLayer":"l","flipXAllowed":true,"visualConfidence":"confirmed","semanticConfidence":"probable","gameplayConfidence":"unverified","variantWeight":4294967296})", "tileSemantics[0].variantWeight"),
           "13A3 tile semantic nested enums, range and bool are strict");
    expect(invalidPath("stamps", R"({"id":"s","displayName":"s","width":4294967296,"height":1,"cells":[],"anchor":{"x":0,"y":0},"flipXAllowed":false,"atomic":false,"confidence":"unverified"})", "stamps[0].width") &&
               invalidPath("stamps", R"({"id":"s","displayName":"s","width":1,"height":1,"cells":{},"anchor":{"x":0,"y":0},"flipXAllowed":false,"atomic":false,"confidence":"unverified"})", "stamps[0].cells") &&
               invalidPath("stamps", R"({"id":"s","displayName":"s","width":1,"height":1,"cells":[{"x":0,"tileId":"t"}],"anchor":{"x":0,"y":0},"flipXAllowed":false,"atomic":false,"confidence":"unverified"})", "stamps[0].cells[0].y") &&
               invalidPath("stamps", R"({"id":"s","displayName":"s","width":1,"height":1,"cells":[],"anchor":{"x":0},"flipXAllowed":false,"atomic":false,"confidence":"unverified"})", "stamps[0].anchor.y") &&
               invalidPath("stamps", R"({"id":"s","displayName":"s","width":1,"height":1,"cells":[],"anchor":{"x":0,"y":0},"flipXAllowed":1,"atomic":false,"confidence":"unverified"})", "stamps[0].flipXAllowed") &&
               invalidPath("stamps", R"({"id":"s","displayName":"s","width":1,"height":1,"cells":[],"anchor":{"x":0,"y":0},"flipXAllowed":false,"atomic":1,"confidence":"unverified"})", "stamps[0].atomic"),
           "13A3 stamp dimensions, nested shapes and flags are strict");
    const auto builtin = makeBuiltinAuthoredContent();
    const auto json1 = encodeAuthoredContentJson(builtin);
    const auto decoded = decodeAuthoredContentJson(json1);
    expect(decoded.content.has_value() && decoded.diagnostics.empty(), "full builtin JSON decodes through the public decoder");
    if (decoded.content) {
        const auto json2 = encodeAuthoredContentJson(*decoded.content);
        expect(json1 == json2, "full builtin JSON roundtrip is byte-identical");
        const auto validation = ContentValidator{}.validate(*decoded.content);
        const auto directCompiled = compileContent(builtin);
        const auto decodedCompiled = compileContent(*decoded.content);
        expect(!validation.hasErrors() && directCompiled.registry.has_value() && directCompiled.report.valid() &&
                   decodedCompiled.registry.has_value() && decodedCompiled.report.valid(),
               "decoded builtin validates and both source paths compile");
        if (directCompiled.registry && decodedCompiled.registry) {
            const auto& a = *directCompiled.registry;
            const auto& b = *decodedCompiled.registry;
            const auto same = [](bool condition, std::string_view message) { expect(condition, message); };
            const auto* at = a.tilesets().find({"tileset.dungeon"}); const auto* bt = b.tilesets().find({"tileset.dungeon"});
            same(at && bt && at->displayName == bt->displayName && at->tileSize == bt->tileSize && at->columns == bt->columns && at->rows == bt->rows, "registry equivalence preserves tileset");
            const auto* ap = a.projectiles().find({"projectile.player.arrow"}); const auto* bp = b.projectiles().find({"projectile.player.arrow"});
            same(ap && bp && ap->canonicalFacing == bp->canonicalFacing && ap->lifetimeTicks == bp->lifetimeTicks && ap->spawnOffsets.values == bp->spawnOffsets.values, "registry equivalence preserves projectile");
            const auto* aa = a.attacks().find({"attack.player.sword"}); const auto* ba = b.attacks().find({"attack.player.sword"});
            same(aa && ba && aa->kind == ba->kind && aa->damage.amount == ba->damage.amount && aa->totalTicks == ba->totalTicks && aa->timeline == ba->timeline, "registry equivalence preserves attack");
            const auto* ab = a.behaviors().find({"behavior.soldier.melee"}); const auto* bb = b.behaviors().find({"behavior.soldier.melee"});
            same(ab && bb && ab->detectionRangePixels == bb->detectionRangePixels && ab->idleDurationTicks == bb->idleDurationTicks, "registry equivalence preserves behavior");
            const auto* ae = a.enemies().find({"enemy.evil_soldier"}); const auto* be = b.enemies().find({"enemy.evil_soldier"});
            same(ae && be && ae->maximumHealth == be->maximumHealth && ae->movementSpeedSubpixelsPerTick == be->movementSpeedSubpixelsPerTick && ae->attackIds == be->attackIds, "registry equivalence preserves enemy");
            const auto* ai = a.items().find({"item.training_armor"}); const auto* bi = b.items().find({"item.training_armor"});
            same(ai && bi && ai->category == bi->category && ai->stackLimit == bi->stackLimit && ai->equipment && bi->equipment && ai->equipment->modifiers.maximumHealthBonus == bi->equipment->modifiers.maximumHealthBonus, "registry equivalence preserves item");
            const auto* ao = a.objects().find({"object.bank_access"}); const auto* bo = b.objects().find({"object.bank_access"});
            same(ao && bo && ao->visualSetId == bo->visualSetId && ao->bankAccess.has_value() == bo->bankAccess.has_value(), "registry equivalence preserves world object");
            const auto* ax = a.pickup({"pickup.life_potion"}); const auto* bx = b.pickup({"pickup.life_potion"});
            same(ax && bx && ax->collectionBounds == bx->collectionBounds && ax->payload.index() == bx->payload.index() &&
                     std::get<gameplay::ItemPickup>(ax->payload).itemId == std::get<gameplay::ItemPickup>(bx->payload).itemId &&
                     std::get<gameplay::ItemPickup>(ax->payload).quantity == std::get<gameplay::ItemPickup>(bx->payload).quantity,
                 "registry equivalence preserves pickup");
            const auto* av = a.npcVisuals().find({"visual.npc.merchant"}); const auto* bv = b.npcVisuals().find({"visual.npc.merchant"});
            same(av && bv && av->markerColor.r == bv->markerColor.r && av->markerColor.g == bv->markerColor.g && av->markerColor.b == bv->markerColor.b && av->markerColor.a == bv->markerColor.a, "registry equivalence preserves NPC visual");
            const auto* avi = a.visualImages().find({"image.enemy.soldier.idle"}); const auto* bvi = b.visualImages().find({"image.enemy.soldier.idle"});
            same(avi && bvi && *avi == *bvi, "registry equivalence preserves visual image");
            const auto* ass = a.staticSprites().find({"visual.pickup.heart"}); const auto* bss = b.staticSprites().find({"visual.pickup.heart"});
            same(ass && bss && *ass == *bss, "registry equivalence preserves static sprite");
            const auto* aan = a.animations().find({"anim.enemy.soldier.idle.down"}); const auto* ban = b.animations().find({"anim.enemy.soldier.idle.down"});
            same(aan && ban && *aan == *ban, "registry equivalence preserves animation");
            const auto* aev = a.enemyVisuals().find({"visual.enemy.evil_soldier"}); const auto* bev = b.enemyVisuals().find({"visual.enemy.evil_soldier"});
            same(aev && bev && *aev == *bev, "registry equivalence preserves flexible enemy visual profile");
            const auto* aov = a.objectVisuals().find({"visual.object.chest"}); const auto* bov = b.objectVisuals().find({"visual.object.chest"});
            same(aov && bov && *aov == *bov, "registry equivalence preserves world object visual profile");
            const auto* an = a.npcs().find({"npc.merchant"}); const auto* bn = b.npcs().find({"npc.merchant"});
            same(an && bn && an->interaction.bounds == bn->interaction.bounds && an->defaultDialogueId == bn->defaultDialogueId && an->tags == bn->tags, "registry equivalence preserves NPC");
            const auto* ad = a.dialogues().find({"dialogue.merchant.greeting"}); const auto* bd = b.dialogues().find({"dialogue.merchant.greeting"});
            same(ad && bd && ad->entryNodeId == bd->entryNodeId && ad->nodes.size() == bd->nodes.size() &&
                     ad->nodes[0].pages == bd->nodes[0].pages && ad->nodes[0].choices.size() == bd->nodes[0].choices.size() &&
                     ad->nodes[0].choices[0].label == bd->nodes[0].choices[0].label &&
                     ad->nodes[0].choices[0].targetNodeId == bd->nodes[0].choices[0].targetNodeId &&
                     ad->nodes[0].choices[0].actions.size() == bd->nodes[0].choices[0].actions.size() &&
                     ad->nodes[0].choices[0].actions[0].kind == bd->nodes[0].choices[0].actions[0].kind &&
                     ad->nodes[0].choices[0].actions[0].targetId == bd->nodes[0].choices[0].actions[0].targetId,
                 "registry equivalence preserves dialogue");
            const auto* aq = a.quests().find({"quest.scholar.path"}); const auto* bq = b.quests().find({"quest.scholar.path"});
            same(aq && bq && aq->objectives == bq->objectives && aq->tags == bq->tags && aq->rewardGrantId == bq->rewardGrantId, "registry equivalence preserves quest");
            const auto* apr = a.progressions().find({"progression.player.default"}); const auto* bpr = b.progressions().find({"progression.player.default"});
            same(apr && bpr && apr->baseStats.maximumHealth == bpr->baseStats.maximumHealth && apr->cumulativeExperienceThresholds == bpr->cumulativeExperienceThresholds, "registry equivalence preserves progression");
            const auto* arp = a.rewardProfiles().find({"reward.enemy.evil_soldier"}); const auto* brp = b.rewardProfiles().find({"reward.enemy.evil_soldier"});
            same(arp && brp && arp->experience == brp->experience && arp->loot.size() == brp->loot.size() && arp->loot[0].chanceBasisPoints == brp->loot[0].chanceBasisPoints, "registry equivalence preserves reward profile");
            const auto* arg = a.rewardGrants().find({"reward.quest.scholar.path"}); const auto* brg = b.rewardGrants().find({"reward.quest.scholar.path"});
            same(arg && brg && arg->experience == brg->experience && arg->gold == brg->gold && arg->items.size() == brg->items.size() && arg->items[0].quantity == brg->items[0].quantity, "registry equivalence preserves reward grant");
            const auto* ash = a.shops().find({"shop.development.general"}); const auto* bsh = b.shops().find({"shop.development.general"});
            same(ash && bsh && ash->offers.size() == bsh->offers.size() && ash->offers[0].playerBuyPrice == bsh->offers[0].playerBuyPrice && ash->offers[0].playerSellPrice == bsh->offers[0].playerSellPrice, "registry equivalence preserves shop");
            const auto descriptor = [](const auto& registry) { return std::find_if(registry.authoringDescriptors().begin(), registry.authoringDescriptors().end(), [](const auto& value) { return value.definitionId.value() == "npc.merchant"; }); };
            const auto da = descriptor(a); const auto db = descriptor(b);
            same(da != a.authoringDescriptors().end() && db != b.authoringDescriptors().end() && da->displayName == db->displayName && da->tags == db->tags, "registry equivalence preserves authoring descriptor");
            const auto* ats = a.authoringSemantics().findTile({"tile.dungeon.masonry.16"}); const auto* bts = b.authoringSemantics().findTile({"tile.dungeon.masonry.16"});
            same(ats && bts && ats->sourceIndex == bts->sourceIndex && ats->role == bts->role && ats->topology == bts->topology && ats->north == bts->north && ats->variantWeight == bts->variantWeight, "registry equivalence preserves tile semantic and variant weight");
            const auto* ast = a.authoringSemantics().findStamp({"stamp.dungeon.masonry_frame_3x3"}); const auto* bst = b.authoringSemantics().findStamp({"stamp.dungeon.masonry_frame_3x3"});
            same(ast && bst && ast->width == bst->width && ast->height == bst->height && ast->cells.size() == bst->cells.size() && ast->anchor == bst->anchor && ast->confidence == bst->confidence, "registry equivalence preserves stamp");
        }
    }
}

void testContentJsonAtomicWriter() {
    namespace content = underworld::game::content;
    const auto root = std::filesystem::temp_directory_path() / "undre content atomic writer";
    std::error_code fsError;
    std::filesystem::remove_all(root, fsError);
    std::filesystem::create_directories(root, fsError);
    const auto path = root / "content file.json";
    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    const content::AuthoredContentPack original = content::makeBuiltinAuthoredContent();
    content::AuthoredContentPack replacement = original;
    replacement.tilesets.front().displayName = "Atomic replacement";
    std::string error = "previous error";
    {
        std::ofstream staleBackup(backup, std::ios::binary);
        staleBackup << "stale backup";
    }
    expect(content::writeAuthoredContentJsonFile(path, original, error) && error.empty() &&
               std::filesystem::exists(path) && !std::filesystem::exists(temporary) &&
               !std::filesystem::exists(backup),
           "Content JSON atomic writer creates a new native-path file and cleans temporary artifacts");
    const auto firstRead = content::readAuthoredContentJsonFile(path);
    expect(firstRead && firstRead.content &&
               content::encodeAuthoredContentJson(*firstRead.content) ==
                   content::encodeAuthoredContentJson(original),
           "Content JSON atomic writer supports a native-path save/load roundtrip");
    error = "previous error";
    expect(content::writeAuthoredContentJsonFile(path, replacement, error) && error.empty() &&
               std::filesystem::exists(path) && !std::filesystem::exists(temporary) &&
               !std::filesystem::exists(backup),
           "Content JSON atomic writer replaces an existing file and removes its backup after success");
    const auto secondRead = content::readAuthoredContentJsonFile(path);
    expect(secondRead && secondRead.content && secondRead.content->tilesets.front().displayName ==
               "Atomic replacement",
           "Content JSON atomic writer leaves the replacement content readable");
    std::filesystem::remove_all(root, fsError);
}

void testPhase13B1ContentWorkspace() {
    namespace content = underworld::game::content;
    namespace gameplay = underworld::game::gameplay;
    const auto builtin = content::makeBuiltinAuthoredContent();
    const auto root = std::filesystem::temp_directory_path() / "undre_content_workspace_13b1";
    std::filesystem::create_directories(root);
    const auto write = [](const std::filesystem::path& path, const content::AuthoredContentPack& pack) {
        std::ofstream file(path, std::ios::binary);
        file << content::encodeAuthoredContentJson(pack);
        return file.good();
    };
    content::AuthoredContentPack combat;
    combat.tilesets = builtin.tilesets;
    combat.projectiles = builtin.projectiles;
    combat.attacks = builtin.attacks;
    content::AuthoredContentPack creatures;
    creatures.behaviors = builtin.behaviors;
    creatures.enemies = builtin.enemies;
    creatures.rewardProfiles = builtin.rewardProfiles;
    creatures.rewardGrants = builtin.rewardGrants;
    content::AuthoredContentPack items;
    items.items = builtin.items;
    items.objects = builtin.objects;
    items.pickups = builtin.pickups;
    content::AuthoredContentPack narrative;
    narrative.npcVisuals = builtin.npcVisuals;
    narrative.npcs = builtin.npcs;
    narrative.dialogues = builtin.dialogues;
    narrative.quests = builtin.quests;
    narrative.playerProgressions = builtin.playerProgressions;
    narrative.shops = builtin.shops;
    narrative.authoringDescriptors = builtin.authoringDescriptors;
    narrative.tileSemantics = builtin.tileSemantics;
    narrative.stamps = builtin.stamps;
    const auto combatPath = root / "z_combat.data";
    const auto creaturesPath = root / "a_creatures.json";
    const auto itemsPath = root / "m_items.json";
    const auto narrativePath = root / "n_narrative.json";
    expect(write(combatPath, combat) && write(creaturesPath, creatures) && write(itemsPath, items) && write(narrativePath, narrative),
           "13B1 writes explicit multi-file workspace fixtures");
    const std::vector<std::filesystem::path> forward{combatPath, creaturesPath, itemsPath, narrativePath};
    const std::vector<std::filesystem::path> reverse{narrativePath, itemsPath, combatPath, creaturesPath};
    const auto first = content::loadContentWorkspaceFiles(forward);
    const auto second = content::loadContentWorkspaceFiles(reverse);
    expect(first.workspace.has_value() && first.diagnostics.empty() && second.workspace.has_value() && second.diagnostics.empty(),
           "13B1 merges all explicit content files and compiles cross-file references");
    if (first.workspace && second.workspace) {
        expect(content::encodeAuthoredContentJson(first.workspace->authored) == content::encodeAuthoredContentJson(second.workspace->authored),
               "13B1 workspace result is independent of caller file order");
        expect(first.workspace->registry.enemies().find({"enemy.evil_soldier"}) != nullptr &&
                   first.workspace->registry.attacks().find({"attack.soldier.sword"}) != nullptr &&
                   first.workspace->registry.dialogues().find({"dialogue.merchant.greeting"}) != nullptr &&
                   first.workspace->registry.authoringSemantics().findStamp({"stamp.dungeon.masonry_frame_3x3"}) != nullptr,
               "13B1 merged registry retains representative cross-file definitions");
        const auto* origin = first.workspace->sources.find("enemies", {"enemy.evil_soldier"});
        expect(origin && origin->sourcePath == creaturesPath.lexically_normal() && origin->line > 0 &&
                   origin->column > 0 && origin->jsonPath == "enemies[0]",
               "13B1 provenance retains source path, location and JSON path");
        expect(first.workspace->sources.find("npcVisuals", {"visual.npc.merchant"}) != nullptr,
               "13B1 provenance indexes NPC visual definitions");
    }

    content::AuthoredContentPack duplicateItem;
    duplicateItem.items.push_back(builtin.items.front());
    const auto duplicateA = root / "a_duplicate.json";
    const auto duplicateB = root / "b_duplicate.json";
    expect(write(duplicateA, duplicateItem) && write(duplicateB, duplicateItem),
           "13B1 writes duplicate definition fixtures");
    const auto duplicate = content::loadContentWorkspaceFiles(std::array<std::filesystem::path, 2>{duplicateB, duplicateA});
    expect(!duplicate.workspace && std::any_of(duplicate.diagnostics.begin(), duplicate.diagnostics.end(),
               [](const auto& value) { return value.code == "duplicate_definition" && value.relatedSourcePath.filename() == "a_duplicate.json"; }),
           "13B1 rejects duplicate definitions with both source locations");
    content::AuthoredContentPack duplicateVisual;
    duplicateVisual.npcVisuals.push_back(builtin.npcVisuals.back());
    const auto duplicateVisualA = root / "a_visual.json";
    const auto duplicateVisualB = root / "b_visual.json";
    write(duplicateVisualA, duplicateVisual);
    write(duplicateVisualB, duplicateVisual);
    const auto visualDuplicate = content::loadContentWorkspaceFiles(std::array<std::filesystem::path, 2>{duplicateVisualA, duplicateVisualB});
    expect(!visualDuplicate.workspace && std::any_of(visualDuplicate.diagnostics.begin(), visualDuplicate.diagnostics.end(),
               [](const auto& value) { return value.code == "duplicate_definition" && value.category == "npcVisuals"; }),
           "13B1 rejects duplicate NPC visual definitions");
    const auto duplicatePath = content::loadContentWorkspaceFiles(std::array<std::filesystem::path, 2>{root / "content" / "items.json", root / "content" / "./items.json"});
    expect(!duplicatePath.workspace && std::any_of(duplicatePath.diagnostics.begin(), duplicatePath.diagnostics.end(),
               [](const auto& value) { return value.code == "duplicate_source_file"; }),
           "13B1 rejects duplicate normalized source paths");

    const auto malformedPath = root / "malformed.json";
    { std::ofstream file(malformedPath); file << "{\n  \"format\": \"dungeon-underworld-content\",\n  \"version\": 1,\n  \"items\": [\n    {\"id\": \"broken\"}\n"; }
    const auto malformed = content::loadContentWorkspaceFiles(std::array<std::filesystem::path, 1>{malformedPath});
    expect(!malformed.workspace && std::any_of(malformed.diagnostics.begin(), malformed.diagnostics.end(),
               [&](const auto& value) { return value.stage == content::ContentWorkspaceDiagnosticStage::decode && value.sourcePath == malformedPath; }),
           "13B1 preserves malformed-file decode provenance");
    const auto missingPath = root / "missing.json";
    const auto missing = content::loadContentWorkspaceFiles(std::array<std::filesystem::path, 1>{missingPath});
    expect(!missing.workspace && missing.diagnostics.size() == 1 && missing.diagnostics[0].stage == content::ContentWorkspaceDiagnosticStage::io &&
               missing.diagnostics[0].sourcePath == missingPath,
           "13B1 reports missing files as structured IO diagnostics");
    const auto empty = content::loadContentWorkspaceFiles(std::span<const std::filesystem::path>{});
    expect(!empty.workspace && !empty.diagnostics.empty() && empty.diagnostics[0].code == "empty_workspace",
           "13B1 rejects an empty explicit workspace");

    content::AuthoredContentPack npcOnly;
    npcOnly.npcs.push_back(builtin.npcs.back());
    npcOnly.dialogues.push_back(builtin.dialogues.back());
    const auto npcPath = root / "npc.json";
    write(npcPath, npcOnly);
    const auto npcResult = content::loadContentWorkspaceFiles(std::array<std::filesystem::path, 1>{npcPath});
    expect(!npcResult.workspace && std::any_of(npcResult.diagnostics.begin(), npcResult.diagnostics.end(),
               [](const auto& value) { return value.code == "unknown_reference" && value.definitionId.value() == "npc.merchant" && value.jsonPath == "npcs[0].visualSetId"; }),
           "13B1 maps NPC visual reference validation to source provenance");
    content::AuthoredContentPack visualOnly;
    visualOnly.npcVisuals.push_back(builtin.npcVisuals.back());
    const auto visualPath = root / "visual.json";
    write(visualPath, visualOnly);
    const auto visualResult = content::loadContentWorkspaceFiles(std::array<std::filesystem::path, 1>{visualPath});
    expect(visualResult.workspace.has_value() && visualResult.workspace->sources.find("npcVisuals", {"visual.npc.merchant"}) != nullptr,
           "13B1 accepts standalone NPC visual authored content");

    const auto discoveredRoot = root / "discovered";
    std::filesystem::create_directories(discoveredRoot / "z");
    std::filesystem::create_directories(discoveredRoot / "a");
    write(discoveredRoot / "z" / "enemy.json", creatures);
    write(discoveredRoot / "a" / "attack.json", combat);
    write(discoveredRoot / "items.json", items);
    { std::ofstream ignored(discoveredRoot / "ignored.txt"); ignored << "not JSON"; }
    const auto discovered = content::discoverContentWorkspaceFiles(discoveredRoot);
    expect(discovered.files && discovered.files->size() == 3 &&
               (*discovered.files)[0].generic_string() < (*discovered.files)[1].generic_string() &&
               (*discovered.files)[1].generic_string() < (*discovered.files)[2].generic_string(),
           "13B2 discovers only recursive JSON files in deterministic order");
    const auto directoryLoaded = content::loadContentWorkspaceDirectory(discoveredRoot);
    expect(directoryLoaded.workspace && directoryLoaded.sourceFileCount == 3,
           "13B2 directory loader reuses explicit multi-file merge");
    const auto missingRoot = content::discoverContentWorkspaceFiles(root / "missing-root");
    expect(!missingRoot.files && !missingRoot.diagnostics.empty() &&
               missingRoot.diagnostics[0].code == "workspace_root_missing",
           "13B2 reports a missing workspace root");
    const auto fileRoot = content::discoverContentWorkspaceFiles(itemsPath);
    expect(!fileRoot.files && !fileRoot.diagnostics.empty() &&
               fileRoot.diagnostics[0].code == "workspace_root_not_directory",
           "13B2 rejects a file as workspace root");
    const auto emptyRoot = root / "empty";
    std::filesystem::create_directories(emptyRoot);
    const auto emptyDirectory = content::discoverContentWorkspaceFiles(emptyRoot);
    expect(!emptyDirectory.files && !emptyDirectory.diagnostics.empty() &&
               emptyDirectory.diagnostics[0].code == "empty_workspace",
           "13B2 rejects directories without JSON sources");
    const auto builtinSource = content::loadContentSource({content::ContentSourceKind::builtin, {}});
    expect(builtinSource && builtinSource.content->sourceKind == content::ContentSourceKind::builtin,
           "13B3 loads builtin content through the shared source bootstrap");
    auto changed = builtin;
    changed.items.front().stackLimit += 1;
    const auto changedRoot = root / "changed";
    std::filesystem::create_directories(changedRoot);
    write(changedRoot / "changed.json", changed);
    const auto externalSource = content::loadContentSource({content::ContentSourceKind::workspaceDirectory, changedRoot});
    expect(externalSource && externalSource.content->registry.items().require(changed.items.front().id).stackLimit ==
               changed.items.front().stackLimit,
           "13B3 external source changes reach the compiled registry without builtin fallback");
    const auto mixedVersionRoot = root / "mixed-versions";
    std::filesystem::create_directories(mixedVersionRoot);
    { std::ofstream legacy(mixedVersionRoot / "legacy-v1.json");
      legacy << R"({"format":"dungeon-underworld-content","version":1})"; }
    content::AuthoredContentPack v2Door;
    content::AuthoredWorldObject door;
    door.id = {"object.workspace.door"};
    door.visualSetId = {"visual.workspace.door"};
    door.door = gameplay::ObjectDoorDefinition{gameplay::DoorState::closed, {0, 0, 16, 16}};
    v2Door.objects.push_back(door);
    write(mixedVersionRoot / "door-v2.json", v2Door);
    const auto mixedVersions = content::loadContentWorkspaceDirectory(mixedVersionRoot);
    expect(mixedVersions.workspace && mixedVersions.workspace->registry.objects().find(
               {"object.workspace.door"}) != nullptr,
           "content workspace accepts v1 and v2 files in one deterministic merge");
    auto runtimeIncompatible = builtin;
    runtimeIncompatible.attacks.erase(std::remove_if(runtimeIncompatible.attacks.begin(),
                                                     runtimeIncompatible.attacks.end(),
        [](const auto& value) { return value.id.value() == "attack.player.sword"; }),
        runtimeIncompatible.attacks.end());
    const auto incompatibleCompiled = content::compileContent(runtimeIncompatible);
    expect(incompatibleCompiled.registry.has_value() &&
               !content::validateCurrentRuntimeContentRequirements(
                    *incompatibleCompiled.registry).empty(),
           "runtime-incompatible registry is rejected by explicit bootstrap requirements");
    const content::ContentWorkspaceDiagnostic formatterDiagnostic{
        content::ContentWorkspaceDiagnosticStage::validation, "content/enemies.json", {}, 14, 9,
        "enemies[2].behaviorProfileId", "unknown_reference", "enemies", {"enemy.test"},
        "behavior profile does not exist"};
    const auto formatted = content::formatContentWorkspaceDiagnostic(formatterDiagnostic);
    expect(formatted.find("[validation/unknown_reference]") != std::string::npos &&
               formatted.find("enemies[2].behaviorProfileId") != std::string::npos,
           "workspace diagnostics format stage and JSON path deterministically");
    const auto invalidSource = content::loadContentSource({content::ContentSourceKind::workspaceDirectory, root / "missing-root"});
    expect(!invalidSource && !invalidSource.content,
           "13B3 invalid explicit workspace does not fall back to builtin");
    std::filesystem::remove_all(root);
}

void testPhase14AuthoredMapFoundation() {
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;
    maps::AuthoredMapSource source;
    source.geometry.id = simulation::MapId{"map.authored.test"};
    source.geometry.width = 2;
    source.geometry.height = 2;
    source.geometry.tileSize = 16;
    source.geometry.layers.push_back({"ground", true, std::vector<std::optional<std::uint32_t>>(4)});
    source.geometry.collision.assign(4, 0);
    source.geometry.tileReferences.push_back({simulation::DefinitionId{"tileset.test"}, 7,
                                               underworld::world::TileFlags::none});
    source.geometry.layers.front().cells[1] = 0;
    source.geometry.collision[1] = 1;
    source.geometry.collisionBindings.push_back({0, 1, 0,
        source.geometry.tileReferences.front()});
    source.geometry.playerSpawns.push_back({simulation::SpawnId{"entry.start"}, {8, 8},
                                       underworld::game::gameplay::FacingDirection::down});
    source.geometry.enemies.push_back({{42}, simulation::DefinitionId{"enemy.test"}, {8, 8},
                                       underworld::game::gameplay::FacingDirection::down});
    source.regions.push_back({simulation::DefinitionId{"region.test"}, {0, 0, 16, 16}});
    source.encounters.push_back({simulation::DefinitionId{"encounter.test"}, {{42}}, std::nullopt});

    const auto json = maps::encodeAuthoredMapJson(source);
    const auto decoded = maps::decodeAuthoredMapJson(json);
    expect(decoded.source.has_value() && decoded.diagnostics.empty(),
           "authored map JSON decodes through the strict parser");
    expect(decoded.source && maps::encodeAuthoredMapJson(*decoded.source) == json,
           "authored map JSON roundtrip is deterministic");
    expect(decoded.source && decoded.source->geometry.collisionBindings.size() == 1 &&
               decoded.source->geometry.collisionBindings.front().layer == 0 &&
               decoded.source->geometry.collisionBindings.front().x == 1 &&
               decoded.source->geometry.collisionBindings.front().tile.sourceIndex == 7,
           "authored collision binding roundtrips its layer, cell and tile reference");
    auto invalidBinding = source;
    invalidBinding.geometry.layers.front().cells[1] = std::nullopt;
    expect(!maps::decodeAuthoredMapJson(maps::encodeAuthoredMapJson(invalidBinding)).source,
           "authored collision binding cannot point at an empty layer cell");

    const auto compiled = maps::mapDataFromAuthored(source);
    const auto dmap = maps::deserializeDmap(maps::serializeDmap(compiled));
    expect(dmap && dmap.data.regions == source.regions,
           "DMAP 1.2 preserves authored regions");

    simulation::EventBuffer events;
    maps::RegionTracker tracker;
    tracker.update(source.geometry.id, source.regions, {8, 8}, events);
    expect(events.events().size() == 1 &&
               std::holds_alternative<simulation::RegionEntered>(events.events().front()),
           "region tracker emits entry on first tick inside a region");
    events.clear();
    tracker.update(source.geometry.id, source.regions, {8, 8}, events);
    expect(events.events().empty(), "region tracker does not repeat entry while staying inside");
    tracker.update(source.geometry.id, source.regions, {24, 8}, events);
    expect(events.events().size() == 1 &&
               std::holds_alternative<simulation::RegionExited>(events.events().front()),
           "region tracker emits one exit after leaving a region");
    maps::RegionTracker orderedTracker;
    const std::vector<maps::MapRegionDefinition> orderedRegions{
        {{"region.second"}, {0, 0, 16, 16}},
        {{"region.first"}, {0, 0, 16, 16}}};
    events.clear();
    orderedTracker.update(source.geometry.id, orderedRegions, {8, 8}, events);
    expect(events.events().size() == 2 &&
               std::get<simulation::RegionEntered>(events.events().front()).regionId ==
                   simulation::DefinitionId{"region.second"} &&
               std::get<simulation::RegionEntered>(events.events().back()).regionId ==
                   simulation::DefinitionId{"region.first"},
           "region tracker preserves authored order for deterministic multi-region events");
    events.clear();
    orderedTracker.update(source.geometry.id, orderedRegions, {24, 8}, events);
    expect(events.events().size() == 2 &&
               std::get<simulation::RegionExited>(events.events().front()).regionId ==
                   simulation::DefinitionId{"region.second"} &&
               std::get<simulation::RegionExited>(events.events().back()).regionId ==
                   simulation::DefinitionId{"region.first"},
           "region tracker preserves authored order for deterministic exits");
}

void testPhase14WorldClosure() {
    namespace content = underworld::game::content;
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;
    namespace editor = underworld::editor;
    namespace save = underworld::game::save;
    const auto compiledContent = content::compileBuiltinContentOrThrow();

    auto map = makeSyntheticMap("map.phase14.codec", "map.phase14.codec");
    map.npcs.push_back({{6}, gameplay::npcs::guardNpcId(), {24, 24}, gameplay::FacingDirection::up});
    map.regions.push_back({{"region.codec"}, {0, 0, 32, 32}});
    map.encounters.push_back({{"encounter.codec"}, {{1}},
                              simulation::DefinitionId{"reward.quest.scholar.path"}});
    map.worldRules.push_back({{"rule.enter"},
                              {maps::WorldTriggerKind::mapEntered, {}, {}}, {},
                              {{maps::WorldActionKind::setFlag, {"flag.codec"}, {}}}, true});
    auto authored = maps::authoredMapFromMapData(map);
    authored.placementOverrides.push_back({{1}, "enemy.detection_range",
        {maps::AuthoredPropertyValueKind::integer, false, 777, {}, {}, {}}});
    const auto json = maps::encodeAuthoredMapJson(authored);
    const auto decoded = maps::decodeAuthoredMapJson(json);
    expect(decoded.source && decoded.diagnostics.empty() &&
               maps::encodeAuthoredMapJson(*decoded.source) == json &&
               decoded.source->geometry.npcs.size() == 1 &&
               decoded.source->geometry.objects.size() == map.objects.size() &&
               decoded.source->geometry.pickups.size() == map.pickups.size() &&
               decoded.source->geometry.links.size() == map.links.size() &&
               decoded.source->encounters.front().rewardGrantId ==
                   simulation::DefinitionId{"reward.quest.scholar.path"} &&
               decoded.source->placementOverrides == authored.placementOverrides,
           "UMAP roundtrip preserves geometry, NPCs, objects, pickups, links, encounters and overrides");

    auto unknownOverride = authored;
    unknownOverride.placementOverrides.front().instanceId = {999};
    const auto unknownOverrideResult = maps::decodeAuthoredMapJson(
        maps::encodeAuthoredMapJson(unknownOverride));
    auto duplicateOverride = authored;
    duplicateOverride.placementOverrides.push_back(authored.placementOverrides.front());
    const auto duplicateOverrideResult = maps::decodeAuthoredMapJson(
        maps::encodeAuthoredMapJson(duplicateOverride));
    expect(!unknownOverrideResult.source &&
               std::any_of(unknownOverrideResult.diagnostics.begin(),
                           unknownOverrideResult.diagnostics.end(), [](const auto& diagnostic) {
                               return diagnostic.code == "invalid_override" &&
                                      diagnostic.path == "placementOverrides[0].instanceId";
                           }) &&
               !duplicateOverrideResult.source &&
               std::any_of(duplicateOverrideResult.diagnostics.begin(),
                           duplicateOverrideResult.diagnostics.end(), [](const auto& diagnostic) {
                               return diagnostic.code == "invalid_override" &&
                                      diagnostic.path == "placementOverrides[1]";
                           }),
           "UMAP validation rejects unknown and duplicate placement overrides with paths");

    const auto compiledMap = maps::compileAuthoredMap(*decoded.source, compiledContent);
    const bool objectsPreserved = compiledMap.map &&
        compiledMap.map->objects.size() == map.objects.size() &&
        std::equal(compiledMap.map->objects.begin(), compiledMap.map->objects.end(),
                   map.objects.begin(), [](const auto& left, const auto& right) {
                       return left.id == right.id && left.definitionId == right.definitionId &&
                              left.position == right.position &&
                              left.initialContents.size() == right.initialContents.size() &&
                              std::equal(left.initialContents.begin(), left.initialContents.end(),
                                         right.initialContents.begin(), [](const auto& leftStack,
                                                                          const auto& rightStack) {
                                  return leftStack.itemId == rightStack.itemId &&
                                         leftStack.quantity == rightStack.quantity;
                              });
                   });
    expect(compiledMap.map && compiledMap.map->id == map.id &&
               maps::semanticallyEqual(*compiledMap.map, map) &&
               compiledMap.map->npcs == map.npcs &&
               objectsPreserved &&
               compiledMap.map->pickups.size() == map.pickups.size() &&
               compiledMap.map->links == map.links &&
               compiledMap.map->regions == map.regions &&
               compiledMap.map->worldRules == map.worldRules,
           "MapCompiler creates a fresh MapData from AuthoredMapSource and resolves authored references");
    const auto mapCatalogs = game::mapValidationCatalogs(compiledContent);
    auto duplicateRegion = map;
    duplicateRegion.regions.push_back(map.regions.front());
    auto invalidRegionBounds = map;
    invalidRegionBounds.regions.front().bounds.x = -1;
    auto duplicateRule = map;
    duplicateRule.worldRules.push_back(map.worldRules.front());
    auto unknownRegionRule = map;
    unknownRegionRule.worldRules.front().trigger.definitionTarget = {"region.missing"};
    auto unknownEncounterRule = map;
    unknownEncounterRule.worldRules.front().trigger.kind = maps::WorldTriggerKind::encounterStarted;
    unknownEncounterRule.worldRules.front().trigger.definitionTarget = {"encounter.missing"};
    auto unknownDoorAction = map;
    unknownDoorAction.worldRules.front().actions.front() =
        {maps::WorldActionKind::setDoorState, {}, {999}, gameplay::DoorState::open};
    auto nonDoorAction = map;
    nonDoorAction.worldRules.front().actions.front() =
        {maps::WorldActionKind::setDoorState, {}, {3}, gameplay::DoorState::open};
    auto duplicateEncounter = map;
    duplicateEncounter.encounters.push_back(map.encounters.front());
    auto unknownParticipant = map;
    unknownParticipant.encounters.front().participants = {{999}};
    auto duplicateParticipant = map;
    duplicateParticipant.encounters.front().participants = {{1}, {1}};
    auto sharedParticipant = map;
    sharedParticipant.encounters.push_back({{"encounter.other"}, {{1}}, std::nullopt});
    auto unknownReward = map;
    unknownReward.encounters.front().rewardGrantId = {"reward.missing"};
    expect(!maps::validateMapData(duplicateRegion, &mapCatalogs) &&
               !maps::validateMapData(invalidRegionBounds, &mapCatalogs) &&
               !maps::validateMapData(duplicateRule, &mapCatalogs) &&
               !maps::validateMapData(unknownRegionRule, &mapCatalogs) &&
               !maps::validateMapData(unknownEncounterRule, &mapCatalogs) &&
               !maps::validateMapData(unknownDoorAction, &mapCatalogs) &&
               !maps::validateMapData(nonDoorAction, &mapCatalogs) &&
               !maps::validateMapData(duplicateEncounter, &mapCatalogs) &&
               !maps::validateMapData(unknownParticipant, &mapCatalogs) &&
               !maps::validateMapData(duplicateParticipant, &mapCatalogs) &&
               !maps::validateMapData(sharedParticipant, &mapCatalogs) &&
               !maps::validateMapData(unknownReward, &mapCatalogs),
           "Map validation rejects duplicate, unknown and wrongly typed world references");
    const auto dmap = maps::deserializeDmap(maps::serializeDmap(*compiledMap.map));
    expect(dmap && maps::semanticallyEqual(*compiledMap.map, dmap.data),
           "DMAP 1.2 roundtrip preserves compiled authored-world structures");

    const auto temporary = std::filesystem::temp_directory_path() / "underworld_phase14_map.umap";
    const auto exported = std::filesystem::temp_directory_path() / "underworld_phase14_map.dmap";
    std::error_code fsError;
    std::filesystem::remove(temporary, fsError);
    std::filesystem::remove(exported, fsError);
    editor::EditorDocument document(map);
    document.commandRegions().push_back({{99}, "region.editor", {16, 16, 16, 16}});
    document.commandPropertyOverrides()[1][editor::PropertyId{"enemy.detection_range"}] =
        std::int64_t{123};
    std::string error;
    const bool savedDocument = document.saveAs(temporary, compiledContent, error);
    expect(savedDocument &&
               document.autosavePath() &&
               document.autosavePath()->filename() == "underworld_phase14_map.autosave.umap" &&
               std::filesystem::exists(temporary),
           "EditorDocument saves authored maps atomically and uses an authored autosave extension");
    const auto reopened = editor::EditorDocument::open(temporary, compiledContent, error);
    expect(reopened && reopened->authoredSource().placementOverrides.size() == 1 &&
               reopened->regions().size() == 2 &&
               reopened->authoredSource().regions.size() == 2,
           "EditorDocument opens UMAP into authored source data without dropping regions or overrides");
    const bool exportedDmap = reopened && reopened->exportDmap(exported, compiledContent, error);
    expect(exportedDmap &&
               std::filesystem::exists(exported),
           "EditorDocument exposes explicit compiled DMAP export");

    editor::EditorDocument authoringDocument(map);
    maps::WorldRuleDefinition editorRule{
        {"rule.editor"}, {maps::WorldTriggerKind::mapEntered, {}, {}}, {}, {}, false};
    const auto editorEncounterId = simulation::DefinitionId{"encounter.editor"};
    const auto editorRewardId = simulation::DefinitionId{"reward.quest.scholar.path"};
    expect(authoringDocument.addRule(editorRule, error) &&
               authoringDocument.setRuleTrigger(
                   {"rule.editor"},
                   {maps::WorldTriggerKind::regionEntered, {"region.codec"}, {}}, error) &&
               authoringDocument.addRuleCondition(
                   {"rule.editor"}, {maps::WorldConditionKind::flagNotSet, {"flag.editor"}, {}, {}}, error) &&
               authoringDocument.addRuleAction(
                   {"rule.editor"}, {maps::WorldActionKind::setFlag, {"flag.editor"}, {}}, error) &&
               authoringDocument.setRuleOnce({"rule.editor"}, true, error) &&
               authoringDocument.addEncounter({editorEncounterId, {}, std::nullopt}, error) &&
               authoringDocument.addEncounterParticipant(editorEncounterId, {1}, error) &&
               authoringDocument.setEncounterRewardGrant(editorEncounterId, editorRewardId, error) &&
               authoringDocument.rules().size() == map.worldRules.size() + 1U &&
               authoringDocument.encounters().back().participants ==
                   std::vector<simulation::PersistentInstanceId>{{1}} &&
               authoringDocument.encounters().back().rewardGrantId == editorRewardId,
           "EditorDocument exposes structured authoring operations for rules and encounters");
    expect(authoringDocument.removeRuleCondition({"rule.editor"}, 0, error) &&
               authoringDocument.removeRuleAction({"rule.editor"}, 0, error) &&
               authoringDocument.clearEncounterRewardGrant(editorEncounterId, error) &&
               authoringDocument.removeEncounterParticipant(editorEncounterId, {1}, error) &&
               authoringDocument.removeEncounter(editorEncounterId, error),
           "EditorDocument rule and encounter operations support removal and clearing");
    std::filesystem::remove(temporary, fsError);
    std::filesystem::remove(temporary.string() + ".bak", fsError);
    std::filesystem::remove(exported, fsError);

    gameplay::EncounterSystem encounters;
    const simulation::MapId mapId{"map.phase14.logic"};
    const simulation::DefinitionId encounterId{"encounter.logic"};
    const simulation::DefinitionId regionId{"region.logic"};
    const simulation::PersistentInstanceId doorId{9};
    const std::vector<maps::EncounterDefinition> encounterDefinitions{
        {encounterId, {{101}}, std::nullopt}};
    std::vector<maps::WorldRuleDefinition> rules;
    rules.push_back({{"rule.start"},
                     {maps::WorldTriggerKind::regionEntered, regionId, {}}, {},
                     {{maps::WorldActionKind::startEncounter, encounterId, {}}}, true});
    rules.push_back({{"rule.generated"},
                     {maps::WorldTriggerKind::encounterStarted, encounterId, {}}, {},
                     {{maps::WorldActionKind::setFlag, {"flag.generated"}, {}}}, false});
    rules.push_back({{"rule.flags"},
                     {maps::WorldTriggerKind::mapEntered, {}, {}},
                     {{maps::WorldConditionKind::flagNotSet, {"flag.clear"}, {}, {}}},
                     {{maps::WorldActionKind::setFlag, {"flag.clear"}, {}},
                      {maps::WorldActionKind::clearFlag, {"flag.clear"}, {}}}, false});
    rules.push_back({{"rule.encounter.condition"},
                     {maps::WorldTriggerKind::mapEntered, {}, {}},
                     {{maps::WorldConditionKind::encounterNotCompleted, encounterId, {}, {}}},
                     {{maps::WorldActionKind::setFlag, {"flag.encounter_not_completed"}, {}}}, false});
    rules.push_back({{"rule.door"},
                     {maps::WorldTriggerKind::mapEntered, {}, {}},
                     {{maps::WorldConditionKind::doorState, {}, doorId, gameplay::DoorState::closed}},
                     {{maps::WorldActionKind::setDoorState, {}, doorId, gameplay::DoorState::open}}, false});
    rules.push_back({{"rule.object"},
                     {maps::WorldTriggerKind::objectOpened, {}, doorId}, {},
                     {{maps::WorldActionKind::setFlag, {"flag.object"}, {}}}, false});
    rules.push_back({{"rule.exit"},
                     {maps::WorldTriggerKind::regionExited, regionId, {}}, {},
                     {{maps::WorldActionKind::setFlag, {"flag.exit"}, {}}}, false});
    rules.push_back({{"rule.completed"},
                     {maps::WorldTriggerKind::encounterCompleted, encounterId, {}}, {},
                     {{maps::WorldActionKind::setFlag, {"flag.completed"}, {}}}, false});
    rules.push_back({{"rule.flag-set"},
                     {maps::WorldTriggerKind::mapEntered, {}, {}},
                     {{maps::WorldConditionKind::flagSet, {"flag.generated"}, {}, {}}},
                     {{maps::WorldActionKind::setFlag, {"flag.condition"}, {}}}, false});
    gameplay::dialogue::DialogueFlagSet flags;
    simulation::EventBuffer events;
    std::vector<gameplay::WorldRuleState> ruleState;
    std::vector<gameplay::EncounterRuntimeState> encounterState;
    gameplay::DoorState doorState = gameplay::DoorState::closed;
    const gameplay::WorldLogicRuntime runtime{
        [&](const simulation::DefinitionId& id) {
            return encounters.state(encounterState, mapId, id) == gameplay::EncounterState::completed;
        },
        [&](const simulation::DefinitionId& id, simulation::EventBuffer& generated) {
            return encounters.start(encounterDefinitions, mapId, id, encounterState, generated);
        },
        [&](simulation::PersistentInstanceId id, maps::DoorState state) {
            if (id != doorId || doorState == state) return false;
            doorState = state;
            return true;
        },
        [&](simulation::PersistentInstanceId id) -> std::optional<maps::DoorState> {
            return id == doorId ? std::optional<maps::DoorState>{doorState} : std::nullopt;
        }};
    events.emit(simulation::MapEntered{mapId});
    events.emit(simulation::RegionEntered{mapId, regionId});
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            ruleState, runtime));
    expect(flags.isSet({"flag.generated"}) && encounters.state(encounterState, mapId, encounterId) ==
               gameplay::EncounterState::active && doorState == gameplay::DoorState::open,
           "World Logic processes generated EncounterStarted events and door conditions in one controlled cycle");
    expect(flags.isSet({"flag.encounter_not_completed"}),
           "World Logic evaluates encounterNotCompleted against the live encounter state");
    events.clear();
    events.emit(simulation::MapEntered{mapId});
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            ruleState, runtime));
    expect(!flags.isSet({"flag.clear"}),
           "World Logic implements flagSet/flagNotSet and clearFlag without silently ignoring conditions");
    events.clear();
    events.emit(simulation::ObjectOpened{{1}, {2, 1}, {"object.chest"}, doorId});
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            ruleState, runtime));
    expect(flags.isSet({"flag.object"}), "objectOpened rules receive the persistent placement identity");
    events.clear();
    events.emit(simulation::RegionEntered{mapId, regionId});
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            ruleState, runtime));
    expect(std::count_if(events.events().begin(), events.events().end(), [](const auto& event) {
               return std::holds_alternative<simulation::EncounterStarted>(event);
           }) == 0,
           "once world rules remain fired on re-entry instead of firing again");

    events.clear();
    events.emit(simulation::RegionExited{mapId, regionId});
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            ruleState, runtime));
    expect(flags.isSet({"flag.exit"}),
           "World Logic implements the regionExited trigger");

    events.clear();
    events.emit(simulation::MapEntered{mapId});
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            ruleState, runtime));
    expect(flags.isSet({"flag.condition"}),
           "World Logic evaluates flagSet conditions after earlier actions set the flag");

    events.clear();
    events.emit(simulation::MapEntered{simulation::MapId{"map.other"}});
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            ruleState, runtime));
    events.clear();
    events.emit(simulation::RegionEntered{mapId, regionId});
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            ruleState, runtime));
    expect(std::none_of(events.events().begin(), events.events().end(), [](const auto& event) {
               return std::holds_alternative<simulation::EncounterStarted>(event);
           }),
           "once world rule state survives a map transition and prevents re-entry replay");

    events.clear();
    encounters.evaluate(encounterDefinitions, mapId, std::span<const simulation::PersistentInstanceId>{},
                        encounterState, events);
    expect(encounters.state(encounterState, mapId, encounterId) == gameplay::EncounterState::completed &&
               events.events().size() == 1 &&
               std::holds_alternative<simulation::EncounterCompleted>(events.events().front()),
           "EncounterSystem transitions active encounters to completed exactly once from persistent participants");
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            ruleState, runtime));
    expect(flags.isSet({"flag.completed"}),
           "World Logic implements the encounterCompleted trigger");
    const auto before = events.events().size();
    encounters.evaluate(encounterDefinitions, mapId, std::span<const simulation::PersistentInstanceId>{},
                        encounterState, events);
    expect(events.events().size() == before,
           "completed encounter evaluation is idempotent and emits no duplicate completion event");

    save::SaveData worldSave;
    worldSave.player.currentMapId = map.id;
    worldSave.player.health = compiledContent.progressions().require(
        gameplay::rpg::defaultPlayerProgressionId()).baseStats.maximumHealth;
    worldSave.progression = {gameplay::rpg::defaultPlayerProgressionId(), 0};
    worldSave.world.worldRules.push_back({map.id, {"rule.enter"}, true});
    worldSave.world.encounters.push_back({map.id, {"encounter.codec"},
                                           gameplay::EncounterState::completed, true});
    const save::SaveValidationCatalogs worldSaveCatalogs{
        &compiledContent.items(), {&map}, nullptr, &compiledContent.progressions(),
        &compiledContent.objects()};
    const auto worldSaveBytes = save::serializeSave(worldSave);
    const auto loadedWorldSave = save::deserializeSave(worldSaveBytes, worldSaveCatalogs);
    expect(loadedWorldSave && loadedWorldSave.data.world.worldRules == worldSave.world.worldRules &&
               loadedWorldSave.data.world.encounters == worldSave.world.encounters &&
               worldSaveBytes == save::serializeSave(loadedWorldSave.data),
           "DSAV 1.8 roundtrips persistent once-rule and completed encounter state deterministically");
    auto invalidDoorSave = worldSave;
    invalidDoorSave.world.objects.push_back({{map.id, {3}}, false, false, {},
                                             static_cast<gameplay::DoorState>(99)});
    auto invalidEncounterSave = worldSave;
    invalidEncounterSave.world.encounters.front().state =
        static_cast<gameplay::EncounterState>(99);
    expect(!save::validateSaveData(invalidDoorSave, worldSaveCatalogs).empty() &&
               !save::validateSaveData(invalidEncounterSave, worldSaveCatalogs).empty(),
           "save validation rejects invalid door and encounter state enums before serialization");
    auto loadedRuleState = loadedWorldSave ? loadedWorldSave.data.world.worldRules :
                                           std::vector<gameplay::WorldRuleState>{};
    loadedRuleState.push_back({mapId, {"rule.start"}, true});
    events.clear();
    events.emit(simulation::RegionEntered{mapId, regionId});
    const auto beforeLoadedRuleEvents = events.size();
    static_cast<void>(gameplay::WorldLogicSystem{}.consume(rules, mapId, flags, events,
                                                            loadedRuleState, runtime));
    expect(events.size() == beforeLoadedRuleEvents,
           "once world rule state loaded from DSAV remains fired after re-entry");

    auto authoredDoorContent = content::makeBuiltinAuthoredContent();
    const auto doorDefinition = std::find_if(authoredDoorContent.objects.begin(),
        authoredDoorContent.objects.end(), [](const auto& value) {
            return value.id == simulation::DefinitionId{"object.crate"};
        });
    if (doorDefinition != authoredDoorContent.objects.end()) {
        doorDefinition->door = gameplay::ObjectDoorDefinition{
            gameplay::DoorState::closed, {0, 0, 16, 16}};
    }
    const auto compiledDoorContent = content::compileContent(authoredDoorContent);
    auto doorMap = makeSyntheticMap("map.phase14.door", "map.phase14.door");
    doorMap.objects[1].position = {32, 16};
    bool doorBehaviorPassed = false;
    if (compiledDoorContent.registry) {
        const auto doorCatalogs = game::mapValidationCatalogs(*compiledDoorContent.registry);
        simulation::EntityHandlePool doorHandles;
        const std::array doorVisuals{gameplay::creatures::soldierVisualId(),
                                     gameplay::creatures::skullVisualId()};
        gameplay::creatures::EnemyFactory doorEnemies(doorHandles,
            compiledDoorContent.registry->enemies(), compiledDoorContent.registry->behaviors(),
            compiledDoorContent.registry->attacks(), compiledDoorContent.registry->projectiles(),
            doorVisuals);
        gameplay::WorldObjectFactory doorObjects(doorHandles,
            compiledDoorContent.registry->objects(), compiledDoorContent.registry->items());
        game::RuntimeTilesetCatalog doorTilesets(compiledDoorContent.registry->tilesets());
        maps::RuntimeWorldBuilder doorBuilder(doorCatalogs, doorEnemies, doorObjects,
                                              doorHandles, doorTilesets);
        const auto doorWorld = doorBuilder.build(doorMap, simulation::SpawnId{"entry.start"});
        doorBehaviorPassed = doorWorld &&
            doorWorld.world->doorState({3}) == gameplay::DoorState::closed &&
            doorWorld.world->map().collision().isSolid(2, 1) &&
            doorWorld.world->interactDoor({3}) &&
            doorWorld.world->doorState({3}) == gameplay::DoorState::open &&
            !doorWorld.world->map().collision().isSolid(2, 1) &&
            doorWorld.world->setDoorState({3}, gameplay::DoorState::locked) &&
            doorWorld.world->map().collision().isSolid(2, 1) &&
            doorWorld.world->setDoorState({3}, gameplay::DoorState::open) &&
            !doorWorld.world->map().collision().isSolid(2, 1);
    }
    expect(doorBehaviorPassed,
           "stateful doors change interaction state and restore authored base collision");
    const auto doorCatalogs = compiledDoorContent.registry ?
        game::mapValidationCatalogs(*compiledDoorContent.registry) : maps::MapValidationCatalogs{};
    auto overlappingDoors = doorMap;
    overlappingDoors.objects.push_back({{7}, simulation::DefinitionId{"object.crate"}, {32, 16}, {}});
    expect(!maps::validateMapData(overlappingDoors, &doorCatalogs),
           "map validation rejects overlapping dynamic door collision cells");

    auto arenaAuthoredContent = content::makeBuiltinAuthoredContent();
    for (auto& behavior : arenaAuthoredContent.behaviors) {
        if (behavior.id == gameplay::creatures::soldierBehaviorId()) {
            behavior.detectionRangePixels = 1;
            behavior.disengageRangePixels = 2;
            behavior.idleDurationTicks = 1000;
            behavior.wanderDurationTicks = 1000;
        }
    }
    for (auto& enemy : arenaAuthoredContent.enemies) {
        if (enemy.id == gameplay::creatures::soldierEnemyId()) enemy.maximumHealth = 1;
    }
    for (auto& object : arenaAuthoredContent.objects) {
        if (object.id == simulation::DefinitionId{"object.crate"}) {
            object.door = gameplay::ObjectDoorDefinition{gameplay::DoorState::closed,
                                                         {0, 0, 16, 16}};
        }
    }
    const auto arenaContent = content::compileContent(arenaAuthoredContent);
    bool arenaSessionPassed = false;
    if (arenaContent.registry) {
        auto arenaMap = makeSyntheticMap("map.phase14.arena", "map.phase14.arena");
        arenaMap.objects[1].position = {32, 16};
        arenaMap.enemies.push_back({{6}, gameplay::creatures::soldierEnemyId(), {40, 24},
                                    gameplay::FacingDirection::left});
        arenaMap.regions.push_back({{"region.arena"}, {0, 0, 64, 48}});
        arenaMap.encounters.push_back({{"encounter.arena"}, {{1}, {6}},
                                       simulation::DefinitionId{"reward.quest.scholar.path"}});
        arenaMap.worldRules.push_back({
            {"rule.arena.enter"},
            {maps::WorldTriggerKind::regionEntered, {"region.arena"}, {}}, {},
            {{maps::WorldActionKind::setDoorState, {}, {3}, gameplay::DoorState::locked},
             {maps::WorldActionKind::startEncounter, {"encounter.arena"}, {}}}, true});
        arenaMap.worldRules.push_back({
            {"rule.arena.complete"},
            {maps::WorldTriggerKind::encounterCompleted, {"encounter.arena"}, {}}, {},
            {{maps::WorldActionKind::setDoorState, {}, {3}, gameplay::DoorState::open},
             {maps::WorldActionKind::setFlag, {"flag.arena.completed"}, {}}}, true});
        const auto arenaValidation = game::mapValidationCatalogs(*arenaContent.registry);
        const auto arenaValidationResult = maps::validateMapData(arenaMap, &arenaValidation);
        expect(arenaValidationResult.valid,
               "authored arena validates with door, region, world rules and encounter references");
        const auto arenaDmapPath = std::filesystem::temp_directory_path() /
            "underworld_phase14_arena.dmap";
        std::string arenaIoError;
        std::error_code arenaFsError;
        std::filesystem::remove(arenaDmapPath, arenaFsError);
        const bool arenaDmapWritten = maps::writeDmap(arenaDmapPath, arenaMap, arenaIoError);
        maps::MapCatalog arenaCatalog;
        if (arenaDmapWritten) arenaCatalog.add(arenaMap.id, arenaDmapPath);
        simulation::EntityHandlePool arenaHandles;
        const std::array arenaVisuals{gameplay::creatures::soldierVisualId(),
                                      gameplay::creatures::skullVisualId()};
        gameplay::creatures::EnemyFactory arenaEnemies(
            arenaHandles, arenaContent.registry->enemies(), arenaContent.registry->behaviors(),
            arenaContent.registry->attacks(), arenaContent.registry->projectiles(), arenaVisuals);
        gameplay::WorldObjectFactory arenaObjects(arenaHandles, arenaContent.registry->objects(),
                                                   arenaContent.registry->items());
        game::RuntimeTilesetCatalog arenaTilesets(arenaContent.registry->tilesets());
        maps::RuntimeWorldBuilder arenaBuilder(arenaValidation, arenaEnemies, arenaObjects,
                                                arenaHandles, arenaTilesets);
        game::GameSession arenaSession({0}, testProgression());
        arenaSession.configureCombat(arenaContent.registry->attacks(),
                                     arenaContent.registry->projectiles(),
                                     arenaContent.registry->behaviors(),
                                     arenaContent.registry->attacks().require(
                                         gameplay::playerSwordAttackId()),
                                     arenaContent.registry->attacks().require(
                                         gameplay::playerBowAttackId()));
        arenaSession.configureItems(arenaContent.registry->items());
        arenaSession.configureNarrative(arenaContent.registry->dialogues(),
                                         arenaContent.registry->quests());
        arenaSession.configureRewards(arenaContent.registry->rewardProfiles(),
                                       arenaContent.registry->pickups());
            arenaSession.configureRewardGrants(arenaContent.registry->rewardGrants());
        std::string arenaSessionError;
        const bool initialized = arenaDmapWritten && arenaValidationResult.valid &&
            arenaSession.initializeMap(arenaCatalog, arenaValidation, arenaBuilder,
                                       arenaMap.id, simulation::SpawnId{"entry.start"}, arenaSessionError);
        expect(initialized, "GameSession initializes the authored arena through the DMAP catalog");
        if (initialized) {
            arenaSession.tick(movementCommand(1, 0, 0));
            const auto arenaEncounterState = arenaSession.worldState().encounters;
            const auto encounter = std::find_if(arenaEncounterState.begin(), arenaEncounterState.end(),
                [](const auto& value) { return value.encounterId == simulation::DefinitionId{"encounter.arena"}; });
            const bool entered = std::any_of(arenaSession.events().events().begin(),
                arenaSession.events().events().end(), [](const auto& event) {
                    return std::holds_alternative<simulation::RegionEntered>(event);
                });
            const bool started = std::any_of(arenaSession.events().events().begin(),
                arenaSession.events().events().end(), [](const auto& event) {
                    return std::holds_alternative<simulation::EncounterStarted>(event);
                });
            expect(entered && started && encounter != arenaEncounterState.end() &&
                       encounter->state == gameplay::EncounterState::active &&
                       arenaSession.world().doorState({3}) == gameplay::DoorState::locked &&
                       arenaSession.world().map().collision().isSolid(2, 1),
                   "arena entry emits RegionEntered, locks the door and starts the encounter");
            arenaSession.relocatePlayer({28, 24}, gameplay::FacingDirection::right);
            for (std::uint64_t tick = 2; tick < 80; ++tick) {
                const auto encounterNow = std::find_if(arenaSession.worldState().encounters.begin(),
                    arenaSession.worldState().encounters.end(), [](const auto& value) {
                        return value.encounterId == simulation::DefinitionId{"encounter.arena"};
                    });
                if (encounterNow != arenaSession.worldState().encounters.end() &&
                    encounterNow->state == gameplay::EncounterState::completed) break;
                const auto command = arenaSession.player().actionState() ==
                    gameplay::PlayerActionState::none
                    ? actionCommand(tick, true, false)
                    : movementCommand(tick, 0, 0);
                arenaSession.tick(command);
            }
            const auto completed = std::find_if(arenaSession.worldState().encounters.begin(),
                arenaSession.worldState().encounters.end(), [](const auto& value) {
                    return value.encounterId == simulation::DefinitionId{"encounter.arena"};
                });
            const bool completedEvent = std::any_of(arenaSession.events().events().begin(),
                arenaSession.events().events().end(), [](const auto& event) {
                    return std::holds_alternative<simulation::EncounterCompleted>(event);
                });
            arenaSessionPassed = completed != arenaSession.worldState().encounters.end() &&
                completed->state == gameplay::EncounterState::completed && completed->rewardClaimed &&
                completedEvent && arenaSession.world().doorState({3}) == gameplay::DoorState::open &&
                !arenaSession.world().map().collision().isSolid(2, 1) &&
                arenaSession.dialogueFlags().isSet({"flag.arena.completed"}) &&
                arenaSession.world().enemies().empty();
            expect(arenaSessionPassed,
                   "arena gameplay defeats participants, completes encounter, opens door, sets flag and rewards once");
            const auto savedArena = arenaSession.captureSaveData();
            const auto savedDoor = savedArena.world.findObject({arenaMap.id, {3}});
            const save::SaveValidationCatalogs arenaSaveCatalogs{
                &arenaContent.registry->items(), {&arenaMap}, &arenaContent.registry->quests(),
                &arenaContent.registry->progressions(), &arenaContent.registry->objects()};
            const auto serializedArenaSave = save::serializeSave(savedArena);
            const auto restoredArenaSave = save::deserializeSave(serializedArenaSave,
                                                                 arenaSaveCatalogs);
            const bool savedState = savedDoor && savedDoor->doorState &&
                *savedDoor->doorState == gameplay::DoorState::open && restoredArenaSave;
            expect(savedState, "arena save captures open door and completed encounter state");
            if (restoredArenaSave) {
                game::GameSession loadedArena({0}, testProgression());
                loadedArena.configureCombat(arenaContent.registry->attacks(),
                                            arenaContent.registry->projectiles(),
                                            arenaContent.registry->behaviors(),
                                            arenaContent.registry->attacks().require(
                                                gameplay::playerSwordAttackId()),
                                            arenaContent.registry->attacks().require(
                                                gameplay::playerBowAttackId()));
                loadedArena.configureItems(arenaContent.registry->items());
                loadedArena.configureNarrative(arenaContent.registry->dialogues(),
                                               arenaContent.registry->quests());
                loadedArena.configureRewards(arenaContent.registry->rewardProfiles(),
                                              arenaContent.registry->pickups());
                loadedArena.configureRewardGrants(arenaContent.registry->rewardGrants());
                std::string loadError;
                const bool loaded = loadedArena.initializeMap(arenaCatalog, arenaValidation,
                    arenaBuilder, arenaMap.id, simulation::SpawnId{"entry.start"}, loadError) &&
                    loadedArena.restoreSaveData(restoredArenaSave.data, loadError);
                const auto rewardItems = loaded ? loadedArena.playerItems().inventory().items().count(
                    gameplay::lifePotionItemId()) : 0;
                expect(loaded && loadedArena.world().doorState({3}) == gameplay::DoorState::open &&
                           loadedArena.world().enemies().empty() && loadedArena.dialogueFlags().isSet(
                               {"flag.arena.completed"}) && rewardItems ==
                               arenaSession.playerItems().inventory().items().count(
                                   gameplay::lifePotionItemId()),
                       "arena save/load preserves completed encounter, open door, flag and exactly-once reward");
            }
        }
        std::filesystem::remove(arenaDmapPath, arenaFsError);
    }
    expect(arenaSessionPassed, "vertical authored arena slice passes through GameSession");
}

void testPhase15PresentationFeedback() {
    namespace presentation = underworld::game::presentation;
    namespace content = underworld::game::content;
    namespace maps = underworld::game::maps;
    namespace gameplay = underworld::game::gameplay;
    namespace simulation = underworld::simulation;
    namespace core = underworld::core;
    namespace render = underworld::render;
    namespace game = underworld::game;
    namespace editor = underworld::editor;

    presentation::PresentationEffectCatalog catalog;
    presentation::PresentationEffectDefinition hit;
    hit.id = {"effect.test.hit"};
    hit.lifetime = presentation::PresentationEffectLifetime::transient;
    hit.durationTicks = 6;
    hit.priority = 20;
    hit.cameraShake = presentation::CameraShakeDefinition{4};
    hit.overlay = presentation::ColorOverlayDefinition{
        {255, 0, 0, 128}, presentation::PresentationOverlayMode::linearFadeOut, 0,
        presentation::PresentationCompositionLayer::world};
    catalog.add(hit);
    presentation::PresentationEffectDefinition other;
    other.id = {"effect.test.other"};
    other.lifetime = presentation::PresentationEffectLifetime::transient;
    other.durationTicks = 8;
    other.priority = 30;
    other.overlay = presentation::ColorOverlayDefinition{
        {255, 255, 255, 200}, presentation::PresentationOverlayMode::pulse, 4,
        presentation::PresentationCompositionLayer::final};
    catalog.add(other);
    presentation::PresentationEffectDefinition dark;
    dark.id = {"effect.test.dark"};
    dark.lifetime = presentation::PresentationEffectLifetime::persistent;
    dark.priority = 10;
    dark.visionMask = presentation::VisionMaskDefinition{2, 4, 220, {0, 0, 0, 255}};
    catalog.add(dark);
    auto playerHit = hit;
    playerHit.id = {"effect.player.hit"};
    catalog.add(playerHit);
    auto environmentDark = dark;
    environmentDark.id = {"effect.environment.dark"};
    catalog.add(environmentDark);
    presentation::PresentationEffectDefinition heavyImpact;
    heavyImpact.id = {"effect.world.heavy_impact"};
    heavyImpact.lifetime = presentation::PresentationEffectLifetime::transient;
    heavyImpact.durationTicks = 12;
    heavyImpact.priority = 80;
    heavyImpact.cameraShake = presentation::CameraShakeDefinition{6};
    heavyImpact.overlay = presentation::ColorOverlayDefinition{
        {255, 255, 255, 160}, presentation::PresentationOverlayMode::linearFadeOut, 0,
        presentation::PresentationCompositionLayer::final};
    catalog.add(heavyImpact);
    presentation::PresentationEffectDefinition fade;
    fade.id = {"effect.test.fade"};
    fade.lifetime = presentation::PresentationEffectLifetime::transient;
    fade.durationTicks = 4;
    fade.priority = 40;
    fade.fade = presentation::FadeDefinition{{0, 0, 0, 255}, 0, 255};
    catalog.add(fade);

    presentation::PresentationEffectSystem effects(catalog);
    expect(effects.play({"effect.test.hit"}) && effects.isActive({"effect.test.hit"}),
           "presentation transient effect starts on play");
    const auto initialHit = effects.resolveFrame();
    effects.advance(3);
    expect(effects.play({"effect.test.hit"}) && effects.resolveFrame().worldOverlays.size() == 1,
           "presentation transient retrigger restarts one keyed instance");
    effects.advance(6);
    expect(!effects.isActive({"effect.test.hit"}),
           "presentation transient lifetime expires in fixed ticks");
    expect(effects.play({"effect.test.hit"}) && effects.play({"effect.test.other"}) &&
               (effects.advance(1), true) &&
               effects.isActive({"effect.test.hit"}) && effects.isActive({"effect.test.other"}) &&
               effects.resolveFrame().finalOverlays.size() == 1,
           "different presentation effect IDs coexist with deterministic composition layers");
    const auto sourceA = presentation::PresentationEffectSourceKey{
        presentation::PresentationEffectSourceKind::region, simulation::MapId{"map.test"},
        {"region.a"}};
    const auto sourceB = presentation::PresentationEffectSourceKey{
        presentation::PresentationEffectSourceKind::region, simulation::MapId{"map.test"},
        {"region.b"}};
    expect(effects.activatePersistent({"effect.test.dark"}, sourceA) &&
               effects.activatePersistent({"effect.test.dark"}, sourceB) &&
               effects.persistentSourceCount({"effect.test.dark"}) == 2,
           "persistent presentation effects track multiple structured sources");
    expect(effects.deactivatePersistent({"effect.test.dark"}, sourceA) &&
               effects.isActive({"effect.test.dark"}) &&
               effects.deactivatePersistent({"effect.test.dark"}, sourceB) &&
               !effects.isActive({"effect.test.dark"}),
           "persistent presentation effects remain until their final source leaves");

    presentation::PresentationEffectCatalog shakeCatalog;
    presentation::PresentationEffectDefinition shake;
    shake.id = {"effect.test.shake"};
    shake.lifetime = presentation::PresentationEffectLifetime::transient;
    shake.durationTicks = 12;
    shake.cameraShake = presentation::CameraShakeDefinition{64};
    shakeCatalog.add(shake);
    presentation::PresentationEffectSystem shakeA(shakeCatalog);
    presentation::PresentationEffectSystem shakeB(shakeCatalog);
    static_cast<void>(shakeA.play(shake.id));
    static_cast<void>(shakeB.play(shake.id));
    bool deterministicShake = true;
    bool boundedShake = true;
    for (int tick = 0; tick < 8; ++tick) {
        const auto frameA = shakeA.resolveFrame();
        const auto frameB = shakeB.resolveFrame();
        deterministicShake = deterministicShake && frameA.cameraOffset == frameB.cameraOffset;
        boundedShake = boundedShake && std::abs(frameA.cameraOffset.x) <= 12 &&
            std::abs(frameA.cameraOffset.y) <= 12;
        shakeA.advance();
        shakeB.advance();
    }
    expect(deterministicShake && boundedShake,
           "camera shake uses a deterministic pattern and defensive clamp");
    static_cast<void>(effects.play({"effect.test.fade"}));
    static_cast<void>(effects.advance(1));
    const auto pulseFrame = effects.resolveFrame();
    expect(!initialHit.worldOverlays.empty() && !pulseFrame.fades.empty() &&
               pulseFrame.fades.front().alpha > 0,
           "presentation frame exposes transient overlay and final fade data");

    render::Framebuffer maskBuffer(8, 8);
    maskBuffer.clear({255, 255, 255, 255});
    presentation::PresentationEffectFrame maskFrame;
    maskFrame.visionMask = presentation::ResolvedVisionMask{
        {"effect.test.dark"}, 10, 1, 3, 200, {0, 0, 0, 255}};
    presentation::PresentationEffectRenderer::applyWorld(maskBuffer, maskFrame, {4, 4});
    expect(maskBuffer.pixels()[static_cast<std::size_t>(4 * 8 + 4)] ==
               core::ColorRGBA8{255, 255, 255, 255} &&
               maskBuffer.pixels().front().r < 255,
           "vision mask preserves its inner center and darkens pixels outside the radius");
    render::Framebuffer layerBuffer(4, 4);
    layerBuffer.clear({255, 255, 255, 255});
    presentation::PresentationEffectFrame layerFrame;
    layerFrame.worldOverlays.push_back({{"effect.world"}, 1, {0, 0, 0, 255},
                                        presentation::PresentationCompositionLayer::world});
    layerFrame.finalOverlays.push_back({{"effect.final"}, 1, {255, 0, 0, 255},
                                        presentation::PresentationCompositionLayer::final});
    presentation::PresentationEffectRenderer::applyWorld(layerBuffer, layerFrame, {0, 0});
    const bool worldLayerBeforeHud = layerBuffer.pixels().front() == core::ColorRGBA8{0, 0, 0, 255} &&
        layerBuffer.pixels()[1] == core::ColorRGBA8{0, 0, 0, 255};
    render::Renderer2D(layerBuffer).setPixel(0, 0, {255, 255, 255, 255});
    presentation::PresentationEffectRenderer::applyFinal(layerBuffer, layerFrame);
    expect(worldLayerBeforeHud && layerBuffer.pixels().front() == core::ColorRGBA8{255, 0, 0, 255} &&
               layerBuffer.pixels()[1] == core::ColorRGBA8{255, 0, 0, 255},
           "world and final presentation layers compose around HUD timing");

    auto builtin = content::makeBuiltinAuthoredContent();
    const auto builtinJson = content::encodeAuthoredContentJson(builtin);
    const auto decodedBuiltin = content::decodeAuthoredContentJson(builtinJson);
    expect(decodedBuiltin.content && decodedBuiltin.content->presentationEffects.size() ==
               builtin.presentationEffects.size() && content::encodeAuthoredContentJson(
                   *decodedBuiltin.content) == builtinJson,
           "content JSON v3 roundtrips authored presentation effects deterministically");
    const auto invalidV2Effect = content::decodeAuthoredContentJson(
        R"({"format":"dungeon-underworld-content","version":2,"presentationEffects":[]})");
    expect(!invalidV2Effect.content && !invalidV2Effect.diagnostics.empty(),
           "content schema v2 rejects the presentation effect category");
    auto compiledBuiltin = content::compileContent(builtin);
    expect(compiledBuiltin.registry && compiledBuiltin.registry->presentationEffects().find(
               {"effect.player.hit"}) != nullptr,
           "presentation effects compile into the shared GameContentRegistry catalog");
    const auto decodedCompiled = decodedBuiltin.content
        ? content::compileContent(*decodedBuiltin.content) : content::ContentCompileResult{};
    expect(decodedCompiled.registry && compiledBuiltin.registry &&
               decodedCompiled.registry->presentationEffects().definitions() ==
                   compiledBuiltin.registry->presentationEffects().definitions(),
           "direct and decoded registries remain equivalent for presentation effects");
    auto invalidPresentation = builtin;
    invalidPresentation.presentationEffects.front().durationTicks = 0;
    invalidPresentation.presentationEffects.front().lifetime =
        presentation::PresentationEffectLifetime::transient;
    expect(content::ContentValidator{}.validate(invalidPresentation).hasErrors(),
           "presentation effect validation rejects an invalid transient duration");

    simulation::EventBuffer feedbackEvents;
    const simulation::EntityHandle player{7, 1};
    maps::MapData feedbackMap;
    feedbackMap.id = simulation::MapId{"map.presentation"};
    feedbackMap.regions.push_back({{"region.dark"}, {0, 0, 32, 32}, {"effect.environment.dark"}});
    presentation::PresentationFeedbackController feedback;
    feedbackEvents.emit(simulation::MapEntered{feedbackMap.id});
    feedbackEvents.emit(simulation::RegionEntered{feedbackMap.id, {"region.dark"}});
    feedbackEvents.emit(simulation::EntityDamaged{{1, 1}, player, 1, 9, 1});
    feedback.consume(feedbackEvents, feedbackMap, player, effects);
    expect(effects.isActive({"effect.environment.dark"}) &&
               effects.isActive({"effect.player.hit"}),
           "damage cues and region environment cues reach the presentation runtime");
    feedbackEvents.clear();
    feedbackEvents.emit(simulation::EntityDamaged{{1, 1}, {8, 1}, 1, 9, 2});
    feedback.consume(feedbackEvents, feedbackMap, player, effects);
    expect(effects.isActive({"effect.player.hit"}),
           "damage to non-player entities does not retrigger the player hit binding");
    feedbackEvents.clear();
    feedbackEvents.emit(simulation::RegionExited{feedbackMap.id, {"region.dark"}});
    feedback.consume(feedbackEvents, feedbackMap, player, effects);
    expect(!effects.isActive({"effect.environment.dark"}),
           "region exit releases the corresponding persistent presentation source");

    editor::EditorDocument editorDocument = editor::EditorDocument::newMap(
        simulation::MapId{"map.presentation.editor"}, 4, 3, 16, false);
    editorDocument.commandRegions().push_back({{1}, "region.editor", {0, 0, 32, 32}, {}});
    std::string editorError;
    const bool boundEnvironmentEffect = editorDocument.setRegionEnvironmentEffect(
        {"region.editor"}, {"effect.environment.dark"}, editorError);
    expect(boundEnvironmentEffect && editorError.empty() &&
               editorDocument.authoredSource().regions.front().environmentEffectId ==
                   simulation::DefinitionId{"effect.environment.dark"},
           "Map Maker document can author a region presentation environment binding");

    maps::WorldRuleDefinition cueRule;
    cueRule.id = {"rule.presentation.cue"};
    cueRule.trigger = {maps::WorldTriggerKind::regionEntered, {"region.impact"}, {}};
    cueRule.actions.push_back({maps::WorldActionKind::playPresentationEffect,
                               {"effect.world.heavy_impact"}, {}});
    gameplay::dialogue::DialogueFlagSet flags;
    std::vector<gameplay::WorldRuleState> ruleState;
    feedbackEvents.clear();
    feedbackEvents.emit(simulation::RegionEntered{feedbackMap.id, {"region.impact"}});
    expect(gameplay::WorldLogicSystem{}.consume({cueRule}, feedbackMap.id, flags,
                                                feedbackEvents, ruleState),
           "World Logic accepts a presentation cue action without renderer access");
    const bool cueEmitted = std::any_of(feedbackEvents.events().begin(),
        feedbackEvents.events().end(), [](const auto& event) {
            return std::holds_alternative<simulation::PresentationEffectRequested>(event);
        });
    feedback.consume(feedbackEvents, feedbackMap, player, effects);
    expect(cueEmitted && effects.isActive({"effect.world.heavy_impact"}),
           "World Logic presentation cue flows through EventBuffer to effects");

    auto authored = maps::authoredMapFromMapData(makeSyntheticMap("map.presentation.umap", "map.presentation"));
    authored.regions.push_back({{"region.dark"}, {0, 0, 64, 48}, {"effect.environment.dark"}});
    authored.regions.push_back({{"region.impact"}, {0, 0, 64, 48}, {}});
    maps::WorldRuleDefinition authoredCue;
    authoredCue.id = {"rule.presentation.impact"};
    authoredCue.trigger = {maps::WorldTriggerKind::regionEntered, {"region.impact"}, {}};
    authoredCue.actions.push_back({maps::WorldActionKind::playPresentationEffect,
                                   {"effect.world.heavy_impact"}, {}});
    authored.worldRules.push_back(authoredCue);
    const auto authoredJson = maps::encodeAuthoredMapJson(authored);
    const auto decodedAuthored = maps::decodeAuthoredMapJson(authoredJson);
    const auto compiledAuthored = compiledBuiltin.registry && decodedAuthored.source ?
        maps::compileAuthoredMap(*decodedAuthored.source, *compiledBuiltin.registry) :
        maps::MapCompileResult{};
    expect(decodedAuthored.source && maps::encodeAuthoredMapJson(*decodedAuthored.source) == authoredJson &&
               compiledAuthored.map && compiledAuthored.map->regions.front().environmentEffectId &&
               compiledAuthored.map->worldRules.back().actions.front().kind ==
                   maps::WorldActionKind::playPresentationEffect,
           "UMAP v2 preserves region environment bindings and presentation actions through MapCompiler");
    std::string legacyUmap = maps::encodeAuthoredMapJson(
        maps::authoredMapFromMapData(makeSyntheticMap("map.presentation.legacy", "map.presentation.legacy")));
    // A genuine v1 source predates both map-authored scenes and placement
    // persistence.  The current encoder always emits the optional fields, so
    // remove them from this fixture before exercising the legacy reader.
    const auto legacyScenes = legacyUmap.find("\"scenes\"");
    if (legacyScenes != std::string::npos) {
        const auto comma = legacyUmap.find(',', legacyScenes);
        legacyUmap.erase(legacyScenes,
                         comma == std::string::npos ? std::string::npos : comma - legacyScenes + 1);
    }
    for (std::size_t legacyPersistence = legacyUmap.find("\"persistence\"");
         legacyPersistence != std::string::npos;
         legacyPersistence = legacyUmap.find("\"persistence\"")) {
        const auto comma = legacyUmap.find(',', legacyPersistence);
        legacyUmap.erase(legacyPersistence,
                         comma == std::string::npos ? std::string::npos : comma - legacyPersistence + 1);
    }
    const auto legacyVersion = legacyUmap.find("\"version\"");
    const auto legacyValue = legacyVersion == std::string::npos
        ? std::string::npos : legacyUmap.find('4', legacyVersion);
    if (legacyValue != std::string::npos) legacyUmap.replace(legacyValue, 1, "1");
    const auto legacyDecoded = maps::decodeAuthoredMapJson(legacyUmap);
    auto incompatibleUmap = authoredJson;
    const auto incompatibleVersion = incompatibleUmap.find("\"version\"");
    const auto incompatibleValue = incompatibleVersion == std::string::npos
        ? std::string::npos : incompatibleUmap.find('4', incompatibleVersion);
    if (incompatibleValue != std::string::npos) incompatibleUmap.replace(incompatibleValue, 1, "1");
    const auto incompatibleDecoded = maps::decodeAuthoredMapJson(incompatibleUmap);
    expect(legacyDecoded.source.has_value(), "UMAP v1 remains readable");
    expect(!incompatibleDecoded.source.has_value(),
           "UMAP v1 rejects presentation-only v2 fields");
    if (compiledAuthored.map && compiledBuiltin.registry) {
        const auto dmapPath = std::filesystem::temp_directory_path() /
            "underworld_phase15_presentation.dmap";
        std::string ioError;
        std::error_code fsError;
        std::filesystem::remove(dmapPath, fsError);
        const auto catalogs = game::mapValidationCatalogs(*compiledBuiltin.registry);
        const bool written = maps::writeDmap(dmapPath, *compiledAuthored.map, ioError);
        const auto loaded = written ? maps::readDmap(dmapPath, &catalogs) : maps::DmapLoadResult{};
        expect(loaded && loaded.data.regions.front().environmentEffectId &&
                   loaded.data.worldRules.back().actions.front().kind ==
                       maps::WorldActionKind::playPresentationEffect,
               "DMAP 1.3 roundtrips presentation region and world-rule data");
        auto incompatibleDmap = maps::serializeDmap(*compiledAuthored.map);
        if (incompatibleDmap.size() >= 8) {
            incompatibleDmap[6] = 2;
            incompatibleDmap[7] = 0;
        }
        expect(!maps::deserializeDmap(incompatibleDmap),
               "DMAP 1.2 rejects presentation-only world-rule actions");
        std::filesystem::remove(dmapPath, fsError);
    }
    const auto legacyDmap = makeDmapV12WithoutObjectPersistence(maps::serializeDmap(
        maps::mapDataFromAuthored(maps::authoredMapFromMapData(
            makeSyntheticMap("map.presentation.dmap12", "map.presentation.dmap12")))));
    const auto legacyDmapLoaded = legacyDmap ? maps::deserializeDmap(*legacyDmap)
                                              : maps::DmapLoadResult{};
    expect(legacyDmapLoaded && legacyDmapLoaded.data.regions.empty(),
           "DMAP 1.2 remains readable without presentation environment bindings");

    const auto mixedRoot = std::filesystem::temp_directory_path() / "underworld_phase15_content_versions";
    std::error_code mixedError;
    std::filesystem::remove_all(mixedRoot, mixedError);
    std::filesystem::create_directories(mixedRoot);
    const auto writeText = [](const std::filesystem::path& path, std::string_view text) {
        std::ofstream output(path, std::ios::binary);
        output << text;
        return output.good();
    };
    const bool mixedWritten =
        writeText(mixedRoot / "v1.json", R"({"format":"dungeon-underworld-content","version":1,"items":[]})") &&
        writeText(mixedRoot / "v2.json", R"({"format":"dungeon-underworld-content","version":2,"objects":[]})") &&
        writeText(mixedRoot / "v3.json", R"({"format":"dungeon-underworld-content","version":3,"presentationEffects":[{"id":"effect.mixed","lifetime":"persistent","durationTicks":0,"priority":1,"visionMask":{"innerRadiusPixels":1,"outerRadiusPixels":2,"outsideAlpha":100,"color":{"r":0,"g":0,"b":0,"a":255}}}]})");
    const auto mixed = content::loadContentWorkspaceDirectory(mixedRoot);
    expect(mixedWritten && mixed.workspace && mixed.diagnostics.empty() &&
               mixed.workspace->authored.presentationEffects.size() == 1,
           "content workspace merges v1, v2 and v3 files without reinterpretation");
    std::filesystem::remove_all(mixedRoot, mixedError);
}

underworld::game::content::AuthoredContentPack makePhase16Content() {
    namespace content = underworld::game::content;
    namespace gameplay = underworld::game::gameplay;
    namespace world = underworld::world;
    auto authored = content::makeBuiltinAuthoredContent();

    content::AuthoredWorldObject leverA;
    leverA.id = {"object.phase16.lever.a"};
    leverA.visualSetId = {"visual.object.crate"};
    leverA.interactable = gameplay::ObjectInteractionDefinition{{-12, -12, 24, 24}};
    leverA.activation = gameplay::ObjectActivationDefinition{
        gameplay::ObjectActivationMode::interactToggle, false, std::nullopt};
    authored.objects.push_back(leverA);

    content::AuthoredWorldObject leverB = leverA;
    leverB.id = {"object.phase16.lever.b"};
    authored.objects.push_back(leverB);

    content::AuthoredWorldObject plate;
    plate.id = {"object.phase16.plate"};
    plate.visualSetId = {"visual.object.crate"};
    plate.activation = gameplay::ObjectActivationDefinition{
        gameplay::ObjectActivationMode::playerPressure, false,
        world::AabbI{-8, -8, 16, 16}};
    authored.objects.push_back(plate);

    content::AuthoredWorldObject door;
    door.id = {"object.phase16.door"};
    door.visualSetId = {"visual.object.crate"};
    door.door = gameplay::ObjectDoorDefinition{gameplay::DoorState::closed,
                                                {0, 0, 16, 16}};
    authored.objects.push_back(door);

    return authored;
}

underworld::game::maps::MapData makePhase16Map() {
    namespace gameplay = underworld::game::gameplay;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;
    auto map = makeSyntheticMap("map.phase16.puzzle", "map.phase16.puzzle");
    map.enemies.clear();
    map.npcs.clear();
    map.pickups.clear();
    map.links.clear();
    map.objects.clear();
    map.playerSpawns.resize(1);
    map.playerSpawns.front().position = {16, 24};
    map.objects.push_back({{101}, {"object.phase16.lever.a"}, {16, 24}, {}});
    map.objects.push_back({{102}, {"object.phase16.lever.b"}, {16, 8}, {}});
    map.objects.push_back({{103}, {"object.phase16.plate"}, {48, 24}, {}});
    map.objects.push_back({{104}, {"object.phase16.door"}, {32, 16}, {}});
    map.objects.push_back({{105}, {"object.phase16.door"}, {0, 16}, {}});

    const auto objectTrigger = [](maps::WorldTriggerKind kind,
                                  simulation::PersistentInstanceId id) {
        return maps::WorldTrigger{kind, {}, id};
    };
    const auto objectCondition = [](maps::WorldConditionKind kind,
                                    simulation::PersistentInstanceId id) {
        return maps::WorldCondition{kind, {}, id, gameplay::DoorState::closed};
    };
    const auto doorAction = [](simulation::PersistentInstanceId id,
                               gameplay::DoorState state) {
        return maps::WorldAction{maps::WorldActionKind::setDoorState, {}, id, state};
    };
    maps::WorldRuleDefinition rule;
    rule.id = {"rule.phase16.a.opens"};
    rule.trigger = objectTrigger(maps::WorldTriggerKind::objectActivated, {101});
    rule.conditions.push_back(objectCondition(maps::WorldConditionKind::objectActive, {102}));
    rule.actions.push_back(doorAction({104}, gameplay::DoorState::open));
    map.worldRules.push_back(rule);
    rule = {};
    rule.id = {"rule.phase16.b.opens"};
    rule.trigger = objectTrigger(maps::WorldTriggerKind::objectActivated, {102});
    rule.conditions.push_back(objectCondition(maps::WorldConditionKind::objectActive, {101}));
    rule.actions.push_back(doorAction({104}, gameplay::DoorState::open));
    map.worldRules.push_back(rule);
    rule = {};
    rule.id = {"rule.phase16.a.closes"};
    rule.trigger = objectTrigger(maps::WorldTriggerKind::objectDeactivated, {101});
    rule.actions.push_back(doorAction({104}, gameplay::DoorState::closed));
    map.worldRules.push_back(rule);
    rule = {};
    rule.id = {"rule.phase16.b.closes"};
    rule.trigger = objectTrigger(maps::WorldTriggerKind::objectDeactivated, {102});
    rule.actions.push_back(doorAction({104}, gameplay::DoorState::closed));
    map.worldRules.push_back(rule);
    rule = {};
    rule.id = {"rule.phase16.plate.opens"};
    rule.trigger = objectTrigger(maps::WorldTriggerKind::objectActivated, {103});
    rule.actions.push_back(doorAction({105}, gameplay::DoorState::open));
    map.worldRules.push_back(rule);
    rule = {};
    rule.id = {"rule.phase16.plate.closes"};
    rule.trigger = objectTrigger(maps::WorldTriggerKind::objectDeactivated, {103});
    rule.conditions.push_back(objectCondition(maps::WorldConditionKind::objectInactive, {103}));
    rule.actions.push_back(doorAction({105}, gameplay::DoorState::closed));
    map.worldRules.push_back(rule);
    return map;
}

void testPhase16InteractiveWorld() {
    namespace content = underworld::game::content;
    namespace game = underworld::game;
    namespace gameplay = underworld::game::gameplay;
    namespace maps = underworld::game::maps;
    namespace save = underworld::game::save;
    namespace simulation = underworld::simulation;

    auto authoredContent = makePhase16Content();
    const auto contentJson = content::encodeAuthoredContentJson(authoredContent);
    const auto decodedContent = content::decodeAuthoredContentJson(contentJson);
    const auto decodedLever = decodedContent.content ? std::find_if(
        decodedContent.content->objects.begin(), decodedContent.content->objects.end(),
        [](const auto& value) { return value.id == simulation::DefinitionId{"object.phase16.lever.a"}; }) :
        std::vector<content::AuthoredWorldObject>::const_iterator{};
    const auto decodedPlate = decodedContent.content ? std::find_if(
        decodedContent.content->objects.begin(), decodedContent.content->objects.end(),
        [](const auto& value) { return value.id == simulation::DefinitionId{"object.phase16.plate"}; }) :
        std::vector<content::AuthoredWorldObject>::const_iterator{};
    expect(decodedContent.content && decodedContent.diagnostics.empty() &&
               contentJson.find("\"version\"") != std::string::npos &&
               decodedLever != decodedContent.content->objects.end() && decodedLever->activation &&
               decodedLever->activation->mode == gameplay::ObjectActivationMode::interactToggle &&
               decodedPlate != decodedContent.content->objects.end() && decodedPlate->activation &&
               decodedPlate->activation->mode == gameplay::ObjectActivationMode::playerPressure &&
               decodedPlate->activation->activationBounds.has_value(),
           "Content JSON v5 roundtrips interact-toggle and player-pressure capabilities");
    expect(decodedContent.content &&
               content::encodeAuthoredContentJson(*decodedContent.content) == contentJson,
           "Content JSON v5 has deterministic encode-decode-encode output");

    const auto replaceVersion = [](std::string& json, std::uint64_t version) {
        const auto key = json.find("\"version\"");
        const auto colon = key == std::string::npos ? std::string::npos : json.find(':', key);
        const auto digit = colon == std::string::npos ? std::string::npos :
            json.find_first_of("0123456789", colon + 1);
        if (digit != std::string::npos) json.replace(digit, 1, std::to_string(version));
    };
    auto contentV3 = contentJson;
    replaceVersion(contentV3, 3);
    expect(!content::decodeAuthoredContentJson(contentV3).content,
           "Content v3 rejects the activation capability instead of reinterpreting it");
    const auto phase16Object = [](auto& objects, std::string_view id) -> auto* {
        const auto found = std::find_if(objects.begin(), objects.end(), [&](const auto& value) {
            return value.id == simulation::DefinitionId{std::string{id}};
        });
        return found == objects.end() ? nullptr : &*found;
    };
    auto invalidContent = authoredContent;
    if (auto* plate = phase16Object(invalidContent.objects, "object.phase16.plate")) {
        plate->activation->activationBounds.reset();
    }
    expect(!content::compileContent(invalidContent).registry,
           "content validation rejects a pressure plate without activation bounds");
    invalidContent = authoredContent;
    if (auto* lever = phase16Object(invalidContent.objects, "object.phase16.lever.a")) {
        lever->activation->mode = static_cast<gameplay::ObjectActivationMode>(99);
    }
    expect(!content::compileContent(invalidContent).registry,
           "content validation rejects an unknown activation mode");
    invalidContent = authoredContent;
    if (auto* lever = phase16Object(invalidContent.objects, "object.phase16.lever.a")) {
        lever->interactable.reset();
    }
    expect(!content::compileContent(invalidContent).registry,
           "content validation rejects an interact-toggle without interaction capability");
    auto unknownModeJson = contentJson;
    const auto modeToken = unknownModeJson.find("interactToggle");
    if (modeToken != std::string::npos) {
        unknownModeJson.replace(modeToken, std::string{"interactToggle"}.size(), "unknownMode");
    }
    expect(!content::decodeAuthoredContentJson(unknownModeJson).content,
           "content decoder rejects an unknown activation mode");

    const auto mixedRoot = std::filesystem::temp_directory_path() / "underworld_phase16_content";
    std::error_code fsError;
    std::filesystem::remove_all(mixedRoot, fsError);
    std::filesystem::create_directories(mixedRoot, fsError);
    std::ofstream v1(mixedRoot / "legacy-v1.json", std::ios::binary);
    v1 << R"({"format":"dungeon-underworld-content","version":1,"items":[]})";
    std::ofstream v4(mixedRoot / "activation-v4.json", std::ios::binary);
    v4 << contentJson;
    v1.close();
    v4.close();
    const auto mixed = content::loadContentWorkspaceDirectory(mixedRoot);
    expect(mixed.workspace && mixed.diagnostics.empty() && mixed.sourceFileCount == 2 &&
               mixed.workspace->sources.find("objects", {"object.phase16.lever.a"}) != nullptr,
           "workspace merge accepts legacy and v4 content while retaining activation provenance");
    std::filesystem::remove_all(mixedRoot, fsError);

    const auto compiledContent = content::compileContent(authoredContent);
    if (!compiledContent.registry) {
        expect(false, "Phase 16 content fixture compiles");
        return;
    }
    const auto decodedCompiled = decodedContent.content
        ? content::compileContent(*decodedContent.content)
        : content::ContentCompileResult{};
    const auto* directLever = compiledContent.registry->objects().find(
        simulation::DefinitionId{"object.phase16.lever.a"});
    const auto* decodedLeverDefinition = decodedCompiled.registry
        ? decodedCompiled.registry->objects().find(
              simulation::DefinitionId{"object.phase16.lever.a"})
        : nullptr;
    expect(decodedCompiled.registry && directLever && decodedLeverDefinition &&
               directLever->activation == decodedLeverDefinition->activation,
           "direct and decoded registries preserve activation capability equivalence");
    const auto map = makePhase16Map();
    const auto source = maps::authoredMapFromMapData(map);
    const auto mapJson = maps::encodeAuthoredMapJson(source);
    const auto decodedMap = maps::decodeAuthoredMapJson(mapJson);
    const auto objectPlacementsEqual = [](const auto& left, const auto& right) {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index) {
            const auto& a = left[index];
            const auto& b = right[index];
            if (!(a.id == b.id) || !(a.definitionId == b.definitionId) ||
                !(a.position == b.position) || a.initialContents.size() != b.initialContents.size()) {
                return false;
            }
            for (std::size_t stackIndex = 0; stackIndex < a.initialContents.size(); ++stackIndex) {
                if (!(a.initialContents[stackIndex].itemId == b.initialContents[stackIndex].itemId) ||
                    a.initialContents[stackIndex].quantity != b.initialContents[stackIndex].quantity) {
                    return false;
                }
            }
        }
        return true;
    };
    expect(decodedMap.source && decodedMap.diagnostics.empty() &&
               maps::encodeAuthoredMapJson(*decodedMap.source) == mapJson &&
               objectPlacementsEqual(decodedMap.source->geometry.objects, source.geometry.objects) &&
               decodedMap.source->worldRules == source.worldRules,
           "UMAP v3 roundtrips activation placements and rules deterministically");
    auto legacyMapJson = mapJson;
    replaceVersion(legacyMapJson, 2);
    expect(!maps::decodeAuthoredMapJson(legacyMapJson).source,
           "UMAP v2 rejects object activation rule kinds without reinterpretation");

    const auto& registry = *compiledContent.registry;
    const auto validation = game::mapValidationCatalogs(registry);
    auto invalidActivationMap = source;
    invalidActivationMap.worldRules.front().trigger.instanceTarget = {999};
    const auto invalidActivationCompile = maps::compileAuthoredMap(invalidActivationMap, registry);
    expect(!invalidActivationCompile.map && !invalidActivationCompile.diagnostics.empty() &&
               invalidActivationCompile.diagnostics.front().path == "worldRules[0].trigger",
           "map activation validation reports the authored world-rule trigger path");
    const auto compiledMap = maps::compileAuthoredMap(*decodedMap.source, registry);
    expect(compiledMap.map && maps::semanticallyEqual(*compiledMap.map, map) &&
               compiledMap.map->objects.data() != map.objects.data(),
           "MapCompiler constructs a fresh MapData from the authored activation source");
    if (!compiledMap.map) return;
    const auto dmapBytes = maps::serializeDmap(*compiledMap.map);
    const auto loadedDmap = maps::deserializeDmap(dmapBytes, &validation);
    expect(loadedDmap && loadedDmap.data.worldRules == map.worldRules &&
               maps::semanticallyEqual(loadedDmap.data, map),
           "DMAP 1.4 roundtrips activation rules and preserves the compiled map");
    auto legacyDmap = dmapBytes;
    if (legacyDmap.size() >= 8) { legacyDmap[6] = 3; legacyDmap[7] = 0; }
    expect(!maps::deserializeDmap(legacyDmap, &validation),
           "DMAP 1.3 rejects newer activation rule kinds explicitly");

    simulation::EntityHandlePool handles;
    const std::array visuals{gameplay::creatures::soldierVisualId(),
                             gameplay::creatures::skullVisualId()};
    gameplay::creatures::EnemyFactory enemies(handles, registry.enemies(), registry.behaviors(),
                                              registry.attacks(), registry.projectiles(), visuals);
    gameplay::WorldObjectFactory objects(handles, registry.objects(), registry.items());
    game::RuntimeTilesetCatalog runtimeTilesets(registry.tilesets());
    maps::RuntimeWorldBuilder builder(validation, enemies, objects, handles, runtimeTilesets);
    const auto world = builder.build(map, simulation::SpawnId{"entry.start"});
    expect(world && world.world->objectActivation({101}) == std::optional<bool>{false} &&
               world.world->objectActivation({103}) == std::optional<bool>{false},
           "RuntimeWorld creates activation-capable instances with deterministic initial state");
    if (world) {
        simulation::EventBuffer activationEvents;
        world.world->updatePressureActivations({48, 24}, activationEvents);
        expect(activationEvents.size() == 1 &&
                   std::get<simulation::ObjectActivationChanged>(activationEvents.events().front()).objectInstanceId ==
                       simulation::PersistentInstanceId{103},
               "pressure evaluation emits one ordered activation on the first inside tick");
        activationEvents.clear();
        world.world->updatePressureActivations({48, 24}, activationEvents);
        expect(activationEvents.size() == 0, "pressure activation does not emit duplicate events while stationary");
        world.world->updatePressureActivations({16, 24}, activationEvents);
        expect(activationEvents.size() == 1 &&
                   !std::get<simulation::ObjectActivationChanged>(activationEvents.events().front()).active,
               "pressure activation emits one deactivation after the player leaves");
    }

    const auto dmapPath = std::filesystem::temp_directory_path() / "underworld_phase16_puzzle.dmap";
    std::filesystem::remove(dmapPath, fsError);
    std::string ioError;
    const bool dmapWritten = maps::writeDmap(dmapPath, map, ioError);
    maps::MapCatalog mapCatalog;
    if (dmapWritten) mapCatalog.add(map.id, dmapPath);
    game::GameSession session({0}, testProgression());
    session.configureItems(registry.items());
    session.configureNarrative(registry.dialogues(), registry.quests());
    session.configureRewards(registry.rewardProfiles(), registry.pickups());
    session.configureRewardGrants(registry.rewardGrants());
    std::string sessionError;
    const bool initialized = dmapWritten &&
        session.initializeMap(mapCatalog, validation, builder, map.id,
                              simulation::SpawnId{"entry.start"}, sessionError);
    expect(initialized, "GameSession initializes the compiled activation puzzle map");
    if (initialized) {
        session.tick(movementCommand(1, 0, 0));
        auto interact = movementCommand(2, 0, 0);
        interact.actions.interactPressed = true;
        session.tick(interact);
        const auto activationEventCount = std::count_if(
            session.events().events().begin(), session.events().events().end(),
            [](const auto& event) {
                const auto* activation = std::get_if<simulation::ObjectActivationChanged>(&event);
                return activation != nullptr && activation->objectInstanceId == simulation::PersistentInstanceId{101};
            });
        expect(session.world().objectActivation({101}) == std::optional<bool>{true} &&
                   session.world().doorState({104}) == gameplay::DoorState::closed &&
                   activationEventCount == 1,
               "first lever interaction toggles once and keeps the AND door closed");

        session.relocatePlayer({16, 8}, gameplay::FacingDirection::down);
        interact = movementCommand(3, 0, 0);
        interact.actions.interactPressed = true;
        session.tick(interact);
        expect(session.world().objectActivation({102}) == std::optional<bool>{true} &&
                   session.world().doorState({104}) == gameplay::DoorState::open &&
                   !session.world().map().collision().isSolid(2, 1),
               "second lever satisfies the authored AND rules and opens the main door collision");

        session.relocatePlayer({16, 24}, gameplay::FacingDirection::down);
        interact = movementCommand(4, 0, 0);
        interact.actions.interactPressed = true;
        session.tick(interact);
        expect(session.world().objectActivation({101}) == std::optional<bool>{false} &&
                   session.world().doorState({104}) == gameplay::DoorState::closed &&
                   session.world().map().collision().isSolid(2, 1),
               "deactivating one lever closes the door through objectDeactivated World Logic");

        session.relocatePlayer({48, 24}, gameplay::FacingDirection::left);
        session.tick(movementCommand(5, 0, 0));
        expect(session.world().objectActivation({103}) == std::optional<bool>{true} &&
                   session.world().doorState({105}) == gameplay::DoorState::open,
               "player pressure activates its authored exit-door rule");
        session.relocatePlayer({16, 24}, gameplay::FacingDirection::down);
        session.tick(movementCommand(6, 0, 0));
        expect(session.world().objectActivation({103}) == std::optional<bool>{false} &&
                   session.world().doorState({105}) == gameplay::DoorState::closed,
               "leaving a pressure plate closes its authored door without repeated events");

        // Return both persistent switches to active and prove that a prior
        // activation delta is removed when a toggle returns to its authored state.
        interact = movementCommand(7, 0, 0);
        interact.actions.interactPressed = true;
        session.tick(interact);
        session.relocatePlayer({16, 8}, gameplay::FacingDirection::down);
        interact = movementCommand(8, 0, 0);
        interact.actions.interactPressed = true;
        session.tick(interact);
        auto toggledOff = session.captureSaveData();
        session.relocatePlayer({16, 24}, gameplay::FacingDirection::down);
        interact = movementCommand(9, 0, 0);
        interact.actions.interactPressed = true;
        session.tick(interact);
        const auto afterToggleOff = session.captureSaveData();
        expect(!afterToggleOff.world.findObject({map.id, {101}}),
               "returning a toggle to its authored state removes its stale save delta");

        // Reactivate A so the saved state represents the completed two-switch
        // puzzle, while the pressure plate remains derived and unsaved.
        interact = movementCommand(10, 0, 0);
        interact.actions.interactPressed = true;
        session.tick(interact);
        session.relocatePlayer({16, 8}, gameplay::FacingDirection::down);
        interact = movementCommand(11, 0, 0);
        interact.actions.interactPressed = true;
        session.tick(interact);
        const auto saved = session.captureSaveData();
        const auto savedLever = saved.world.findObject({map.id, {101}});
        const auto savedPlate = saved.world.findObject({map.id, {103}});
        const auto saveCatalogs = save::SaveValidationCatalogs{
            &registry.items(), {&map}, &registry.quests(), &registry.progressions(), &registry.objects()};
        const auto savedBytes = save::serializeSave(saved);
        const auto loadedSave = save::deserializeSave(savedBytes, saveCatalogs);
        expect(savedLever && savedLever->activationState == std::optional<bool>{true} &&
                   savedPlate == nullptr && loadedSave && savedBytes == save::serializeSave(loadedSave.data),
               "DSAV 1.8 persists toggle activation but never persists derived pressure state");
        auto legacySave = saved;
        legacySave.world.objects.clear();
        auto legacyBytes = save::serializeSave(legacySave);
        if (legacyBytes.size() >= 8) { legacyBytes[6] = 7; legacyBytes[7] = 0; }
        expect(static_cast<bool>(save::deserializeSave(legacyBytes, saveCatalogs)),
               "DSAV 1.7 remains readable when no activation delta is present");

        if (loadedSave) {
            game::GameSession restored({0}, testProgression());
            restored.configureItems(registry.items());
            restored.configureNarrative(registry.dialogues(), registry.quests());
            restored.configureRewards(registry.rewardProfiles(), registry.pickups());
            restored.configureRewardGrants(registry.rewardGrants());
            std::string restoreError;
            const bool restoredOk = restored.initializeMap(
                mapCatalog, validation, builder, map.id, simulation::SpawnId{"entry.start"}, restoreError) &&
                restored.restoreSaveData(loadedSave.data, restoreError);
            expect(restoredOk && restored.world().objectActivation({101}) == std::optional<bool>{true} &&
                       restored.world().objectActivation({102}) == std::optional<bool>{true} &&
                       restored.world().doorState({104}) == gameplay::DoorState::open &&
                       restored.world().objectActivation({103}) == std::optional<bool>{false},
                   "save/load restores persistent switches and door state while recalculating pressure state");
        }
    }
    std::filesystem::remove(dmapPath, fsError);
}

class SyntheticVisualDecoder final : public underworld::platform::ImageDecoder {
public:
    underworld::core::ImageData decode(const std::filesystem::path& path) override {
        paths.push_back(path);
        return {64, 64, 64U * 4U, std::vector<std::uint8_t>(64U * 64U * 4U, 255)};
    }

    std::vector<std::filesystem::path> paths;
};

class FailingVisualDecoder final : public underworld::platform::ImageDecoder {
public:
    underworld::core::ImageData decode(const std::filesystem::path&) override {
        throw std::runtime_error("bad png");
    }
};

underworld::game::content::AuthoredContentPack makePhase17VisualContent() {
    namespace content = underworld::game::content;
    namespace gameplay = underworld::game::gameplay;
    namespace presentation = underworld::game::presentation;
    namespace core = underworld::core;
    namespace simulation = underworld::simulation;
    content::AuthoredContentPack pack;
    pack.visualImages.push_back({{"image.external.character"},
                                 presentation::VisualAssetRoot::contentWorkspace,
                                 "assets/custom-character.png"});
    const auto animation = [](const char* id, std::uint32_t duration = 2) {
        content::AuthoredAnimation value;
        value.id = {id};
        value.imageId = {"image.external.character"};
        value.frames.push_back({{0, 0, 16, 16}, {8, 15}, {1, 0}, duration, {"frame"}});
        value.loop = true;
        return value;
    };
    pack.animations.push_back(animation("anim.external.idle"));
    pack.animations.push_back(animation("anim.external.move"));
    pack.animations.push_back(animation("anim.external.attack"));
    content::AuthoredStaticSprite sprite;
    sprite.id = {"visual.external.item"};
    sprite.imageId = {"image.external.character"};
    sprite.source = core::RectI{16, 16, 16, 16};
    sprite.anchor = {8, 8};
    pack.staticSprites.push_back(sprite);

    presentation::DirectionalAnimationRef idle;
    idle.defaultAnimation = simulation::DefinitionId{"anim.external.idle"};
    presentation::DirectionalAnimationRef move;
    move.down = simulation::DefinitionId{"anim.external.move"};
    presentation::DirectionalAnimationRef attack;
    attack.defaultAnimation = simulation::DefinitionId{"anim.external.attack"};
    content::AuthoredEnemyVisual enemyVisual;
    enemyVisual.id = {"visual.enemy.external"};
    enemyVisual.idle = idle;
    enemyVisual.move = move;
    enemyVisual.attacks.push_back({{"attack.external"}, attack});
    pack.enemyVisuals.push_back(enemyVisual);
    pack.behaviors.push_back({{"behavior.external"}, 96, 128, 30, 30});
    gameplay::DirectionalBoxes externalBoxes{};
    for (auto& box : externalBoxes.values) box = {0, 0, 1, 1};
    content::AuthoredAttack externalAttack;
    externalAttack.id = {"attack.external"};
    externalAttack.damage = {1, 0};
    externalAttack.totalTicks = 2;
    externalAttack.maximumRangePixels = 32;
    externalAttack.visualActionId = {"attack.external"};
    externalAttack.meleeHitboxes = externalBoxes;
    pack.attacks.push_back(externalAttack);
    pack.enemies.push_back({{"enemy.external"}, {"visual.enemy.external"},
                            {"behavior.external"}, gameplay::Faction::enemy, 12, 128,
                            {-4, -8, 8, 8}, {-6, -20, 12, 20}, {{"attack.external"}}, std::nullopt});

    content::AuthoredWorldObjectVisual objectVisual;
    objectVisual.id = {"visual.object.external"};
    objectVisual.idleAnimationId = {"anim.external.idle"};
    objectVisual.activationInactiveAnimationId = {"anim.external.idle"};
    objectVisual.activationActiveAnimationId = {"anim.external.move"};
    pack.objectVisuals.push_back(objectVisual);

    content::AuthoredNpcVisualSet npcVisual;
    npcVisual.id = {"visual.npc.external"};
    npcVisual.markerColor = {12, 34, 56, 255};
    npcVisual.idle = idle;
    pack.npcVisuals.push_back(npcVisual);
    return pack;
}

void testPhase17VisualContentBoundary() {
    namespace content = underworld::game::content;
    namespace gameplay = underworld::game::gameplay;
    namespace presentation = underworld::game::presentation;
    namespace game = underworld::game;
    namespace simulation = underworld::simulation;
    const presentation::VisualContentDiagnostic formattedDiagnostic{
        presentation::VisualContentDiagnosticStage::decode,
        "image_decode_failed",
        {"image.enemy.custom"},
        presentation::VisualAssetRoot::contentWorkspace,
        "assets/enemy/custom.png",
        "bad png"};
    const auto formatted = presentation::formatVisualContentDiagnostic(formattedDiagnostic);
    expect(formatted.find("decode") != std::string::npos &&
               formatted.find("image_decode_failed") != std::string::npos &&
               formatted.find("image.enemy.custom") != std::string::npos &&
               formatted.find("contentWorkspace") != std::string::npos &&
               formatted.find("assets/enemy/custom.png") != std::string::npos &&
               formatted.find("bad png") != std::string::npos,
           "visual diagnostic formatter preserves stage, code, definition, root, path and message");
    const presentation::VisualContentDiagnostic invalidPathDiagnostic{
        presentation::VisualContentDiagnosticStage::resolve,
        "invalid_asset_path",
        {"image.enemy.custom"},
        presentation::VisualAssetRoot::contentWorkspace,
        "../outside.png",
        "visual asset path must be a normalized relative path"};
    const auto invalidPathFormatted = presentation::formatVisualContentDiagnostic(
        invalidPathDiagnostic);
    expect(invalidPathFormatted.find("invalid_asset_path") != std::string::npos &&
               invalidPathFormatted.find("image.enemy.custom") != std::string::npos &&
               invalidPathFormatted.find("contentWorkspace") != std::string::npos &&
               invalidPathFormatted.find("../outside.png") != std::string::npos,
           "visual diagnostic formatter preserves invalid asset path context");
    const auto authored = makePhase17VisualContent();
    const auto json = content::encodeAuthoredContentJson(authored);
    const auto decoded = content::decodeAuthoredContentJson(json);
    expect(decoded.content && decoded.diagnostics.empty() &&
               json.find("\"version\": 5") != std::string::npos &&
               decoded.content->visualImages.size() == 1 &&
               decoded.content->animations.size() == 3 &&
               decoded.content->enemyVisuals.front().idle.defaultAnimation &&
               decoded.content->enemyVisuals.front().move->down,
           "Content JSON v5 decodes flexible visual definitions and partial directions");
    expect(decoded.content && content::encodeAuthoredContentJson(*decoded.content) == json,
           "Content JSON v5 visual content has deterministic roundtrip encoding");

    const auto compiled = content::compileContent(authored);
    expect(compiled.registry && compiled.registry->visualImages().find(
               {"image.external.character"}) && compiled.registry->staticSprites().find(
               {"visual.external.item"}) && compiled.registry->animations().find(
               {"anim.external.idle"}) && compiled.registry->enemyVisuals().find(
               {"visual.enemy.external"}) && compiled.registry->objectVisuals().find(
               {"visual.object.external"}) &&
               compiled.registry->enemies().find({"enemy.external"}),
               "authored visual categories cross validation, compilation and registry catalogs");

    if (!compiled.registry) return;
    SyntheticVisualDecoder decoder;
    presentation::VisualContentLoader loader(decoder);
    const auto workspaceRoot = std::filesystem::temp_directory_path() /
        "underworld_phase17_visual_workspace";
    std::error_code fsError;
    std::filesystem::remove_all(workspaceRoot, fsError);
    std::filesystem::create_directories(workspaceRoot, fsError);
    const auto loaded = loader.load(*compiled.registry,
                                    {workspaceRoot / "game-assets", workspaceRoot});
    expect(loaded && decoder.paths.size() == 1 &&
               decoder.paths.front() == workspaceRoot / "assets/custom-character.png" &&
               loaded.content->staticSprites.find({"visual.external.item"}) &&
               loaded.content->animations.find({"anim.external.idle"}) &&
               loaded.content->enemies.find({"visual.enemy.external"}) &&
               &loaded.content->objects.require({"visual.object.external"}) &&
               loaded.content->npcs.find({"visual.npc.external"}),
           "VisualContentLoader resolves an external workspace image once and builds runtime catalogs");
    if (loaded) {
        const auto& enemy = loaded.content->enemies.require({"visual.enemy.external"});
        expect(enemy.walk[0]->id() == "anim.external.move" &&
                   enemy.walk[1]->id() == "anim.external.move" &&
                   enemy.death[2]->id() == "anim.external.idle" &&
                   enemy.attacks.at({"attack.external"})[2]->id() == "anim.external.attack",
               "visual loader applies default and deterministic directional fallbacks without requiring optional states");
        simulation::EntityHandlePool handles;
        gameplay::creatures::EnemyFactory factory(compiled.registry->enemies(),
            compiled.registry->behaviors(), compiled.registry->attacks(),
            compiled.registry->projectiles());
        auto instance = factory.create(handles, {"enemy.external"}, {24, 24});
        game::EnemyVisualInstance visual(instance.handle(), enemy);
        visual.update(instance, 0);
        expect(visual.animator().clip().id() == "anim.external.idle" &&
                   visual.animator().hasClip(),
               "external enemy gameplay instance resolves its authored flexible idle visual without GameRuntime changes");
    }

    auto traversal = authored;
    traversal.visualImages.front().relativePath = "../outside.png";
    expect(content::compileContent(traversal).report.hasErrors(),
           "content validation rejects visual asset path traversal");
    auto duplicate = authored;
    duplicate.visualImages.push_back(duplicate.visualImages.front());
    expect(content::compileContent(duplicate).report.hasErrors(),
           "content validation rejects duplicate visual image definitions");
    auto missingItemSprite = authored;
    missingItemSprite.items.push_back({{"item.external"}, {"visual.missing"},
                                       gameplay::ItemCategory::misc, 1, std::nullopt,
                                       std::nullopt});
    expect(content::compileContent(missingItemSprite).report.hasErrors(),
           "content validation rejects item references to unavailable static sprites");
    auto outOfBounds = authored;
    outOfBounds.animations.front().frames.front().source = {60, 60, 8, 8};
    const auto outOfBoundsRegistry = content::compileContent(outOfBounds);
    const auto boundsLoaded = outOfBoundsRegistry.registry ? loader.load(
        *outOfBoundsRegistry.registry, {workspaceRoot / "game-assets", workspaceRoot}) :
        presentation::VisualContentLoadResult{};
    expect(!boundsLoaded && std::any_of(boundsLoaded.diagnostics.begin(),
               boundsLoaded.diagnostics.end(), [](const auto& value) {
                   return value.code == "frame_out_of_bounds";
               }),
           "VisualContentLoader rejects animation frames outside decoded image bounds");
    FailingVisualDecoder failingDecoder;
    presentation::VisualContentLoader failingLoader(failingDecoder);
    const auto decodeFailure = failingLoader.load(
        *compiled.registry, {workspaceRoot / "game-assets", workspaceRoot});
    const auto* decodeDiagnostic = decodeFailure.diagnostics.empty()
        ? nullptr : &decodeFailure.diagnostics.front();
    expect(!decodeFailure && decodeDiagnostic &&
               decodeDiagnostic->code == "image_decode_failed" &&
               decodeDiagnostic->definitionId == simulation::DefinitionId{"image.external.character"} &&
               decodeDiagnostic->assetRoot &&
               *decodeDiagnostic->assetRoot == presentation::VisualAssetRoot::contentWorkspace &&
               decodeDiagnostic->relativePath == "assets/custom-character.png" &&
               presentation::formatVisualContentDiagnostic(*decodeDiagnostic).find("bad png") !=
                   std::string::npos,
           "VisualContentLoader preserves external image context on decoder failure");
    auto missingRoot = authored;
    missingRoot.visualImages.front().root = presentation::VisualAssetRoot::contentWorkspace;
    const auto missingRootRegistry = content::compileContent(missingRoot);
    presentation::VisualContentLoader missingRootLoader(decoder);
    const auto missingRootResult = missingRootRegistry.registry ? missingRootLoader.load(
        *missingRootRegistry.registry, {workspaceRoot / "game-assets", std::nullopt}) :
        presentation::VisualContentLoadResult{};
    const auto* missingRootDiagnostic = missingRootResult.diagnostics.empty()
        ? nullptr : &missingRootResult.diagnostics.front();
    expect(!missingRootResult && std::any_of(missingRootResult.diagnostics.begin(),
               missingRootResult.diagnostics.end(), [](const auto& value) {
                   return value.code == "workspace_root_missing";
               }),
           "VisualContentLoader reports a missing content workspace root explicitly");
    expect(missingRootDiagnostic && missingRootDiagnostic->definitionId ==
               simulation::DefinitionId{"image.external.character"} &&
               missingRootDiagnostic->assetRoot &&
               *missingRootDiagnostic->assetRoot == presentation::VisualAssetRoot::contentWorkspace &&
               missingRootDiagnostic->relativePath == "assets/custom-character.png" &&
               presentation::formatVisualContentDiagnostic(*missingRootDiagnostic).find(
                   "workspace_root_missing") != std::string::npos,
           "missing workspace diagnostic retains visual definition context");

    std::filesystem::remove_all(workspaceRoot, fsError);
}

void testPhase18ContentStudioFoundation() {
    namespace content = underworld::game::content;
    namespace editor = underworld::editor;
    namespace presentation = underworld::game::presentation;
    namespace simulation = underworld::simulation;
    namespace core = underworld::core;
    const auto root = std::filesystem::temp_directory_path() / "underworld_phase18_content_studio";
    std::error_code fsError;
    std::filesystem::remove_all(root, fsError);
    std::filesystem::create_directories(root / "visuals", fsError);

    const auto writePack = [](const std::filesystem::path& path, const content::AuthoredContentPack& pack) {
        std::ofstream file(path, std::ios::binary);
        file << content::encodeAuthoredContentJson(pack);
        return file.good();
    };
    const auto readBytes = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };

    { std::ofstream legacy(root / "base-v4.json", std::ios::binary);
      legacy << R"({"format":"dungeon-underworld-content","version":4})"; }
    content::AuthoredContentPack initial;
    initial.visualImages.push_back({{"image.studio.initial"}, presentation::VisualAssetRoot::contentWorkspace,
                                    "assets/initial.png"});
    const auto visualsPath = root / "visuals" / "visuals-v5.json";
    expect(writePack(visualsPath, initial), "18A writes initial content studio fixture");
    const auto untouchedBefore = readBytes(root / "base-v4.json");

    std::string error;
    auto document = editor::ContentWorkspaceDocument::open(root, error);
    expect(document && document->files().size() == 2 && document->valid() && error.empty(),
           "18A opens mixed-version workspace with derived valid registry");
    if (!document) { std::filesystem::remove_all(root, fsError); return; }
    const editor::ContentDefinitionKey imageKey{editor::ContentDefinitionKind::visualImage,
                                                 {"image.studio.initial"}};
    const auto* imageSource = document->sourceFor(imageKey);
    expect(imageSource && imageSource->sourcePath == visualsPath.lexically_normal() &&
               imageSource->jsonPath == "visualImages[0]",
           "18A preserves definition ownership and source location");
    expect(editor::ContentWorkspaceDocument::categoryOrder().size() == 25 &&
               document->index().front() == imageKey,
           "18A exposes all categories and deterministic definition index");

    auto changed = initial.visualImages.front();
    changed.relativePath = "assets/changed.png";
    expect(document->updateVisualImage(changed.id, changed, error) && document->dirty(),
           "18A typed visual image mutation marks only its source file dirty");
    expect(document->saveAll(error) && !document->dirty() &&
               readBytes(root / "base-v4.json") == untouchedBefore,
           "18A Save All writes dirty files only and keeps untouched legacy bytes unchanged");
    auto reloaded = editor::ContentWorkspaceDocument::open(root, error);
    expect(reloaded && reloaded->mergedAuthored() && reloaded->mergedAuthored()->visualImages.front().relativePath == "assets/changed.png",
           "18A save/reload persists structured visual edits");

    expect(document->createContentFile("visuals/slime.json", error),
           "18A creates a new content source file inside workspace");
    const auto slimeFile = root / "visuals" / "slime.json";
    expect(!document->createContentFile("../outside.json", error) &&
               !document->createContentFile("/outside.json", error) &&
               !document->createContentFile("C:/outside.json", error),
           "18A rejects content file path traversal and absolute paths");
    std::error_code symlinkError;
    const auto symlinkPath = root / "visuals" / "linked";
    std::filesystem::create_directory_symlink(root.parent_path() / "outside-content-target",
                                              symlinkPath, symlinkError);
    if (!symlinkError) {
        expect(!document->createContentFile("visuals/linked/escape.json", error),
               "18A rejects content files crossing a workspace symlink");
    }

    content::AuthoredVisualImage slimeImage{{"image.studio.slime"}, presentation::VisualAssetRoot::contentWorkspace,
                                            "assets/slime.png"};
    expect(document->addVisualImage(slimeFile, slimeImage, error),
           "18A adds a typed visual image to the selected source file");
    content::AuthoredAnimationFrame frame{{0, 0, 16, 16}, {8, 15}, {0, 0}, 4, {"idle"}};
    content::AuthoredAnimation idle{{"animation.studio.slime.idle"}, slimeImage.id, {frame}, true};
    content::AuthoredAnimation death{{"animation.studio.slime.death"}, slimeImage.id, {frame}, false};
    expect(document->addAnimation(slimeFile, idle, error) && document->addAnimation(slimeFile, death, error),
           "18A creates typed animation definitions without raw JSON editing");
    expect(document->addAnimationMarker(idle.id, 0, "contact", error) &&
               document->updateAnimationMarker(idle.id, 0, 1, "contact.updated", error),
           "18A edits animation markers through typed document operations");
    auto secondFrame = frame;
    secondFrame.durationTicks = 6;
    expect(document->addAnimationFrame(idle.id, secondFrame, error) &&
               document->moveAnimationFrame(idle.id, 1, 0, error) &&
               document->updateAnimationFrame(idle.id, 0, secondFrame, error) &&
               document->removeAnimationFrame(idle.id, 1, error),
           "18A supports animation frame add/edit/reorder/remove operations");
    presentation::DirectionalAnimationRef idleBinding;
    idleBinding.defaultAnimation = idle.id;
    presentation::DirectionalAnimationRef deathBinding;
    deathBinding.defaultAnimation = death.id;
    content::AuthoredEnemyVisual slimeVisual;
    slimeVisual.id = {"visual.enemy.studio.slime"};
    slimeVisual.idle = idleBinding;
    slimeVisual.death = deathBinding;
    expect(document->addEnemyVisual(slimeFile, slimeVisual, error),
           "18A authors a Slime-like idle-plus-death flexible profile");

    content::AuthoredAnimation richAction{{"animation.studio.sleep"}, slimeImage.id, {frame}, true};
    expect(document->addAnimation(slimeFile, richAction, error), "18A creates optional profile action animation");
    content::AuthoredEnemyAttackVisual sleepAction;
    sleepAction.visualActionId = {"sleep"};
    sleepAction.clips.defaultAnimation = richAction.id;
    expect(document->addEnemyVisualAction(slimeVisual.id, sleepAction, error),
           "18A authors arbitrary EnemyVisual action mappings");
    expect(document->addStaticSprite(slimeFile, {{"visual.studio.icon"}, slimeImage.id, std::nullopt, {8, 8}}, error),
           "18A creates a typed static sprite definition");
    auto staticEdited = content::AuthoredStaticSprite{{"visual.studio.icon"}, slimeImage.id,
                                                       core::RectI{2, 3, 8, 9}, {4, 5}};
    const auto staticRevision = document->revision();
    expect(document->updateStaticSprite(staticEdited.id, staticEdited, error) &&
               document->revision() > staticRevision && document->mergedAuthored() &&
               document->mergedAuthored()->staticSprites.front().source == staticEdited.source &&
               document->mergedAuthored()->staticSprites.front().anchor == staticEdited.anchor,
           "18A edits a StaticSprite source rectangle and anchor through the document API");
    staticEdited.source.reset();
    expect(document->updateStaticSprite(staticEdited.id, staticEdited, error) &&
               document->mergedAuthored() && !document->mergedAuthored()->staticSprites.front().source,
           "18A disables a StaticSprite source rectangle to restore full-image semantics");
    staticEdited.source = core::RectI{2, 3, 8, 9};
    expect(document->updateStaticSprite(staticEdited.id, staticEdited, error),
           "18A restores an optional StaticSprite source rectangle without raw JSON editing");
    const auto fullFrame = content::AuthoredAnimationFrame{{3, 4, 10, 11}, {5, 6}, {7, 8}, 9, {"full"}};
    expect(document->updateAnimationFrame(idle.id, 0, fullFrame, error) && document->mergedAuthored() &&
               document->mergedAuthored()->animations.front().frames.front().source == fullFrame.source &&
               document->mergedAuthored()->animations.front().frames.front().anchor == fullFrame.anchor &&
               document->mergedAuthored()->animations.front().frames.front().drawOffset == fullFrame.drawOffset &&
               document->mergedAuthored()->animations.front().frames.front().durationTicks == fullFrame.durationTicks &&
               document->mergedAuthored()->animations.front().frames.front().markers == fullFrame.markers,
           "18A edits every authored Animation frame field through the typed API");
    const auto markerIndex = fullFrame.markers.size();
    expect(document->addAnimationMarker(idle.id, 0, "temporary", error) &&
               document->updateAnimationMarker(idle.id, 0, markerIndex, "temporary.edited", error) &&
               document->removeAnimationMarker(idle.id, 0, markerIndex, error),
           "18A adds edits and removes an Animation marker through the typed API");

    expect(document->saveAll(error) && std::filesystem::exists(slimeFile) &&
               readBytes(slimeFile).find("\"version\": 5") != std::string::npos,
           "18A saves newly created files using canonical Content JSON v5");
    auto slimeReload = editor::ContentWorkspaceDocument::open(root, error);
    expect(slimeReload && slimeReload->valid() && slimeReload->mergedAuthored() &&
               slimeReload->mergedAuthored()->enemyVisuals.front().death.has_value() &&
               slimeReload->mergedAuthored()->animations.size() == 3,
           "18A reloads the complete structured Slime and optional-action profile");

    content::AuthoredAnimation invalid{{"animation.studio.invalid"}, {"image.missing"}, {frame}, true};
    expect(document->addAnimation(slimeFile, invalid, error) && !document->valid() &&
               !document->compiledRegistry() && !document->diagnostics().empty(),
           "18A allows semantic invalid references while exposing diagnostics and no stale registry");
    const auto invalidDiagnostic = content::formatContentWorkspaceDiagnostic(document->diagnostics().front());
    expect(invalidDiagnostic.find("[validation/") != std::string::npos &&
               invalidDiagnostic.find("category=animations") != std::string::npos &&
               invalidDiagnostic.find("definitionId=animation.studio.invalid") != std::string::npos &&
               invalidDiagnostic.find("imageId") != std::string::npos,
           "18A validation view preserves stage, category, definition ID and JSON path");
    expect(document->saveAll(error), "18A saves structurally valid content even when semantically invalid");
    content::AuthoredVisualImage missingImage{{"image.missing"}, presentation::VisualAssetRoot::contentWorkspace,
                                               "assets/missing.png"};
    expect(document->addVisualImage(slimeFile, missingImage, error) && document->valid(),
           "18A rebuilds the registry after an invalid reference is fixed");

    content::AuthoredVisualImage duplicate = slimeImage;
    expect(document->createContentFile("visuals/duplicates.json", error) &&
               document->addVisualImage(root / "visuals" / "duplicates.json", duplicate, error) &&
               !document->valid() && std::any_of(document->diagnostics().begin(), document->diagnostics().end(),
                   [](const auto& diagnostic) { return diagnostic.code == "duplicate_definition"; }),
           "18A reports cross-file duplicate definitions with a derived invalid workspace");
    expect(!document->createContentFile("visuals/duplicates.json", error),
           "18A rejects duplicate content source file creation");

    auto builtinDocument = editor::ContentWorkspaceDocument::fromBuiltin(content::makeBuiltinAuthoredContent());
    expect(builtinDocument.builtinReadOnly() && builtinDocument.index().size() >= 25 &&
               !builtinDocument.createContentFile("new.json", error),
           "18A exposes builtin content as a navigable read-only document");

    // Map and content documents coexist without sharing mutable source state.
    editor::EditorDocument mapDocument = editor::EditorDocument::newAuthoredMap(
        simulation::MapId{"map.studio.test"}, 8, 8, 16, content::compileBuiltinContentOrThrow());
    expect(mapDocument.data().id.value() == "map.studio.test" && document->files().size() == 4,
           "18A preserves Map mode state while a content workspace remains open");

    std::filesystem::remove_all(root, fsError);
}

void testPhase18StudioVisualValidation() {
    namespace content = underworld::game::content;
    namespace editor = underworld::editor;
    namespace presentation = underworld::game::presentation;
    namespace simulation = underworld::simulation;
    namespace core = underworld::core;
    const auto root = std::filesystem::temp_directory_path() / "underworld_phase18_asset_validation";
    std::error_code fsError;
    std::filesystem::remove_all(root, fsError);
    std::filesystem::create_directories(root, fsError);

    content::AuthoredContentPack pack;
    pack.visualImages.push_back({{"image.studio.validation"},
                                 presentation::VisualAssetRoot::contentWorkspace,
                                 "assets/validation.png"});
    const content::AuthoredAnimationFrame validFrame{{0, 0, 16, 16}, {8, 15}, {0, 0}, 2, {}};
    pack.animations.push_back({{"animation.studio.validation"}, {"image.studio.validation"},
                               {validFrame}, true});
    pack.staticSprites.push_back({{"visual.studio.validation"}, {"image.studio.validation"},
                                  core::RectI{0, 0, 16, 16}, {8, 8}});
    const auto sourcePath = root / "visuals.json";
    std::string error;
    expect(content::writeAuthoredContentJsonFile(sourcePath, pack, error),
           "18A asset validation writes a structured visual fixture");
    auto document = editor::ContentWorkspaceDocument::open(root, error);
    expect(document && document->compiledRegistry(),
           "18A asset validation opens a semantically valid workspace");
    if (!document || !document->compiledRegistry()) {
        std::filesystem::remove_all(root, fsError);
        return;
    }

    SyntheticVisualDecoder decoder;
    editor::EditorApp app(decoder, root / "game-assets", *document->compiledRegistry(),
                          std::move(document));
    app.shellCommand(editor::EditorShellCommand::contentMode);
    decoder.paths.clear();
    expect(app.validateWorkspace() && app.visualValidationAttempted() &&
               app.visualDiagnostics().empty() && decoder.paths.size() == 1 &&
               decoder.paths.front() == root / "assets/validation.png",
           "18A Studio validation reuses VisualContentLoader with the workspace root");
    const auto decodeCount = decoder.paths.size();
    for (int tick = 0; tick < 3; ++tick) app.updateAndRender({});
    expect(decoder.paths.size() == decodeCount,
           "18A visual asset validation is not repeated during render frames");

    const auto invalidFrame = content::AuthoredAnimationFrame{{60, 60, 8, 8}, {0, 0}, {0, 0}, 2, {}};
    expect(app.contentWorkspace()->updateAnimationFrame({"animation.studio.validation"}, 0,
                                                         invalidFrame, error) &&
               !app.validateWorkspace() && !app.visualDiagnostics().empty() &&
               app.visualDiagnostics().front().code == "frame_out_of_bounds" &&
               presentation::formatVisualContentDiagnostic(app.visualDiagnostics().front()).find(
                   "animation.studio.validation") != std::string::npos,
           "18A Studio exposes VisualContentLoader frame bounds diagnostics");
    expect(app.contentWorkspace()->updateAnimationFrame({"animation.studio.validation"}, 0,
                                                         validFrame, error),
           "18A repairs a visual asset frame through the document API");

    content::AuthoredAnimation invalid{{"animation.studio.invalid"}, {"image.missing"},
                                       {validFrame}, true};
    decoder.paths.clear();
    expect(app.contentWorkspace()->addAnimation(sourcePath, invalid, error) &&
               !app.validateWorkspace() && app.visualDiagnostics().empty() &&
               decoder.paths.empty(),
           "18A skips VisualContentLoader when semantic content validation is invalid");
    content::AuthoredVisualImage missing{{"image.missing"},
                                         presentation::VisualAssetRoot::contentWorkspace,
                                         "assets/missing.png"};
    expect(app.contentWorkspace()->addVisualImage(sourcePath, missing, error) &&
               app.validateWorkspace() && app.visualDiagnostics().empty(),
           "18A reruns asset validation after semantic references are fixed");

    auto failingDocument = editor::ContentWorkspaceDocument::open(root, error);
    expect(failingDocument && failingDocument->compiledRegistry(),
           "18A opens the workspace for decoder failure validation");
    if (failingDocument && failingDocument->compiledRegistry()) {
        FailingVisualDecoder failingDecoder;
        editor::EditorApp failingApp(failingDecoder, root / "game-assets",
                                     *failingDocument->compiledRegistry(),
                                     std::move(failingDocument));
        expect(!failingApp.validateWorkspace() && !failingApp.visualDiagnostics().empty() &&
                   failingApp.visualDiagnostics().front().code == "image_decode_failed" &&
                   presentation::formatVisualContentDiagnostic(
                       failingApp.visualDiagnostics().front()).find("validation.png") !=
                       std::string::npos,
               "18A Studio exposes decoder failure context through the shared formatter");
    }
    std::filesystem::remove_all(root, fsError);
}

void testPhase18BVisualPreview() {
    namespace content = underworld::game::content;
    namespace editor = underworld::editor;
    namespace presentation = underworld::game::presentation;
    namespace simulation = underworld::simulation;
    namespace core = underworld::core;
    const auto root = std::filesystem::temp_directory_path() / "underworld_phase18b_preview";
    std::error_code fsError;
    std::filesystem::remove_all(root, fsError);
    std::filesystem::create_directories(root, fsError);
    content::AuthoredContentPack authored;
    authored.visualImages.push_back({{"image.preview"}, presentation::VisualAssetRoot::contentWorkspace,
                                     "assets/preview.png"});
    authored.animations.push_back({{"animation.preview"}, {"image.preview"},
                                   {{{0, 0, 16, 16}, {8, 15}, {2, 3}, 2, {}},
                                    {{16, 0, 16, 16}, {8, 15}, {-1, 0}, 3, {"release"}}}, true});
    authored.animations.push_back({{"animation.preview.death"}, {"image.preview"},
                                   {{{32, 0, 16, 16}, {8, 15}, {0, 0}, 1, {}}}, false});
    authored.animations.push_back({{"animation.preview.action"}, {"image.preview"},
                                   {{{48, 0, 16, 16}, {8, 15}, {0, 0}, 1, {}}}, true});
    authored.staticSprites.push_back({{"sprite.preview"}, {"image.preview"},
                                      core::RectI{16, 0, 16, 16}, {8, 15}});
    content::AuthoredEnemyVisual enemy;
    enemy.id = {"visual.enemy.preview"};
    enemy.idle.defaultAnimation = {"animation.preview"};
    enemy.death = presentation::DirectionalAnimationRef{};
    enemy.death->defaultAnimation = {"animation.preview.death"};
    enemy.attacks.push_back({{"attack.sword"}, {{"animation.preview.action"}, {}, {}, {}}});
    authored.enemyVisuals.push_back(enemy);
    content::AuthoredWorldObjectVisual object;
    object.id = {"visual.object.preview"};
    object.idleAnimationId = {"animation.preview"};
    object.activationActiveAnimationId = {"animation.preview.action"};
    object.destroyedAnimationId = {"animation.preview.death"};
    authored.objectVisuals.push_back(object);
    content::AuthoredNpcVisualSet npc;
    npc.id = {"visual.npc.preview"};
    npc.markerColor = {10, 20, 30, 255};
    npc.idle = enemy.idle;
    authored.npcVisuals.push_back(npc);
    const auto file = root / "visuals.json";
    std::string error;
    expect(content::writeAuthoredContentJsonFile(file, authored, error),
           "18B writes a preview workspace fixture");
    auto document = editor::ContentWorkspaceDocument::open(root, error);
    expect(document && document->valid(), "18B opens the preview workspace through the authored pipeline");
    if (!document) { std::filesystem::remove_all(root, fsError); return; }

    editor::VisualPreviewViewport viewport;
    viewport.zoomStep = 0;
    const auto transform = editor::makeVisualPreviewTransform(viewport, {0, 0, 128, 128}, {64, 64});
    expect(editor::previewImageToScreen(transform, {16, 16}) == core::PointI{48, 48} &&
               editor::previewScreenToImage(transform, {48, 48}) == core::PointI{16, 16},
           "18B preview coordinate conversion respects nearest-neighbor zoom");
    expect(editor::previewDragRectangle(transform, {48, 48}, {64, 64}, {64, 64}, false,
                                         {0, 0}, {16, 16}) == std::optional<core::RectI>{core::RectI{16, 16, 16, 16}},
           "18B preview mouse drag produces an inclusive-exclusive source rectangle");
    expect(editor::previewDragRectangle(transform, {12, 12}, {52, 52}, {64, 64}, false,
                                         {0, 0}, {16, 16}) == std::optional<core::RectI>{core::RectI{0, 0, 20, 20}},
           "18B preview drag clamps source rectangles inside the image");
    expect(editor::previewGridSelections({0, 16, 48, 16}, {0, 0}, {16, 16}) ==
               std::vector<core::RectI>{{0, 16, 16, 16}, {16, 16, 16, 16}, {32, 16, 16, 16}},
           "18B grid cell expansion is deterministic and row-major");

    SyntheticVisualDecoder decoder;
    editor::EditorVisualPreview preview(decoder, root / "game-assets");
    editor::VisualPreviewRequest request;
    request.key = {editor::ContentDefinitionKind::animation, {"animation.preview"}};
    preview.prepare(*document, request);
    expect(preview.hasClip() && preview.image() && decoder.paths.size() == 1 &&
               preview.animator().frameIndex() == 0,
           "18B preview lazily resolves one image and builds the shared AnimationClip");
    const auto decodeCount = decoder.paths.size();
    preview.prepare(*document, request);
    preview.advanceTicks(1);
    expect(decoder.paths.size() == decodeCount && preview.animator().frameIndex() == 0,
           "18B preview cache avoids decoding on repeated paint/prepare calls");
    preview.advanceTicks(1);
    expect(preview.animator().frameIndex() == 1 && !preview.markerEvents().empty() &&
               preview.markerEvents().front().marker == "release",
           "18B preview playback reuses frame timing and exposes authored markers");
    preview.setPlaying(false);
    preview.advanceTicks(8);
    expect(preview.animator().frameIndex() == 1,
           "18B paused preview ignores injected preview ticks");
    preview.restart();
    expect(preview.animator().frameIndex() == 0 && preview.animator().elapsedFrameTicks() == 0,
           "18B preview restart returns to frame zero");
    preview.stepFrame(1);
    expect(preview.animator().frameIndex() == 1 && !preview.playing(),
           "18B paused preview supports deterministic frame stepping");

    request.frameIndex = 1;
    preview.prepare(*document, request);
    expect(preview.animator().frameIndex() == 1 && !preview.playing(),
           "18B preview honors the independently selected authored animation frame");

    request = {};
    request.key = {editor::ContentDefinitionKind::staticSprite, {"sprite.preview"}};
    preview.prepare(*document, request);
    expect(preview.image() && !preview.hasClip() && preview.diagnostics().empty(),
           "18B StaticSprite preview resolves an isolated source without an animation clip");
    auto invalidSprite = *document->staticSprite({"sprite.preview"});
    invalidSprite.source = core::RectI{60, 60, 8, 8};
    expect(document->updateStaticSprite(invalidSprite.id, invalidSprite, error),
           "18B accepts a temporary invalid StaticSprite source edit");
    preview.prepare(*document, request);
    expect(!preview.diagnostics().empty() && preview.diagnostics().front().code ==
               "source_out_of_bounds",
           "18B StaticSprite preview reports source bounds diagnostics before drawing");
    auto validSprite = *document->staticSprite({"sprite.preview"});
    validSprite.source = core::RectI{16, 0, 16, 16};
    expect(document->updateStaticSprite(validSprite.id, validSprite, error),
           "18B restores the StaticSprite source after bounds validation");

    request.key = {editor::ContentDefinitionKind::enemyVisual, {"visual.enemy.preview"}};
    request.state = editor::PreviewClipState::idle;
    request.actionId = {};
    preview.prepare(*document, request);
    expect(preview.hasClip() && preview.animator().clip().id() == "animation.preview" &&
               !preview.flipX(), "18B flexible enemy preview resolves required idle binding");
    request.state = editor::PreviewClipState::death;
    preview.prepare(*document, request);
    expect(preview.hasClip() && preview.animator().clip().id() == "animation.preview.death",
           "18B optional enemy death state previews without requiring move or attack states");
    request.state = editor::PreviewClipState::action;
    request.actionId = {"attack.sword"};
    request.facing = underworld::game::gameplay::FacingDirection::right;
    preview.prepare(*document, request);
    expect(preview.hasClip() && preview.animator().clip().id() == "animation.preview.action" &&
               preview.flipX(), "18B arbitrary enemy action uses authored side/facing policy");

    request = {};
    request.key = {editor::ContentDefinitionKind::objectVisual, {"visual.object.preview"}};
    request.state = editor::PreviewClipState::activationActive;
    preview.prepare(*document, request);
    expect(preview.hasClip() && preview.animator().clip().id() == "animation.preview.action",
           "18B object visual preview resolves authored activation state");
    request.state = editor::PreviewClipState::destroyed;
    preview.prepare(*document, request);
    expect(preview.hasClip() && preview.animator().clip().id() == "animation.preview.death",
           "18B object visual preview resolves the optional destroyed state");
    request = {};
    request.key = {editor::ContentDefinitionKind::npcVisual, {"visual.npc.preview"}};
    request.facing = underworld::game::gameplay::FacingDirection::down;
    preview.prepare(*document, request);
    expect(preview.hasClip() && preview.animator().clip().id() == "animation.preview",
           "18B NPC visual preview resolves directional idle while retaining marker fallback data");

    auto changed = *document->visualImage({"image.preview"});
    changed.relativePath = "assets/preview-renamed.png";
    const auto pathDecodeCount = decoder.paths.size();
    expect(document->updateVisualImage(changed.id, changed, error),
           "18B visual image path edit increments preview revision");
    preview.prepare(*document, request);
    expect(decoder.paths.size() == pathDecodeCount + 1 &&
               decoder.paths.back() == root / "assets/preview-renamed.png",
           "18B authored image path changes invalidate the lazy preview cache");
    std::filesystem::remove_all(root, fsError);
}

void testPhase18CGameplayContentEditors() {
    namespace content = underworld::game::content;
    namespace editor = underworld::editor;
    namespace gameplay = underworld::game::gameplay;
    namespace presentation = underworld::game::presentation;
    namespace simulation = underworld::simulation;
    namespace creatures = underworld::game::gameplay::creatures;
    namespace dialogue = underworld::game::gameplay::dialogue;
    namespace quests = underworld::game::gameplay::quests;
    const auto root = std::filesystem::temp_directory_path() / "underworld_phase18c_gameplay_editors";
    std::error_code fsError;
    std::filesystem::remove_all(root, fsError);
    std::filesystem::create_directories(root, fsError);
    const auto source = root / "gameplay.json";
    std::string error;
    expect(content::writeAuthoredContentJsonFile(source, content::makeBuiltinAuthoredContent(), error),
           "18C writes a gameplay editor workspace fixture");
    auto document = editor::ContentWorkspaceDocument::open(root, error);
    expect(document && document->valid(), "18C opens the gameplay editor workspace through the existing pipeline");
    if (!document) { std::filesystem::remove_all(root, fsError); return; }

    content::AuthoredProjectile projectile;
    projectile.id = {"projectile.studio.gel"};
    projectile.visualId = {"visual.projectile.player.arrow"};
    projectile.canonicalFacing = gameplay::FacingDirection::right;
    projectile.speedPixelsPerTick = 3;
    projectile.lifetimeTicks = 30;
    projectile.hitboxWidth = projectile.hitboxHeight = 6;
    projectile.spawnOffsets.values = {{{0, 0}, {0, 0}, {-2, 0}, {2, 0}}};
    expect(document->addProjectile(source, projectile, error) && document->projectile(projectile.id),
           "18C creates a typed Projectile with visual, facing, timing, hitbox and offsets");

    content::AuthoredAttack attack;
    attack.id = {"attack.studio.gel"};
    attack.kind = gameplay::AttackKind::projectile;
    attack.damage = {2, 1};
    attack.totalTicks = 8;
    attack.cooldownTicks = 12;
    attack.maximumRangePixels = 96;
    attack.visualActionId = {"special.gel"};
    attack.projectileDefinitionId = projectile.id;
    attack.timeline = {{1, gameplay::AttackTimelineEventKind::spawnProjectile}};
    expect(document->addAttack(source, attack, error) && document->attack(attack.id),
           "18C creates a typed Attack with projectile reference and authored timeline");

    content::AuthoredBehaviorProfile behavior{{"behavior.studio.gel"}, 96, 144, 20, 30};
    expect(document->addBehavior(source, behavior, error) && document->behavior(behavior.id),
           "18C creates a typed Behavior profile");

    content::AuthoredItem item;
    item.id = {"item.studio.gel"};
    item.visualId = {"visual.item.life_potion"};
    item.category = gameplay::ItemCategory::consumable;
    item.stackLimit = 10;
    item.use = gameplay::ItemUseDefinition{gameplay::ItemUseKind::restoreHealth, 3};
    expect(document->addItem(source, item, error) && document->item(item.id),
           "18C creates an Item with use and stack data");

    content::AuthoredPickup pickup;
    pickup.id = {"pickup.studio.gel"};
    pickup.visualId = {"visual.pickup.heart"};
    pickup.collectionBounds = {-5, -5, 10, 10};
    pickup.payload = content::AuthoredItemPickup{item.id, 2};
    expect(document->addPickup(source, pickup, error) && document->pickup(pickup.id),
           "18C creates an item Pickup variant");

    content::AuthoredRewardProfile rewardProfile;
    rewardProfile.id = {"reward.studio.gel"};
    rewardProfile.experience = 12;
    rewardProfile.loot.push_back({pickup.id, 5000, 1, 2});
    expect(document->addRewardProfile(source, rewardProfile, error) && document->rewardProfile(rewardProfile.id),
           "18C creates a probabilistic RewardProfile with ordered loot");

    content::AuthoredEnemy enemy;
    enemy.id = {"enemy.studio.gel"};
    enemy.visualSetId = creatures::soldierVisualId();
    enemy.behaviorProfileId = behavior.id;
    enemy.faction = gameplay::Faction::enemy;
    enemy.maximumHealth = 7;
    enemy.movementSpeedSubpixelsPerTick = 128;
    enemy.collisionBody = {0, -8, 12, 8};
    enemy.hurtbox = {-6, -24, 12, 24};
    enemy.attackIds = {attack.id};
    enemy.rewardProfileId = rewardProfile.id;
    expect(document->addEnemy(source, enemy, error) && document->enemy(enemy.id),
           "18C creates a custom Enemy linking visual, behavior, attack and reward");

    content::AuthoredWorldObject object;
    object.id = {"object.studio.switch"};
    object.visualSetId = {"visual.object.chest"};
    object.interactable = gameplay::ObjectInteractionDefinition{{-8, -8, 16, 16}};
    object.activation = gameplay::ObjectActivationDefinition{gameplay::ObjectActivationMode::interactToggle, false, std::nullopt};
    expect(document->addObject(source, object, error) && document->object(object.id),
           "18C creates an interactive World Object through capability data");

    content::AuthoredRewardGrant grant;
    grant.id = {"reward.studio.quest"};
    grant.experience = 20;
    grant.gold = 5;
    grant.items.push_back({item.id, 1});
    expect(document->addRewardGrant(source, grant, error) && document->rewardGrant(grant.id),
           "18C creates a guaranteed RewardGrant");

    content::AuthoredQuest quest;
    quest.id = {"quest.studio.gel"};
    quest.title = "Gel lesson";
    quest.tags = {"studio", "combat"};
    quest.rewardGrantId = grant.id;
    quest.objectives.push_back({{"objective.gel"}, quests::QuestObjectiveKind::kill, enemy.id, 2, "Defeat gel"});
    expect(document->addQuest(source, quest, error) && document->quest(quest.id),
           "18C creates a Quest objective and reward reference");

    content::AuthoredDialogue authoredDialogue;
    authoredDialogue.id = {"dialogue.studio.scholar"};
    authoredDialogue.entryNodeId = {"entry"};
    content::AuthoredDialogueNode node;
    node.id = {"entry"}; node.speaker = "Scholar"; node.pages = {"Welcome."};
    node.choices.push_back({"Accept", {"entry"}, {}, {{dialogue::DialogueActionKind::startQuest, quest.id}}});
    authoredDialogue.nodes.push_back(node);
    expect(document->addDialogue(source, authoredDialogue, error) && document->dialogue(authoredDialogue.id),
           "18C creates structured Dialogue nodes, pages, choices and actions");

    content::AuthoredNpc npc;
    npc.id = {"npc.studio.scholar"}; npc.visualSetId = {"visual.npc.scholar"};
    npc.interaction = gameplay::InteractionArea{{-8, -8, 16, 16}, true};
    npc.defaultDialogueId = authoredDialogue.id; npc.tags = {"teacher"};
    expect(document->addNpc(source, npc, error) && document->npc(npc.id),
           "18C creates an NPC with dialogue, interaction and tags");

    content::AuthoredShop shop;
    shop.id = {"shop.studio"}; shop.offers.push_back({item.id, 12, 4});
    expect(document->addShop(source, shop, error) && document->shop(shop.id),
           "18C creates a Shop offer with optional buy and sell prices");

    content::AuthoredPlayerProgression progression;
    progression.id = {"progression.studio"}; progression.baseStats.maximumHealth = 8;
    progression.cumulativeExperienceThresholds = {0, 25, 70};
    expect(document->addPlayerProgression(source, progression, error) && document->playerProgression(progression.id),
           "18C creates Player Progression thresholds");

    content::AuthoredPresentationEffect presentationEffect;
    presentationEffect.id = {"effect.studio.gel"};
    presentationEffect.durationTicks = 6;
    presentationEffect.priority = 10;
    presentationEffect.overlay = presentation::ColorOverlayDefinition{{40, 220, 80, 64}, presentation::PresentationOverlayMode::pulse, 6, presentation::PresentationCompositionLayer::world};
    expect(document->addPresentationEffect(source, presentationEffect, error) && document->presentationEffect(presentationEffect.id),
           "18C creates a PresentationEffect with authored overlay data");

    const auto index = document->index();
    expect(std::any_of(index.begin(), index.end(), [&](const auto& key) { return key.kind == editor::ContentDefinitionKind::enemy && key.id == enemy.id; }) &&
               std::any_of(index.begin(), index.end(), [&](const auto& key) { return key.kind == editor::ContentDefinitionKind::presentationEffect && key.id == presentationEffect.id; }),
           "18C content browser index includes typed gameplay categories deterministically");

    auto brokenAttack = attack;
    const bool removedAttack = document->removeAttack(attack.id, error);
    expect(removedAttack && !document->valid() && document->enemy(enemy.id) &&
               std::any_of(document->diagnostics().begin(), document->diagnostics().end(), [](const auto& value) {
                   return value.code == "unknown_reference" && value.message.find("attack") != std::string::npos;
               }), "18C delete has no cascade and exposes a broken reference diagnostic");
    const bool restoredAttack = document->addAttack(source, brokenAttack, error);
    expect(restoredAttack && document->valid(),
           "18C restoring a referenced definition repairs the workspace through the same document APIs");
    expect(document->saveAll(error), "18C saves all typed gameplay edits as canonical Content JSON v5");
    auto reloaded = editor::ContentWorkspaceDocument::open(root, error);
    expect(reloaded && reloaded->valid() && reloaded->enemy(enemy.id) && reloaded->dialogue(authoredDialogue.id) &&
               reloaded->quest(quest.id) && reloaded->shop(shop.id) && reloaded->presentationEffect(presentationEffect.id),
           "18C save/reload preserves the gameplay registry relationships");
    if (reloaded && reloaded->compiledRegistry()) {
        simulation::EntityHandlePool handles;
        creatures::EnemyFactory factory(handles, reloaded->compiledRegistry()->enemies(),
                                        reloaded->compiledRegistry()->behaviors(),
                                        reloaded->compiledRegistry()->attacks(),
                                        reloaded->compiledRegistry()->projectiles());
        auto instance = factory.create(handles, enemy.id, {32, 32}, gameplay::FacingDirection::down);
        expect(instance.definition().id == enemy.id && instance.definition().attackIds.front() == attack.id,
               "18C compiled custom Enemy instantiates through the existing runtime factory");
        gameplay::WorldObjectFactory objectFactory(handles, reloaded->compiledRegistry()->objects(),
                                                   reloaded->compiledRegistry()->items());
        auto objectInstance = objectFactory.create(handles, object.id, {16, 16});
        expect(objectInstance.hasActivation() && objectInstance.definition().id == object.id,
               "18C compiled interactive object instantiates through the existing factory");
    }
    std::filesystem::remove_all(root, fsError);
}

void testPhase18DUnifiedStudioWorkflow() {
    namespace content = underworld::game::content;
    namespace editor = underworld::editor;
    namespace simulation = underworld::simulation;
    namespace gameplay = underworld::game::gameplay;
    const auto root = std::filesystem::temp_directory_path() / "underworld_phase18d_unified_workflow";
    std::error_code fsError; std::filesystem::remove_all(root, fsError); std::filesystem::create_directories(root, fsError);
    const auto source = root / "studio.json"; std::string error;
    expect(content::writeAuthoredContentJsonFile(source, content::makeBuiltinAuthoredContent(), error),
           "18D writes a unified Content v5 fixture");
    auto document = editor::ContentWorkspaceDocument::open(root, error);
    expect(document && document->valid(), "18D opens the unified workspace through the existing source pipeline");
    content::AuthoredTileset tileset{{"tileset.test.18d"}, "18D Tiles", "assets/tiles.png", 16, 4, 4};
    content::AuthoringDescriptor descriptor{{"enemy.test.18d"}, "Test Enemy", content::AuthoringCategory::enemy, {"test"}};
    content::AuthoredTileSemantic semantic; semantic.id = {"tile.test.18d"}; semantic.tilesetId = tileset.id; semantic.sourceIndex = 0; semantic.family = "test";
    content::AuthoredStamp stamp; stamp.id = {"stamp.test.18d"}; stamp.displayName = "Test Stamp"; stamp.width = 1; stamp.height = 1; stamp.cells.push_back({0, 0, semantic.id});
    content::AuthoredEnemy enemy; enemy.id = descriptor.definitionId; enemy.visualSetId = underworld::game::gameplay::creatures::soldierVisualId(); enemy.behaviorProfileId = underworld::game::gameplay::creatures::soldierBehaviorId(); enemy.maximumHealth = 1; enemy.movementSpeedSubpixelsPerTick = 256; enemy.collisionBody = {0, -8, 12, 8}; enemy.hurtbox = {-6, -18, 12, 18}; enemy.attackIds = {{"attack.soldier.sword"}};
    expect(document && document->addTileset(source, tileset, error) && document->addAuthoringDescriptor(source, descriptor, error) &&
               document->addTileSemantic(source, semantic, error) && document->addStamp(source, stamp, error) && document->addEnemy(source, enemy, error) && document->valid(),
           "18D provides typed authoring paths for Tileset, Descriptor, Semantic, Stamp and Enemy");
    expect(document->tileset(tileset.id) && document->authoringDescriptor(descriptor.definitionId) &&
               document->tileSemantic(semantic.id) && document->stamp(stamp.id),
           "18D typed getters preserve all remaining category ownership");
    editor::EditorDocument map = editor::EditorDocument::newMap(simulation::MapId{"map.test.18d"}, 8, 8, 16, true);
    editor::TileBrushSelection brush{tileset.id, 2, 2, {{tileset.id, 0, underworld::world::TileFlags::none}, {tileset.id, 1, underworld::world::TileFlags::none}, {tileset.id, 4, underworld::world::TileFlags::none}, {tileset.id, 5, underworld::world::TileFlags::none}}};
    const auto placements = editor::brushPlacements(brush, {3, 2}, map.data());
    expect(placements.size() == 4 && placements[0].first.x == 3 && placements[1].first.x == 4 && placements[2].first.y == 3,
           "18D expands a multi-tile palette brush in row-major order");
    const editor::TileBrushSelection horizontalBrush{
        tileset.id, 2, 1, {{tileset.id, 0, underworld::world::TileFlags::none},
                           {tileset.id, 1, underworld::world::TileFlags::none}}};
    const auto patterned = editor::patternRectanglePlacements(
        horizontalBrush, {1, 2}, {4, 3}, map.data());
    expect(patterned.size() == 8 && patterned[0].second.sourceIndex == 0 &&
               patterned[1].second.sourceIndex == 1 && patterned[2].second.sourceIndex == 0 &&
               patterned[4].second.sourceIndex == 0 &&
               std::none_of(patterned.begin(), patterned.end(), [](const auto& placement) {
                   return placement.first.x == 5;
               }),
           "18D rectangle repeats a 2x1 brush exactly inside its destination bounds");
    const editor::TileBrushSelection checkerBrush{
        tileset.id, 2, 2, {{tileset.id, 0, underworld::world::TileFlags::none},
                           {tileset.id, 1, underworld::world::TileFlags::none},
                           {tileset.id, 4, underworld::world::TileFlags::none},
                           {tileset.id, 5, underworld::world::TileFlags::none}}};
    const auto oddPattern = editor::patternRectanglePlacements(
        checkerBrush, {0, 0}, {4, 2}, map.data());
    const std::array<std::uint32_t, 15> expectedOddPattern{
        0, 1, 0, 1, 0, 4, 5, 4, 5, 4, 0, 1, 0, 1, 0};
    const bool oddPatternMatches = oddPattern.size() == expectedOddPattern.size() &&
        std::equal(oddPattern.begin(), oddPattern.end(), expectedOddPattern.begin(),
                   [](const auto& placement, std::uint32_t sourceIndex) {
                       return placement.second.sourceIndex == sourceIndex;
                   });
    expect(oddPatternMatches,
           "18D odd-sized rectangle repeats a 2x2 brush in row-major order");
    const auto clippedPattern = editor::patternRectanglePlacements(
        horizontalBrush, {7, 7}, {99, 99}, map.data());
    expect(clippedPattern.size() == 1 && clippedPattern.front().first.x == 7 &&
               clippedPattern.front().first.y == 7,
           "18D patterned rectangles are clipped to map bounds without expanding their authored area");
    editor::EditorDocument patternedMap = editor::EditorDocument::newMap(
        simulation::MapId{"map.patterned.rectangle"}, 8, 8);
    const auto beforePatternedMap = patternedMap.data();
    auto patternCommand = std::make_unique<editor::CompoundEditorCommand>("Pattern Rectangle");
    for (const auto& placement : patterned) {
        patternCommand->add(std::make_unique<editor::PaintTilesCommand>(
            0, std::vector<editor::TileCoordinate>{placement.first}, placement.second));
    }
    const bool paintedPattern = patternedMap.execute(std::move(patternCommand), error);
    expect(paintedPattern && patternedMap.undo() &&
               patternedMap.data().layers[0].cells == beforePatternedMap.layers[0].cells &&
               patternedMap.data().tileReferences == beforePatternedMap.tileReferences &&
               patternedMap.redo(error) && patternedMap.data().layers[0].cells[2 * 8 + 1].has_value() &&
               patternedMap.data().tileReferences[patternedMap.data().layers[0].cells[2 * 8 + 2].value()].sourceIndex == 1,
           "18D patterned rectangle remains one undoable operation and redoes exactly");
    expect(map.execute(std::make_unique<editor::AddLayerCommand>(1, "Foreground"), error) && map.data().layers.size() == 2,
           "18D adds a layer through an undoable command");
    map.layerStates()[0].locked = true;
    expect(!map.execute(std::make_unique<editor::PaintTilesCommand>(0, std::vector<editor::TileCoordinate>{{0, 0}}, placements.front().second), error),
           "18D prevents painting a locked layer");
    map.layerStates()[0].locked = false;
    auto brushPaint = std::make_unique<editor::CompoundEditorCommand>("Paint 18D Brush");
    for (const auto& placement : placements) brushPaint->add(std::make_unique<editor::PaintTilesCommand>(
        1, std::vector<editor::TileCoordinate>{placement.first}, placement.second));
    expect(map.execute(std::move(brushPaint), error) &&
               map.data().layers[1].cells[static_cast<std::size_t>(2) * map.data().width + 3].has_value() &&
               map.data().tileReferences[map.data().layers[1].cells[static_cast<std::size_t>(2) * map.data().width + 3].value()].sourceIndex == 0 &&
               map.data().tileReferences[map.data().layers[1].cells[static_cast<std::size_t>(3) * map.data().width + 4].value()].sourceIndex == 5,
           "18D paints a multi-tile brush as one authored map edit");
    expect(map.execute(std::make_unique<editor::RenameLayerCommand>(1, "Foreground Renamed"), error) &&
               map.execute(std::make_unique<editor::MoveLayerCommand>(1, 0), error) && map.data().layers.front().name == "Foreground Renamed",
           "18D renames and reorders layers through command history");
    expect(map.undo() && map.data().layers.front().name == "Ground" && map.redo(error) && map.data().layers.front().name == "Foreground Renamed",
           "18D layer operations support undo and redo");
    editor::EditorDocument usageMap = editor::EditorDocument::newMap(
        simulation::MapId{"map.find.usages"}, 12, 12);
    const simulation::DefinitionId findEnemyId{"enemy.find.test"};
    const simulation::DefinitionId findNpcId{"npc.find.test"};
    const simulation::DefinitionId findObjectId{"object.find.test"};
    const simulation::DefinitionId findPickupId{"pickup.find.test"};
    expect(usageMap.execute(std::make_unique<editor::PlaceEntityCommand>(
                   underworld::game::maps::EnemyPlacement{{1}, findEnemyId, {16, 16}, gameplay::FacingDirection::down}), error) &&
               usageMap.execute(std::make_unique<editor::PlaceEntityCommand>(
                   underworld::game::maps::EnemyPlacement{{2}, findEnemyId, {32, 16}, gameplay::FacingDirection::down}), error) &&
               usageMap.execute(std::make_unique<editor::PlaceEntityCommand>(
                   underworld::game::maps::NpcPlacement{{3}, findNpcId, {48, 16}, gameplay::FacingDirection::down}), error) &&
               usageMap.execute(std::make_unique<editor::PlaceEntityCommand>(
                   underworld::game::maps::ObjectPlacement{{4}, findObjectId, {64, 16}, {}}), error) &&
               usageMap.execute(std::make_unique<editor::PlaceEntityCommand>(
                   underworld::game::maps::PickupPlacement{{5}, findPickupId, {}, {80, 16}, {0, 0, 16, 16}, gameplay::HealthPickup{1}}), error),
           "18D creates authored usages for every placeable Content category");
    const auto enemyUsages = editor::findPlacementUsages(
        usageMap, {editor::ContentDefinitionKind::enemy, findEnemyId});
    const auto npcUsages = editor::findPlacementUsages(
        usageMap, {editor::ContentDefinitionKind::npc, findNpcId});
    const auto objectUsages = editor::findPlacementUsages(
        usageMap, {editor::ContentDefinitionKind::object, findObjectId});
    const auto pickupUsages = editor::findPlacementUsages(
        usageMap, {editor::ContentDefinitionKind::pickup, findPickupId});
    expect(enemyUsages.size() == 2 && enemyUsages[0].kind == editor::SelectionKind::enemy &&
               enemyUsages[0].instanceId == simulation::PersistentInstanceId{1} &&
               enemyUsages[1].instanceId == simulation::PersistentInstanceId{2} &&
               npcUsages.size() == 1 && npcUsages[0].kind == editor::SelectionKind::npc &&
               objectUsages.size() == 1 && objectUsages[0].kind == editor::SelectionKind::object &&
               pickupUsages.size() == 1 && pickupUsages[0].kind == editor::SelectionKind::pickup,
           "18D Find In Map uses one common helper for Enemy, NPC, Object and Pickup placements");
    expect(editor::nextPlacementUsageIndex(0, enemyUsages.size()) == 1 &&
               editor::nextPlacementUsageIndex(1, enemyUsages.size()) == 0 &&
               editor::nextPlacementUsageIndex(0, 0) == 0,
           "18D Find In Map cycles multiple usages deterministically with wraparound");
    const auto mapPath = root / "map.test.18d.umap";
    expect(document->saveAll(error) && document->compiledRegistry() && map.saveAs(mapPath, *document->compiledRegistry(), error),
           "18D saves authored Content and UMAP documents without schema changes");
    auto reloaded = editor::ContentWorkspaceDocument::open(root, error);
    auto reloadedMap = editor::EditorDocument::open(mapPath, *document->compiledRegistry(), error);
    expect(reloaded && reloaded->valid() && reloaded->tileset(tileset.id) && reloaded->stamp(stamp.id) && reloadedMap && reloadedMap->data().layers.size() == 2,
           "18D save/reload preserves category ownership and authored layer order");
    std::filesystem::remove_all(root, fsError);
}

void testContentStudioDeepAuthoringHelpers() {
    namespace content = underworld::game::content;
    namespace editor = underworld::editor;
    namespace gameplay = underworld::game::gameplay;
    namespace core = underworld::core;

    expect(!editor::clampCollectionSelection(0, std::size_t{0}) &&
               editor::clampCollectionSelection(3, std::size_t{9}) == std::optional<std::size_t>{2} &&
               !editor::clampCollectionSelection(3, std::nullopt),
           "content collection selection clamps and clears empty selections");
    expect(editor::selectionAfterErase(0, 0) == std::nullopt &&
               editor::selectionAfterErase(2, 1) == std::optional<std::size_t>{1} &&
               editor::selectionAfterErase(2, 4) == std::optional<std::size_t>{1},
           "content collection removal selects a valid adjacent row");
    expect(editor::selectionAfterMove(1, 0, 2) == 0 &&
               editor::selectionAfterMove(1, 2, 0) == 2 &&
               editor::selectionAfterMove(4, 1, 3) == 4,
           "content collection move preserves the selected entry identity");

    auto builtin = editor::ContentWorkspaceDocument::fromBuiltin(content::makeBuiltinAuthoredContent());
    const auto index = builtin.index();
    bool allCategoriesSafe = true;
    for (const auto kind : editor::ContentWorkspaceDocument::categoryOrder()) {
        for (const auto& key : index) {
            if (key.kind != kind) continue;
            allCategoriesSafe = allCategoriesSafe &&
                !editor::contentDefinitionDisplayName(builtin, key).empty();
            static_cast<void>(editor::contentDefinitionSummary(builtin, key));
        }
    }
    expect(allCategoriesSafe && !index.empty(),
           "content audit can enumerate every registered category without inspector assumptions");

    const auto itemCandidates = editor::contentReferenceCandidates(
        builtin, editor::ContentDefinitionKind::item, "item.");
    expect(!itemCandidates.empty() &&
               std::is_sorted(itemCandidates.begin(), itemCandidates.end(),
                              [](const auto& left, const auto& right) {
                                  if (left.displayName != right.displayName)
                                      return left.displayName < right.displayName;
                                  return left.key.id.value() < right.key.id.value();
                              }),
           "reference picker returns deterministic filtered candidates");

    expect(editor::AssetBrowserCatalog::isSafeRelativePath("tiles/crate.png") &&
               !editor::AssetBrowserCatalog::isSafeRelativePath("../crate.png") &&
               !editor::AssetBrowserCatalog::isSafeRelativePath("C:/crate.png") &&
               !editor::AssetBrowserCatalog::isSafeRelativePath("tiles\\..\\crate.png"),
           "asset browser accepts relative paths and rejects absolute traversal paths");

    const auto rewardKey = editor::ContentDefinitionKey{
        editor::ContentDefinitionKind::rewardGrant,
        underworld::simulation::DefinitionId{"reward.quest.scholar.path"}};
    const auto rewardSummary = editor::contentDefinitionSummary(builtin, rewardKey);
    expect(rewardSummary.find("item.life_potion x2") != std::string::npos &&
               rewardSummary.find("item.training_armor x1") != std::string::npos,
           "reference summaries expose individual reward items instead of only a count");

    const auto imageSummary = editor::contentDefinitionSummary(
        builtin, {editor::ContentDefinitionKind::visualImage,
                  underworld::simulation::DefinitionId{"image.enemy.soldier.idle"}});
    const auto animationSummary = editor::contentDefinitionSummary(
        builtin, {editor::ContentDefinitionKind::animation,
                  underworld::simulation::DefinitionId{"anim.enemy.soldier.idle.down"}});
    const auto projectileSummary = editor::contentDefinitionSummary(
        builtin, {editor::ContentDefinitionKind::projectile,
                  underworld::simulation::DefinitionId{"projectile.player.arrow"}});
    expect(imageSummary.find("evil_soldier_idle.png") != std::string::npos &&
               animationSummary.find("2 frames") != std::string::npos &&
               animationSummary.find("60 ticks") != std::string::npos &&
               projectileSummary.find("4 px/tick") != std::string::npos,
           "reference picker and Quick Inspect summarize visual and gameplay dependencies");

    const auto transform = editor::VisualPreviewTransform{{0, 0, 128, 128}, {10, 12}, 2.0};
    expect(editor::anchorFromPreviewPointer(transform, {16, 20}, {0, 0, 16, 16}) ==
               core::PointI{3, 4} &&
               editor::offsetFromPreviewDrag(transform, {10, 10}, {16, 6}, {2, 3}) ==
               core::PointI{5, 1},
           "visual authoring converts preview drags into logical anchor and offset values");
    expect(editor::anchorForPreset(editor::AnchorPreset::center, {0, 0, 17, 9}) ==
               core::PointI{8, 4} &&
               editor::clampPreviewSource({-2, 3, 20, 20}, {16, 16}) ==
               core::RectI{0, 0, 16, 16} &&
               editor::snapPreviewSource({3, 5, 9, 9}, {0, 0}, {4, 4}, {32, 32}) ==
               core::RectI{4, 4, 8, 8},
           "visual authoring presets, clamps and grid-snaps source rectangles safely");

    const std::vector<std::uint8_t> mask{1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1};
    const auto regions = gameplay::compileAttackShapeMask(4, 3, mask, -2, 5);
    expect(regions.size() == 2 && regions[0].offsetX == -2 && regions[0].offsetY == 5 &&
               regions[0].width == 2 && regions[0].height == 2 &&
               regions[1].offsetX == 1 && regions[1].offsetY == 6 &&
               regions[1].width == 1 && regions[1].height == 2,
           "authored attack masks compile to deterministic compact rectangles");

    content::AuthoredAttack authored;
    authored.id = {"attack.shape.roundtrip"};
    authored.visualActionId = {"visual.attack"};
    authored.damage = {3, 0};
    authored.totalTicks = 8;
    authored.maximumRangePixels = 32;
    authored.timeline = {{1, gameplay::AttackTimelineEventKind::activateHitbox}};
    authored.shapes.push_back({gameplay::FacingDirection::down, {{2, 3, 2, 2, {1, 0, 1, 1}}}});
    content::AuthoredContentPack pack;
    pack.attacks.push_back(authored);
    const auto decoded = content::decodeAuthoredContentJson(
        content::encodeAuthoredContentJson(pack));
    bool shapeRoundTrip = false;
    if (decoded.content && decoded.content->attacks.size() == 1) {
        const auto& decodedAttack = decoded.content->attacks.front();
        shapeRoundTrip = decodedAttack.shapes.size() == 1 &&
            decodedAttack.shapes.front().facing == gameplay::FacingDirection::down &&
            decodedAttack.shapes.front().frames.size() == 1 &&
            decodedAttack.shapes.front().frames.front().frameIndex == 2 &&
            decodedAttack.shapes.front().frames.front().cells == authored.shapes.front().frames.front().cells;
    }
    expect(shapeRoundTrip,
           "authored attack shape data round-trips through the strict content codec");
}

void testContentStudioAuthoredEntityPlacementWorkflow() {
    namespace content = underworld::game::content;
    namespace editor = underworld::editor;
    namespace game = underworld::game;
    namespace maps = underworld::game::maps;
    namespace gameplay = underworld::game::gameplay;

    const auto root = std::filesystem::temp_directory_path() /
        "underworld_authored_entity_index_workflow";
    std::error_code fsError;
    std::filesystem::remove_all(root, fsError);
    std::filesystem::create_directories(root, fsError);

    auto authored = content::makeBuiltinAuthoredContent();
    const auto enemy = std::find_if(authored.enemies.begin(), authored.enemies.end(),
        [](const auto& value) { return value.id == gameplay::creatures::soldierEnemyId(); });
    expect(enemy != authored.enemies.end(),
           "entity authoring fixture contains the configured builtin enemy");
    if (enemy == authored.enemies.end()) {
        std::filesystem::remove_all(root, fsError);
        return;
    }
    auto brokenEnemy = *enemy;
    brokenEnemy.id = {"enemy.authoring.broken"};
    brokenEnemy.visualSetId = {"visual.enemy.missing"};
    authored.enemies.push_back(brokenEnemy);
    authored.quests.push_back({{"quest.authoring.unrelated.broken"}, {}, {}, {}, std::nullopt});

    std::string error;
    expect(content::writeAuthoredContentJsonFile(root / "content.json", authored, error),
           "entity authoring fixture writes authored content with unrelated invalid data");
    auto document = editor::ContentWorkspaceDocument::open(root, error);
    expect(document && !document->valid() && !document->compiledRegistry(),
           "entity authoring workspace retains authored data without a compiled registry");
    if (!document) {
        std::filesystem::remove_all(root, fsError);
        return;
    }

    const editor::AuthoredEntityIndex index(*document);
    const editor::ContentDefinitionKey enemyKey{
        editor::ContentDefinitionKind::enemy, gameplay::creatures::soldierEnemyId()};
    const editor::ContentDefinitionKey brokenKey{
        editor::ContentDefinitionKind::enemy, {"enemy.authoring.broken"}};
    const auto* validCandidate = index.find(enemyKey);
    const auto* invalidCandidate = index.find(brokenKey);
    expect(validCandidate && validCandidate->runtimeValid &&
               validCandidate->source == editor::AuthoredEntitySource::project,
           "authored enemy remains placeable when an unrelated definition invalidates the workspace");
    expect(invalidCandidate && !invalidCandidate->runtimeValid &&
               invalidCandidate->diagnostic.find("visual") != std::string::npos,
           "broken authored enemy remains visible with a local dependency diagnostic");
    expect(validCandidate && !index.candidates(editor::ContentDefinitionKind::enemy,
                                               validCandidate->displayName).empty() &&
               !index.candidates(editor::ContentDefinitionKind::enemy,
                                  enemyKey.id.value()).empty(),
           "entity browser search matches both display name and definition ID");
    for (const auto kind : {editor::ContentDefinitionKind::enemy,
                            editor::ContentDefinitionKind::npc,
                            editor::ContentDefinitionKind::object,
                            editor::ContentDefinitionKind::pickup}) {
        const auto candidates = index.candidates(kind);
        expect(!candidates.empty() && std::all_of(candidates.begin(), candidates.end(),
                   [&](const auto& candidate) { return candidate.key.kind == kind; }),
               "entity browser category returns only its own authored definitions");
    }

    editor::EditorDocument map = editor::EditorDocument::newMap(
        underworld::simulation::MapId{"map.authored.entity"}, 8, 8, 16, false);
    const auto firstId = map.allocatePersistentId();
    const auto firstPlacement = maps::EnemyPlacement{
        firstId, enemyKey.id, {32, 48}, gameplay::FacingDirection::right};
    expect(map.execute(std::make_unique<editor::PlaceEntityCommand>(firstPlacement), error) &&
               map.data().enemies.size() == 1 && map.data().enemies.front().definitionId == enemyKey.id &&
               map.data().enemies.front().id == firstId &&
               map.data().enemies.front().position == underworld::core::WorldPointI{32, 48} &&
               map.data().enemies.front().facing == gameplay::FacingDirection::right,
           "placing an authored enemy creates the expected placement, ID, position and facing");
    expect(map.undo() && map.data().enemies.empty() && map.redo(error) &&
               map.data().enemies.size() == 1 && map.data().enemies.front().id == firstId,
           "authored enemy placement roundtrips through undo and redo");
    const auto secondId = map.allocatePersistentId();
    expect(map.execute(std::make_unique<editor::PlaceEntityCommand>(maps::EnemyPlacement{
                   secondId, enemyKey.id, {64, 48}, gameplay::FacingDirection::down}), error) &&
               secondId != firstId && map.data().enemies.size() == 2,
           "repeated authored placement creates distinct persistent IDs");

    SyntheticVisualDecoder decoder;
    editor::EditorApp invalidApp(decoder, root / "game-assets", game::GameContentRegistry{},
                                 std::move(document));
    invalidApp.shellCommand(editor::EditorShellCommand::playtest);
    expect(invalidApp.status().find("Content Workspace is invalid") != std::string::npos,
           "invalid authored workspace still blocks playtest despite an available entity index");
    editor::EditorApp blankApp(decoder, root / "game-assets", game::GameContentRegistry{});
    expect(blankApp.selectedDefinition().empty() &&
               blankApp.document().activeTool() == editor::EditorTool::select &&
               blankApp.document().data().playerSpawns.empty() &&
               blankApp.contentWorkspace() == nullptr,
           "blank editor startup has no implicit enemy, builtin workspace or player spawn");
    const auto builtinDocument = editor::ContentWorkspaceDocument::fromBuiltin(
        content::makeBuiltinAuthoredContent());
    const editor::AuthoredEntityIndex builtinIndex(builtinDocument);
    expect(builtinIndex.find(enemyKey) &&
               builtinIndex.find(enemyKey)->source == editor::AuthoredEntitySource::builtin,
           "builtin entity source is explicit and distinguishable from project content");

    std::filesystem::remove_all(root, fsError);
}

void testEditorLocalization() {
    using namespace underworld;
    using editor::EditorLanguage;
    using editor::EditorLocalization;
    using editor::EditorTextId;
    EditorLocalization portuguese;
    EditorLocalization english(EditorLanguage::englishUnitedStates);
    expect(portuguese.language() == EditorLanguage::portugueseBrazil &&
               portuguese.text(EditorTextId::settings) == "Configurações" &&
               portuguese.text(EditorTextId::contentMode) == "Modo Conteúdo" &&
               english.text(EditorTextId::settings) == "Settings" &&
               english.text(EditorTextId::contentMode) == "Content Mode",
           "Content Studio localization provides Portuguese default and English catalog");
    bool catalogsComplete = true;
    for (const auto id : EditorLocalization::allTextIds()) {
        catalogsComplete = catalogsComplete && !portuguese.text(id).empty() && !english.text(id).empty();
    }
    expect(catalogsComplete && EditorLocalization::catalogComplete(EditorLanguage::portugueseBrazil) &&
               EditorLocalization::catalogComplete(EditorLanguage::englishUnitedStates),
           "Content Studio localization catalogs contain every registered text ID");
    portuguese.setLanguage(EditorLanguage::englishUnitedStates);
    expect(portuguese.text(EditorTextId::save) == "Save" && portuguese.localize("Frame 2") == "Frame 2",
           "Content Studio localization switches language without changing authored identifiers");
    expect(portuguese.localize("meleeHitbox") == "Melee" &&
               portuguese.localize("consumable") == "Consumable" &&
               portuguese.localize("relativePath") == "Relative Path",
           "Content Studio English catalog localizes enum values and inspector fields");
    portuguese.setLanguage(EditorLanguage::portugueseBrazil);
    expect(portuguese.localize("Frame 2") == "Quadro 2" &&
               portuguese.localize("enemy.studio.slime") == "enemy.studio.slime",
           "Content Studio localization translates UI prefixes but preserves content IDs");

    const auto path = std::filesystem::temp_directory_path() / "underworld_editor_localization_settings.json";
    std::error_code fsError;
    std::filesystem::remove(path, fsError);
    const auto defaults = editor::loadEditorPreferences(path);
    std::string error;
    editor::EditorPreferences saved;
    saved.language = EditorLanguage::englishUnitedStates;
    saved.leftPanelWidth = 420;
    saved.rightPanelWidth = 360;
    expect(defaults.language == EditorLanguage::portugueseBrazil &&
               editor::saveEditorPreferences(path, saved, error) && error.empty() &&
               editor::loadEditorPreferences(path).language == EditorLanguage::englishUnitedStates &&
               editor::loadEditorPreferences(path).leftPanelWidth == 420 &&
               editor::loadEditorPreferences(path).rightPanelWidth == 360,
           "Content Studio language and panel-width preferences persist between runs");
    {
        std::ofstream invalidWidths(path, std::ios::binary | std::ios::trunc);
        invalidWidths << R"({"language":"en-US","leftPanelWidth":1,"rightPanelWidth":9999})";
    }
    const auto clampedPreferences = editor::loadEditorPreferences(path);
    expect(clampedPreferences.leftPanelWidth == editor::EditorLayoutMetrics::minimumLeft &&
               clampedPreferences.rightPanelWidth == editor::EditorLayoutMetrics::maximumPanel,
           "Invalid saved panel widths use safe clamped values");
    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "not valid settings";
    }
    expect(editor::loadEditorPreferences(path).language == EditorLanguage::portugueseBrazil,
           "Corrupt Content Studio preferences safely fall back to Portuguese");
    std::filesystem::remove(path, fsError);

    std::string utf8 = "Missão";
    expect(core::utf8CodepointCount(utf8) == 6 && core::eraseLastUtf8Codepoint(utf8) && utf8 == "Missã" &&
               core::appendUtf8Codepoint(utf8, 0x006fU) && utf8 == "Missão",
           "UTF-8 editor text editing counts codepoints and backspaces safely");
    render::Framebuffer framebuffer(32, 16);
    editor::EditorInputState input;
    input.textInput = "çã";
    render::Renderer2D renderer(framebuffer);
    editor::EditorUiContext ui(renderer, nullptr, input, portuguese);
    std::string field;
    (void)ui.textField({0, 0, 20, 9}, field, true, 2);
    expect(field == "çã", "Editor text fields accept Portuguese UTF-8 input");
    {
        render::Framebuffer editFramebuffer(160, 16);
        editor::EditorInputState editInput;
        editor::TextEditState editState;
        render::Renderer2D editRenderer(editFramebuffer);
        editor::EditorUiContext editUi(editRenderer, nullptr, editInput, portuguese,
                                       {}, &editState);
        std::string editable = "map.untitled";
        const core::RectI editBounds{0, 0, 160, 16};
        static_cast<void>(editUi.textField(editBounds, editable, true));
        editInput.pointer.x = 4 + 4 * 7;
        editInput.pointer.leftPressed = true;
        static_cast<void>(editUi.textField(editBounds, editable, true));
        editInput.pointer.leftPressed = false;
        editInput.textInput = "room.";
        static_cast<void>(editUi.textField(editBounds, editable, true));
        expect(editable == "map.room.untitled",
               "text fields insert UTF-8 text at the clicked cursor position");
        editInput.textInput.clear();
        editInput.endPressed = true;
        static_cast<void>(editUi.textField(editBounds, editable, true));
        editInput.endPressed = false;
        editInput.leftPressed = true;
        static_cast<void>(editUi.textField(editBounds, editable, true));
        editInput.leftPressed = false;
        editInput.textInput = "X";
        static_cast<void>(editUi.textField(editBounds, editable, true));
        expect(editable == "map.room.untitleXd",
               "text fields move the cursor with the arrow keys before insertion");
        editInput.textInput.clear();
        editInput.backspacePressed = true;
        static_cast<void>(editUi.textField(editBounds, editable, true));
        editInput.backspacePressed = false;
        editInput.homePressed = true;
        static_cast<void>(editUi.textField(editBounds, editable, true));
        editInput.homePressed = false;
        editInput.textInput = "X";
        static_cast<void>(editUi.textField(editBounds, editable, true));
        expect(editable == "Xmap.room.untitled",
               "text fields support Backspace and Home cursor navigation");
        editInput.textInput.clear();
        editInput.deletePressed = true;
        static_cast<void>(editUi.textField(editBounds, editable, true));
        expect(editable == "Xap.room.untitled",
               "text fields delete the character at the cursor");
    }
}

void testEditorResponsiveLayerWorkflow() {
    using namespace underworld;
    using editor::EditorIcon;

    const auto columns = editor::equalColumns({10, 20, 100, 18}, 3, 4);
    expect(columns.size() == 3 && columns[0].x == 10 && columns[0].width == 31 &&
               columns[1].x == 45 && columns[1].width == 31 &&
               columns[2].x == 80 && columns[2].width == 30,
           "responsive layout distributes extra width deterministically across columns");
    const auto narrowRow = editor::makeLayerRowLayout({8, 50, 174, 18});
    const auto wideRow = editor::makeLayerRowLayout({8, 50, 334, 18});
    expect(narrowRow.dragHandle.width == editor::EditorLayoutMetrics::layerDragHandleWidth &&
               narrowRow.visibility.width == editor::EditorLayoutMetrics::layerVisibilityWidth &&
               narrowRow.lock.width == editor::EditorLayoutMetrics::layerLockWidth &&
               narrowRow.name.x > narrowRow.dragHandle.x &&
               narrowRow.name.x + narrowRow.name.width <= narrowRow.visibility.x,
           "layer row keeps fixed affordances separate from the flexible name bounds");
    expect(wideRow.name.width > narrowRow.name.width &&
               wideRow.name.x + wideRow.name.width <= wideRow.visibility.x,
           "layer names consume additional panel width without overlapping icons");

    const auto splitLeft = editor::splitFixedLeft({0, 0, 200, 20}, 32, 4);
    const auto splitRight = editor::splitFixedRight({0, 0, 200, 20}, 32, 4);
    expect(splitLeft.first.width == 32 && splitLeft.second.x == 36 &&
               splitLeft.first.width + splitLeft.second.width + 4 == 200 &&
               splitRight.second.width == 32 && splitRight.first.width + splitRight.second.width + 4 == 200,
           "fixed utility controls leave the remaining row space flexible");

    const auto first = editor::layerDropPreview({20, 5}, {10, 0, 100, 60}, 3, 2, 0);
    const auto middle = editor::layerDropPreview({20, 39}, {10, 0, 100, 60}, 3, 0, 0);
    const auto last = editor::layerDropPreview({20, 55}, {10, 0, 100, 60}, 3, 0, 0);
    const auto scrolled = editor::layerDropPreview({20, 5}, {10, 0, 100, 40}, 4, 3, 20);
    expect(first && first->hoveredIndex == 0 && first->targetIndex == 0 && !first->after &&
               middle && middle->hoveredIndex == 1 && middle->targetIndex == 1 && middle->after &&
               last && last->targetIndex == 2 && last->after &&
               scrolled && scrolled->hoveredIndex == 1 &&
               !editor::layerDropPreview({120, 5}, {10, 0, 100, 60}, 3, 0, 0),
           "layer drop index respects before/after halves, scroll offset and list bounds");
    expect(editor::clampScroll(-10, 200, 60) == 0 &&
               editor::clampScroll(500, 200, 60) == 140 &&
               editor::autoScrollLayerList(20, {20, 2}, {10, 0, 100, 60}, 200) == 12 &&
               editor::autoScrollLayerList(20, {20, 58}, {10, 0, 100, 60}, 200) == 28,
           "layer scrolling clamps and auto-scrolls at both list edges");

    const auto tooltipTopRight = editor::placeTooltip({92, 1, 8, 18}, 80, 18, {0, 0, 100, 80});
    const auto tooltipBottom = editor::placeTooltip({10, 78, 8, 2}, 40, 18, {0, 0, 100, 80});
    expect(tooltipTopRight.x >= 0 && tooltipTopRight.y >= 0 &&
               tooltipTopRight.x + tooltipTopRight.width <= 100 &&
               tooltipTopRight.y + tooltipTopRight.height <= 80 &&
               tooltipBottom.x >= 0 && tooltipBottom.y >= 0 &&
               tooltipBottom.x + tooltipBottom.width <= 100 &&
               tooltipBottom.y + tooltipBottom.height <= 80,
           "tooltip placement stays inside the framebuffer near all edges");

    const std::array<EditorIcon, 15> icons{{
        EditorIcon::add, EditorIcon::remove, EditorIcon::visibilityOn,
        EditorIcon::visibilityOff, EditorIcon::lock, EditorIcon::unlock,
        EditorIcon::dragHandle, EditorIcon::moveUp, EditorIcon::moveDown,
        EditorIcon::play, EditorIcon::save, EditorIcon::folder, EditorIcon::map,
        EditorIcon::link, EditorIcon::warning,
    }};
    bool allIconsSupported = true;
    for (const auto icon : icons) allIconsSupported = allIconsSupported && editor::isEditorIconSupported(icon);
    expect(allIconsSupported, "all editor controls use a procedural icon renderer");

    editor::EditorDocument map = editor::EditorDocument::newMap(
        simulation::MapId{"map.layer.drag"}, 8, 8);
    std::string error;
    expect(map.execute(std::make_unique<editor::AddLayerCommand>(1, "Walls"), error) &&
               map.execute(std::make_unique<editor::AddLayerCommand>(2, "Foreground"), error),
           "layer drag fixture creates multiple layers through commands");
    map.activeLayer() = 0;
    const auto beforeStates = map.layerStates();
    expect(map.execute(std::make_unique<editor::MoveLayerCommand>(0, 2), error) &&
               map.data().layers[2].name == "Ground" && map.activeLayer() == 2 &&
               map.layerStates()[2].visible == beforeStates[0].visible &&
               map.undo() && map.data().layers[0].name == "Ground" && map.activeLayer() == 0 &&
               map.redo(error) && map.data().layers[2].name == "Ground" && map.activeLayer() == 2,
           "computed layer drops use one MoveLayerCommand and preserve undo/redo state");
}

void testWorldProjectAndMultiMapPlaytest() {
    namespace editor = underworld::editor;
    namespace game = underworld::game;
    namespace maps = underworld::game::maps;
    namespace simulation = underworld::simulation;
    const auto content = game::content::compileBuiltinContentOrThrow();

    auto mapA = editor::EditorDocument::newMap(simulation::MapId{"map.a"}, 8, 6, 16, true);
    const maps::MapLink link{"exit", {0, 0, 16, 16}, simulation::MapId{"map.b"},
                             simulation::SpawnId{"entry.start"}};
    std::string error;
    expect(mapA.execute(std::make_unique<editor::PlaceEntityCommand>(link), error),
           "world project fixture authors a cross-map link through an editor command");
    auto project = editor::WorldProjectDocument::newProject(std::move(mapA));
    expect(project.createMap(simulation::MapId{"map.b"}, 8, 6, 16, true, content, error) &&
               project.maps().size() == 2 && project.entryMapId() == simulation::MapId{"map.a"},
           "world project creates a second authored map and keeps the first entry map");
    expect(project.setActiveMap(simulation::MapId{"map.a"}, error) &&
               project.activeMapId() == simulation::MapId{"map.a"},
           "world project switches active maps without replacing documents");
    auto* mapB = project.findMap(simulation::MapId{"map.b"});
    expect(mapB != nullptr && mapB->execute(std::make_unique<editor::PlaceEntityCommand>(
               maps::PlayerSpawn{simulation::SpawnId{"entry.extra"}, {32, 32},
                                 game::gameplay::FacingDirection::down}), error),
           "map-local command history edits the second map");
    expect(project.setActiveMap(simulation::MapId{"map.b"}, error) && project.activeDocument().data().playerSpawns.size() == 2 &&
               project.activeDocument().undo() && project.activeDocument().data().playerSpawns.size() == 1,
           "switching maps preserves per-map undo history");
    expect(project.setActiveMap(simulation::MapId{"map.a"}, error) && project.activeDocument().data().links.size() == 1,
           "returning to map A preserves its authored link and data");

    const auto source = project.authoredSource();
    const auto encoded = maps::encodeAuthoredWorldJson(source);
    const auto decoded = maps::decodeAuthoredWorldJson(encoded);
    expect(decoded.source && decoded.source->entryMapId == source.entryMapId &&
               decoded.source->maps.size() == source.maps.size() &&
               maps::encodeAuthoredWorldJson(*decoded.source) == encoded,
           "UWORLD v1 round-trips multiple maps with canonical deterministic JSON");
    expect(!maps::validateAuthoredWorld([&] { auto value = source; value.entryMapId = simulation::MapId{"missing"}; return value; }()).valid(),
           "UWORLD validation rejects an entry map that is not present");
    auto duplicateMap = source;
    duplicateMap.maps.push_back(source.maps.front());
    const auto duplicateResult = maps::validateAuthoredWorld(duplicateMap);
    expect(!duplicateResult.valid() && duplicateResult.diagnostics.front().code == "duplicate_map_id",
           "UWORLD validation rejects duplicate MapId values");
    auto utf8World = source;
    utf8World.maps[1].geometry.id = simulation::MapId{"map.café"};
    utf8World.maps[0].geometry.links[0].targetMapId = utf8World.maps[1].geometry.id;
    const auto utf8RoundTrip = maps::decodeAuthoredWorldJson(maps::encodeAuthoredWorldJson(utf8World));
    expect(utf8RoundTrip.source && utf8RoundTrip.source->maps[1].geometry.id == simulation::MapId{"map.café"},
           "UWORLD preserves UTF-8 MapId values");
    auto brokenMap = source;
    brokenMap.maps[0].geometry.links[0].targetMapId = simulation::MapId{"map.missing"};
    const auto brokenMapResult = maps::validateAuthoredWorld(brokenMap);
    expect(!brokenMapResult.valid() && brokenMapResult.diagnostics.front().code == "missing_target_map",
           "UWORLD validation diagnoses a missing target map with a stable code");
    auto brokenSpawn = source;
    brokenSpawn.maps[0].geometry.links[0].targetSpawnId = simulation::SpawnId{"spawn.missing"};
    const auto brokenSpawnResult = maps::validateAuthoredWorld(brokenSpawn);
    expect(!brokenSpawnResult.valid() && brokenSpawnResult.diagnostics.front().code == "missing_target_spawn",
           "UWORLD validation diagnoses a missing target spawn");
    const std::string malformed = R"({"format":"dungeon-underworld-world-project","version":1,"entryMapId":"map.a","maps":[],"extra":1})";
    expect(!maps::decodeAuthoredWorldJson(malformed).source,
           "UWORLD decoder rejects unknown fields and an empty map set");
    expect(!maps::decodeAuthoredWorldJson("{").source,
           "UWORLD decoder rejects malformed JSON");
    const std::string wrongVersion = R"({"format":"dungeon-underworld-world-project","version":2,"entryMapId":"map.a","maps":[]})";
    expect(!maps::decodeAuthoredWorldJson(wrongVersion).source,
           "UWORLD decoder rejects unsupported versions");

    const auto root = std::filesystem::temp_directory_path() / "underworld_world_project_tests";
    std::error_code fsError; std::filesystem::remove_all(root, fsError); std::filesystem::create_directories(root, fsError);
    const auto worldPath = root / "Dungeon.uworld";
    expect(project.saveAs(worldPath, content, error) && !project.dirty() &&
               maps::readAuthoredWorldFile(worldPath).source.has_value(),
           "world project saves atomically and clears dirty state only after success");
    std::ifstream savedWorld(worldPath, std::ios::binary);
    std::ostringstream savedWorldText; savedWorldText << savedWorld.rdbuf();
    auto invalidWorld = project.authoredSource();
    invalidWorld.entryMapId = simulation::MapId{"missing"};
    const bool rejectedWrite = !maps::writeAuthoredWorldFile(worldPath, invalidWorld, error);
    std::ifstream verifiedWorld(worldPath, std::ios::binary);
    std::ostringstream verifiedWorldText; verifiedWorldText << verifiedWorld.rdbuf();
    expect(rejectedWrite && savedWorldText.str() == verifiedWorldText.str(),
           "UWORLD atomic validation failure does not truncate the authored project");
    savedWorld.close();
    verifiedWorld.close();
    expect(project.setEntryMap(simulation::MapId{"map.b"}, error) && project.dirty() &&
               !project.saveAs(root / "invalid.extension", content, error) && project.dirty(),
           "failed world save preserves the dirty state");
    expect(project.setEntryMap(simulation::MapId{"map.a"}, error) && project.save(content, error) && !project.dirty(),
           "successful world save restores the saved clean state");
    const auto exportDirectory = root / "runtime-export";
    expect(project.exportDmaps(exportDirectory, content, error) &&
               maps::readDmap(exportDirectory / "map.a.dmap") &&
               maps::readDmap(exportDirectory / "map.b.dmap"),
           "world project exports deterministic per-map DMAP runtime artifacts");
    auto reopened = editor::WorldProjectDocument::open(worldPath, content, error);
    expect(reopened && reopened->maps().size() == 2 && reopened->entryMapId() == simulation::MapId{"map.a"} &&
               reopened->maps()[0].data().links[0].targetMapId == simulation::MapId{"map.b"},
           "reopening UWORLD restores map order, entry map and cross-map link");
    const auto standaloneMapPath = root / "standalone.umap";
    const auto importedMapPath = root / "imported.umap";
    expect(maps::writeAuthoredMapFile(standaloneMapPath,
                                      maps::authoredMapFromMapData(project.maps()[0].data()), error) &&
               maps::writeAuthoredMapFile(importedMapPath,
                                          maps::authoredMapFromMapData(project.maps()[1].data()), error),
           "standalone UMAP fixtures can be written for project import compatibility");
    auto standalone = editor::EditorDocument::open(standaloneMapPath, content, error);
    auto standaloneProject = standalone
        ? std::optional<editor::WorldProjectDocument>{editor::WorldProjectDocument::fromStandalone(std::move(*standalone))}
        : std::nullopt;
    expect(standaloneProject && standaloneProject->importMap(importedMapPath, content, error) &&
               standaloneProject->projectMode() && !standaloneProject->filePath() &&
               !standaloneProject->save(content, error) && error.find("Save As") != std::string::npos,
           "importing a UMAP promotes the project and requires explicit UWORLD Save As");
    expect(project.createMap(simulation::MapId{"map.c"}, 8, 6, 16, true, content, error) &&
               project.removeMap(simulation::MapId{"map.c"}, error),
           "unreferenced project maps can be removed");
    expect(!project.removeMap(simulation::MapId{"map.b"}, error) && error.find("targets it") != std::string::npos,
           "referenced maps cannot be removed silently");
    expect(!project.removeMap(simulation::MapId{"map.a"}, error) && error.find("entry") != std::string::npos,
           "the entry map cannot be removed");

    const auto layout = editor::makeEditorShellLayout(1000, 700, {900, 900});
    expect(layout.viewport.width > 0 && layout.viewport.height > 0 && layout.left.width >= editor::EditorLayoutMetrics::minimumLeft &&
               layout.right.width >= editor::EditorLayoutMetrics::minimumRight,
           "resizable editor layout clamps panel widths while keeping a usable viewport");
    const auto smallLayout = editor::makeEditorShellLayout(500, 350, {400, 400});
    const auto largeLayout = editor::makeEditorShellLayout(1600, 900, {200, 300});
    expect(smallLayout.viewport.width > 0 && smallLayout.viewport.height > 0 &&
               largeLayout.viewport.width > smallLayout.viewport.width,
           "editor layout remains usable at smaller and larger window sizes");
    expect(editor::ellipsizeText("map.village-á", 70) == "map.vil...",
           "editor ellipsis preserves whole UTF-8 codepoints");
    expect(!editor::fitText("C:\\folder\\arquivo.json", 70, true).empty() &&
               editor::fitText("C:\\folder\\arquivo.json", 70, true).front() == '.',
           "editor text fields fit long paths inside their bounds");

    editor::EditorPlaytestSession playtest;
    expect(playtest.start(source, content, simulation::MapId{"map.a"}, error) && playtest.world() &&
               playtest.world()->id() == simulation::MapId{"map.a"},
           "multi-map playtest starts from the active authored map in memory");
    simulation::PlayerCommand command; command.tick = 1;
    playtest.tick(command);
    expect(playtest.world() && playtest.world()->id() == simulation::MapId{"map.b"} &&
               playtest.world()->spawn().id == simulation::SpawnId{"entry.start"},
           "multi-map playtest crosses MapLink through the existing MapSession runtime logic");
    auto* unsavedMapB = project.findMap(simulation::MapId{"map.b"});
    auto* unsavedMapA = project.findMap(simulation::MapId{"map.a"});
    expect(unsavedMapB != nullptr && unsavedMapA != nullptr &&
               unsavedMapB->execute(std::make_unique<editor::PlaceEntityCommand>(
                   maps::PlayerSpawn{simulation::SpawnId{"entry.unsaved"}, {48, 48},
                                     game::gameplay::FacingDirection::down}), error) &&
               unsavedMapA->execute(std::make_unique<editor::SetMapLinkTargetCommand>(
                   "exit", simulation::MapId{"map.b"}, simulation::SpawnId{"entry.unsaved"}), error) &&
               project.dirty(),
           "unsaved edits to both maps remain available to project playtest");
    const auto unsavedSource = project.authoredSource();
    editor::EditorPlaytestSession unsavedPlaytest;
    expect(unsavedPlaytest.start(unsavedSource, content, simulation::MapId{"map.a"}, error),
           "playtest compiles the current in-memory project instead of requiring a saved file");
    unsavedPlaytest.tick(command);
    expect(unsavedPlaytest.world() && unsavedPlaytest.world()->id() == simulation::MapId{"map.b"} &&
               unsavedPlaytest.world()->spawn().id == simulation::SpawnId{"entry.unsaved"},
           "multi-map playtest transition resolves an unsaved target spawn");
    std::filesystem::remove_all(root, fsError);
}

int main() {
    try {
        testMetrics();
        testFramebuffer();
        testRendererPrimitives();
        testImagesAndBlits();
        testNearestImageRegions();
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
        testGameSessionCommandBoundary();
        testPlayerCollision();
        testPlayerAuthoredMovementCollision();
        testPlayerVisualAndCameraFollow();
        testActionCommandsAndPlayerAttackState();
        testAttackTimeline();
        testEntityHandlesAndActorOrder();
        testCombatSystem();
        testProjectilesAndEffects();
        testPhase6CombatGeneralization();
        testCreatureDefinitionsAndBehavior();
        testCreatureCombatIntegration();
        testItemsInventoryAndWallet();
        testPickupsQuickSlotsAndInventoryOverlay();
        testViewModelAndWorldObjects();
        testBreakableProps();
        testPhase8PersistentMapsAndSave();
        testSceneRuntimeController();
        testSceneTimelineAuthoring();
        testWorldObjectPersistencePolicies();
        testPhase10NpcFoundation();
        testPhase10DialogueDataModel();
        testPhase10DialogueSession();
        testPhase11QuestDefinitions();
        testPhase11QuestState();
        testPhase11QuestEvents();
        testPhase11QuestPersistence();
        testPhase9EditorFoundation();
        testSyntheticMapIntegrationFixture();
        testOfficialGameplayMapAuthoringAsset();
        testOfficialGameplayMapSet();
        testPhase9StartupAndEditorPerformanceContracts();
        testGameplayMapDiscovery();
        testRuntimeVisualSynchronization();
        testMultiTilesetAuthoringAndRuntime();
        testSemanticAuthoringFoundation();
        testMapCompositionFoundation();
        testAuditSessionFoundation();
        testAuditFramebufferScreenshots();
        testHeadlessAuditPlatform();
        testPresentationRect();
        testFixedStepAccumulator();
        testWin32Clock();
        testAuthoredContentBoundary();
        testContentJsonAtomicWriter();
        testPhase12AProgressionFoundation();
        testPhase12BRewards();
        testPhase12BRewardEventSnapshot();
        testPhase12C1Equipment();
        testPhase12C1EquipmentTransactions();
        testPhase12DBank();
        testPhase12D2BankInterface();
        testPhase12E1RewardGrants();
        testPhase12E2Shops();
        testPhase12E3ShopInterface();
        testPhase13AJsonFoundation();
        testPhase13A2JsonDecoders();
        testPhase13A3JsonDecoders();
        testPhase13B1ContentWorkspace();
        testPhase14AuthoredMapFoundation();
        testPhase14WorldClosure();
        testPhase15PresentationFeedback();
        testPhase16InteractiveWorld();
        testPhase17VisualContentBoundary();
        testPhase18ContentStudioFoundation();
        testPhase18StudioVisualValidation();
        testPhase18BVisualPreview();
        testPhase18CGameplayContentEditors();
        testPhase18DUnifiedStudioWorkflow();
        testContentStudioDeepAuthoringHelpers();
        testContentStudioAuthoredEntityPlacementWorkflow();
        testEditorLocalization();
        testEditorResponsiveLayerWorkflow();
        testWorldProjectAndMultiMapPlaytest();
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
