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
#include "game/game_view_model.h"
#include "game/actor_render_order.h"
#include "game/combat_debug.h"
#include "game/effect_system.h"
#include "game/enemy_visual.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/combat_system.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/player.h"
#include "game/gameplay/items.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/gameplay/projectile_system.h"
#include "game/player_visual.h"
#include "game/training_puppet.h"
#include "game/world_object_visual.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

std::shared_ptr<const render::AnimationClip> makeObjectClip(
    std::string id, std::shared_ptr<const render::SpriteSheet> sheet,
    int frameWidth, int frameHeight, int frameCount, std::uint32_t ticks,
    core::PointI anchor, bool loop) {
    std::vector<render::AnimationFrame> frames;
    frames.reserve(static_cast<std::size_t>(frameCount));
    for (int column = 0; column < frameCount; ++column) {
        frames.push_back({{{column * frameWidth, 0, frameWidth, frameHeight}, anchor, {}, false},
                          ticks, {}});
    }
    return std::make_shared<const render::AnimationClip>(
        std::move(id), std::move(sheet), std::move(frames), loop);
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

render::QuarterTurn projectileRotation(gameplay::FacingDirection canonical,
                                       gameplay::FacingDirection direction) noexcept {
    const auto turns = gameplay::clockwiseQuarterTurns(canonical, direction);
    switch (turns) {
    case 1: return render::QuarterTurn::r90;
    case 2: return render::QuarterTurn::r180;
    case 3: return render::QuarterTurn::r270;
    default: return render::QuarterTurn::r0;
    }
}

EnemyVisualSet makeEnemyVisualSet(
    const simulation::DefinitionId& id,
    const simulation::DefinitionId& attackVisualId,
    const std::shared_ptr<const render::SpriteSheet>& idle,
    const std::shared_ptr<const render::SpriteSheet>& walk,
    const std::shared_ptr<const render::SpriteSheet>& attack,
    const std::shared_ptr<const render::SpriteSheet>& death,
    int attackFrameSize, int attackFrameCount, std::uint32_t attackFrameTicks,
    core::PointI attackAnchor, std::vector<std::vector<std::string>> markers) {
    EnemyVisualSet result;
    result.id = id;
    const std::string prefix{id.value()};
    result.idle = makeClips(prefix + ".idle", idle, 32, 2, 30, {16, 31}, true);
    result.walk = makeClips(prefix + ".walk", walk, 32, 4, 8, {16, 31}, true);
    result.death = makeClips(prefix + ".death", death, 32, 2, 8, {16, 31}, false);
    result.attacks.emplace(
        attackVisualId,
        makeClips(prefix + ".attack", attack, attackFrameSize, attackFrameCount,
                  attackFrameTicks, attackAnchor, false, markers));
    return result;
}

} // namespace

struct Phase7Demo::State final {
    State(std::shared_ptr<const render::Image> tileImage,
          std::shared_ptr<const render::Image> fontImage,
          std::shared_ptr<const render::Image> idleImage,
          std::shared_ptr<const render::Image> walkImage,
          std::shared_ptr<const render::Image> swordImage,
          std::shared_ptr<const render::Image> bowImage,
          std::shared_ptr<const render::Image> arrowImage,
          std::shared_ptr<const render::Image> puppetImage,
          std::shared_ptr<const render::Image> impactImage,
          std::shared_ptr<const render::Image> soldierIdleImage,
          std::shared_ptr<const render::Image> soldierWalkImage,
          std::shared_ptr<const render::Image> soldierAttackImage,
          std::shared_ptr<const render::Image> soldierDeathImage,
          std::shared_ptr<const render::Image> skullIdleImage,
          std::shared_ptr<const render::Image> skullWalkImage,
          std::shared_ptr<const render::Image> skullAttackImage,
          std::shared_ptr<const render::Image> skullDeathImage,
          std::shared_ptr<const render::Image> skullArrowImage,
          std::shared_ptr<const render::Image> heartImage,
          std::shared_ptr<const render::Image> moneyImage,
          std::shared_ptr<const render::Image> potionImage,
          std::shared_ptr<const render::Image> chestImage,
          std::shared_ptr<const render::Image> crateImage,
          std::shared_ptr<const render::Image> breakingCrateImage,
          std::shared_ptr<const render::Image> hudHeartImage,
          std::shared_ptr<const render::Image> hudMoneyImage)
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
          soldierIdleSheet(std::make_shared<const render::SpriteSheet>(
              std::move(soldierIdleImage))),
          soldierWalkSheet(std::make_shared<const render::SpriteSheet>(
              std::move(soldierWalkImage))),
          soldierAttackSheet(std::make_shared<const render::SpriteSheet>(
              std::move(soldierAttackImage))),
          soldierDeathSheet(std::make_shared<const render::SpriteSheet>(
              std::move(soldierDeathImage))),
          skullIdleSheet(std::make_shared<const render::SpriteSheet>(
              std::move(skullIdleImage))),
          skullWalkSheet(std::make_shared<const render::SpriteSheet>(
              std::move(skullWalkImage))),
          skullAttackSheet(std::make_shared<const render::SpriteSheet>(
              std::move(skullAttackImage))),
          skullDeathSheet(std::make_shared<const render::SpriteSheet>(
              std::move(skullDeathImage))),
          skullArrowSheet(std::make_shared<const render::SpriteSheet>(
              std::move(skullArrowImage))),
          heartPickupImage(std::move(heartImage)),
          moneyPickupImage(std::move(moneyImage)),
          potionImage(std::move(potionImage)),
          chestSheet(std::make_shared<const render::SpriteSheet>(std::move(chestImage))),
          crateSheet(std::make_shared<const render::SpriteSheet>(std::move(crateImage))),
          breakingCrateSheet(std::make_shared<const render::SpriteSheet>(
              std::move(breakingCrateImage))),
          hudHeartImage(std::move(hudHeartImage)), hudMoneyImage(std::move(hudMoneyImage)),
          map(mapWidthTiles, mapHeightTiles, core::GameMetrics::tileSize),
          camera(core::GameMetrics::logicalWidth, core::GameMetrics::logicalHeight),
          playerHandle(handles.create()), player(localPlayerId, playerHandle, playerSpawn),
          swordDefinition(gameplay::makePlayerSwordAttackDefinition()),
          bowDefinition(gameplay::makePlayerBowAttackDefinition()),
          arrowDefinition(gameplay::makePlayerArrowProjectileDefinition()),
          projectiles(handles, projectileCatalog), playerItems(itemCatalog) {
        if (atlas.columns() != 19 || atlas.rows() != 12) {
            throw std::runtime_error("Phase 7 demo expects the audited 19x12 dungeon tileset");
        }
        attackCatalog.add(swordDefinition);
        attackCatalog.add(bowDefinition);
        attackCatalog.add(gameplay::creatures::makeSoldierSwordAttackDefinition());
        attackCatalog.add(gameplay::creatures::makeSkullArrowAttackDefinition());
        projectileCatalog.add(arrowDefinition);
        projectileCatalog.add(gameplay::creatures::makeSkullArrowProjectileDefinition());
        projectileVisuals.emplace(arrowDefinition.visualId, arrowSheet);
        projectileVisuals.emplace(
            projectileCatalog.require(gameplay::creatures::skullArrowProjectileId()).visualId,
            skullArrowSheet);
        behaviorCatalog.add(gameplay::creatures::makeSoldierBehaviorProfile());
        behaviorCatalog.add(gameplay::creatures::makeSkullBehaviorProfile());
        enemyCatalog.add(gameplay::creatures::makeSoldierEnemyDefinition());
        enemyCatalog.add(gameplay::creatures::makeSkullEnemyDefinition());
        enemyVisualCatalog.add(makeEnemyVisualSet(
            gameplay::creatures::soldierVisualId(),
            attackCatalog.require(gameplay::creatures::soldierSwordAttackId()).visualActionId,
            soldierIdleSheet, soldierWalkSheet, soldierAttackSheet, soldierDeathSheet,
            48, 4, 6, {24, 31}, {{}, {"attack_on"}, {}, {"attack_off"}}));
        enemyVisualCatalog.add(makeEnemyVisualSet(
            gameplay::creatures::skullVisualId(),
            attackCatalog.require(gameplay::creatures::skullArrowAttackId()).visualActionId,
            skullIdleSheet, skullWalkSheet, skullAttackSheet, skullDeathSheet,
            32, 2, 8, {16, 31}, {{}, {"spawn_projectile"}}));
        itemCatalog.add(gameplay::makeLifePotionDefinition());
        itemVisuals.emplace(simulation::DefinitionId{"visual.item.life_potion"}, potionImage);

        pickupDefinitions.reserve(3);
        pickupDefinitions.push_back({simulation::DefinitionId{"pickup.heart"},
            simulation::DefinitionId{"visual.pickup.heart"}, {-5, -5, 10, 10},
            gameplay::HealthPickup{2}});
        pickupDefinitions.push_back({simulation::DefinitionId{"pickup.money"},
            simulation::DefinitionId{"visual.pickup.money"}, {-5, -5, 10, 10},
            gameplay::CurrencyPickup{1}});
        pickupDefinitions.push_back({simulation::DefinitionId{"pickup.life_potion"},
            simulation::DefinitionId{"visual.item.life_potion"}, {-5, -5, 10, 10},
            gameplay::ItemPickup{gameplay::lifePotionItemId(), 4}});
        pickupVisuals.emplace(pickupDefinitions[0].visualId, heartPickupImage);
        pickupVisuals.emplace(pickupDefinitions[1].visualId, moneyPickupImage);
        pickupVisuals.emplace(pickupDefinitions[2].visualId, potionImage);
        pickups.emplace_back(handles.create(), pickupDefinitions[0], core::WorldPointI{104, 176});
        pickups.emplace_back(handles.create(), pickupDefinitions[1], core::WorldPointI{120, 144});
        pickups.emplace_back(handles.create(), pickupDefinitions[2], core::WorldPointI{88, 144});

        const simulation::DefinitionId chestVisualId{"visual.object.chest"};
        const simulation::DefinitionId crateVisualId{"visual.object.crate"};
        objectCatalog.add({simulation::DefinitionId{"object.chest"}, chestVisualId,
            gameplay::ObjectInteractionDefinition{{-14, -18, 28, 22}},
            gameplay::ObjectContainerDefinition{5}, std::nullopt});
        objectCatalog.add({simulation::DefinitionId{"object.crate"}, crateVisualId,
            std::nullopt, std::nullopt,
            gameplay::ObjectDestructibleDefinition{2, {-8, -24, 16, 24}}});
        objectVisualCatalog.add({chestVisualId,
            makeObjectClip("chest.closed", chestSheet, 16, 32, 1, 1, {8, 31}, true),
            makeObjectClip("chest.open", chestSheet, 16, 32, 5, 4, {8, 31}, false),
            nullptr});
        objectVisualCatalog.add({crateVisualId,
            makeObjectClip("crate.idle", crateSheet, 16, 32, 1, 1, {8, 31}, true),
            nullptr,
            makeObjectClip("crate.break", breakingCrateSheet, 32, 32, 7, 4,
                           {16, 31}, false)});
        gameplay::WorldObjectFactory objectFactory(handles, objectCatalog, itemCatalog);
        const std::array<gameplay::ItemStack, 1> chestContents{{
            {gameplay::lifePotionItemId(), 2}}};
        objects.push_back(objectFactory.create(
            simulation::DefinitionId{"object.chest"}, {152, 144}, chestContents));
        objects.push_back(objectFactory.create(
            simulation::DefinitionId{"object.crate"}, {184, 144}));
        for (const auto& object : objects) {
            objectVisuals.emplace_back(object.handle(), objectVisualCatalog.require(
                object.definition().visualSetId));
            objectVisuals.back().update(object, 0);
        }
        groundLayer = map.addLayer("ground");
        lowLayer = map.addLayer("decoration_low");
        foregroundLayer = map.addLayer("foreground");
        buildDungeon();
        if (world::querySolidTiles(map.collision(), player.collisionBody(), map.tileSize()).collides) {
            throw std::runtime_error("Phase 6 player spawn overlaps map collision");
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
        const auto availableVisuals = enemyVisualCatalog.ids();
        gameplay::creatures::EnemyFactory enemyFactory(
            handles, enemyCatalog, behaviorCatalog, attackCatalog, projectileCatalog,
            availableVisuals);
        enemies.reserve(3);
        enemyVisuals.reserve(3);
        enemies.push_back(enemyFactory.create(
            gameplay::creatures::soldierEnemyId(), {220, 144},
            gameplay::FacingDirection::left));
        enemies.push_back(enemyFactory.create(
            gameplay::creatures::soldierEnemyId(), {112, 248},
            gameplay::FacingDirection::up));
        enemies.push_back(enemyFactory.create(
            gameplay::creatures::skullEnemyId(), {264, 144},
            gameplay::FacingDirection::left));
        for (const auto& enemy : enemies) {
            enemyVisuals.emplace_back(
                enemy.handle(), enemyVisualCatalog.require(enemy.definition().visualSetId));
            enemyVisuals.back().update(enemy, 0);
        }
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

    void applyResolution(const gameplay::CombatResolution& resolution) {
        if (!resolution.damaged) { return; }
        if (resolution.target == player.entityHandle()) {
            player.applyKnockback(resolution.requestedKnockbackX,
                                  resolution.requestedKnockbackY,
                                  map.collision(), map.tileSize());
            return;
        }
        if (resolution.target == puppet->combatant().handle) {
            puppet->applyKnockback(resolution.requestedKnockbackX,
                                   resolution.requestedKnockbackY,
                                   map.collision(), map.tileSize());
            return;
        }
        const auto found = std::find_if(enemies.begin(), enemies.end(), [&](const auto& enemy) {
            return enemy.handle() == resolution.target;
        });
        if (found != enemies.end()) {
            found->applyKnockback(resolution.requestedKnockbackX,
                                  resolution.requestedKnockbackY,
                                  map.collision(), map.tileSize());
        }
    }

    void resolvePlayerSword() {
        if (!activeSword.enabled) { return; }
        activeSword.bounds = swordDefinition.meleeHitboxes->forFacing(player.facing()).at(
            player.feetPosition());
        applyResolution(combat.resolve(activeSword, puppet->combatTarget(), events));
        for (auto& enemy : enemies) {
            applyResolution(combat.resolve(activeSword, enemy.combatTarget(), events));
        }
        for (auto& object : objects) {
            if (object.combatant()) {
                applyResolution(combat.resolve(activeSword, object.combatTarget(), events));
            }
        }
    }

    void consumeEnemyMarkers(gameplay::creatures::EnemyInstance& enemy,
                             EnemyVisualInstance& enemyVisual) {
        if (!enemy.activeAttack()) { return; }
        for (const render::AnimationMarkerEvent& event : enemyVisual.consumeMarkerEvents()) {
            auto& active = *enemy.activeAttack();
            if (event.marker == "attack_on") {
                active.meleeHitboxActive = true;
            } else if (event.marker == "attack_off") {
                active.meleeHitboxActive = false;
            } else if (event.marker == "spawn_projectile") {
                if (!active.definition->projectileDefinitionId) {
                    throw std::logic_error("projectile marker requires projectile attack data");
                }
                const auto& projectileDefinition = projectileCatalog.require(
                    *active.definition->projectileDefinitionId);
                const auto offset = projectileDefinition.spawnOffsets.forFacing(
                    active.lockedFacing);
                [[maybe_unused]] const auto handle = projectiles.spawn(
                    active.key, enemy.combatant().faction, projectileDefinition.id,
                    gameplay::addOffset(enemy.feetPosition(), offset),
                    active.lockedFacing, active.definition->damage);
            }
        }
    }

    void updateEnemies() {
        for (std::size_t index = 0; index < enemies.size();) {
            auto& enemy = enemies[index];
            auto& enemyVisual = enemyVisuals[index];
            const auto& profile = behaviorCatalog.require(
                enemy.definition().behaviorProfileId);
            const std::optional<gameplay::AttackKey> previousAttack = enemy.activeAttack()
                ? std::optional<gameplay::AttackKey>{enemy.activeAttack()->key}
                : std::nullopt;
            static_cast<void>(enemyBehavior.update(
                enemy, player.entityHandle(), player.feetPosition(),
                !player.health().depleted(), profile, attackCatalog,
                map.collision(), map.tileSize()));
            if (previousAttack && !enemy.activeAttack()) {
                combat.finishAttack(*previousAttack);
            }
            enemyVisual.update(enemy);
            consumeEnemyMarkers(enemy, enemyVisual);

            if (enemy.activeAttack() && enemy.activeAttack()->meleeHitboxActive) {
                const auto& active = *enemy.activeAttack();
                if (!active.definition->meleeHitboxes) {
                    throw std::logic_error("active melee marker requires melee hitbox data");
                }
                const auto direction = gameplay::directionVector(active.lockedFacing);
                gameplay::Hitbox hitbox{
                    active.definition->meleeHitboxes->forFacing(
                        active.lockedFacing).at(enemy.feetPosition()),
                    active.key, enemy.combatant().faction, active.definition->damage,
                    direction.x * active.definition->damage.knockbackPixels,
                    direction.y * active.definition->damage.knockbackPixels, true};
                applyResolution(combat.resolve(hitbox, player.combatTarget(), events));
            }

            if (enemy.state() == gameplay::creatures::BehaviorState::attack &&
                enemyVisual.animator().finished()) {
                combat.finishAttack(enemy.activeAttack()->key);
                enemyBehavior.finishAttack(enemy, profile);
            }
            if (enemy.state() == gameplay::creatures::BehaviorState::dead &&
                enemyVisual.animator().finished()) {
                [[maybe_unused]] const bool destroyed = handles.destroy(enemy.handle());
                enemies.erase(enemies.begin() + static_cast<std::ptrdiff_t>(index));
                enemyVisuals.erase(enemyVisuals.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            ++index;
        }
    }

    std::vector<gameplay::CombatTargetRef> combatTargets() {
        std::vector<gameplay::CombatTargetRef> targets;
        targets.reserve(enemies.size() + objects.size() + 2);
        targets.push_back(player.combatTarget());
        targets.push_back(puppet->combatTarget());
        for (auto& enemy : enemies) { targets.push_back(enemy.combatTarget()); }
        for (auto& object : objects) {
            if (object.combatant()) { targets.push_back(object.combatTarget()); }
        }
        return targets;
    }

    void collectNearbyPickups() {
        const world::AabbI area = player.collisionBody();
        for (std::size_t index = 0; index < pickups.size();) {
            const auto result = gameplay::collectPickup(
                pickups[index], player.entityHandle(), area, player.health(),
                playerItems.inventory().items(), playerItems.wallet(), handles, events);
            if (result.fullyConsumed) {
                pickups.erase(pickups.begin() + static_cast<std::ptrdiff_t>(index));
            } else {
                ++index;
            }
        }
    }

    void updateObjects() {
        for (std::size_t index = 0; index < objects.size();) {
            auto& object = objects[index];
            static_cast<void>(object.syncDestructionState());
            objectVisuals[index].update(object);
            if (object.state() == gameplay::WorldObjectState::destroying &&
                objectVisuals[index].finished()) {
                static_cast<void>(object.completeDestruction(handles));
                objects.erase(objects.begin() + static_cast<std::ptrdiff_t>(index));
                objectVisuals.erase(objectVisuals.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            if (object.combatant()) { gameplay::tickInvulnerability(*object.combatant()); }
            ++index;
        }
    }

    void consumeSimulationEvents() {
        for (const simulation::SimulationEvent& event : events.events()) {
            if (const auto* damaged = std::get_if<simulation::EntityDamaged>(&event)) {
                std::ostringstream text;
                text << "DAMAGE " << damaged->amount << " HP " << damaged->remainingHealth;
                lastEvent = text.str();
            } else if (std::holds_alternative<simulation::EntityDefeated>(event)) {
                lastEvent = "ENTITY DEFEATED";
            } else if (const auto* impact = std::get_if<simulation::ProjectileImpact>(&event)) {
                if (impact->kind != simulation::ProjectileImpactKind::expired) {
                    effects->spawnImpact(impact->position);
                }
            } else if (const auto* pickup = std::get_if<simulation::PickupCollected>(&event)) {
                lastEvent = "PICKUP " + std::to_string(pickup->amount);
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
        if (gameplay::routeInventoryCommand(
                inventoryOverlay, command, playerItems, itemCatalog, player.health())) {
            lastTick = tick;
            lastSequence = command.sequence;
            return;
        }
        if (command.actions.quickSlotPressed >= 0) {
            static_cast<void>(playerItems.useQuickSlot(
                static_cast<std::size_t>(command.actions.quickSlotPressed), itemCatalog,
                player.health()));
        }
        player.update(command, map.collision(), map.tileSize());
        if (command.actions.interactPressed) {
            static_cast<void>(gameplay::interactNearest(
                player.feetPosition(), player.interactionArea().bounds,
                playerItems.inventory().items(), objects));
        }
        visual->update(player.motionState(), player.facing(), player.actionState());
        consumeAnimationMarkers();
        resolvePlayerSword();
        updateEnemies();
        auto targets = combatTargets();
        std::vector<gameplay::CombatResolution> projectileResolutions;
        projectiles.update(map.collision(), map.tileSize(), targets, combat, events,
                           projectileResolutions);
        for (const gameplay::CombatResolution& resolution : projectileResolutions) {
            applyResolution(resolution);
        }
        collectNearbyPickups();
        updateObjects();
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
                    throw std::runtime_error("Phase 6 demo encountered an unknown tileset id");
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
        enum class ActorKind { player, puppet, enemy, object, pickup };
        struct Actor {
            int sortY;
            simulation::EntityHandle handle;
            ActorKind kind;
            std::size_t enemyIndex{};
            std::size_t contentIndex{};
        };
        std::vector<Actor> actors;
        actors.reserve(enemies.size() + 2);
        actors.push_back({player.feetPosition().y, player.entityHandle(), ActorKind::player});
        actors.push_back(
            {puppet->feetPosition().y, puppet->combatant().handle, ActorKind::puppet});
        for (std::size_t index = 0; index < enemies.size(); ++index) {
            actors.push_back({enemies[index].feetPosition().y, enemies[index].handle(),
                              ActorKind::enemy, index});
        }
        for (std::size_t index = 0; index < objects.size(); ++index) {
            actors.push_back({objects[index].position().y, objects[index].handle(),
                              ActorKind::object, 0, index});
        }
        for (std::size_t index = 0; index < pickups.size(); ++index) {
            actors.push_back({pickups[index].position().y, pickups[index].handle(),
                              ActorKind::pickup, 0, index});
        }
        std::sort(actors.begin(), actors.end(), [](const Actor& left, const Actor& right) {
            return actorRendersBefore({left.sortY, left.handle}, {right.sortY, right.handle});
        });
        for (const Actor& actor : actors) {
            if (actor.kind == ActorKind::player) {
                const auto logical = camera.worldToLogical(player.feetPosition());
                render::drawAnimator(renderer, visual->animator(), {logical.x, logical.y},
                                     visual->flipX());
            } else if (actor.kind == ActorKind::puppet) {
                const auto logical = camera.worldToLogical(puppet->feetPosition());
                const render::SpriteFrame frame{{0, 0, 32, 32}, {16, 32}, {}, false};
                render::drawSprite(renderer, *puppetSheet, frame, {logical.x, logical.y});
            } else if (actor.kind == ActorKind::enemy) {
                const auto logical = camera.worldToLogical(
                    enemies[actor.enemyIndex].feetPosition());
                render::drawAnimator(renderer, enemyVisuals[actor.enemyIndex].animator(),
                                     {logical.x, logical.y},
                                     enemyVisuals[actor.enemyIndex].flipX());
            } else if (actor.kind == ActorKind::object) {
                const auto logical = camera.worldToLogical(
                    objects[actor.contentIndex].position());
                render::drawAnimator(renderer, objectVisuals[actor.contentIndex].animator(),
                                     {logical.x, logical.y});
            } else {
                const auto& pickup = pickups[actor.contentIndex];
                const auto found = pickupVisuals.find(pickup.definition().visualId);
                if (found == pickupVisuals.end()) {
                    throw std::runtime_error("pickup visual definition was not registered");
                }
                const auto logical = camera.worldToLogical(pickup.position());
                renderer.drawImage(*found->second, logical.x - 8, logical.y - 8);
            }
        }
    }

    void renderProjectiles(render::Renderer2D& renderer) const {
        const auto cameraPosition = camera.position();
        for (const gameplay::Projectile& projectile : projectiles.projectiles()) {
            if (projectile.definition == nullptr) { continue; }
            const auto found = projectileVisuals.find(projectile.definition->visualId);
            if (found == projectileVisuals.end()) {
                throw std::runtime_error("projectile visual definition was not registered");
            }
            renderer.drawImageRegionQuarterTurn(
                found->second->image(), {0, 0, 16, 16},
                projectile.position.x - cameraPosition.x - 8,
                projectile.position.y - cameraPosition.y - 8,
                projectileRotation(projectile.definition->canonicalFacing,
                                   projectile.direction));
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
            for (const auto& enemy : enemies) {
                outline(renderer, enemy.collisionBody(), cameraPosition, {32, 255, 96, 255});
            }
        }
        if (combatDebug.hurtbox) {
            outline(renderer, player.hurtbox().bounds, cameraPosition, {32, 220, 255, 255});
            if (puppet->hurtbox().enabled) {
                outline(renderer, puppet->hurtbox().bounds, cameraPosition,
                        {32, 220, 255, 255});
            }
            for (const auto& enemy : enemies) {
                if (enemy.hurtbox().enabled) {
                    outline(renderer, enemy.hurtbox().bounds, cameraPosition,
                            {32, 220, 255, 255});
                }
            }
            for (const auto& object : objects) {
                if (object.hurtbox().enabled) {
                    outline(renderer, object.hurtbox().bounds, cameraPosition,
                            {32, 220, 255, 255});
                }
            }
        }
        if (combatDebug.hitbox) {
            if (activeSword.enabled) {
                outline(renderer, activeSword.bounds, cameraPosition, {255, 48, 48, 255});
            }
            for (const gameplay::Projectile& projectile : projectiles.projectiles()) {
                outline(renderer, projectile.hitbox(), cameraPosition, {255, 220, 32, 255});
            }
            for (const auto& enemy : enemies) {
                if (!enemy.activeAttack() || !enemy.activeAttack()->meleeHitboxActive ||
                    !enemy.activeAttack()->definition->meleeHitboxes) {
                    continue;
                }
                outline(renderer,
                        enemy.activeAttack()->definition->meleeHitboxes->forFacing(
                            enemy.activeAttack()->lockedFacing).at(enemy.feetPosition()),
                        cameraPosition, {255, 48, 48, 255});
            }
        }
        if (combatDebug.interaction) {
            outline(renderer, player.interactionArea().bounds, cameraPosition,
                    {255, 64, 255, 255});
            for (const auto& object : objects) {
                if (const auto area = object.interactionArea()) {
                    outline(renderer, *area, cameraPosition, {255, 64, 255, 255});
                }
            }
            for (const auto& pickup : pickups) {
                outline(renderer, pickup.collectionArea(), cameraPosition,
                        {255, 200, 64, 255});
            }
        }
    }

    void renderHud(render::Renderer2D& renderer) const {
        const GameViewModel view = buildGameViewModel(player, playerItems, itemCatalog,
                                                       inventoryOverlay);
        renderer.fillRect({0, 0, core::GameMetrics::logicalWidth, 14}, {8, 10, 16, 220});
        for (int index = 0; index < view.playerMaximumHealth; ++index) {
            if (index < view.playerHealth) {
                renderer.drawImage(*hudHeartImage, 3 + index * 12, 2);
            } else {
                renderer.fillRect({3 + index * 12, 3, 9, 8}, {54, 30, 38, 255});
            }
        }
        renderer.drawImage(*hudMoneyImage, 68, 2);
        render::drawText(renderer, font, std::to_string(view.gold), 79, 2);

        renderer.fillRect({0, 194, core::GameMetrics::logicalWidth, 30}, {8, 10, 16, 220});
        for (std::size_t index = 0; index < view.quickSlots.size(); ++index) {
            const int x = 4 + static_cast<int>(index) * 40;
            renderer.fillRect({x, 197, 34, 23}, {54, 30, 38, 255});
            render::drawText(renderer, font, std::to_string(index + 1), x + 2, 199);
            if (view.quickSlots[index].visualId) {
                const auto found = itemVisuals.find(*view.quickSlots[index].visualId);
                if (found != itemVisuals.end()) { renderer.drawImage(*found->second, x + 10, 199); }
                render::drawText(renderer, font,
                    std::to_string(view.quickSlots[index].quantity), x + 22, 209);
            }
        }
        render::drawText(renderer, font, "I ITEMS  E OPEN", 169, 203);

        if (!view.inventoryOpen) { return; }
        renderer.fillRect({6, 52, 260, 78}, {8, 10, 16, 245});
        render::drawText(renderer, font, "INVENTORY", 10, 55);
        for (std::size_t index = 0; index < view.inventory.size(); ++index) {
            const int column = static_cast<int>(index % 10);
            const int row = static_cast<int>(index / 10);
            const int x = 10 + column * 25;
            const int y = 66 + row * 20;
            const core::ColorRGBA8 slotColor = index == view.inventorySelection
                ? core::ColorRGBA8{220, 180, 72, 255}
                : core::ColorRGBA8{54, 30, 38, 255};
            renderer.fillRect({x, y, 22, 18}, slotColor);
            if (view.inventory[index].visualId) {
                const auto found = itemVisuals.find(*view.inventory[index].visualId);
                if (found != itemVisuals.end()) { renderer.drawImage(*found->second, x + 3, y + 1); }
                if (view.inventory[index].quantity > 1) {
                    render::drawText(renderer, font,
                        std::to_string(view.inventory[index].quantity), x + 10, y + 9);
                }
            }
        }
        render::drawText(renderer, font, "Z USE  1-4 BIND  I CLOSE", 10, 121);
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
    std::shared_ptr<const render::SpriteSheet> soldierIdleSheet;
    std::shared_ptr<const render::SpriteSheet> soldierWalkSheet;
    std::shared_ptr<const render::SpriteSheet> soldierAttackSheet;
    std::shared_ptr<const render::SpriteSheet> soldierDeathSheet;
    std::shared_ptr<const render::SpriteSheet> skullIdleSheet;
    std::shared_ptr<const render::SpriteSheet> skullWalkSheet;
    std::shared_ptr<const render::SpriteSheet> skullAttackSheet;
    std::shared_ptr<const render::SpriteSheet> skullDeathSheet;
    std::shared_ptr<const render::SpriteSheet> skullArrowSheet;
    std::shared_ptr<const render::Image> heartPickupImage;
    std::shared_ptr<const render::Image> moneyPickupImage;
    std::shared_ptr<const render::Image> potionImage;
    std::shared_ptr<const render::SpriteSheet> chestSheet;
    std::shared_ptr<const render::SpriteSheet> crateSheet;
    std::shared_ptr<const render::SpriteSheet> breakingCrateSheet;
    std::shared_ptr<const render::Image> hudHeartImage;
    std::shared_ptr<const render::Image> hudMoneyImage;
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
    gameplay::AttackCatalog attackCatalog;
    gameplay::ProjectileCatalog projectileCatalog;
    std::unordered_map<simulation::DefinitionId,
                       std::shared_ptr<const render::SpriteSheet>,
                       simulation::DefinitionIdHash> projectileVisuals;
    gameplay::creatures::BehaviorCatalog behaviorCatalog;
    gameplay::creatures::EnemyCatalog enemyCatalog;
    EnemyVisualCatalog enemyVisualCatalog;
    gameplay::creatures::EnemyBehaviorSystem enemyBehavior;
    std::vector<gameplay::creatures::EnemyInstance> enemies;
    std::vector<EnemyVisualInstance> enemyVisuals;
    std::unique_ptr<TrainingPuppet> puppet;
    gameplay::CombatSystem combat;
    gameplay::ProjectileSystem projectiles;
    gameplay::ItemCatalog itemCatalog;
    gameplay::PlayerItems playerItems;
    gameplay::InventoryOverlayState inventoryOverlay;
    std::vector<gameplay::PickupDefinition> pickupDefinitions;
    std::vector<gameplay::WorldPickup> pickups;
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::Image>,
                       simulation::DefinitionIdHash> pickupVisuals;
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::Image>,
                       simulation::DefinitionIdHash> itemVisuals;
    gameplay::WorldObjectCatalog objectCatalog;
    WorldObjectVisualCatalog objectVisualCatalog;
    std::vector<gameplay::WorldObjectInstance> objects;
    std::vector<WorldObjectVisualInstance> objectVisuals;
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

Phase7Demo::Phase7Demo(platform::ImageDecoder& decoder,
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
    const auto soldierIdle = assets_.loadImage(
        "enemy.soldier.idle",
        assetRoot / "Characters/Enemies/Evil_soldier/idle/evil_soldier_idle.png", decoder);
    const auto soldierWalk = assets_.loadImage(
        "enemy.soldier.walk",
        assetRoot / "Characters/Enemies/Evil_soldier/walking/evil_soldier_walking.png", decoder);
    const auto soldierAttack = assets_.loadImage(
        "enemy.soldier.attack",
        assetRoot / "Characters/Enemies/Evil_soldier/attacking/evil_soldier_attacking.png", decoder);
    const auto soldierDeath = assets_.loadImage(
        "enemy.soldier.death",
        assetRoot / "Characters/Enemies/Evil_soldier/death/evil_soldier_death.png", decoder);
    const auto skullIdle = assets_.loadImage(
        "enemy.skull.idle",
        assetRoot / "Characters/Enemies/Skull/idle/skull_idle.png", decoder);
    const auto skullWalk = assets_.loadImage(
        "enemy.skull.walk",
        assetRoot / "Characters/Enemies/Skull/walking/skull_walking.png", decoder);
    const auto skullAttack = assets_.loadImage(
        "enemy.skull.attack",
        assetRoot / "Characters/Enemies/Skull/attacking/skull_attacking.png", decoder);
    const auto skullDeath = assets_.loadImage(
        "enemy.skull.death",
        assetRoot / "Characters/Enemies/Skull/death/skull_death.png", decoder);
    const auto skullArrow = assets_.loadImage(
        "enemy.skull.arrow",
        assetRoot / "Characters/Enemies/Skull/attacking/arrow.png", decoder);
    const auto heart = assets_.loadImage(
        "pickup.heart", assetRoot / "Objects/heart.png", decoder);
    const auto money = assets_.loadImage(
        "pickup.money", assetRoot / "Objects/money.png", decoder);
    const auto potion = assets_.loadImage(
        "item.life_potion", assetRoot / "Objects/life_potion.png", decoder);
    const auto chest = assets_.loadImage(
        "object.chest", assetRoot / "Tileset/chest.png", decoder);
    const auto crate = assets_.loadImage(
        "object.crate", assetRoot / "Tileset/crate.png", decoder);
    const auto breakingCrate = assets_.loadImage(
        "object.crate.breaking", assetRoot / "Tileset/breaking_crate.png", decoder);
    const auto hudHeart = assets_.loadImage(
        "hud.heart", assetRoot / "Icons/heart_complete.png", decoder);
    const auto hudMoney = assets_.loadImage(
        "hud.money", assetRoot / "Icons/money.png", decoder);
    state_ = std::make_unique<State>(
        tileset, font, idle, walk, sword, bow, arrow, puppet, impact,
        soldierIdle, soldierWalk, soldierAttack, soldierDeath,
        skullIdle, skullWalk, skullAttack, skullDeath, skullArrow,
        heart, money, potion, chest, crate, breakingCrate, hudHeart, hudMoney);
}

Phase7Demo::~Phase7Demo() = default;

void Phase7Demo::fixedTick(simulation::Tick tick, const platform::InputState& input,
                           platform::DebugInputState debugInput) {
    state_->update(tick, input, debugInput);
}

void Phase7Demo::render(render::Framebuffer& framebuffer) const { state_->render(framebuffer); }

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
