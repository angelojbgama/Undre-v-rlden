#include "game/phase3_demo.h"

#include "engine/core/game_metrics.h"
#include "engine/platform/image_decoder.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/camera_2d.h"
#include "engine/render/framebuffer.h"
#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"
#include "engine/world/collision.h"
#include "engine/world/runtime_map.h"
#include "engine/world/tile.h"

#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace underworld::game {

namespace {

constexpr int mapWidthTiles = 64;
constexpr int mapHeightTiles = 48;
constexpr int atlasColumns = 19;
constexpr world::TilesetId dungeonTilesetId = 1;

constexpr std::uint32_t atlasIndex(int x, int y) {
    return static_cast<std::uint32_t>(y * atlasColumns + x);
}

// Phase 3 demo selection only; this is not a semantic catalog for the full atlas.
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

int axis(bool positive, bool negative, int speed) {
    return (positive ? speed : 0) - (negative ? speed : 0);
}

} // namespace

struct Phase3Demo::State final {
    State(std::shared_ptr<const render::Image> tileImage,
          std::shared_ptr<const render::Image> fontImage)
        : tileset(std::move(tileImage)), atlas(tileset->width(), tileset->height(),
          core::GameMetrics::tileSize), font(std::move(fontImage)),
          map(mapWidthTiles, mapHeightTiles, core::GameMetrics::tileSize),
          camera(core::GameMetrics::logicalWidth, core::GameMetrics::logicalHeight) {
        if (atlas.columns() != 19 || atlas.rows() != 12) {
            throw std::runtime_error("Phase 3 demo expects the audited 19x12 dungeon tileset");
        }
        groundLayer = map.addLayer("ground");
        lowLayer = map.addLayer("decoration_low");
        foregroundLayer = map.addLayer("foreground");
        buildDungeon();
        camera.setPosition({0, 0});
        camera.clampToWorld(map.worldWidthPixels(), map.worldHeightPixels());
    }

    void buildDungeon() {
        auto& ground = map.layer(groundLayer);
        for (int y = 0; y < map.heightTiles(); ++y) {
            for (int x = 0; x < map.widthTiles(); ++x) {
                std::uint32_t floor = floorPlain;
                if ((x * 7 + y * 11) % 29 == 0) {
                    floor = floorCracked;
                }
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

        // A foreground arch over the first corridor demonstrates the post-body pass.
        auto& foreground = map.layer(foregroundLayer);
        for (int index = 0; index < 3; ++index) {
            foreground.set(11 + index, 14, tile(archTiles[static_cast<std::size_t>(index)]));
        }

        auto& low = map.layer(lowLayer);
        for (int y = 5; y < map.heightTiles() - 3; y += 7) {
            for (int x = 5; x < map.widthTiles() - 3; x += 11) {
                if (!map.collision().isSolid(x, y)) {
                    const auto variant = floorMarkFirst + static_cast<std::uint32_t>((x + y) % 5);
                    low.set(x, y, tile(variant));
                }
            }
        }

        // Deliberately independent cells: visual wall/non-solid and floor/solid.
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

    void update(platform::DebugInputState input) {
        constexpr int cameraPixelsPerTick = 1;
        constexpr int bodyPixelsPerTick = 2;
        camera.move(axis(input.cameraRight, input.cameraLeft, cameraPixelsPerTick),
                    axis(input.cameraDown, input.cameraUp, cameraPixelsPerTick));
        camera.clampToWorld(map.worldWidthPixels(), map.worldHeightPixels());

        const int bodyDeltaX = axis(input.bodyRight, input.bodyLeft, bodyPixelsPerTick);
        const int bodyDeltaY = axis(input.bodyDown, input.bodyUp, bodyPixelsPerTick);
        lastMovement = world::moveAgainstSolidTiles(
            map.collision(), debugBody, bodyDeltaX, bodyDeltaY, map.tileSize());
        if (input.toggleCollisionPressed) {
            collisionOverlay = !collisionOverlay;
        }
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
                    throw std::runtime_error("Phase 3 demo encountered an unknown tileset id");
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
    }

    void renderBody(render::Renderer2D& renderer) const {
        const auto logical = camera.worldToLogical({debugBody.x, debugBody.y});
        renderer.fillRect({logical.x, logical.y, debugBody.width, debugBody.height},
                          {40, 220, 255, 255});
        renderer.fillRect({logical.x + 2, logical.y + 2,
                           debugBody.width - 4, debugBody.height - 4},
                          {235, 255, 255, 255});
    }

    void renderHud(render::Renderer2D& renderer, render::VisibleTileRange visible,
                   std::size_t consulted) const {
        renderer.fillRect({0, 0, core::GameMetrics::logicalWidth, 29}, {8, 10, 16, 220});
        std::ostringstream first;
        first << "PHASE_3 CAM " << camera.position().x << ' ' << camera.position().y
              << " BODY " << debugBody.x << ' ' << debugBody.y;
        render::drawText(renderer, font, first.str(), 3, 3);
        std::ostringstream second;
        second << "VISIBLE " << visible.firstX << '.' << visible.lastX << ' '
               << visible.firstY << '.' << visible.lastY << " CELLS " << consulted;
        render::drawText(renderer, font, second.str(), 3, 12);
        render::drawText(renderer, font,
                         collisionOverlay ? "WASD CAMERA ARROWS BODY C COLLISION ON"
                                          : "WASD CAMERA ARROWS BODY C COLLISION OFF",
                         3, 21);
    }

    void render(render::Framebuffer& framebuffer) const {
        framebuffer.clear({28, 13, 22, 255});
        render::Renderer2D renderer(framebuffer);
        const auto visible = camera.visibleTiles(map.widthTiles(), map.heightTiles(), map.tileSize());
        std::size_t consulted = 0;
        consulted += renderLayer(renderer, map.layer(groundLayer), visible);
        consulted += renderLayer(renderer, map.layer(lowLayer), visible);
        renderBody(renderer); // Future entity pass intentionally sits here.
        consulted += renderLayer(renderer, map.layer(foregroundLayer), visible);
        renderCollisionOverlay(renderer, visible);
        renderHud(renderer, visible, consulted);
    }

    std::shared_ptr<const render::Image> tileset;
    world::TileAtlasLayout atlas;
    render::BitmapFont font;
    world::RuntimeMap map;
    render::Camera2D camera;
    world::AabbI debugBody{6 * core::GameMetrics::tileSize + 3,
                           8 * core::GameMetrics::tileSize + 3, 10, 10};
    world::MovementResult lastMovement{};
    std::size_t groundLayer{};
    std::size_t lowLayer{};
    std::size_t foregroundLayer{};
    bool collisionOverlay{true};
};

Phase3Demo::Phase3Demo(platform::ImageDecoder& decoder,
                       const std::filesystem::path& assetRoot) {
    const auto tileset = assets_.loadImage(
        "tileset.dungeon", assetRoot / "Tileset/tileset.png", decoder);
    const auto font = assets_.loadImage("font.main", assetRoot / "fonts_index.png", decoder);
    state_ = std::make_unique<State>(tileset, font);
}

Phase3Demo::~Phase3Demo() = default;

void Phase3Demo::fixedTick(platform::DebugInputState input) {
    state_->update(input);
}

void Phase3Demo::render(render::Framebuffer& framebuffer) const {
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
