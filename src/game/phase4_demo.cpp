#include "game/phase4_demo.h"

#include "engine/core/game_metrics.h"
#include "engine/platform/image_decoder.h"
#include "engine/render/animation.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/camera_2d.h"
#include "engine/render/framebuffer.h"
#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"
#include "engine/world/collision.h"
#include "engine/world/runtime_map.h"
#include "engine/world/tile.h"
#include "game/command_builder.h"
#include "game/gameplay/player.h"
#include "game/player_visual.h"

#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace underworld::game {

namespace {

constexpr int mapWidthTiles = 64;
constexpr int mapHeightTiles = 48;
constexpr int atlasColumns = 19;
constexpr world::TilesetId dungeonTilesetId = 1;
constexpr simulation::PlayerId localPlayerId{0};
constexpr core::WorldPointI playerSpawn{104, 144};

constexpr std::uint32_t atlasIndex(int x, int y) {
    return static_cast<std::uint32_t>(y * atlasColumns + x);
}

// Phase 3/4 demo selection only; this is not a semantic catalog for the full atlas.
constexpr std::uint32_t floorPlain = atlasIndex(10, 4);
constexpr std::uint32_t floorCracked = atlasIndex(11, 4);
constexpr std::uint32_t wallHorizontal = atlasIndex(11, 2);
constexpr std::uint32_t wallVertical = atlasIndex(1, 6);
constexpr std::uint32_t wallLowerFace = atlasIndex(4, 9);
constexpr std::uint32_t floorMarkFirst = atlasIndex(14, 4);
constexpr std::array<std::uint32_t, 3> archTiles{
    atlasIndex(4, 0), atlasIndex(5, 0), atlasIndex(6, 0)};

world::TileCell tile(std::uint32_t sourceIndex, bool flipX = false) {
    return world::TileRef{{dungeonTilesetId, sourceIndex},
                          flipX ? world::TileFlags::flipX : world::TileFlags::none};
}

std::shared_ptr<const render::AnimationClip> makeDirectionalClip(
    std::string id, std::shared_ptr<const render::SpriteSheet> sheet,
    int row, int frameCount, std::uint32_t durationTicks) {
    constexpr int frameSize = 32;
    constexpr core::PointI feetAnchor{16, 31};
    std::vector<render::AnimationFrame> frames;
    frames.reserve(static_cast<std::size_t>(frameCount));
    for (int column = 0; column < frameCount; ++column) {
        frames.push_back({{{column * frameSize, row * frameSize, frameSize, frameSize},
                           feetAnchor, {}, false},
                          durationTicks,
                          {}});
    }
    return std::make_shared<const render::AnimationClip>(
        std::move(id), std::move(sheet), std::move(frames), true);
}

} // namespace

struct Phase4Demo::State final {
    State(std::shared_ptr<const render::Image> tileImage,
          std::shared_ptr<const render::Image> fontImage,
          std::shared_ptr<const render::Image> idleImage,
          std::shared_ptr<const render::Image> walkImage)
        : tileset(std::move(tileImage)),
          atlas(tileset->width(), tileset->height(), core::GameMetrics::tileSize),
          font(std::move(fontImage)),
          idleSheet(std::make_shared<const render::SpriteSheet>(std::move(idleImage))),
          walkSheet(std::make_shared<const render::SpriteSheet>(std::move(walkImage))),
          map(mapWidthTiles, mapHeightTiles, core::GameMetrics::tileSize),
          camera(core::GameMetrics::logicalWidth, core::GameMetrics::logicalHeight),
          player(localPlayerId, playerSpawn) {
        if (atlas.columns() != 19 || atlas.rows() != 12) {
            throw std::runtime_error("Phase 4 demo expects the audited 19x12 dungeon tileset");
        }
        groundLayer = map.addLayer("ground");
        lowLayer = map.addLayer("decoration_low");
        foregroundLayer = map.addLayer("foreground");
        buildDungeon();
        if (world::querySolidTiles(map.collision(), player.collisionBody(), map.tileSize()).collides) {
            throw std::runtime_error("Phase 4 player spawn overlaps map collision");
        }

        PlayerVisual::DirectionalClips idleClips{
            makeDirectionalClip("player.idle.down", idleSheet, 0, 2, 30),
            makeDirectionalClip("player.idle.up", idleSheet, 1, 2, 30),
            makeDirectionalClip("player.idle.side", idleSheet, 2, 2, 30)};
        PlayerVisual::DirectionalClips walkClips{
            makeDirectionalClip("player.walk.down", walkSheet, 0, 4, 8),
            makeDirectionalClip("player.walk.up", walkSheet, 1, 4, 8),
            makeDirectionalClip("player.walk.side", walkSheet, 2, 4, 8)};
        visual = std::make_unique<PlayerVisual>(std::move(idleClips), std::move(walkClips));
        visual->update(player.motionState(), player.facing(), 0);
        followPlayer();
    }

    void buildDungeon() {
        auto& ground = map.layer(groundLayer);
        for (int y = 0; y < map.heightTiles(); ++y) {
            for (int x = 0; x < map.widthTiles(); ++x) {
                const std::uint32_t floor = (x * 7 + y * 11) % 29 == 0
                                                ? floorCracked
                                                : floorPlain;
                ground.set(x, y, tile(floor));
            }
        }

        horizontalWall(2, 61, 2);
        horizontalWall(2, 61, 45);
        verticalWall(2, 3, 44, false);
        verticalWall(61, 3, 44, true);
        horizontalWall(2, 10, 15);
        horizontalWall(14, 28, 15);
        verticalWallWithGap(28, 2, 28, 9, 11, true);
        horizontalWall(28, 43, 28);
        horizontalWall(47, 61, 28);
        verticalWallWithGap(44, 28, 45, 36, 38, false);

        auto& foreground = map.layer(foregroundLayer);
        for (int index = 0; index < 3; ++index) {
            foreground.set(11 + index, 14, tile(archTiles[static_cast<std::size_t>(index)]));
        }

        auto& low = map.layer(lowLayer);
        for (int y = 5; y < map.heightTiles() - 3; y += 7) {
            for (int x = 5; x < map.widthTiles() - 3; x += 11) {
                if (!map.collision().isSolid(x, y)) {
                    low.set(x, y, tile(floorMarkFirst +
                                            static_cast<std::uint32_t>((x + y) % 5)));
                }
            }
        }
        low.set(7, 7, tile(wallVertical));
        map.collision().setSolid(6, 7, true);
    }

    void horizontalWall(int firstX, int lastX, int y) {
        auto& low = map.layer(lowLayer);
        auto& foreground = map.layer(foregroundLayer);
        for (int x = firstX; x <= lastX; ++x) {
            low.set(x, y, tile(wallHorizontal));
            map.collision().setSolid(x, y, true);
            if (y + 1 < map.heightTiles()) {
                foreground.set(x, y + 1, tile(wallLowerFace));
            }
        }
    }

    void verticalWall(int x, int firstY, int lastY, bool flip) {
        auto& low = map.layer(lowLayer);
        for (int y = firstY; y <= lastY; ++y) {
            low.set(x, y, tile(wallVertical, flip));
            map.collision().setSolid(x, y, true);
        }
    }

    void verticalWallWithGap(int x, int firstY, int lastY,
                             int gapFirstY, int gapLastY, bool flip) {
        for (int y = firstY; y <= lastY; ++y) {
            if (y < gapFirstY || y > gapLastY) {
                verticalWall(x, y, y, flip);
            }
        }
    }

    void followPlayer() {
        camera.centerOn(player.feetPosition());
        camera.clampToWorld(map.worldWidthPixels(), map.worldHeightPixels());
    }

    void update(simulation::Tick tick, const platform::InputState& input,
                platform::DebugInputState debugInput) {
        const simulation::PlayerCommand command = commandBuilder.build(tick, localPlayerId, input);
        player.update(command, map.collision(), map.tileSize());
        visual->update(player.motionState(), player.facing());
        followPlayer();
        if (debugInput.toggleCollisionPressed) {
            collisionOverlay = !collisionOverlay;
        }
        lastTick = tick;
        lastSequence = command.sequence;
    }

    std::size_t renderLayer(render::Renderer2D& renderer, const world::TileLayer& layer,
                            render::VisibleTileRange visible) const {
        if (!layer.visible() || visible.empty()) {
            return 0;
        }
        const auto cameraPosition = camera.position();
        for (int y = visible.firstY; y <= visible.lastY; ++y) {
            for (int x = visible.firstX; x <= visible.lastX; ++x) {
                const world::TileCell& cell = layer.cell(x, y);
                if (!cell) {
                    continue;
                }
                if (cell->definition.tilesetId != dungeonTilesetId) {
                    throw std::runtime_error("Phase 4 demo encountered an unknown tileset id");
                }
                const core::RectI source = atlas.sourceRect(cell->definition.sourceIndex);
                const int destinationX = x * map.tileSize() - cameraPosition.x;
                const int destinationY = y * map.tileSize() - cameraPosition.y;
                if (world::hasFlag(cell->flags, world::TileFlags::flipX)) {
                    renderer.drawImageRegionFlipX(*tileset, source, destinationX, destinationY);
                } else {
                    renderer.drawImageRegion(*tileset, source, destinationX, destinationY);
                }
            }
        }
        return visible.tileCount();
    }

    void renderPlayer(render::Renderer2D& renderer) const {
        const auto logical = camera.worldToLogical(player.feetPosition());
        render::drawAnimator(renderer, visual->animator(), {logical.x, logical.y},
                             visual->flipX());
    }

    void renderCollisionOverlay(render::Renderer2D& renderer,
                                render::VisibleTileRange visible) const {
        if (!collisionOverlay || visible.empty()) {
            return;
        }
        const auto cameraPosition = camera.position();
        constexpr core::ColorRGBA8 fill{255, 24, 32, 72};
        constexpr core::ColorRGBA8 edge{255, 96, 96, 180};
        for (int y = visible.firstY; y <= visible.lastY; ++y) {
            for (int x = visible.firstX; x <= visible.lastX; ++x) {
                if (!map.collision().isSolid(x, y)) {
                    continue;
                }
                const int logicalX = x * map.tileSize() - cameraPosition.x;
                const int logicalY = y * map.tileSize() - cameraPosition.y;
                renderer.fillRect({logicalX, logicalY, map.tileSize(), map.tileSize()}, fill);
                renderer.fillRect({logicalX, logicalY, map.tileSize(), 1}, edge);
                renderer.fillRect({logicalX, logicalY, 1, map.tileSize()}, edge);
            }
        }
        const auto body = player.collisionBody();
        const auto bodyLogical = camera.worldToLogical({body.x, body.y});
        renderer.fillRect({bodyLogical.x, bodyLogical.y, body.width, 1}, {32, 255, 255, 255});
        renderer.fillRect({bodyLogical.x, bodyLogical.y, 1, body.height}, {32, 255, 255, 255});
    }

    void renderHud(render::Renderer2D& renderer) const {
        renderer.fillRect({0, 0, core::GameMetrics::logicalWidth, 29}, {8, 10, 16, 220});
        const auto feet = player.feetPosition();
        std::ostringstream first;
        first << "PHASE_4 T " << lastTick << " P " << feet.x << ' ' << feet.y;
        render::drawText(renderer, font, first.str(), 3, 3);
        std::ostringstream second;
        second << gameplay::facingName(player.facing()) << ' '
               << gameplay::motionStateName(player.motionState()) << " CAM "
               << camera.position().x << ' ' << camera.position().y << " S " << lastSequence;
        render::drawText(renderer, font, second.str(), 3, 12);
        render::drawText(renderer, font,
                         collisionOverlay ? "WASD ARROWS MOVE C COLLISION ON"
                                          : "WASD ARROWS MOVE C COLLISION OFF",
                         3, 21);
    }

    void render(render::Framebuffer& framebuffer) const {
        framebuffer.clear({28, 13, 22, 255});
        render::Renderer2D renderer(framebuffer);
        const auto visible = camera.visibleTiles(map.widthTiles(), map.heightTiles(), map.tileSize());
        renderLayer(renderer, map.layer(groundLayer), visible);
        renderLayer(renderer, map.layer(lowLayer), visible);
        renderPlayer(renderer);
        renderLayer(renderer, map.layer(foregroundLayer), visible);
        renderCollisionOverlay(renderer, visible);
        renderHud(renderer);
    }

    std::shared_ptr<const render::Image> tileset;
    world::TileAtlasLayout atlas;
    render::BitmapFont font;
    std::shared_ptr<const render::SpriteSheet> idleSheet;
    std::shared_ptr<const render::SpriteSheet> walkSheet;
    std::unique_ptr<PlayerVisual> visual;
    world::RuntimeMap map;
    render::Camera2D camera;
    gameplay::Player player;
    CommandBuilder commandBuilder;
    simulation::Tick lastTick{};
    std::uint32_t lastSequence{};
    std::size_t groundLayer{};
    std::size_t lowLayer{};
    std::size_t foregroundLayer{};
    bool collisionOverlay{};
};

Phase4Demo::Phase4Demo(platform::ImageDecoder& decoder,
                       const std::filesystem::path& assetRoot) {
    const auto tileset = assets_.loadImage(
        "tileset.dungeon", assetRoot / "Tileset/tileset.png", decoder);
    const auto font = assets_.loadImage("font.main", assetRoot / "fonts_index.png", decoder);
    const auto idle = assets_.loadImage(
        "player.idle", assetRoot / "Characters/Player/idle/player_idle.png", decoder);
    const auto walk = assets_.loadImage(
        "player.walk", assetRoot / "Characters/Player/walking/player_walking.png", decoder);
    state_ = std::make_unique<State>(tileset, font, idle, walk);
}

Phase4Demo::~Phase4Demo() = default;

void Phase4Demo::fixedTick(simulation::Tick tick, const platform::InputState& input,
                           platform::DebugInputState debugInput) {
    state_->update(tick, input, debugInput);
}

void Phase4Demo::render(render::Framebuffer& framebuffer) const {
    state_->render(framebuffer);
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
