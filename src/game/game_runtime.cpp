#include "game/game_runtime.h"

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
#include "game/audit/audit_snapshot.h"
#include "game/combat_debug.h"
#include "game/effect_system.h"
#include "game/enemy_visual.h"
#include "game/game_launch.h"
#include "game/game_presentation.h"
#include "game/game_session.h"
#include "game/runtime_visual_sync.h"
#include "game/gameplay/attack_definitions.h"
#include "game/gameplay/combat_system.h"
#include "game/gameplay/creatures/creature_engine.h"
#include "game/gameplay/dialogue/dialogue_session.h"
#include "game/gameplay/quests/quest_state.h"
#include "game/gameplay/player.h"
#include "game/gameplay/items.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/world_objects.h"
#include "game/gameplay/world_pickups.h"
#include "game/gameplay/projectile_system.h"
#include "game/player_visual.h"
#include "game/presentation/presentation_feedback_controller.h"
#include "game/presentation/visual_content_loader.h"
#include "game/world_object_visual.h"
#include "game/maps/dmap.h"
#include "game/maps/map_catalog.h"
#include "game/maps/gameplay_map_discovery.h"
#include "game/maps/runtime_world.h"
#include "game/save/save_data.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
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

const char* objectStateName(gameplay::WorldObjectState state) noexcept {
    switch (state) {
    case gameplay::WorldObjectState::idle: return "idle";
    case gameplay::WorldObjectState::opened: return "opened";
    case gameplay::WorldObjectState::destroying: return "destroying";
    case gameplay::WorldObjectState::destroyed: return "destroyed";
    }
    return "unknown";
}

const char* questStatusName(gameplay::quests::QuestStatus status) noexcept {
    switch (status) {
    case gameplay::quests::QuestStatus::inactive: return "inactive";
    case gameplay::quests::QuestStatus::active: return "active";
    case gameplay::quests::QuestStatus::completed: return "completed";
    }
    return "unknown";
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

} // namespace

struct GameRuntime::State final {
    State(std::shared_ptr<const render::Image> tileImage,
          std::shared_ptr<const render::Image> fontImage,
          std::shared_ptr<const render::Image> idleImage,
          std::shared_ptr<const render::Image> walkImage,
          std::shared_ptr<const render::Image> swordImage,
          std::shared_ptr<const render::Image> bowImage,
          std::shared_ptr<const render::Image> hurtImage,
          std::shared_ptr<const render::Image> impactImage,
          presentation::RuntimeVisualContent runtimeVisualContent,
          std::shared_ptr<const render::Image> hudHeartImage,
          std::shared_ptr<const render::Image> hudMoneyImage,
          GameContentRegistry contentDefinitions,
          std::filesystem::path executableDirectory,
          const GameLaunchOptions& launchOptions)
        : tileset(std::move(tileImage)),
          atlas(tileset->width(), tileset->height(), core::GameMetrics::tileSize),
          font(std::move(fontImage)),
          idleSheet(std::make_shared<const render::SpriteSheet>(std::move(idleImage))),
          walkSheet(std::make_shared<const render::SpriteSheet>(std::move(walkImage))),
          swordSheet(std::make_shared<const render::SpriteSheet>(std::move(swordImage))),
          bowSheet(std::make_shared<const render::SpriteSheet>(std::move(bowImage))),
          hurtSheet(std::make_shared<const render::SpriteSheet>(std::move(hurtImage))),
          impactSheet(std::make_shared<const render::SpriteSheet>(std::move(impactImage))),
          hudHeartImage(std::move(hudHeartImage)), hudMoneyImage(std::move(hudMoneyImage)),
          executableDirectory(std::move(executableDirectory)),
          content(std::move(contentDefinitions)),
          runtimeVisualContent(std::move(runtimeVisualContent)),
          session(localPlayerId, content.progressions().require(
                                      gameplay::rpg::defaultPlayerProgressionId()), {}) {
        const auto& dungeonDefinition = content.tilesets().require(
            simulation::DefinitionId{"tileset.dungeon"});
        tilesetVisuals.add(runtimeTilesets.requireRuntimeId(dungeonDefinition.id), tileset,
                          dungeonDefinition);
        savePath = this->executableDirectory / "savegame.sav";
        visual = std::make_unique<PlayerVisual>(
            makeClips("player.idle", idleSheet, 32, 2, 30, {16, 31}, true),
            makeClips("player.walk", walkSheet, 32, 4, 8, {16, 31}, true),
            makeClips("player.sword", swordSheet, 48, 4,
                      attackCatalog.require(gameplay::playerSwordAttackId()).totalTicks / 4,
                      {24, 31}, false),
            makeClips("player.bow", bowSheet, 32, 2,
                      attackCatalog.require(gameplay::playerBowAttackId()).totalTicks / 2,
                      {16, 31}, false),
            makeClips("player.hurt", hurtSheet, 32, 2, 4, {16, 31}, false));
        effects = std::make_unique<EffectSystem>(makeImpactClip(impactSheet));
        enemyFactory = std::make_unique<gameplay::creatures::EnemyFactory>(
            enemyCatalog, behaviorCatalog, attackCatalog, projectileCatalog);
        objectFactory = std::make_unique<gameplay::WorldObjectFactory>(
            objectCatalog, itemCatalog);
        npcFactory = std::make_unique<gameplay::npcs::NpcFactory>(npcCatalog);
        validationCatalogs = mapValidationCatalogs(content);
        runtimeBuilder = std::make_unique<maps::RuntimeWorldBuilder>(
            validationCatalogs, *enemyFactory, *objectFactory, runtimeTilesets, npcFactory.get());
        session.configureCombat(attackCatalog, projectileCatalog, behaviorCatalog,
                                attackCatalog.require(gameplay::playerSwordAttackId()),
                                attackCatalog.require(gameplay::playerBowAttackId()));
        session.configureItems(itemCatalog);
        session.configureNarrative(content.dialogues(), content.quests());
        session.configureRewards(content.rewardProfiles(), content.pickups());
        session.configureRewardGrants(content.rewardGrants());
        session.configureShops(content.shops());
        auto discovered = maps::discoverGameplayMaps(
            this->executableDirectory, std::filesystem::current_path(), &validationCatalogs,
            launchOptions.mapPath.has_value());
        if (!discovered) {
            throw std::runtime_error(discovered.error);
        }
        std::cerr << "[info] gameplay map discovery root=" << discovered.root.string() << '\n'
                  << "[info] discovered gameplay maps=" << discovered.maps.size() << '\n';
        for (const auto& map : discovered.maps) {
            std::cerr << "[info] gameplay map: " << map.id.value() << " -> "
                      << map.path.filename().string() << '\n';
        }
        std::string startupError;
        const auto startup = selectDiscoveredStartupMap(launchOptions, discovered.maps,
                                                        startupError);
        if (!startup) { throw std::runtime_error(startupError); }
        auto startupLoaded = maps::readDmap(startup->path, &validationCatalogs);
        if (!startupLoaded) {
            throw std::runtime_error("could not load startup map '" +
                startup->path.string() + "': " + startupLoaded.error);
        }

        maps::MapCatalog startupCatalog = std::move(discovered.catalog);
        const auto discoveredPath = startupCatalog.find(startupLoaded.data.id);
        if (discoveredPath == nullptr) {
            startupCatalog.add(startupLoaded.data.id, startup->path);
        } else {
            std::error_code pathError;
            const auto discoveredCanonical = std::filesystem::weakly_canonical(
                *discoveredPath, pathError);
            const auto startupCanonical = std::filesystem::weakly_canonical(
                startup->path, pathError);
            if (!pathError && discoveredCanonical != startupCanonical) {
                throw std::runtime_error("duplicate gameplay MapId '" +
                    std::string(startupLoaded.data.id.value()) + "' between '" +
                    discoveredPath->string() + "' and '" + startup->path.string() + "'");
            }
        }
        knownMapData.clear();
        knownMapData.reserve(discovered.maps.size() + 1);
        for (const auto& map : discovered.maps) { knownMapData.push_back(map.data); }
        if (std::none_of(knownMapData.begin(), knownMapData.end(),
                         [&](const auto& map) { return map.id == startupLoaded.data.id; })) {
            knownMapData.push_back(startupLoaded.data);
        }
        mapCatalog = std::move(startupCatalog);
        const auto startMap = startupLoaded.data.id;
        std::string spawnError;
        const auto selectedSpawn = selectStartupSpawn(
            startupLoaded.data, launchOptions.spawnId, spawnError);
        if (!selectedSpawn) {
            throw std::runtime_error("could not select startup spawn: " + spawnError);
        }
        std::string sessionError;
        if (!session.initializeMap(mapCatalog, validationCatalogs, *runtimeBuilder,
                                    startMap, *selectedSpawn, sessionError)) {
            throw std::runtime_error("could not activate startup DMAP: " + sessionError);
        }
        std::cerr << "[info] startup map=" << startMap.value()
                  << " spawn=" << selectedSpawn->value() << '\n';
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

    [[nodiscard]] audit::GameAuditSnapshot auditSnapshot() const {
        audit::GameAuditSnapshot snapshot;
        snapshot.tick = lastTick;
        snapshot.currentMap = std::string(activeWorld().id().value());
        snapshot.currentSpawn = std::string(activeWorld().spawn().id.value());
        snapshot.playerX = player.feetPosition().x;
        snapshot.playerY = player.feetPosition().y;
        snapshot.playerFacing = gameplay::facingName(player.facing());
        snapshot.playerMotion = gameplay::motionStateName(player.motionState());
        snapshot.playerAction = gameplay::actionStateName(player.actionState());
        snapshot.playerHealth = player.health().current;
        snapshot.playerMaximumHealth = player.health().maximum;
        snapshot.playerExperience = session.progression().totalExperience();
        snapshot.playerLevel = session.progression().level();
        snapshot.playerDerivedMaximumHealth = session.derivedPlayerStats().maximumHealth;
        snapshot.playerAttackDamageBonus = session.derivedPlayerStats().playerAttackDamageBonus;
        snapshot.equippedArmor = session.playerItems().equipment().item(gameplay::rpg::EquipmentSlot::armor);
        snapshot.equippedAccessory = session.playerItems().equipment().item(gameplay::rpg::EquipmentSlot::accessory);
        snapshot.gold = session.playerItems().wallet().gold();
        snapshot.bankGold = session.playerItems().bank().gold();
        for (std::size_t index = 0; index < session.playerItems().bank().items().capacity(); ++index) {
            if (session.playerItems().bank().items().slot(index)) {
                ++snapshot.bankOccupiedSlots;
            }
        }
        snapshot.inventoryOpen = session.inventoryOverlay().open();

        for (std::size_t index = 0; index < session.playerItems().inventory().items().capacity(); ++index) {
            const auto& slot = session.playerItems().inventory().items().slot(index);
            if (!slot) { continue; }
            snapshot.inventory.push_back({index, std::string(slot->itemId.value()), slot->quantity});
        }
        for (std::size_t index = 0; index < gameplay::QuickSlotBindings::slotCount; ++index) {
            const auto& binding = session.playerItems().quickSlots().binding(index);
            snapshot.quickSlots.push_back({
                index, binding ? std::string(binding->value()) : std::string{}});
        }

        for (const auto& persistent : activeWorld().enemies()) {
            const auto& enemy = persistent.instance;
            snapshot.enemies.push_back({
                persistent.persistentId.value, std::string(enemy.definition().id.value()),
                enemy.feetPosition().x, enemy.feetPosition().y,
                enemy.combatant().health.current, enemy.combatant().health.maximum,
                gameplay::creatures::behaviorStateName(enemy.state())});
        }
        for (const auto& persistent : activeWorld().npcs()) {
            const auto& npc = persistent.instance;
            snapshot.npcs.push_back({
                persistent.persistentId.value, std::string(npc.definition().id.value()),
                npc.position().x, npc.position().y, 0, 0,
                gameplay::facingName(npc.facing())});
        }
        for (const auto& persistent : activeWorld().objects()) {
            const auto& object = persistent.instance;
            int health = 0;
            int maximumHealth = 0;
            if (const auto* combatant = object.combatant()) {
                health = combatant->health.current;
                maximumHealth = combatant->health.maximum;
            }
            audit::AuditActor actor{
                persistent.persistentId.value, std::string(object.definition().id.value()),
                object.position().x, object.position().y, health, maximumHealth,
                objectStateName(object.state())};
            if (object.hasActivation()) { actor.activation = object.activationActive(); }
            if (object.isDoor()) {
                switch (object.doorState()) {
                case gameplay::DoorState::locked: actor.doorState = "locked"; break;
                case gameplay::DoorState::closed: actor.doorState = "closed"; break;
                case gameplay::DoorState::open: actor.doorState = "open"; break;
                }
            }
            snapshot.objects.push_back(std::move(actor));
        }
        for (const auto& persistent : activeWorld().pickups()) {
            const auto& pickup = persistent.instance;
            std::uint64_t quantity = 0;
            std::visit([&quantity](const auto& payload) {
                if constexpr (std::is_same_v<std::decay_t<decltype(payload)>,
                                              gameplay::HealthPickup>) {
                    quantity = static_cast<std::uint64_t>(payload.amount);
                } else if constexpr (std::is_same_v<std::decay_t<decltype(payload)>,
                                                     gameplay::CurrencyPickup>) {
                    quantity = payload.amount;
                } else {
                    quantity = payload.quantity;
                }
            }, pickup.payload());
            snapshot.pickups.push_back({
                persistent.persistentId.value,
                std::string(pickup.definition().id.value()),
                pickup.position().x, pickup.position().y, quantity});
        }

        snapshot.dialogue.active = session.dialogue().isOpen();
        snapshot.dialogue.dialogueId = std::string(session.dialogue().dialogueId());
        snapshot.dialogue.nodeId = std::string(session.dialogue().nodeId());
        snapshot.dialogue.pageIndex = session.dialogue().pageIndex();
        snapshot.dialogue.pageCount = session.dialogue().pageCount();
        snapshot.dialogue.choicesVisible = session.dialogue().choicesVisible();
        snapshot.dialogue.selectedChoice = session.dialogue().selectedChoice();
        for (const auto& progress : session.questState().snapshot()) {
            audit::AuditQuest quest{std::string(progress.questId.value()),
                                    questStatusName(progress.status), {}, progress.rewardClaimed};
            for (const auto& objective : progress.objectives) {
                quest.objectives.push_back({std::string(objective.objectiveId.value()),
                                            objective.currentCount});
            }
            snapshot.quests.push_back(std::move(quest));
        }
        for (const auto& flag : session.dialogueFlags().values()) {
            snapshot.dialogueFlags.emplace_back(flag.value());
        }
        snapshot.activeProjectileCount = session.projectiles().projectiles().size();
        snapshot.lastEvent = lastEvent;
        return snapshot;
    }

    [[nodiscard]] const maps::RuntimeWorld& activeWorld() const { return session.world(); }
    [[nodiscard]] const world::RuntimeMap& activeMap() const { return activeWorld().map(); }

    void rebuildWorldVisuals() {
        const auto result = synchronizeRuntimeWorldVisuals(
            activeWorld(), enemyVisualCatalog, enemyVisuals,
            objectVisualCatalog, objectVisuals, objectResidueVisuals,
            npcCatalogVisuals, npcVisuals);
        if (!result) { throw std::runtime_error(result.error); }
        if (enemyVisuals.size() != activeWorld().enemies().size() ||
            objectVisuals.size() != activeWorld().objects().size() ||
            npcVisuals.size() != activeWorld().npcs().size()) {
            throw std::logic_error("runtime and visual actor counts are out of sync");
        }
    }

    void followPlayer() {
        presentation.followPlayer(player.feetPosition(), activeMap().worldWidthPixels(),
                                  activeMap().worldHeightPixels());
    }


    void consumeSimulationEvents() {
        const auto mapData = std::find_if(knownMapData.begin(), knownMapData.end(),
            [&](const auto& value) { return value.id == activeWorld().id(); });
        if (mapData != knownMapData.end()) {
            presentationFeedback.consume(events, *mapData, player.entityHandle(), presentationEffects);
        }
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
            } else if (std::holds_alternative<simulation::NpcTalked>(event)) {
                lastEvent = "NPC INTERACTION";
            } else if (std::holds_alternative<simulation::RegionEntered>(event)) {
                lastEvent = "REGION ENTERED";
            } else if (std::holds_alternative<simulation::RegionExited>(event)) {
                lastEvent = "REGION EXITED";
            } else if (std::holds_alternative<simulation::EncounterStarted>(event)) {
                lastEvent = "ENCOUNTER STARTED";
            } else if (std::holds_alternative<simulation::EncounterCompleted>(event)) {
                lastEvent = "ENCOUNTER COMPLETED";
            } else if (std::holds_alternative<simulation::ObjectOpened>(event)) {
                lastEvent = "OBJECT OPENED";
            } else if (const auto* activation =
                           std::get_if<simulation::ObjectActivationChanged>(&event)) {
                lastEvent = activation->active ? "OBJECT ACTIVATED" : "OBJECT DEACTIVATED";
            }
        }
    }

    void clearMapTransients() {
        effects->clear();
        presentationEffects.clearAll();
    }

    void commitTransitionIfRequested() {
        bool enteredMap = false;
        for (const auto& event : events.events()) {
            enteredMap = enteredMap || std::holds_alternative<simulation::MapEntered>(event);
        }
        if (!enteredMap) { return; }
        clearMapTransients();
        rebuildWorldVisuals();
        followPlayer();
        lastEvent = "MAP " + std::string(activeWorld().id().value());
    }

    [[nodiscard]] save::SaveValidationCatalogs saveCatalogs() const {
        std::vector<const maps::MapData*> maps;
        maps.reserve(knownMapData.size());
        for (const auto& map : knownMapData) { maps.push_back(&map); }
        return {&itemCatalog, std::move(maps), &content.quests(), &content.progressions(),
                &content.objects()};
    }

    void saveGame() {
        const save::SaveData data = session.captureSaveData();
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
        std::string error;
        if (!session.restoreSaveData(loaded.data, error)) {
            lastEvent = "LOAD ERROR";
            return;
        }
        clearMapTransients();
        rebuildWorldVisuals();
        followPlayer();
        lastEvent = "LOADED";
    }

    void update(simulation::Tick tick, const platform::InputState& input,
                platform::DebugInputState debugInput) {
        presentationEffects.advance();
        // Kept in the Runtime until dialogue/inventory pause semantics are
        // fully owned by the Session. This preserves the old rule that the
        // player's invulnerability timer advances even while those overlays
        // consume a command tick.
        const simulation::PlayerCommand command = commandBuilder.build(tick, localPlayerId, input);
        if (!session.dialogue().isOpen() && !session.sceneActive()) {
            if (command.actions.saveGamePressed) { saveGame(); }
            if (command.actions.loadGamePressed) { loadGame(); }
        }
        session.tick(command);
        // The Session owns map transitions. Rebuild presentation immediately so
        // the remaining systems in this transitional Runtime do not observe a
        // world whose visual instances belong to the previous map.
        commitTransitionIfRequested();
        if (enemyVisuals.size() != activeWorld().enemies().size() ||
            objectVisuals.size() != activeWorld().objects().size() ||
            npcVisuals.size() != activeWorld().npcs().size()) {
            rebuildWorldVisuals();
        }
        for (std::size_t index = 0; index < enemyVisuals.size(); ++index) {
            enemyVisuals[index].update(activeWorld().enemies()[index].instance);
        }
        for (std::size_t index = 0; index < npcVisuals.size(); ++index) {
            npcVisuals[index].update(activeWorld().npcs()[index].instance);
        }
        visual->update(player.motionState(), player.facing(), player.actionState());
        for (std::size_t index = 0; index < objectVisuals.size(); ++index) {
            objectVisuals[index].update(activeWorld().objects()[index].instance);
        }
        consumeSimulationEvents();
        effects->update();
        if (debugInput.toggleCollisionPressed) { collisionOverlay = !collisionOverlay; }
        combatDebug.apply(debugInput);
        followPlayer();
        lastTick = tick;
        lastSequence = command.sequence;
    }

    void render(render::Framebuffer& framebuffer) const {
        const auto view = buildGameViewModel(
            player, session.playerItems(), itemCatalog, session.inventoryOverlay(), session.bankOverlay(),
            session.derivedPlayerStats(), session.shopOverlay(), content.shops());
        const auto presentationFrame = presentationEffects.resolveFrame();
        presentation.render(framebuffer, {
            activeWorld(), player, *visual, enemyVisuals, objectVisuals, objectResidueVisuals,
            *effects,
            presentationFrame,
            session.projectiles(),
            tilesetVisuals, npcCatalogVisuals, npcVisuals, enemyVisualCatalog, objectVisualCatalog,
            runtimeVisualContent.staticSprites, font, hudHeartImage, hudMoneyImage,
            session.dialogue(), view, combatDebug, session.activeSword(),
            session.scenePresentation(), lastEvent, collisionOverlay});
    }


    std::shared_ptr<const render::Image> tileset;
    world::TileAtlasLayout atlas;
    render::BitmapFont font;
    std::shared_ptr<const render::SpriteSheet> idleSheet;
    std::shared_ptr<const render::SpriteSheet> walkSheet;
    std::shared_ptr<const render::SpriteSheet> swordSheet;
    std::shared_ptr<const render::SpriteSheet> bowSheet;
    std::shared_ptr<const render::SpriteSheet> hurtSheet;
    std::shared_ptr<const render::SpriteSheet> impactSheet;
    std::shared_ptr<const render::Image> hudHeartImage;
    std::shared_ptr<const render::Image> hudMoneyImage;
    std::unique_ptr<PlayerVisual> visual;
    std::unique_ptr<EffectSystem> effects;
    std::filesystem::path executableDirectory;
    std::filesystem::path savePath;
    GamePresentation presentation;
    GameContentRegistry content;
    presentation::RuntimeVisualContent runtimeVisualContent;
    presentation::PresentationEffectSystem presentationEffects{content.presentationEffects()};
    presentation::PresentationFeedbackController presentationFeedback;
    GameSession session;
    const gameplay::Player& player{session.player()};
    RuntimeTilesetCatalog runtimeTilesets{content.tilesets()};
    TilesetVisualCatalog tilesetVisuals;
    const gameplay::AttackCatalog& attackCatalog{content.attacks()};
    const gameplay::ProjectileCatalog& projectileCatalog{content.projectiles()};
    const gameplay::creatures::BehaviorCatalog& behaviorCatalog{content.behaviors()};
    const gameplay::creatures::EnemyCatalog& enemyCatalog{content.enemies()};
    const gameplay::ItemCatalog& itemCatalog{content.items()};
    const gameplay::WorldObjectCatalog& objectCatalog{content.objects()};
    const gameplay::npcs::NpcCatalog& npcCatalog{content.npcs()};
    const presentation::RuntimeNpcVisualCatalog& npcCatalogVisuals{runtimeVisualContent.npcs};
    std::vector<presentation::RuntimeNpcVisualInstance> npcVisuals;
    const EnemyVisualCatalog& enemyVisualCatalog{runtimeVisualContent.enemies};
    std::vector<EnemyVisualInstance> enemyVisuals;
    const WorldObjectVisualCatalog& objectVisualCatalog{runtimeVisualContent.objects};
    std::vector<WorldObjectVisualInstance> objectVisuals;
    std::vector<WorldObjectResidueVisualInstance> objectResidueVisuals;
    std::unique_ptr<gameplay::creatures::EnemyFactory> enemyFactory;
    std::unique_ptr<gameplay::WorldObjectFactory> objectFactory;
    std::unique_ptr<gameplay::npcs::NpcFactory> npcFactory;
    maps::MapValidationCatalogs validationCatalogs{};
    std::unique_ptr<maps::RuntimeWorldBuilder> runtimeBuilder;
    maps::MapCatalog mapCatalog;
    std::vector<maps::MapData> knownMapData;
    const simulation::EventBuffer& events{session.events()};
    CommandBuilder commandBuilder;
    simulation::Tick lastTick{};
    std::uint32_t lastSequence{};
    bool collisionOverlay{};
    CombatDebugVisibility combatDebug{};
    std::string lastEvent;
};

GameRuntime::GameRuntime(platform::ImageDecoder& decoder,
                       const std::filesystem::path& assetRoot,
                       const std::filesystem::path& executableDirectory,
                       GameContentRegistry contentDefinitions,
                       const GameLaunchOptions& launchOptions,
                       std::optional<std::filesystem::path> contentWorkspaceRoot) {
    const auto& dungeonDefinition = contentDefinitions.tilesets().require(
        simulation::DefinitionId{"tileset.dungeon"});
    const auto tileset = assets_.loadImage("tileset.dungeon",
        assetRoot / dungeonDefinition.relativeAssetPath, decoder);
    const auto font = assets_.loadImage("font.main", assetRoot / "fonts_index.png", decoder);
    const auto idle = assets_.loadImage("player.idle", assetRoot / "Characters/Player/idle/player_idle.png", decoder);
    const auto walk = assets_.loadImage("player.walk", assetRoot / "Characters/Player/walking/player_walking.png", decoder);
    const auto sword = assets_.loadImage("player.sword", assetRoot / "Characters/Player/attacking/player_attacking.png", decoder);
    const auto bow = assets_.loadImage("player.bow", assetRoot / "Characters/Player/attacking/player_attacking_bow.png", decoder);
    const auto hurt = assets_.loadImage("player.hurt", assetRoot / "Characters/Player/death/player_death.png", decoder);
    const auto impact = assets_.loadImage("effect.arrow_impact", assetRoot / "Explosion/arrow_hits_dust.png", decoder);
    const auto hudHeart = assets_.loadImage(
        "hud.heart", assetRoot / "Icons/heart_complete.png", decoder);
    const auto hudMoney = assets_.loadImage(
        "hud.money", assetRoot / "Icons/money.png", decoder);
    presentation::VisualContentLoader visualLoader(decoder);
    const auto visualContent = visualLoader.load(
        contentDefinitions, {assetRoot, std::move(contentWorkspaceRoot)});
    if (!visualContent) {
        std::string message = "visual content loading failed";
        for (const auto& diagnostic : visualContent.diagnostics) {
            message += "\n" + presentation::formatVisualContentDiagnostic(diagnostic);
        }
        throw std::runtime_error(message);
    }
    state_ = std::make_unique<State>(
        tileset, font, idle, walk, sword, bow, hurt, impact, std::move(*visualContent.content),
        hudHeart, hudMoney, std::move(contentDefinitions), executableDirectory, launchOptions);
    startupSummary_ = state_->startupSummary();
}

GameRuntime::~GameRuntime() = default;

void GameRuntime::fixedTick(simulation::Tick tick, const platform::InputState& input,
                           platform::DebugInputState debugInput) {
    state_->update(tick, input, debugInput);
}

void GameRuntime::render(render::Framebuffer& framebuffer) const { state_->render(framebuffer); }

audit::GameAuditSnapshot GameRuntime::auditSnapshot() const {
    return state_->auditSnapshot();
}

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
