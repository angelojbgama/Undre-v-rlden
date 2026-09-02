#include "game/phase2_demo.h"

#include "engine/core/color_rgba8.h"
#include "engine/platform/image_decoder.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/framebuffer.h"
#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"
#include "engine/render/sprite.h"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace underworld::game {

namespace {

using render::AnimationClip;
using render::AnimationFrame;

std::shared_ptr<const AnimationClip> makeClip(
    std::string id, std::shared_ptr<const render::SpriteSheet> sheet, int row,
    int columns, int cellWidth, int cellHeight, std::uint32_t frameTicks,
    core::PointI anchor) {
    std::vector<AnimationFrame> frames;
    frames.reserve(static_cast<std::size_t>(columns));
    for (int column = 0; column < columns; ++column) {
        frames.push_back({{{column * cellWidth, row * cellHeight, cellWidth, cellHeight},
                           anchor, {0, 0}, false},
                          frameTicks,
                          {}});
    }
    return std::make_shared<const AnimationClip>(std::move(id), std::move(sheet),
                                                  std::move(frames), true);
}

void drawAnchorCross(render::Renderer2D& renderer, core::PointI point) {
    constexpr core::ColorRGBA8 color{255, 40, 210, 255};
    renderer.setPixel(point.x, point.y, color);
    renderer.setPixel(point.x - 2, point.y, color);
    renderer.setPixel(point.x + 2, point.y, color);
    renderer.setPixel(point.x, point.y - 2, color);
    renderer.setPixel(point.x, point.y + 2, color);
}

} // namespace

struct Phase2Demo::Clips final {
    std::shared_ptr<const AnimationClip> idleDown;
    std::shared_ptr<const AnimationClip> idleUp;
    std::shared_ptr<const AnimationClip> idleLeft;
    std::shared_ptr<const AnimationClip> walkDown;
    std::shared_ptr<const AnimationClip> walkUp;
    std::shared_ptr<const AnimationClip> walkLeft;
    std::shared_ptr<const AnimationClip> attackDown;
    std::shared_ptr<const AnimationClip> attackUp;
    std::shared_ptr<const AnimationClip> attackLeft;
};

Phase2Demo::Phase2Demo(platform::ImageDecoder& decoder,
                       const std::filesystem::path& assetRoot)
    : clips_(std::make_unique<Clips>()) {
    const auto idleImage = assets_.loadImage(
        "player.idle", assetRoot / "Characters/Player/idle/player_idle.png", decoder);
    const auto walkImage = assets_.loadImage(
        "player.walk", assetRoot / "Characters/Player/walking/player_walking.png", decoder);
    const auto attackImage = assets_.loadImage(
        "player.attack", assetRoot / "Characters/Player/attacking/player_attacking.png", decoder);
    const auto fontImage = assets_.loadImage("font.main", assetRoot / "fonts_index.png", decoder);

    const auto idleSheet = std::make_shared<const render::SpriteSheet>(idleImage);
    const auto walkSheet = std::make_shared<const render::SpriteSheet>(walkImage);
    const auto attackSheet = std::make_shared<const render::SpriteSheet>(attackImage);

    // Timings are provisional visualization values; PNG files contain no timing metadata.
    constexpr core::PointI regularAnchor{16, 31};
    constexpr core::PointI attackAnchor{24, 31};
    clips_->idleDown = makeClip("player.idle.down", idleSheet, 0, 2, 32, 32, 30, regularAnchor);
    clips_->idleUp = makeClip("player.idle.up", idleSheet, 1, 2, 32, 32, 30, regularAnchor);
    clips_->idleLeft = makeClip("player.idle.left", idleSheet, 2, 2, 32, 32, 30, regularAnchor);
    clips_->walkDown = makeClip("player.walk.down", walkSheet, 0, 4, 32, 32, 8, regularAnchor);
    clips_->walkUp = makeClip("player.walk.up", walkSheet, 1, 4, 32, 32, 8, regularAnchor);
    clips_->walkLeft = makeClip("player.walk.left", walkSheet, 2, 4, 32, 32, 8, regularAnchor);
    clips_->attackDown = makeClip("player.attack.down", attackSheet, 0, 4, 48, 48, 6, attackAnchor);
    clips_->attackUp = makeClip("player.attack.up", attackSheet, 1, 4, 48, 48, 6, attackAnchor);
    clips_->attackLeft = makeClip("player.attack.left", attackSheet, 2, 4, 48, 48, 6, attackAnchor);

    font_ = std::make_unique<render::BitmapFont>(fontImage);
    mainAnimator_.play(clips_->idleDown);
    sideAnimator_.play(clips_->walkLeft);
    attackAnimator_.play(clips_->attackDown);
}

Phase2Demo::~Phase2Demo() = default;

void Phase2Demo::fixedTick() {
    ++ticks_;
    const int selectedMode = static_cast<int>((ticks_ / 120U) % 3U);
    if (selectedMode != mainMode_) {
        mainMode_ = selectedMode;
        if (mainMode_ == 0) {
            mainAnimator_.play(clips_->idleDown);
        } else if (mainMode_ == 1) {
            mainAnimator_.play(clips_->walkDown);
        } else {
            mainAnimator_.play(clips_->attackDown);
        }
    }
    mainAnimator_.updateTicks(1);
    sideAnimator_.updateTicks(1);
    attackAnimator_.updateTicks(1);
}

void Phase2Demo::render(render::Framebuffer& framebuffer) const {
    constexpr core::ColorRGBA8 background{12, 17, 27, 255};
    constexpr core::ColorRGBA8 panel{24, 34, 49, 255};
    constexpr core::ColorRGBA8 edge{64, 91, 116, 255};
    framebuffer.clear(background);
    render::Renderer2D renderer(framebuffer);

    renderer.fillRect({3, 2, 266, 20}, panel);
    renderer.fillRect({3, 24, 266, 112}, panel);
    renderer.fillRect({3, 139, 266, 82}, panel);
    renderer.fillRect({3, 22, 266, 1}, edge);
    renderer.fillRect({3, 136, 266, 1}, edge);
    render::drawText(renderer, *font_, "UNDERWORLD_ENGINE_PHASE_2", 52, 8);

    const char* modeLabel = mainMode_ == 0 ? "IDLE" : (mainMode_ == 1 ? "WALK" : "ATTACK");
    render::drawText(renderer, *font_, modeLabel, 13, 30);
    constexpr core::PointI mainFeet{31, 75};
    render::drawAnimator(renderer, mainAnimator_, mainFeet);
    drawAnchorCross(renderer, mainFeet);

    render::drawText(renderer, *font_, "LEFT", 67, 30);
    render::drawText(renderer, *font_, "RIGHT_FLIP", 115, 30);
    constexpr core::PointI leftFeet{82, 75};
    constexpr core::PointI rightFeet{149, 75};
    render::drawAnimator(renderer, sideAnimator_, leftFeet);
    render::drawAnimator(renderer, sideAnimator_, rightFeet, true);
    drawAnchorCross(renderer, leftFeet);
    drawAnchorCross(renderer, rightFeet);

    render::drawText(renderer, *font_, "ATTACK 48", 196, 30);
    constexpr core::PointI attackFeet{231, 75};
    render::drawAnimator(renderer, attackAnimator_, attackFeet);
    drawAnchorCross(renderer, attackFeet);

    render::drawText(renderer, *font_, "CLIPPING", 13, 89);
    const auto& clippingFrame = sideAnimator_.currentFrame().sprite;
    render::drawSprite(renderer, sideAnimator_.clip().sheet(), clippingFrame, {0, 123});
    render::drawSprite(renderer, sideAnimator_.clip().sheet(), clippingFrame, {271, 123}, true);
    render::drawSprite(renderer, sideAnimator_.clip().sheet(), clippingFrame, {16, 10});
    render::drawSprite(renderer, sideAnimator_.clip().sheet(), clippingFrame, {256, 225}, true);

    render::drawText(renderer, *font_, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 45, 145);
    render::drawText(renderer, *font_, "abcdefghijklmnopqrstuvwxyz", 45, 158);
    render::drawText(renderer, *font_, "0123456789.,!?_", 80, 171);
    render::drawText(renderer, *font_, "UNKNOWN @", 105, 184);
    render::drawText(renderer, *font_, "ANCHOR: MAGENTA CROSS", 62, 204);
}

std::filesystem::path findLicensedAssetRoot(
    const std::filesystem::path& executableDirectory) {
    std::array<std::filesystem::path, 2> starts{
        std::filesystem::current_path(), executableDirectory};
    for (std::filesystem::path start : starts) {
        for (int depth = 0; depth < 6 && !start.empty(); ++depth) {
            const auto candidate = start / "Dungeon Underworld";
            std::error_code error;
            if (std::filesystem::is_directory(candidate, error)) {
                return candidate;
            }
            const auto parent = start.parent_path();
            if (parent == start) {
                break;
            }
            start = parent;
        }
    }
    throw std::runtime_error(
        "Licensed assets were not found. Keep the local 'Dungeon Underworld' directory "
        "at the project root; it is intentionally excluded from Git.");
}

} // namespace underworld::game
