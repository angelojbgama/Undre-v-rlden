#include "game/phase5_demo.h"

#include "engine/core/game_metrics.h"
#include "engine/platform/image_decoder.h"
#include "engine/render/animation.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/camera_2d.h"
#include "engine/render/framebuffer.h"
#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"
#include "engine/simulation/entity_handle.h"
#include "engine/simulation/events.h"
#include "engine/world/collision.h"
#include "engine/world/runtime_map.h"
#include "engine/world/tile.h"
#include "game/command_builder.h"
#include "game/actor_render_order.h"
#include "game/combat_debug.h"
#include "game/effect_system.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/combat_system.h"
#include "game/gameplay/player.h"
#include "game/gameplay/projectile_system.h"
#include "game/player_visual.h"
#include "game/training_puppet.h"

#include <algorithm>
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
constexpr core::WorldPointI puppetSpawn{136, 144};

constexpr std::uint32_t atlasIndex(int x, int y) {
    return static_cast<std::uint32_t>(y * atlasColumns + x);
}

// Demo-only tile selection; it is not a semantic catalog for the full atlas.
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
    std::string id, std::shared_ptr<const render::SpriteSheet> sheet, int row,
    int frameSize, int frameCount, std::uint32_t durationTicks, core::PointI anchor,
    bool loop, std::vector<std::vector<std::string>> markers = {}) {
    std::vector<render::AnimationFrame> frames;
    frames.reserve(static_cast<std::size_t>(frameCount));
    for (int column = 0; column < frameCount; ++column) {
        std::vector<std::string> frameMarkers;
        if (static_cast<std::size_t>(column) < markers.size()) {
            frameMarkers = std::move(markers[static_cast<std::size_t>(column)]);
        }
        frames.push_back({{{column * frameSize, row * frameSize, frameSize, frameSize},
                           anchor, {}, false}, durationTicks, std::move(frameMarkers)});
    }
    return std::make_shared<const render::AnimationClip>(
        std::move(id), std::move(sheet), std::move(frames), loop);
}

PlayerVisual::DirectionalClips makeClips(
    const std::string& prefix, const std::shared_ptr<const render::SpriteSheet>& sheet,
    int frameSize, int frameCount, std::uint32_t durationTicks, core::PointI anchor,
    bool loop, const std::vector<std::vector<std::string>>& markers = {}) {
    return {
        makeDirectionalClip(prefix + ".down", sheet, 0, frameSize, frameCount,
                            durationTicks, anchor, loop, markers),
        makeDirectionalClip(prefix + ".up", sheet, 1, frameSize, frameCount,
                            durationTicks, anchor, loop, markers),
        makeDirectionalClip(prefix + ".side", sheet, 2, frameSize, frameCount,
                            durationTicks, anchor, loop, markers)};
}

std::shared_ptr<const render::AnimationClip> makeImpactClip(
    const std::shared_ptr<const render::SpriteSheet>& sheet) {
    std::vector<render::AnimationFrame> frames;
    for (int column = 0; column < 3; ++column) {
        frames.push_back({{{column * 16, 0, 16, 16}, {8, 8}, {}, false}, 4, {}});
    }
    return std::make_shared<const render::AnimationClip>(
        "effect.arrow_impact", sheet, std::move(frames), false);
}

void outline(render::Renderer2D& renderer, world::AabbI box,
             core::WorldPointI camera, core::ColorRGBA8 color) {
    const int x = box.x - camera.x;
    const int y = box.y - camera.y;
    renderer.fillRect({x, y, box.width, 1}, color);
    renderer.fillRect({x, y + box.height - 1, box.width, 1}, color);
    renderer.fillRect({x, y, 1, box.height}, color);
    renderer.fillRect({x + box.width - 1, y, 1, box.height}, color);
}

render::QuarterTurn arrowRotation(gameplay::FacingDirection direction) noexcept {
    switch (direction) {
    case gameplay::FacingDirection::up: return render::QuarterTurn::r0;
    case gameplay::FacingDirection::right: return render::QuarterTurn::r90;
    case gameplay::FacingDirection::down: return render::QuarterTurn::r180;
    case gameplay::FacingDirection::left: return render::QuarterTurn::r270;
    }
    return render::QuarterTurn::r0;
}

} // namespace

struct Phase5Demo::State final {
    State(std::shared_ptr<const render::Image> tileImage,
          std::shared_ptr<const render::Image> fontImage,
          std::shared_ptr<const render::Image> idleImage,
          std::shared_ptr<const render::Image> walkImage,
          std::shared_ptr<const render::Image> swordImage,
          std::shared_ptr<const render::Image> bowImage,
          std::shared_ptr<const render::Image> arrowImage,
          std::shared_ptr<const render::Image> puppetImage,
          std::shared_ptr<const render::Image> impactImage)
        : tileset(std::move(tileImage)),
          atlas(tileset->width(), tileset->height(), core::GameMetrics::tileSize),
          font(std::move(fontImage)),
          idleSheet(std::make_shared<const render::SpriteSheet>(std::move(idleImage))),
          walkSheet(std::make_shared<const render::SpriteSheet>(std::move(walkImage))),
          swordSheet(std::make_shared<const render::SpriteSheet>(std::move(swordImage))),
          bowSheet(std::make_shared<const render::SpriteSheet>(std::move(bowImage))),
          arrowSheet(std::make_shared<const render::SpriteSheet>(std::move(arrowImage))),
          puppetSheet(std::make_shared<const render::SpriteSheet>(std::move(puppetImage))),
          impactSheet(std::make_shared<const render::SpriteSheet>(std::move(impactImage))),
          map(mapWidthTiles, mapHeightTiles, core::GameMetrics::tileSize),
          camera(core::GameMetrics::logicalWidth, core::GameMetrics::logicalHeight),
          playerHandle(handles.create()), player(localPlayerId, playerHandle, playerSpawn),
          swordDefinition(gameplay::makePlayerSwordAttackDefinition()),
          bowDefinition(gameplay::makePlayerBowAttackDefinition()),
          arrowDefinition(gameplay::makePlayerArrowProjectileDefinition()),
          projectiles(handles, projectileCatalog) {
        if (atlas.columns() != 19 || atlas.rows() != 12) {
            throw std::runtime_error("Phase 5 demo expects the audited 19x12 dungeon tileset");
        }
        projectileCatalog.add(arrowDefinition);
        groundLayer = map.addLayer("ground");
        lowLayer = map.addLayer("decoration_low");
        foregroundLayer = map.addLayer("foreground");
        buildDungeon();
        if (world::querySolidTiles(map.collision(), player.collisionBody(), map.tileSize()).collides) {
            throw std::runtime_error("Phase 5 player spawn overlaps map collision");
        }
        puppet = std::make_unique<TrainingPuppet>(handles.create(), puppetSpawn);
        visual = std::make_unique<PlayerVisual>(
            makeClips("player.idle", idleSheet, 32, 2, 30, {16, 31}, true),
            makeClips("player.walk", walkSheet, 32, 4, 8, {16, 31}, true),
            makeClips("player.sword", swordSheet, 48, 4,
                      swordDefinition.totalTicks / 4, {24, 31}, false,
                      {{}, {"attack_on"}, {}, {"attack_off"}}),
            makeClips("player.bow", bowSheet, 32, 2,
                      bowDefinition.totalTicks / 2, {16, 31}, false,
                      {{}, {"spawn_projectile"}}));
        effects = std::make_unique<EffectSystem>(makeImpactClip(impactSheet));
        visual->update(player.motionState(), player.facing(), player.actionState(), 0);
        followPlayer();
    }

    void buildDungeon() {
        auto& ground = map.layer(groundLayer);
        for (int y = 0; y < map.heightTiles(); ++y) {
            for (int x = 0; x < map.widthTiles(); ++x) {
                ground.set(x, y, tile((x * 7 + y * 11) % 29 == 0 ? floorCracked : floorPlain));
            }
        }
        horizontalWall(2, 61, 2); horizontalWall(2, 61, 45);
        verticalWall(2, 3, 44, false); verticalWall(61, 3, 44, true);
        horizontalWall(2, 10, 15); horizontalWall(14, 28, 15);
        verticalWallWithGap(28, 2, 28, 9, 11, true);
        horizontalWall(28, 43, 28); horizontalWall(47, 61, 28);
        verticalWallWithGap(44, 28, 45, 36, 38, false);
        for (int index = 0; index < 3; ++index) {
            map.layer(foregroundLayer).set(
                11 + index, 14, tile(archTiles[static_cast<std::size_t>(index)]));
        }
        auto& low = map.layer(lowLayer);
        for (int y = 5; y < map.heightTiles() - 3; y += 7) {
            for (int x = 5; x < map.widthTiles() - 3; x += 11) {
                if (!map.collision().isSolid(x, y)) {
                    low.set(x, y, tile(floorMarkFirst + static_cast<std::uint32_t>((x + y) % 5)));
                }
            }
        }
        low.set(7, 7, tile(wallVertical));
        map.collision().setSolid(6, 7, true);
    }

    void horizontalWall(int firstX, int lastX, int y) {
        for (int x = firstX; x <= lastX; ++x) {
            map.layer(lowLayer).set(x, y, tile(wallHorizontal));
            map.collision().setSolid(x, y, true);
            if (y + 1 < map.heightTiles()) {
                map.layer(foregroundLayer).set(x, y + 1, tile(wallLowerFace));
            }
        }
    }
    void verticalWall(int x, int firstY, int lastY, bool flip) {
        for (int y = firstY; y <= lastY; ++y) {
            map.layer(lowLayer).set(x, y, tile(wallVertical, flip));
            map.collision().setSolid(x, y, true);
        }
    }
    void verticalWallWithGap(int x, int firstY, int lastY,
                             int gapFirstY, int gapLastY, bool flip) {
        for (int y = firstY; y <= lastY; ++y) {
            if (y < gapFirstY || y > gapLastY) { verticalWall(x, y, y, flip); }
        }
    }

    void followPlayer() {
        camera.centerOn(player.feetPosition());
        camera.clampToWorld(map.worldWidthPixels(), map.worldHeightPixels());
    }

    void consumeAnimationMarkers() {
        for (const render::AnimationMarkerEvent& event : visual->consumeMarkerEvents()) {
            if (event.marker == "attack_on") {
                const auto direction = gameplay::directionVector(player.facing());
                activeSword = {
                    swordDefinition.meleeHitboxes->forFacing(player.facing()).at(
                        player.feetPosition()),
                    {player.entityHandle(), player.attackInstance()}, gameplay::Faction::player,
                    swordDefinition.damage,
                    direction.x * swordDefinition.damage.knockbackPixels,
                    direction.y * swordDefinition.damage.knockbackPixels, true};
            } else if (event.marker == "attack_off") {
                activeSword.enabled = false;
            } else if (event.marker == "spawn_projectile") {
                const auto offset = arrowDefinition.spawnOffsets.forFacing(player.facing());
                [[maybe_unused]] const auto handle = projectiles.spawn(
                    {player.entityHandle(), player.attackInstance()}, gameplay::Faction::player,
                    arrowDefinition.id, gameplay::addOffset(player.feetPosition(), offset),
                    player.facing(), bowDefinition.damage);
            }
        }
    }

    void consumeSimulationEvents() {
        for (const simulation::SimulationEvent& event : events.events()) {
            if (const auto* damaged = std::get_if<simulation::EntityDamaged>(&event)) {
                std::ostringstream text;
                text << "DAMAGE " << damaged->amount << " HP " << damaged->remainingHealth;
                lastEvent = text.str();
            } else if (std::holds_alternative<simulation::EntityDefeated>(event)) {
                lastEvent = "PUPPET DEFEATED";
            } else if (const auto* impact = std::get_if<simulation::ProjectileImpact>(&event)) {
                if (impact->kind != simulation::ProjectileImpactKind::expired) {
                    effects->spawnImpact(impact->position);
                }
            }
        }
    }

    void update(simulation::Tick tick, const platform::InputState& input,
                platform::DebugInputState debugInput) {
        events.clear();
        gameplay::tickInvulnerability(puppet->combatant());
        gameplay::tickInvulnerability(player.combatant());
        const gameplay::PlayerActionState previousAction = player.actionState();
        const simulation::PlayerCommand command = commandBuilder.build(tick, localPlayerId, input);
        player.update(command, map.collision(), map.tileSize());
        visual->update(player.motionState(), player.facing(), player.actionState());
        consumeAnimationMarkers();
        if (activeSword.enabled) {
            activeSword.bounds = swordDefinition.meleeHitboxes->forFacing(player.facing()).at(
                player.feetPosition());
            const gameplay::CombatResolution hit = combat.resolve(
                activeSword, puppet->combatTarget(), events);
            if (hit.damaged) {
                puppet->applyKnockback(hit.requestedKnockbackX, hit.requestedKnockbackY,
                                       map.collision(), map.tileSize());
            }
        }
        std::array<gameplay::CombatTargetRef, 1> targets{puppet->combatTarget()};
        std::vector<gameplay::CombatResolution> projectileResolutions;
        projectiles.update(map.collision(), map.tileSize(), targets, combat, events,
                           projectileResolutions);
        for (const gameplay::CombatResolution& resolution : projectileResolutions) {
            if (resolution.target == puppet->combatant().handle) {
                puppet->applyKnockback(resolution.requestedKnockbackX,
                                       resolution.requestedKnockbackY,
                                       map.collision(), map.tileSize());
            }
        }
        consumeSimulationEvents();
        effects->update();
        if (player.actionState() != gameplay::PlayerActionState::none &&
            visual->animator().finished()) {
            if (player.actionState() == gameplay::PlayerActionState::swordAttack) {
                combat.finishAttack({player.entityHandle(), player.attackInstance()});
            }
            activeSword.enabled = false;
            player.finishAttack();
        }
        if (previousAction == gameplay::PlayerActionState::none &&
            player.actionState() != gameplay::PlayerActionState::none) {
            lastAttack = player.attackInstance();
        }
        if (debugInput.toggleCollisionPressed) { collisionOverlay = !collisionOverlay; }
        combatDebug.apply(debugInput);
        followPlayer();
        lastTick = tick;
        lastSequence = command.sequence;
    }

    std::size_t renderLayer(render::Renderer2D& renderer, const world::TileLayer& layer,
                            render::VisibleTileRange visible) const {
        if (!layer.visible() || visible.empty()) { return 0; }
        const auto cameraPosition = camera.position();
        for (int y = visible.firstY; y <= visible.lastY; ++y) {
            for (int x = visible.firstX; x <= visible.lastX; ++x) {
                const world::TileCell& cell = layer.cell(x, y);
                if (!cell) { continue; }
                if (cell->definition.tilesetId != dungeonTilesetId) {
                    throw std::runtime_error("Phase 5 demo encountered an unknown tileset id");
                }
                const core::RectI source = atlas.sourceRect(cell->definition.sourceIndex);
                const int dx = x * map.tileSize() - cameraPosition.x;
                const int dy = y * map.tileSize() - cameraPosition.y;
                if (world::hasFlag(cell->flags, world::TileFlags::flipX)) {
                    renderer.drawImageRegionFlipX(*tileset, source, dx, dy);
                } else {
                    renderer.drawImageRegion(*tileset, source, dx, dy);
                }
            }
        }
        return visible.tileCount();
    }

    void renderActors(render::Renderer2D& renderer) const {
        enum class ActorKind { player, puppet };
        struct Actor { int sortY; simulation::EntityHandle handle; ActorKind kind; };
        std::array<Actor, 2> actors{{
            {player.feetPosition().y, player.entityHandle(), ActorKind::player},
            {puppet->feetPosition().y, puppet->combatant().handle, ActorKind::puppet}}};
        std::sort(actors.begin(), actors.end(), [](const Actor& left, const Actor& right) {
            return actorRendersBefore({left.sortY, left.handle}, {right.sortY, right.handle});
        });
        for (const Actor& actor : actors) {
            if (actor.kind == ActorKind::player) {
                const auto logical = camera.worldToLogical(player.feetPosition());
                render::drawAnimator(renderer, visual->animator(), {logical.x, logical.y},
                                     visual->flipX());
            } else {
                const auto logical = camera.worldToLogical(puppet->feetPosition());
                const render::SpriteFrame frame{{0, 0, 32, 32}, {16, 32}, {}, false};
                render::drawSprite(renderer, *puppetSheet, frame, {logical.x, logical.y});
            }
        }
    }

    void renderProjectiles(render::Renderer2D& renderer) const {
        const auto cameraPosition = camera.position();
        for (const gameplay::Projectile& projectile : projectiles.projectiles()) {
            renderer.drawImageRegionQuarterTurn(
                arrowSheet->image(), {0, 0, 16, 16},
                projectile.position.x - cameraPosition.x - 8,
                projectile.position.y - cameraPosition.y - 8,
                arrowRotation(projectile.direction));
        }
    }

    void renderEffects(render::Renderer2D& renderer) const {
        for (const EffectInstance& effect : effects->effects()) {
            const auto logical = camera.worldToLogical(effect.position);
            render::drawAnimator(renderer, effect.animator, {logical.x, logical.y});
        }
    }

    void renderDebug(render::Renderer2D& renderer, render::VisibleTileRange visible) const {
        const auto cameraPosition = camera.position();
        if (collisionOverlay && !visible.empty()) {
            constexpr core::ColorRGBA8 fill{255, 24, 32, 72};
            for (int y = visible.firstY; y <= visible.lastY; ++y) {
                for (int x = visible.firstX; x <= visible.lastX; ++x) {
                    if (map.collision().isSolid(x, y)) {
                        renderer.fillRect({x * map.tileSize() - cameraPosition.x,
                                           y * map.tileSize() - cameraPosition.y,
                                           map.tileSize(), map.tileSize()}, fill);
                    }
                }
            }
        }
        if (combatDebug.collisionBody) {
            outline(renderer, player.collisionBody(), cameraPosition, {32, 255, 96, 255});
            outline(renderer, puppet->collisionBody().bounds, cameraPosition,
                    {32, 255, 96, 255});
        }
        if (combatDebug.hurtbox) {
            outline(renderer, player.hurtbox().bounds, cameraPosition, {32, 220, 255, 255});
            if (puppet->hurtbox().enabled) {
                outline(renderer, puppet->hurtbox().bounds, cameraPosition,
                        {32, 220, 255, 255});
            }
        }
        if (combatDebug.hitbox) {
            if (activeSword.enabled) {
                outline(renderer, activeSword.bounds, cameraPosition, {255, 48, 48, 255});
            }
            for (const gameplay::Projectile& projectile : projectiles.projectiles()) {
                outline(renderer, projectile.hitbox(), cameraPosition, {255, 220, 32, 255});
            }
        }
        if (combatDebug.interaction) {
            outline(renderer, player.interactionArea().bounds, cameraPosition,
                    {255, 64, 255, 255});
        }
    }

    void renderHud(render::Renderer2D& renderer) const {
        renderer.fillRect({0, 0, core::GameMetrics::logicalWidth, 38}, {8, 10, 16, 220});
        const auto feet = player.feetPosition();
        std::ostringstream first;
        first << "PHASE_5 T " << lastTick << " P " << feet.x << ' ' << feet.y;
        render::drawText(renderer, font, first.str(), 3, 3);
        std::ostringstream second;
        second << gameplay::facingName(player.facing()) << ' '
               << gameplay::actionStateName(player.actionState()) << " A " << lastAttack
               << " PUPPET " << puppet->combatant().health.current << '/'
               << puppet->combatant().health.maximum;
        render::drawText(renderer, font, second.str(), 3, 12);
        std::ostringstream third;
        third << "ARROWS " << projectiles.projectiles().size() << " VFX "
              << effects->effects().size() << " INV "
              << puppet->combatant().invulnerabilityTicks << " S " << lastSequence;
        render::drawText(renderer, font, third.str(), 3, 21);
        render::drawText(renderer, font,
                         lastEvent.empty() ? "Z SWORD X BOW C F1 F2 F3 F4" : lastEvent,
                         3, 30);
    }

    void render(render::Framebuffer& framebuffer) const {
        framebuffer.clear({28, 13, 22, 255});
        render::Renderer2D renderer(framebuffer);
        const auto visible = camera.visibleTiles(map.widthTiles(), map.heightTiles(), map.tileSize());
        renderLayer(renderer, map.layer(groundLayer), visible);
        renderLayer(renderer, map.layer(lowLayer), visible);
        renderActors(renderer);
        renderProjectiles(renderer);
        renderLayer(renderer, map.layer(foregroundLayer), visible);
        renderEffects(renderer);
        renderDebug(renderer, visible);
        renderHud(renderer);
    }

    std::shared_ptr<const render::Image> tileset;
    world::TileAtlasLayout atlas;
    render::BitmapFont font;
    std::shared_ptr<const render::SpriteSheet> idleSheet;
    std::shared_ptr<const render::SpriteSheet> walkSheet;
    std::shared_ptr<const render::SpriteSheet> swordSheet;
    std::shared_ptr<const render::SpriteSheet> bowSheet;
    std::shared_ptr<const render::SpriteSheet> arrowSheet;
    std::shared_ptr<const render::SpriteSheet> puppetSheet;
    std::shared_ptr<const render::SpriteSheet> impactSheet;
    std::unique_ptr<PlayerVisual> visual;
    std::unique_ptr<EffectSystem> effects;
    world::RuntimeMap map;
    render::Camera2D camera;
    simulation::EntityHandlePool handles;
    simulation::EntityHandle playerHandle{};
    gameplay::Player player;
    gameplay::AttackDefinition swordDefinition;
    gameplay::AttackDefinition bowDefinition;
    gameplay::ProjectileDefinition arrowDefinition;
    gameplay::ProjectileCatalog projectileCatalog;
    std::unique_ptr<TrainingPuppet> puppet;
    gameplay::CombatSystem combat;
    gameplay::ProjectileSystem projectiles;
    simulation::EventBuffer events;
    gameplay::Hitbox activeSword{};
    CommandBuilder commandBuilder;
    simulation::Tick lastTick{};
    std::uint32_t lastSequence{};
    gameplay::AttackInstanceId lastAttack{};
    std::size_t groundLayer{};
    std::size_t lowLayer{};
    std::size_t foregroundLayer{};
    bool collisionOverlay{};
    CombatDebugVisibility combatDebug{};
    std::string lastEvent;
};

Phase5Demo::Phase5Demo(platform::ImageDecoder& decoder,
                       const std::filesystem::path& assetRoot) {
    const auto tileset = assets_.loadImage("tileset.dungeon", assetRoot / "Tileset/tileset.png", decoder);
    const auto font = assets_.loadImage("font.main", assetRoot / "fonts_index.png", decoder);
    const auto idle = assets_.loadImage("player.idle", assetRoot / "Characters/Player/idle/player_idle.png", decoder);
    const auto walk = assets_.loadImage("player.walk", assetRoot / "Characters/Player/walking/player_walking.png", decoder);
    const auto sword = assets_.loadImage("player.sword", assetRoot / "Characters/Player/attacking/player_attacking.png", decoder);
    const auto bow = assets_.loadImage("player.bow", assetRoot / "Characters/Player/attacking/player_attacking_bow.png", decoder);
    const auto arrow = assets_.loadImage("player.arrow", assetRoot / "Characters/Player/attacking/arrow.png", decoder);
    const auto puppet = assets_.loadImage("training_puppet", assetRoot / "Training_puppet/training_puppet.png", decoder);
    const auto impact = assets_.loadImage("effect.arrow_impact", assetRoot / "Explosion/arrow_hits_dust.png", decoder);
    state_ = std::make_unique<State>(tileset, font, idle, walk, sword, bow, arrow, puppet, impact);
}

Phase5Demo::~Phase5Demo() = default;

void Phase5Demo::fixedTick(simulation::Tick tick, const platform::InputState& input,
                           platform::DebugInputState debugInput) {
    state_->update(tick, input, debugInput);
}

void Phase5Demo::render(render::Framebuffer& framebuffer) const { state_->render(framebuffer); }

std::filesystem::path findLicensedAssetRoot(const std::filesystem::path& executableDirectory) {
    std::array<std::filesystem::path, 2> starts{std::filesystem::current_path(), executableDirectory};
    for (std::filesystem::path start : starts) {
        for (int depth = 0; depth < 6 && !start.empty(); ++depth) {
            const auto candidate = start / "Dungeon Underworld";
            std::error_code error;
            if (std::filesystem::is_directory(candidate, error)) { return candidate; }
            const auto parent = start.parent_path();
            if (parent == start) { break; }
            start = parent;
        }
    }
    throw std::runtime_error(
        "Licensed assets were not found. Keep the local 'Dungeon Underworld' directory "
        "at the project root; it is intentionally excluded from Git.");
}

} // namespace underworld::game
