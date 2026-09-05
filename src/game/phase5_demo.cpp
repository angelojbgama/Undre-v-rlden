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
#include "game/game_content.h"
#include "game/actor_render_order.h"
#include "game/combat_debug.h"
#include "game/effect_system.h"
#include "game/enemy_visual.h"
#include "game/game_launch.h"
#include "game/runtime_visual_sync.h"
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
#include "game/world_object_visual.h"
#include "game/maps/dmap.h"
#include "game/maps/map_catalog.h"
#include "game/maps/official_maps.h"
#include "game/maps/runtime_world.h"
#include "game/save/save_data.h"

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

constexpr simulation::PlayerId localPlayerId{0};

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
          std::shared_ptr<const render::Image> hudMoneyImage,
          std::filesystem::path executableDirectory,
          const GameLaunchOptions& launchOptions)
        : tileset(std::move(tileImage)),
          atlas(tileset->width(), tileset->height(), core::GameMetrics::tileSize),
          font(std::move(fontImage)),
          idleSheet(std::make_shared<const render::SpriteSheet>(std::move(idleImage))),
          walkSheet(std::make_shared<const render::SpriteSheet>(std::move(walkImage))),
          swordSheet(std::make_shared<const render::SpriteSheet>(std::move(swordImage))),
          bowSheet(std::make_shared<const render::SpriteSheet>(std::move(bowImage))),
          arrowSheet(std::make_shared<const render::SpriteSheet>(std::move(arrowImage))),
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
          executableDirectory(std::move(executableDirectory)),
          camera(core::GameMetrics::logicalWidth, core::GameMetrics::logicalHeight),
          playerHandle(handles.create()), player(localPlayerId, playerHandle, {}),
          swordDefinition(gameplay::makePlayerSwordAttackDefinition()),
          bowDefinition(gameplay::makePlayerBowAttackDefinition()),
          arrowDefinition(gameplay::makePlayerArrowProjectileDefinition()),
          projectiles(handles, projectileCatalog), playerItems(itemCatalog) {
        const auto& dungeonDefinition = content.tilesets().require(
            simulation::DefinitionId{"tileset.dungeon"});
        tilesetVisuals.add(runtimeTilesets.requireRuntimeId(dungeonDefinition.id), tileset,
                          dungeonDefinition);
        savePath = this->executableDirectory / "savegame.sav";
        attackCatalog.add(swordDefinition);
        attackCatalog.add(bowDefinition);
        projectileCatalog.add(arrowDefinition);
        projectileVisuals.emplace(arrowDefinition.visualId, arrowSheet);
        projectileVisuals.emplace(
            projectileCatalog.require(gameplay::creatures::skullArrowProjectileId()).visualId,
            skullArrowSheet);
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
        itemVisuals.emplace(
            simulation::DefinitionId{"visual.item.life_potion"}, this->potionImage);

        pickupVisuals.emplace(simulation::DefinitionId{"visual.pickup.heart"}, heartPickupImage);
        pickupVisuals.emplace(simulation::DefinitionId{"visual.pickup.money"}, moneyPickupImage);
        pickupVisuals.emplace(simulation::DefinitionId{"visual.item.life_potion"}, this->potionImage);

        const simulation::DefinitionId chestVisualId{"visual.object.chest"};
        const simulation::DefinitionId crateVisualId{"visual.object.crate"};
        objectVisualCatalog.add({chestVisualId,
            makeObjectClip("chest.closed", chestSheet, 16, 32, 1, 1, {8, 31}, true),
            makeObjectClip("chest.open", chestSheet, 16, 32, 5, 4, {8, 31}, false),
            nullptr});
        objectVisualCatalog.add({crateVisualId,
            makeObjectClip("crate.idle", crateSheet, 16, 32, 1, 1, {8, 31}, true),
            nullptr,
            makeObjectClip("crate.break", breakingCrateSheet, 32, 32, 7, 4,
                           {16, 31}, false)});
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
        enemyFactory = std::make_unique<gameplay::creatures::EnemyFactory>(
            handles, enemyCatalog, behaviorCatalog, attackCatalog, projectileCatalog,
            availableVisuals);
        objectFactory = std::make_unique<gameplay::WorldObjectFactory>(
            handles, objectCatalog, itemCatalog);
        npcFactory = std::make_unique<gameplay::npcs::NpcFactory>(handles, npcCatalog);
        validationCatalogs = mapValidationCatalogs(content);
        runtimeBuilder = std::make_unique<maps::RuntimeWorldBuilder>(
            validationCatalogs, *enemyFactory, *objectFactory, handles,
            runtimeTilesets, npcFactory.get());
        auto startup = selectStartupMap(launchOptions, this->executableDirectory,
                                        std::filesystem::current_path());
        const auto startupLoaded = maps::readDmap(startup.path, &validationCatalogs);
        if (!startupLoaded) {
            throw std::runtime_error("could not load startup map '" +
                startup.path.string() + "': " + startupLoaded.error);
        }

        maps::MapCatalog startupCatalog;
        if (startup.source == StartupMapSource::explicitPath) {
            startupCatalog.add(startupLoaded.data.id, startup.path);
        }
        for (const auto& entry : maps::officialGameplayMaps()) {
            if (startupCatalog.find(entry.id)) { continue; }
            const auto path = maps::resolveOfficialGameplayMapPath(
                entry.relativePath, this->executableDirectory,
                std::filesystem::current_path());
            if (!path) {
                throw std::runtime_error("official gameplay map is missing: " +
                                         entry.relativePath.string());
            }
            startupCatalog.add(entry.id, *path);
        }
        if (const auto linkError = startupCatalog.validateLinks(&validationCatalogs);
            !linkError.empty()) {
            throw std::runtime_error("invalid startup map links: " + linkError);
        }
        knownMapData.clear();
        for (const auto& entry : maps::officialGameplayMaps()) {
            const auto loaded = startupCatalog.load(entry.id, &validationCatalogs);
            if (!loaded) { throw std::runtime_error("could not load official map: " + loaded.error); }
            knownMapData.push_back(loaded.data);
        }
        if (std::none_of(knownMapData.begin(), knownMapData.end(),
                         [&](const auto& map) { return map.id == startupLoaded.data.id; })) {
            knownMapData.push_back(startupLoaded.data);
        }
        mapCatalog = std::move(startupCatalog);
        mapSession = std::make_unique<maps::MapSession>(
            mapCatalog, validationCatalogs, *runtimeBuilder, handles, sessionWorldState);
        const auto startMap = startupLoaded.data.id;
        std::string spawnError;
        const auto selectedSpawn = selectStartupSpawn(
            startupLoaded.data, launchOptions.spawnId, spawnError);
        if (!selectedSpawn) {
            throw std::runtime_error("could not select startup spawn: " + spawnError);
        }
        const auto activated = mapSession->activate(startMap, *selectedSpawn);
        if (!activated.changed) {
            throw std::runtime_error("could not activate startup DMAP: " + activated.error);
        }
        player.relocate(activated.spawn.position, activated.spawn.facing);
        resolveRenderLayers();
        rebuildWorldVisuals();
        visual->update(player.motionState(), player.facing(), player.actionState(), 0);
        followPlayer();
    }

    [[nodiscard]] std::string startupSummary() const {
        std::ostringstream summary;
        summary << "startup map=" << activeWorld().id().value()
                << " spawn=" << activeWorld().spawn().id.value()
                << " enemies=" << activeWorld().enemies().size()
                << " objects=" << activeWorld().objects().size()
                << " pickups=" << activeWorld().pickups().size();
        return summary.str();
    }

    [[nodiscard]] maps::RuntimeWorld& activeWorld() { return *mapSession->world(); }
    [[nodiscard]] const maps::RuntimeWorld& activeWorld() const { return *mapSession->world(); }
    [[nodiscard]] world::RuntimeMap& activeMap() { return activeWorld().map(); }
    [[nodiscard]] const world::RuntimeMap& activeMap() const { return activeWorld().map(); }

    void rebuildWorldVisuals() {
        const auto result = synchronizeRuntimeWorldVisuals(
            activeWorld(), enemyVisualCatalog, enemyVisuals,
            objectVisualCatalog, objectVisuals);
        if (!result) { throw std::runtime_error(result.error); }
        if (enemyVisuals.size() != activeWorld().enemies().size() ||
            objectVisuals.size() != activeWorld().objects().size()) {
            throw std::logic_error("runtime and visual actor counts are out of sync");
        }
    }

    void resolveRenderLayers() {
        groundLayer = 0;
        foregroundLayer = activeMap().layerCount();
        for (std::size_t index = 0; index < activeMap().layerCount(); ++index) {
            const auto name = activeMap().layer(index).name();
            if (name == "ground") {
                groundLayer = index;
            } else if (name == "foreground") {
                foregroundLayer = index;
            }
        }

        lowLayers.clear();
        for (std::size_t index = 0; index < activeMap().layerCount(); ++index) {
            if (index != groundLayer && index != foregroundLayer) {
                lowLayers.push_back(index);
        }
        }
    }

    void followPlayer() {
        camera.centerOn(player.feetPosition());
        camera.clampToWorld(activeMap().worldWidthPixels(), activeMap().worldHeightPixels());
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
                                  activeMap().collision(), activeMap().tileSize());
            return;
        }
        auto& enemies = activeWorld().enemies();
        const auto found = std::find_if(enemies.begin(), enemies.end(), [&](const auto& enemy) {
            return enemy.instance.handle() == resolution.target;
        });
        if (found != enemies.end()) {
            found->instance.applyKnockback(resolution.requestedKnockbackX,
                                           resolution.requestedKnockbackY,
                                           activeMap().collision(), activeMap().tileSize());
        }
    }

    void resolvePlayerSword() {
        if (!activeSword.enabled) { return; }
        activeSword.bounds = swordDefinition.meleeHitboxes->forFacing(player.facing()).at(
            player.feetPosition());
        for (auto& enemy : activeWorld().enemies()) {
            applyResolution(combat.resolve(activeSword, enemy.instance.combatTarget(), events));
        }
        for (auto& object : activeWorld().objects()) {
            if (object.instance.combatant()) {
                applyResolution(combat.resolve(activeSword, object.instance.combatTarget(), events));
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
        auto& enemies = activeWorld().enemies();
        for (std::size_t index = 0; index < enemies.size();) {
            auto& enemy = enemies[index].instance;
            auto& enemyVisual = enemyVisuals[index];
            const auto& profile = behaviorCatalog.require(
                enemy.definition().behaviorProfileId);
            const std::optional<gameplay::AttackKey> previousAttack = enemy.activeAttack()
                ? std::optional<gameplay::AttackKey>{enemy.activeAttack()->key}
                : std::nullopt;
            static_cast<void>(enemyBehavior.update(
                enemy, player.entityHandle(), player.feetPosition(),
                !player.health().depleted(), profile, attackCatalog,
                activeMap().collision(), activeMap().tileSize()));
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
        targets.reserve(activeWorld().enemies().size() + activeWorld().objects().size() + 1);
        targets.push_back(player.combatTarget());
        for (auto& enemy : activeWorld().enemies()) {
            targets.push_back(enemy.instance.combatTarget());
        }
        for (auto& object : activeWorld().objects()) {
            if (object.instance.combatant()) { targets.push_back(object.instance.combatTarget()); }
        }
        return targets;
    }

    void collectNearbyPickups() {
        const world::AabbI area = player.collisionBody();
        auto& pickups = activeWorld().pickups();
        for (std::size_t index = 0; index < pickups.size();) {
            const auto result = gameplay::collectPickup(
                pickups[index].instance, player.entityHandle(), area, player.health(),
                playerItems.inventory().items(), playerItems.wallet(), handles, events);
            if (result.fullyConsumed) {
                pickups.erase(pickups.begin() + static_cast<std::ptrdiff_t>(index));
            } else {
                ++index;
            }
            if (result.collected) { captureActiveWorld(); }
        }
    }

    void updateObjects() {
        auto& objects = activeWorld().objects();
        for (std::size_t index = 0; index < objects.size();) {
            auto& object = objects[index].instance;
            static_cast<void>(object.syncDestructionState());
            objectVisuals[index].update(object);
            if (object.state() == gameplay::WorldObjectState::destroying &&
                objectVisuals[index].finished()) {
                static_cast<void>(object.completeDestruction(handles));
                objects.erase(objects.begin() + static_cast<std::ptrdiff_t>(index));
                objectVisuals.erase(objectVisuals.begin() + static_cast<std::ptrdiff_t>(index));
                captureActiveWorld();
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

    void captureActiveWorld() {
        if (mapSession->data() && mapSession->world()) {
            save::captureWorldState(*mapSession->data(), *mapSession->world(), sessionWorldState);
        }
    }

    void interactWithWorld() {
        maps::PersistentObject* selected{};
        std::int64_t selectedDistance{};
        const auto playerFeet = player.feetPosition();
        const auto playerArea = player.interactionArea().bounds;
        const auto npcInteraction = gameplay::npcs::interactNearest(
            playerFeet, playerArea, activeWorld().npcs());
        if (npcInteraction.npc) {
            lastEvent = "NPC INTERACTION";
            return;
        }
        for (auto& persistent : activeWorld().objects()) {
            auto& object = persistent.instance;
            const auto area = object.interactionArea();
            if (!area || !gameplay::overlaps(playerArea, *area)) { continue; }
            const auto dx = static_cast<std::int64_t>(playerFeet.x) - object.position().x;
            const auto dy = static_cast<std::int64_t>(playerFeet.y) - object.position().y;
            const auto distance = dx * dx + dy * dy;
            const bool earlierHandle = selected &&
                (object.handle().index < selected->instance.handle().index ||
                 (object.handle().index == selected->instance.handle().index &&
                  object.handle().generation < selected->instance.handle().generation));
            if (!selected || distance < selectedDistance ||
                (distance == selectedDistance && earlierHandle)) {
                selected = &persistent;
                selectedDistance = distance;
            }
        }
        if (!selected) { return; }
        auto& object = selected->instance;
        object.open();
        if (auto* contents = object.contents()) {
            for (std::size_t index = 0; index < contents->capacity(); ++index) {
                const auto slot = contents->slot(index);
                if (slot) {
                    static_cast<void>(contents->transferTo(
                        playerItems.inventory().items(), slot->itemId, slot->quantity));
                }
            }
        }
        captureActiveWorld();
    }

    void clearMapTransients() {
        projectiles.clear(combat);
        effects->clear();
        combat.clearTransientRecords();
        activeSword.enabled = false;
        player.finishAttack();
    }

    void commitTransitionIfRequested() {
        mapSession->beginTick();
        static_cast<void>(mapSession->requestTransition(player.collisionBody()));
        if (!mapSession->pending()) { return; }
        const auto transition = mapSession->commitPending();
        if (!transition.changed) {
            lastEvent = "MAP ERROR";
            return;
        }
        clearMapTransients();
        player.relocate(transition.spawn.position, transition.spawn.facing);
        resolveRenderLayers();
        rebuildWorldVisuals();
        followPlayer();
        lastEvent = "MAP " + std::string(activeWorld().id().value());
    }

    [[nodiscard]] save::SaveValidationCatalogs saveCatalogs() const {
        std::vector<const maps::MapData*> maps;
        maps.reserve(knownMapData.size());
        for (const auto& map : knownMapData) { maps.push_back(&map); }
        return {&itemCatalog, std::move(maps)};
    }

    void saveGame() {
        captureActiveWorld();
        save::SaveData data{save::capturePlayer(player, playerItems, activeWorld().id()),
                            sessionWorldState};
        std::string error;
        if (save::writeSaveAtomic(savePath, data, error)) {
            lastEvent = "SAVED";
        } else {
            lastEvent = "SAVE ERROR";
        }
    }

    void loadGame() {
        const auto loaded = save::readSave(savePath, saveCatalogs());
        if (!loaded) {
            lastEvent = "LOAD ERROR";
            return;
        }
        const auto previousPlayer = save::capturePlayer(player, playerItems, activeWorld().id());
        const auto previousWorldState = sessionWorldState;
        const auto restored = mapSession->restore(
            loaded.data.player.currentMapId, loaded.data.world);
        if (!restored.changed) {
            lastEvent = "LOAD ERROR";
            return;
        }
        std::string error;
        if (!save::applyPlayer(loaded.data.player, player, playerItems, itemCatalog, error)) {
            static_cast<void>(mapSession->restore(previousPlayer.currentMapId, previousWorldState));
            static_cast<void>(save::applyPlayer(
                previousPlayer, player, playerItems, itemCatalog, error));
            resolveRenderLayers();
            rebuildWorldVisuals();
            lastEvent = "LOAD ERROR";
            return;
        }
        clearMapTransients();
        resolveRenderLayers();
        rebuildWorldVisuals();
        followPlayer();
        lastEvent = "LOADED";
    }

    void update(simulation::Tick tick, const platform::InputState& input,
                platform::DebugInputState debugInput) {
        events.clear();
        gameplay::tickInvulnerability(player.combatant());
        const gameplay::PlayerActionState previousAction = player.actionState();
        const simulation::PlayerCommand command = commandBuilder.build(tick, localPlayerId, input);
        if (command.actions.saveGamePressed) { saveGame(); }
        if (command.actions.loadGamePressed) { loadGame(); }
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
        player.update(command, activeMap().collision(), activeMap().tileSize());
        if (command.actions.interactPressed) {
            interactWithWorld();
        }
        visual->update(player.motionState(), player.facing(), player.actionState());
        consumeAnimationMarkers();
        resolvePlayerSword();
        updateEnemies();
        auto targets = combatTargets();
        std::vector<gameplay::CombatResolution> projectileResolutions;
        projectiles.update(activeMap().collision(), activeMap().tileSize(), targets, combat, events,
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
        commitTransitionIfRequested();
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
                const auto* tilesetVisual = tilesetVisuals.find(cell->definition.tilesetId);
                if (!tilesetVisual) { throw std::runtime_error("active map references an unavailable tileset image"); }
                const core::RectI source = tilesetVisual->atlas.sourceRect(cell->definition.sourceIndex);
                const int dx = x * activeMap().tileSize() - cameraPosition.x;
                const int dy = y * activeMap().tileSize() - cameraPosition.y;
                if (world::hasFlag(cell->flags, world::TileFlags::flipX)) {
                    renderer.drawImageRegionFlipX(*tilesetVisual->image, source, dx, dy);
                } else {
                    renderer.drawImageRegion(*tilesetVisual->image, source, dx, dy);
                }
            }
        }
        return visible.tileCount();
    }

    void renderActors(render::Renderer2D& renderer) const {
        enum class ActorKind { player, enemy, npc, object, pickup };
        struct Actor {
            int sortY;
            simulation::EntityHandle handle;
            ActorKind kind;
            std::size_t enemyIndex{};
            std::size_t contentIndex{};
        };
        std::vector<Actor> actors;
        const auto& enemies = activeWorld().enemies();
        const auto& npcs = activeWorld().npcs();
        const auto& objects = activeWorld().objects();
        const auto& pickups = activeWorld().pickups();
        actors.reserve(enemies.size() + npcs.size() + objects.size() + pickups.size() + 1);
        actors.push_back({player.feetPosition().y, player.entityHandle(), ActorKind::player});
        for (std::size_t index = 0; index < enemies.size(); ++index) {
            actors.push_back({enemies[index].instance.feetPosition().y,
                              enemies[index].instance.handle(),
                              ActorKind::enemy, index});
        }
        for (std::size_t index = 0; index < npcs.size(); ++index) {
            actors.push_back({npcs[index].instance.position().y,
                              npcs[index].instance.handle(), ActorKind::npc, 0, index});
        }
        for (std::size_t index = 0; index < objects.size(); ++index) {
            actors.push_back({objects[index].instance.position().y,
                              objects[index].instance.handle(),
                              ActorKind::object, 0, index});
        }
        for (std::size_t index = 0; index < pickups.size(); ++index) {
            actors.push_back({pickups[index].instance.position().y,
                              pickups[index].instance.handle(),
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
            } else if (actor.kind == ActorKind::enemy) {
                const auto logical = camera.worldToLogical(
                    enemies[actor.enemyIndex].instance.feetPosition());
                render::drawAnimator(renderer, enemyVisuals[actor.enemyIndex].animator(),
                                     {logical.x, logical.y},
                                     enemyVisuals[actor.enemyIndex].flipX());
            } else if (actor.kind == ActorKind::npc) {
                const auto& npc = npcs[actor.contentIndex].instance;
                const auto& visualSet = npcCatalogVisuals.require(npc.definition().visualSetId);
                const auto logical = camera.worldToLogical(npc.position());
                renderer.fillRect({logical.x - 6, logical.y - 20, 12, 20},
                                  visualSet.markerColor);
            } else if (actor.kind == ActorKind::object) {
                const auto logical = camera.worldToLogical(
                    objects[actor.contentIndex].instance.position());
                render::drawAnimator(renderer, objectVisuals[actor.contentIndex].animator(),
                                     {logical.x, logical.y});
            } else {
                const auto& pickup = pickups[actor.contentIndex].instance;
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
                    if (activeMap().collision().isSolid(x, y)) {
                        renderer.fillRect({x * activeMap().tileSize() - cameraPosition.x,
                                           y * activeMap().tileSize() - cameraPosition.y,
                                           activeMap().tileSize(), activeMap().tileSize()}, fill);
                    }
                }
            }
        }
        if (combatDebug.collisionBody) {
            outline(renderer, player.collisionBody(), cameraPosition, {32, 255, 96, 255});
            for (const auto& enemy : activeWorld().enemies()) {
                outline(renderer, enemy.instance.collisionBody(), cameraPosition, {32, 255, 96, 255});
            }
        }
        if (combatDebug.hurtbox) {
            outline(renderer, player.hurtbox().bounds, cameraPosition, {32, 220, 255, 255});
            for (const auto& enemy : activeWorld().enemies()) {
                if (enemy.instance.hurtbox().enabled) {
                    outline(renderer, enemy.instance.hurtbox().bounds, cameraPosition,
                            {32, 220, 255, 255});
                }
            }
            for (const auto& object : activeWorld().objects()) {
                if (object.instance.hurtbox().enabled) {
                    outline(renderer, object.instance.hurtbox().bounds, cameraPosition,
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
            for (const auto& persistent : activeWorld().enemies()) {
                const auto& enemy = persistent.instance;
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
            for (const auto& object : activeWorld().objects()) {
                if (const auto area = object.instance.interactionArea()) {
                    outline(renderer, *area, cameraPosition, {255, 64, 255, 255});
                }
            }
            for (const auto& pickup : activeWorld().pickups()) {
                outline(renderer, pickup.instance.collectionArea(), cameraPosition,
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
        render::drawText(renderer, font,
                         "MAP: " + std::string(activeWorld().id().value()), 116, 2);
        if (!lastEvent.empty()) { render::drawText(renderer, font, lastEvent, 190, 2); }

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
        const auto visible = camera.visibleTiles(activeMap().widthTiles(), activeMap().heightTiles(), activeMap().tileSize());
        if (groundLayer < activeMap().layerCount()) {
        renderLayer(renderer, activeMap().layer(groundLayer), visible);
        }
        for (const auto layer : lowLayers) {
            renderLayer(renderer, activeMap().layer(layer), visible);
        }
        renderActors(renderer);
        renderProjectiles(renderer);
        if (foregroundLayer < activeMap().layerCount()) {
        renderLayer(renderer, activeMap().layer(foregroundLayer), visible);
        }
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
    std::filesystem::path executableDirectory;
    std::filesystem::path savePath;
    render::Camera2D camera;
    simulation::EntityHandlePool handles;
    simulation::EntityHandle playerHandle{};
    gameplay::Player player;
    GameContentRegistry content;
    RuntimeTilesetCatalog runtimeTilesets{content.tilesets()};
    TilesetVisualCatalog tilesetVisuals;
    gameplay::AttackCatalog& attackCatalog{content.attacks()};
    gameplay::ProjectileCatalog& projectileCatalog{content.projectiles()};
    gameplay::creatures::BehaviorCatalog& behaviorCatalog{content.behaviors()};
    gameplay::creatures::EnemyCatalog& enemyCatalog{content.enemies()};
    gameplay::ItemCatalog& itemCatalog{content.items()};
    gameplay::WorldObjectCatalog& objectCatalog{content.objects()};
    gameplay::npcs::NpcCatalog& npcCatalog{content.npcs()};
    gameplay::npcs::NpcVisualCatalog& npcCatalogVisuals{content.npcVisuals()};
    gameplay::AttackDefinition swordDefinition;
    gameplay::AttackDefinition bowDefinition;
    gameplay::ProjectileDefinition arrowDefinition;
    std::unordered_map<simulation::DefinitionId,
                       std::shared_ptr<const render::SpriteSheet>,
                       simulation::DefinitionIdHash> projectileVisuals;
    EnemyVisualCatalog enemyVisualCatalog;
    gameplay::creatures::EnemyBehaviorSystem enemyBehavior;
    std::vector<EnemyVisualInstance> enemyVisuals;
    gameplay::CombatSystem combat;
    gameplay::ProjectileSystem projectiles;
    gameplay::PlayerItems playerItems;
    gameplay::InventoryOverlayState inventoryOverlay;
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::Image>,
                       simulation::DefinitionIdHash> pickupVisuals;
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::Image>,
                       simulation::DefinitionIdHash> itemVisuals;
    WorldObjectVisualCatalog objectVisualCatalog;
    std::vector<WorldObjectVisualInstance> objectVisuals;
    std::unique_ptr<gameplay::creatures::EnemyFactory> enemyFactory;
    std::unique_ptr<gameplay::WorldObjectFactory> objectFactory;
    std::unique_ptr<gameplay::npcs::NpcFactory> npcFactory;
    maps::MapValidationCatalogs validationCatalogs{};
    std::unique_ptr<maps::RuntimeWorldBuilder> runtimeBuilder;
    maps::MapCatalog mapCatalog;
    std::vector<maps::MapData> knownMapData;
    save::SessionWorldState sessionWorldState;
    std::unique_ptr<maps::MapSession> mapSession;
    simulation::EventBuffer events;
    gameplay::Hitbox activeSword{};
    CommandBuilder commandBuilder;
    simulation::Tick lastTick{};
    std::uint32_t lastSequence{};
    gameplay::AttackInstanceId lastAttack{};
    std::size_t groundLayer{};
    std::size_t foregroundLayer{};
    std::vector<std::size_t> lowLayers;
    bool collisionOverlay{};
    CombatDebugVisibility combatDebug{};
    std::string lastEvent;
};

Phase7Demo::Phase7Demo(platform::ImageDecoder& decoder,
                       const std::filesystem::path& assetRoot,
                       const std::filesystem::path& executableDirectory,
                       const GameLaunchOptions& launchOptions) {
    const GameContentRegistry contentDefinitions;
    const auto& dungeonDefinition = contentDefinitions.tilesets().require(
        simulation::DefinitionId{"tileset.dungeon"});
    const auto tileset = assets_.loadImage("tileset.dungeon",
        assetRoot / dungeonDefinition.relativeAssetPath, decoder);
    const auto font = assets_.loadImage("font.main", assetRoot / "fonts_index.png", decoder);
    const auto idle = assets_.loadImage("player.idle", assetRoot / "Characters/Player/idle/player_idle.png", decoder);
    const auto walk = assets_.loadImage("player.walk", assetRoot / "Characters/Player/walking/player_walking.png", decoder);
    const auto sword = assets_.loadImage("player.sword", assetRoot / "Characters/Player/attacking/player_attacking.png", decoder);
    const auto bow = assets_.loadImage("player.bow", assetRoot / "Characters/Player/attacking/player_attacking_bow.png", decoder);
    const auto arrow = assets_.loadImage("player.arrow", assetRoot / "Characters/Player/attacking/arrow.png", decoder);
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
        tileset, font, idle, walk, sword, bow, arrow, impact,
        soldierIdle, soldierWalk, soldierAttack, soldierDeath,
        skullIdle, skullWalk, skullAttack, skullDeath, skullArrow,
        heart, money, potion, chest, crate, breakingCrate, hudHeart, hudMoney,
        executableDirectory, launchOptions);
    startupSummary_ = state_->startupSummary();
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
