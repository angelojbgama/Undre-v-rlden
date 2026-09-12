#include "game/game_session.h"

#include "game/save/save_data.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace underworld::game {

GameSession::GameSession(simulation::PlayerId playerId,
                         const gameplay::rpg::PlayerProgressionDefinition& progression,
                         core::WorldPointI initialPosition,
                         gameplay::PlayerMovementConfig movementConfig)
    : player_(playerId, handles_.create(), initialPosition,
              progression.baseStats.maximumHealth, std::move(movementConfig)),
      progression_(progression) {}

void GameSession::relocatePlayer(core::WorldPointI position,
                                 gameplay::FacingDirection facing) noexcept {
    player_.relocate(position, facing);
}

void GameSession::configureCombat(const gameplay::AttackCatalog& attacks,
                                  const gameplay::ProjectileCatalog& projectiles,
                                  const gameplay::creatures::BehaviorCatalog& behaviors,
                                  const gameplay::AttackDefinition& sword,
                                  const gameplay::AttackDefinition& bow) {
    attackCatalog_ = &attacks;
    projectileCatalog_ = &projectiles;
    behaviorCatalog_ = &behaviors;
    swordDefinition_ = &sword;
    bowDefinition_ = &bow;
    projectiles_ = std::make_unique<gameplay::ProjectileSystem>(handles_, projectiles);
}

void GameSession::configureItems(const gameplay::ItemCatalog& items) {
    itemCatalog_ = &items;
    playerItems_ = std::make_unique<gameplay::PlayerItems>(items);
    refreshDerivedPlayerStats();
}

void GameSession::refreshDerivedPlayerStats() {
    if (!itemCatalog_ || !playerItems_) { return; }
    derivedPlayerStats_ = gameplay::rpg::derivePlayerStats(
        progression_.definition().baseStats, playerItems_->equipment(), *itemCatalog_);
    player_.health().setMaximum(derivedPlayerStats_.maximumHealth);
}

gameplay::DamageSpec GameSession::effectivePlayerDamage(
    const gameplay::DamageSpec& base) const noexcept {
    gameplay::DamageSpec result = base;
    if (result.amount > std::numeric_limits<int>::max() -
                            derivedPlayerStats_.playerAttackDamageBonus) {
        result.amount = std::numeric_limits<int>::max();
    } else {
        result.amount += derivedPlayerStats_.playerAttackDamageBonus;
    }
    return result;
}

void GameSession::configureNarrative(
    const gameplay::dialogue::DialogueCatalog& dialogues,
    const gameplay::quests::QuestCatalog& quests) {
    dialogueCatalog_ = &dialogues;
    questCatalog_ = &quests;
    dialogue_ = std::make_unique<gameplay::dialogue::DialogueSession>(
        dialogues, dialogueFlags_);
    questSystem_ = std::make_unique<gameplay::quests::QuestSystem>(quests, questState_);
}

save::SaveData GameSession::captureSaveData() const {
    save::SavedPlayerBank bank;
    for (std::size_t index = 0; index < bank.items.size(); ++index) {
        bank.items[index] = playerItems_->bank().items().slot(index);
    }
    bank.gold = playerItems_->bank().gold();
    auto world = worldState_;
    if (mapSession_ && mapSession_->world() && mapSession_->data()) {
        save::captureWorldState(*mapSession_->data(), *mapSession_->world(), world);
    }
    return {save::capturePlayer(player_, *playerItems_, mapSession_->world()->id()),
            {progression_.definition().id, progression_.totalExperience()}, std::move(world),
            dialogueFlags_, questState_,
            {playerItems_->equipment().item(gameplay::rpg::EquipmentSlot::armor),
             playerItems_->equipment().item(gameplay::rpg::EquipmentSlot::accessory)}, bank};
}

bool GameSession::restoreSaveData(const save::SaveData& data, std::string& error) {
    if (!mapSession_ || !playerItems_ || !itemCatalog_ || !dialogue_ || !questSystem_) {
        error = "GameSession is not fully configured";
        return false;
    }
    if (data.progression.definitionId != progression_.definition().id) {
        error = "save references a different player progression";
        return false;
    }
    const auto validEquipment = [&](const std::optional<simulation::DefinitionId>& item,
                                    gameplay::rpg::EquipmentSlot slot) {
        if (!item) { return true; }
        const auto* definition = itemCatalog_->find(*item);
        return definition && definition->category == gameplay::ItemCategory::equipment &&
               definition->equipment && definition->equipment->slot == slot &&
               definition->stackLimit == 1;
    };
    if (!validEquipment(data.equipment.armor, gameplay::rpg::EquipmentSlot::armor) ||
        !validEquipment(data.equipment.accessory, gameplay::rpg::EquipmentSlot::accessory)) {
        error = "save references invalid equipment";
        return false;
    }
    for (const auto& slot : data.bank.items) {
        if (slot) {
            const auto* definition = itemCatalog_->find(slot->itemId);
            if (!definition || slot->quantity == 0 || slot->quantity > definition->stackLimit) {
                error = "save references invalid bank item";
                return false;
            }
        }
    }
    const save::SaveData previous = captureSaveData();
    if (sceneController_.active()) {
        sceneController_.abort(events_, "scene aborted by save restore");
        clearCombatTransients();
    }
    if (!restoreMap(data.player.currentMapId, data.world, error)) { return false; }
    // MapSession applies the object/pickup deltas while rebuilding the runtime
    // world; the session-owned rule and encounter state must be replaced as one
    // atomic save restore as well.
    worldState_ = data.world;
    playerItems_->equipment().restore(data.equipment.armor, data.equipment.accessory);
    refreshDerivedPlayerStats();
    playerItems_->bank().items().restoreSlots(data.bank.items);
    playerItems_->bank().restoreGold(data.bank.gold);
    if (!save::applyPlayer(data.player, player_, *playerItems_, *itemCatalog_, error) ||
        !restoreNarrativeState(data.dialogueFlags, data.quests.snapshot(), error) ||
        !progression_.restoreExperience(data.progression.totalExperience)) {
        std::string rollbackError;
        static_cast<void>(restoreMap(previous.player.currentMapId, previous.world,
                                     rollbackError));
        worldState_ = previous.world;
        playerItems_->equipment().restore(previous.equipment.armor, previous.equipment.accessory);
        refreshDerivedPlayerStats();
        playerItems_->bank().items().restoreSlots(previous.bank.items);
        playerItems_->bank().restoreGold(previous.bank.gold);
        static_cast<void>(save::applyPlayer(previous.player, player_, *playerItems_,
                                            *itemCatalog_, rollbackError));
        static_cast<void>(restoreNarrativeState(previous.dialogueFlags,
                                                previous.quests.snapshot(), rollbackError));
        static_cast<void>(progression_.restoreExperience(previous.progression.totalExperience));
        return false;
    }
    clearCombatTransients();
    closeDialogue();
    bankOverlay_.close();
    shopOverlay_.close();
    error.clear();
    return true;
}

void GameSession::closeDialogue() noexcept {
    if (dialogue_) { dialogue_->close(); }
}

gameplay::WorldLogicRuntime GameSession::worldLogicRuntime() {
    return {
        [&](const simulation::DefinitionId& id) {
            return encounters_.state(worldState_.encounters, mapSession_->world()->id(), id) ==
                   gameplay::EncounterState::completed;
        },
        [&](const simulation::DefinitionId& id, simulation::EventBuffer& generated) {
            return encounters_.start(mapSession_->data()->encounters, mapSession_->world()->id(), id,
                                     worldState_.encounters, generated);
        },
        [&](simulation::PersistentInstanceId id, maps::DoorState state) {
            return mapSession_->world()->setDoorState(id, state);
        },
        [&](simulation::PersistentInstanceId id) {
            return mapSession_->world()->doorState(id);
        },
        [&](simulation::PersistentInstanceId id) {
            return mapSession_->world()->objectActivation(id);
        },
        [&](const simulation::DefinitionId& id) { return requestScene(id); }};
}

gameplay::scenes::SceneRuntimeHooks GameSession::sceneRuntimeHooks() {
    using gameplay::scenes::SceneActorKind;
    using gameplay::scenes::SceneActorSnapshot;
    return {
        [&](const gameplay::scenes::SceneActorBinding& binding)
            -> std::optional<SceneActorSnapshot> {
            if (!mapSession_ || !mapSession_->world()) return std::nullopt;
            if (binding.kind == SceneActorKind::player) {
                return SceneActorSnapshot{player_.feetPosition(), player_.facing()};
            }
            if (binding.kind == SceneActorKind::npc) {
                const auto found = std::find_if(mapSession_->world()->npcs().begin(),
                    mapSession_->world()->npcs().end(), [&](const auto& value) {
                        return value.persistentId == binding.instanceId;
                    });
                if (found == mapSession_->world()->npcs().end()) return std::nullopt;
                return SceneActorSnapshot{found->instance.position(), found->instance.facing()};
            }
            const auto found = std::find_if(mapSession_->world()->enemies().begin(),
                mapSession_->world()->enemies().end(), [&](const auto& value) {
                    return value.persistentId == binding.instanceId;
                });
            if (found == mapSession_->world()->enemies().end()) return std::nullopt;
            return SceneActorSnapshot{found->instance.feetPosition(), found->instance.facing()};
        },
        [&](const gameplay::scenes::SceneActorBinding& binding, core::WorldPointI position) {
            if (!mapSession_ || !mapSession_->world()) return false;
            if (binding.kind == SceneActorKind::player) { player_.relocate(position, player_.facing()); return true; }
            if (binding.kind == SceneActorKind::npc) {
                const auto found = std::find_if(mapSession_->world()->npcs().begin(),
                    mapSession_->world()->npcs().end(), [&](const auto& value) {
                        return value.persistentId == binding.instanceId;
                    });
                if (found == mapSession_->world()->npcs().end()) return false;
                found->instance.sceneRelocate(position); return true;
            }
            const auto found = std::find_if(mapSession_->world()->enemies().begin(),
                mapSession_->world()->enemies().end(), [&](const auto& value) {
                    return value.persistentId == binding.instanceId;
                });
            if (found == mapSession_->world()->enemies().end()) return false;
            found->instance.sceneRelocate(position); return true;
        },
        [&](const gameplay::scenes::SceneActorBinding& binding,
            gameplay::FacingDirection facing) {
            if (!mapSession_ || !mapSession_->world()) return false;
            if (binding.kind == SceneActorKind::player) { player_.relocate(player_.feetPosition(), facing); return true; }
            if (binding.kind == SceneActorKind::npc) {
                const auto found = std::find_if(mapSession_->world()->npcs().begin(),
                    mapSession_->world()->npcs().end(), [&](const auto& value) {
                        return value.persistentId == binding.instanceId;
                    });
                if (found == mapSession_->world()->npcs().end()) return false;
                found->instance.sceneSetFacing(facing); return true;
            }
            const auto found = std::find_if(mapSession_->world()->enemies().begin(),
                mapSession_->world()->enemies().end(), [&](const auto& value) {
                    return value.persistentId == binding.instanceId;
                });
            if (found == mapSession_->world()->enemies().end()) return false;
            found->instance.sceneSetFacing(facing); return true;
        },
        [&](const simulation::DefinitionId& id, std::string& error) {
            return dialogue_ && !dialogue_->isOpen() && dialogue_->begin(id, error);
        },
        [&]() { return dialogue_ && dialogue_->isOpen(); },
        [&]() { closeDialogue(); },
        [&](const simulation::DefinitionId& id, simulation::EventBuffer& events) {
            if (mapSession_) events.emit(simulation::PresentationEffectRequested{
                mapSession_->world()->id(), id});
        },
        [&](const maps::WorldAction& action, simulation::EventBuffer& events) {
            if (!mapSession_ || !mapSession_->world()) return false;
            return gameplay::executeWorldAction(action, mapSession_->world()->id(), dialogueFlags_,
                                                events, worldLogicRuntime());
        }};
}

bool GameSession::startScene(const simulation::DefinitionId& sceneId) {
    if (!mapSession_ || !mapSession_->data() || sceneController_.active()) return false;
    const auto found = std::find_if(mapSession_->data()->scenes.begin(),
        mapSession_->data()->scenes.end(), [&](const auto& scene) { return scene.id == sceneId; });
    if (found == mapSession_->data()->scenes.end()) return false;
    std::string error;
    const bool started = sceneController_.start(mapSession_->world()->id(), *found,
                                                sceneRuntimeHooks(), events_, error);
    if (started) {
        clearCombatTransients();
        inventoryOverlay_.close();
        bankOverlay_.close();
        shopOverlay_.close();
    }
    return started;
}

bool GameSession::requestScene(const simulation::DefinitionId& sceneId) {
    if (!mapSession_ || !mapSession_->data() || sceneController_.active() ||
        pendingSceneId_ || sceneId.empty()) {
        return false;
    }
    const auto found = std::find_if(mapSession_->data()->scenes.begin(),
        mapSession_->data()->scenes.end(),
        [&](const auto& scene) { return scene.id == sceneId; });
    if (found == mapSession_->data()->scenes.end()) return false;
    pendingSceneId_ = sceneId;
    return true;
}

bool GameSession::startPendingScene() {
    if (!pendingSceneId_) return false;
    const auto sceneId = *pendingSceneId_;
    pendingSceneId_.reset();
    return startScene(sceneId);
}

bool GameSession::restoreNarrativeState(
    const gameplay::dialogue::DialogueFlagSet& flags,
    std::span<const gameplay::quests::QuestProgress> progress,
    std::string& error) {
    if (!dialogueCatalog_ || !questCatalog_ || !dialogue_ || !questSystem_) {
        error = "GameSession narrative is not configured";
        return false;
    }
    gameplay::dialogue::DialogueFlagSet restoredFlags;
    if (!restoredFlags.restore(flags.values())) {
        error = "invalid dialogue flags";
        return false;
    }
    gameplay::quests::QuestStateStore restoredQuests;
    if (!restoredQuests.restore(progress, *questCatalog_)) {
        error = "invalid quest state";
        return false;
    }
    dialogueFlags_ = std::move(restoredFlags);
    questState_ = std::move(restoredQuests);
    dialogue_->close();
    error.clear();
    return true;
}

void GameSession::clearCombatTransients() noexcept {
    if (projectiles_) { projectiles_->clear(combat_); }
    combat_.clearTransientRecords();
    activeSword_.enabled = false;
    playerAttack_.reset();
    player_.finishAttack();
    if (mapSession_ && mapSession_->world()) {
        for (auto& persistent : mapSession_->world()->enemies()) {
            if (persistent.instance.activeAttack()) {
                combat_.finishAttack(persistent.instance.activeAttack()->key);
                persistent.instance.activeAttack().reset();
            }
        }
    }
}

void GameSession::startPlayerAttack() {
    const auto& definition = player_.actionState() == gameplay::PlayerActionState::swordAttack
                                  ? *swordDefinition_ : *bowDefinition_;
    playerAttack_ = {&definition, {player_.entityHandle(), player_.attackInstance()},
                     player_.facing()};
}

void GameSession::advancePlayerAttack() {
    if (!playerAttack_) { return; }
    std::vector<gameplay::AttackTimelineEvent> eventsAtTick;
    playerAttack_->advance(eventsAtTick);
    for (const auto& event : eventsAtTick) {
        if (event.kind == gameplay::AttackTimelineEventKind::activateHitbox) {
            const auto direction = gameplay::directionVector(playerAttack_->lockedFacing);
            const auto damage = effectivePlayerDamage(playerAttack_->definition->damage);
            activeSword_ = {
                playerAttack_->definition->meleeHitboxes->forFacing(
                    playerAttack_->lockedFacing).at(player_.feetPosition()),
                playerAttack_->key, gameplay::Faction::player,
                damage,
                direction.x * damage.knockbackPixels,
                direction.y * damage.knockbackPixels, true};
        } else if (event.kind == gameplay::AttackTimelineEventKind::deactivateHitbox) {
            activeSword_.enabled = false;
        } else if (event.kind == gameplay::AttackTimelineEventKind::spawnProjectile) {
            const auto& definition = projectileCatalog_->require(
                *playerAttack_->definition->projectileDefinitionId);
            const auto offset = definition.spawnOffsets.forFacing(playerAttack_->lockedFacing);
            static_cast<void>(projectiles_->spawn(
                playerAttack_->key, gameplay::Faction::player, definition.id,
                gameplay::addOffset(player_.feetPosition(), offset),
                playerAttack_->lockedFacing,
                effectivePlayerDamage(playerAttack_->definition->damage)));
        }
    }
    if (playerAttack_->finished) {
        combat_.finishAttack(playerAttack_->key);
        activeSword_.enabled = false;
        player_.finishAttack();
        playerAttack_.reset();
    }
}

void GameSession::applyResolution(const gameplay::CombatResolution& resolution) {
    if (!resolution.damaged) { return; }
    if (resolution.target == player_.entityHandle()) {
        activeSword_.enabled = false;
        if (playerAttack_) {
            combat_.finishAttack(playerAttack_->key);
            playerAttack_.reset();
        }
        player_.beginHurt();
        player_.combatant().invulnerabilityTicks = std::max<std::uint32_t>(
            player_.combatant().invulnerabilityTicks, 30U);
        player_.applyDamageKnockback(resolution.requestedKnockbackX,
                                     resolution.requestedKnockbackY,
                                     mapSession_->world()->map().collision(),
                                     mapSession_->world()->map().tileSize());
        return;
    }
    for (auto& enemy : mapSession_->world()->enemies()) {
        if (enemy.instance.handle() != resolution.target) { continue; }
        const auto objectCollisions = mapSession_->world()->objectCollisionBounds();
        enemy.instance.applyKnockback(resolution.requestedKnockbackX,
                                      resolution.requestedKnockbackY,
                                      mapSession_->world()->map().collision(),
                                      mapSession_->world()->map().tileSize(), objectCollisions);
        break;
    }
}

void GameSession::resolvePlayerSword() {
    if (!activeSword_.enabled) { return; }
    activeSword_.bounds = swordDefinition_->meleeHitboxes->forFacing(player_.facing()).at(
        player_.feetPosition());
    for (auto& enemy : mapSession_->world()->enemies()) {
        applyResolution(combat_.resolve(activeSword_, enemy.instance.combatTarget(), events_));
    }
    for (auto& object : mapSession_->world()->objects()) {
        if (object.instance.combatant()) {
            applyResolution(combat_.resolve(activeSword_, object.instance.combatTarget(), events_));
        }
    }
}

void GameSession::resolveEnemyContacts() {
    constexpr int contactDamage = 1;
    constexpr int contactKnockback = gameplay::Player::damageKnockbackPixels;
    auto playerBody = player_.collisionBody();
    const auto& map = mapSession_->world()->map();
    const auto objectCollisions = mapSession_->world()->objectCollisionBounds();
    for (auto& persistentEnemy : mapSession_->world()->enemies()) {
        auto& enemy = persistentEnemy.instance;
        if (enemy.state() == gameplay::creatures::BehaviorState::dead) { continue; }
        const auto enemyBody = enemy.collisionBody();
        if (!gameplay::overlaps(playerBody, enemyBody)) { continue; }
        const int playerCenterX = playerBody.x + playerBody.width / 2;
        const int playerCenterY = playerBody.y + playerBody.height / 2;
        const int enemyCenterX = enemyBody.x + enemyBody.width / 2;
        const int enemyCenterY = enemyBody.y + enemyBody.height / 2;
        const int distanceX = playerCenterX - enemyCenterX;
        const int distanceY = playerCenterY - enemyCenterY;
        int knockbackX = 0;
        int knockbackY = 0;
        if (distanceX == 0 && distanceY == 0) {
            switch (player_.facing()) {
            case gameplay::FacingDirection::left: knockbackX = contactKnockback; break;
            case gameplay::FacingDirection::right: knockbackX = -contactKnockback; break;
            case gameplay::FacingDirection::up: knockbackY = contactKnockback; break;
            case gameplay::FacingDirection::down: knockbackY = -contactKnockback; break;
            }
        } else if (std::abs(distanceX) >= std::abs(distanceY)) {
            knockbackX = distanceX > 0 ? contactKnockback : -contactKnockback;
        } else {
            knockbackY = distanceY > 0 ? contactKnockback : -contactKnockback;
        }
        const gameplay::Hitbox contact{
            enemyBody, {enemy.handle(), nextContactAttackInstance_++},
            gameplay::Faction::enemy, {contactDamage, contactKnockback},
            knockbackX, knockbackY, true};
        const auto resolution = combat_.resolve(contact, player_.combatTarget(), events_);
        combat_.finishAttack(contact.attack);
        player_.applyDamageKnockback(knockbackX, knockbackY, map.collision(), map.tileSize());
        enemy.applyKnockback(-knockbackX, -knockbackY, map.collision(), map.tileSize(), objectCollisions);
        playerBody = player_.collisionBody();
        if (resolution.damaged) {
            player_.beginHurt();
            player_.combatant().invulnerabilityTicks = std::max<std::uint32_t>(
                player_.combatant().invulnerabilityTicks, 30U);
        }
    }
}

void GameSession::updateEnemies() {
    auto& enemies = mapSession_->world()->enemies();
    const auto& map = mapSession_->world()->map();
    const auto objectCollisions = mapSession_->world()->objectCollisionBounds();
    for (auto& persistent : enemies) {
        auto& enemy = persistent.instance;
        const auto& profile = behaviorCatalog_->require(enemy.definition().behaviorProfileId);
        const std::optional<gameplay::AttackKey> previousAttack = enemy.activeAttack()
            ? std::optional<gameplay::AttackKey>{enemy.activeAttack()->key} : std::nullopt;
        static_cast<void>(enemyBehavior_.update(
            enemy, player_.entityHandle(), player_.feetPosition(),
            !player_.health().depleted(), profile, *attackCatalog_,
            map.collision(), map.tileSize(), objectCollisions));
        if (previousAttack && !enemy.activeAttack()) { combat_.finishAttack(*previousAttack); }
        if (!enemy.activeAttack()) { continue; }
        auto& active = *enemy.activeAttack();
        std::vector<gameplay::AttackTimelineEvent> eventsAtTick;
        active.advance(eventsAtTick);
        for (const auto& event : eventsAtTick) {
            if (event.kind != gameplay::AttackTimelineEventKind::spawnProjectile) { continue; }
            const auto& definition = projectileCatalog_->require(
                *active.definition->projectileDefinitionId);
            const auto offset = definition.spawnOffsets.forFacing(active.lockedFacing);
            static_cast<void>(projectiles_->spawn(
                active.key, enemy.combatant().faction, definition.id,
                gameplay::addOffset(enemy.feetPosition(), offset),
                active.lockedFacing, active.definition->damage));
        }
        if (active.meleeHitboxActive) {
            const auto direction = gameplay::directionVector(active.lockedFacing);
            const gameplay::Hitbox hitbox{
                active.definition->meleeHitboxes->forFacing(active.lockedFacing)
                    .at(enemy.feetPosition()), active.key, enemy.combatant().faction,
                active.definition->damage,
                direction.x * active.definition->damage.knockbackPixels,
                direction.y * active.definition->damage.knockbackPixels, true};
            applyResolution(combat_.resolve(hitbox, player_.combatTarget(), events_));
        }
        if (active.finished) {
            combat_.finishAttack(active.key);
            enemyBehavior_.finishAttack(enemy, profile);
        }
    }
}

std::vector<gameplay::CombatTargetRef> GameSession::combatTargets() {
    std::vector<gameplay::CombatTargetRef> targets;
    targets.reserve(mapSession_->world()->enemies().size() +
                    mapSession_->world()->objects().size() + 1);
    targets.push_back(player_.combatTarget());
    for (auto& enemy : mapSession_->world()->enemies()) {
        targets.push_back(enemy.instance.combatTarget());
    }
    for (auto& object : mapSession_->world()->objects()) {
        if (object.instance.combatant()) { targets.push_back(object.instance.combatTarget()); }
    }
    return targets;
}

void GameSession::collectNearbyPickups() {
    if (!playerItems_) { return; }
    bool changed = false;
    const auto collectorArea = player_.collisionBody();
    auto& pickups = mapSession_->world()->pickups();
    for (std::size_t index = 0; index < pickups.size();) {
        const auto result = gameplay::collectPickup(
            pickups[index].instance, player_.entityHandle(), collectorArea, player_.health(),
            playerItems_->inventory().items(), playerItems_->wallet(), handles_, events_);
        if (result.fullyConsumed) {
            pickups.erase(pickups.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
        changed = changed || result.collected;
    }
    if (changed) { captureWorldState(); }
}

void GameSession::resolveObjectDestructionReward(const maps::PersistentObject& persistent) {
    if (rewardCatalog_ == nullptr || pickupDefinitions_ == nullptr ||
        mapSession_ == nullptr || mapSession_->world() == nullptr) {
        return;
    }
    const auto& destructible = persistent.instance.definition().destructible;
    if (!destructible || !destructible->rewardProfileId) return;
    const auto* profile = rewardCatalog_->find(*destructible->rewardProfileId);
    if (profile == nullptr) return;

    const auto resolution = rewardResolver_.resolve(
        *profile, {mapSession_->world()->id(), persistent.persistentId});
    const auto gain = progression_.grantExperience(resolution.experience);
    if (gain.granted != 0) {
        events_.emit(simulation::ExperienceGranted{
            player_.entityHandle(), persistent.instance.definition().id, gain.granted,
            progression_.totalExperience(), gain.previousLevel, gain.newLevel});
    }
    for (const auto& drop : resolution.loot) {
        const auto definition = std::find_if(
            pickupDefinitions_->begin(), pickupDefinitions_->end(),
            [&](const auto& value) { return value.id == drop.pickupDefinitionId; });
        if (definition == pickupDefinitions_->end()) continue;
        for (std::uint32_t count = 0; count < drop.count; ++count) {
            mapSession_->world()->pickups().push_back({
                {}, true, gameplay::WorldPickup{
                    handles_.create(), *definition, persistent.instance.position()}});
        }
    }
}

void GameSession::updateObjects() {
    auto& objects = mapSession_->world()->objects();
    bool changed = false;
    for (std::size_t index = 0; index < objects.size();) {
        auto& object = objects[index].instance;
        object.advanceDamageTick();
        static_cast<void>(object.syncDamageState());
        const bool beganDestruction = object.syncDestructionState();
        if (beganDestruction) resolveObjectDestructionReward(objects[index]);
        if (auto* combatant = object.combatant()) {
            gameplay::tickInvulnerability(*combatant);
        }
        object.advanceDestructionTick();
        if (object.destructionComplete()) {
            if (object.definition().destructible &&
                object.definition().destructible->leaveDestroyedResidue) {
                mapSession_->world()->addDestroyedObjectResidue(
                    objects[index].persistentId, object.definition().visualSetId,
                    object.position());
            }
            static_cast<void>(object.completeDestruction(handles_));
            objects.erase(objects.begin() + static_cast<std::ptrdiff_t>(index));
            changed = true;
            continue;
        }
        ++index;
    }
    if (changed) { captureWorldState(); }
}

void GameSession::interactWithWorld() {
    if (!playerItems_) { return; }
    const auto playerFeet = player_.feetPosition();
    const auto playerArea = player_.interactionArea().bounds;
    const auto npcInteraction = gameplay::npcs::interactNearest(
        playerFeet, playerArea, mapSession_->world()->npcs());
    if (npcInteraction.npc) {
        const auto found = std::find_if(
            mapSession_->world()->npcs().begin(), mapSession_->world()->npcs().end(),
            [&](const auto& persistent) {
                return persistent.instance.handle() == npcInteraction.npc;
            });
        if (found != mapSession_->world()->npcs().end()) {
            events_.emit(simulation::NpcTalked{
                player_.entityHandle(), npcInteraction.npc,
                found->instance.definition().id});
            if (dialogue_) {
                std::string error;
                static_cast<void>(dialogue_->begin(
                    found->instance.definition().defaultDialogueId, error));
            }
        }
        return;
    }
    maps::PersistentObject* selected = nullptr;
    std::int64_t selectedDistance = 0;
    for (auto& persistent : mapSession_->world()->objects()) {
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
    bool opened = false;
    if (object.definition().bankAccess) {
        inventoryOverlay_.close();
        bankOverlay_.toggle();
        return;
    }
    if (object.definition().activation &&
        object.definition().activation->mode == gameplay::ObjectActivationMode::interactToggle) {
        if (!mapSession_->world()->toggleObjectActivation(selected->persistentId)) { return; }
        events_.emit(simulation::ObjectActivationChanged{
            mapSession_->world()->id(), selected->persistentId,
            object.activationActive()});
        captureWorldState();
        return;
    }
    if (object.isDoor()) {
        if (!mapSession_->world()->interactDoor(selected->persistentId)) { return; }
        // Doors have their own state transition and must not masquerade as
        // container/object-open events for quests or world rules.
    } else if (!object.open()) {
        return;
    } else {
        opened = true;
    }
    if (auto* contents = object.contents()) {
        for (std::size_t index = 0; index < contents->capacity(); ++index) {
            const auto slot = contents->slot(index);
            if (slot) {
                static_cast<void>(contents->transferTo(
                    playerItems_->inventory().items(), slot->itemId, slot->quantity));
            }
        }
    }
    if (opened) {
        events_.emit(simulation::ObjectOpened{
            player_.entityHandle(), object.handle(), object.definition().id,
            selected->persistentId, mapSession_->world()->id()});
    }
    captureWorldState();
}

bool GameSession::handleDialogueCommand(const simulation::PlayerCommand& command) {
    if (!dialogue_ || !dialogue_->isOpen()) { return false; }
    const bool sceneOwnsDialogue = sceneController_.active();
    static_cast<void>(dialogue_->handleCommand(command));
    applyDialogueActions();
    if (sceneController_.active() && !dialogue_->isOpen()) {
        sceneController_.notifyDialogueCompleted();
    }
    // A scene dialogue only blocks the timeline when its clip explicitly asks
    // to wait. Non-blocking dialogue still consumes the input command, but the
    // scene must be allowed to advance on this same fixed tick.
    return !sceneOwnsDialogue || sceneController_.waitingForDialogue();
}

void GameSession::applyDialogueActions() {
    if (!dialogue_) { return; }
    const auto actions = dialogue_->takeActions();
    for (const auto& action : actions) {
        if (action.kind == gameplay::dialogue::DialogueActionKind::setFlag) {
            static_cast<void>(dialogueFlags_.set(action.targetId));
        } else if (action.kind == gameplay::dialogue::DialogueActionKind::clearFlag) {
            static_cast<void>(dialogueFlags_.clear(action.targetId));
        } else if (action.kind == gameplay::dialogue::DialogueActionKind::startQuest && questSystem_) {
            static_cast<void>(questSystem_->start(action.targetId));
        } else if (action.kind == gameplay::dialogue::DialogueActionKind::openShop && shopCatalog_) {
            if (const auto* shop = shopCatalog_->find(action.targetId)) {
                closeDialogue(); inventoryOverlay_.close(); bankOverlay_.close(); shopOverlay_.open(*shop);
            }
        }
    }
}

void GameSession::consumeQuestEvents() {
    if (questSystem_) { questSystem_->consume(events_); }
}

void GameSession::consumeWorldLogic() {
    if (!mapSession_ || !mapSession_->world() || !mapSession_->data()) { return; }
    static_cast<void>(worldLogic_.consume(
        mapSession_->data()->worldRules, mapSession_->world()->id(), dialogueFlags_, events_,
        worldState_.worldRules, worldLogicRuntime(), worldLogicEventCursor_));
    worldLogicEventCursor_ = events_.size();
}

void GameSession::resolvePendingQuestRewards() {
    if (!questCatalog_ || !rewardGrantCatalog_ || !playerItems_) return;
    for (const auto& questId : questState_.pendingRewardQuestIds(*questCatalog_)) {
        const auto& quest = questCatalog_->require(questId);
        if (!quest.rewardGrantId) continue;
        const auto result = rewardGrantService_.grant(rewardGrantCatalog_->require(*quest.rewardGrantId), progression_, *playerItems_);
        if (!result.applied) continue;
        if (result.experience.granted != 0) events_.emit(simulation::ExperienceGranted{
            player_.entityHandle(), quest.id, result.experience.granted,
            progression_.totalExperience(), result.experience.previousLevel,
            result.experience.newLevel});
        static_cast<void>(questState_.markRewardClaimed(quest));
    }
}

void GameSession::resolveEncounterRewards() {
    if (!rewardGrantCatalog_ || !playerItems_ || !mapSession_ || !mapSession_->world()) return;
    // Granting experience can append an ExperienceGranted event.  Consume a
    // stable snapshot so appending to EventBuffer cannot invalidate the range
    // being traversed.
    const auto eventSnapshot = events_.events();
    for (const auto& event : eventSnapshot) {
        const auto* completed = std::get_if<simulation::EncounterCompleted>(&event);
        if (!completed || completed->mapId != mapSession_->world()->id()) continue;
        const auto definition = std::find_if(mapSession_->data()->encounters.begin(),
            mapSession_->data()->encounters.end(), [&](const auto& value) {
                return value.id == completed->encounterId;
            });
        if (definition == mapSession_->data()->encounters.end() || !definition->rewardGrantId) continue;
        auto state = std::find_if(worldState_.encounters.begin(), worldState_.encounters.end(),
            [&](const auto& value) {
                return value.mapId == completed->mapId &&
                       value.encounterId == completed->encounterId;
            });
        if (state == worldState_.encounters.end() || state->rewardClaimed) continue;
        const auto* grant = rewardGrantCatalog_->find(*definition->rewardGrantId);
        if (!grant) continue;
        const auto result = rewardGrantService_.grant(*grant, progression_, *playerItems_);
        if (!result.applied) continue;
        state->rewardClaimed = true;
        if (result.experience.granted != 0) {
            events_.emit(simulation::ExperienceGranted{
                player_.entityHandle(), completed->encounterId, result.experience.granted,
                progression_.totalExperience(), result.experience.previousLevel,
                result.experience.newLevel});
        }
    }
}

void GameSession::captureWorldState() {
    if (mapSession_ && mapSession_->world() && mapSession_->data()) {
        save::captureWorldState(*mapSession_->data(), *mapSession_->world(), worldState_);
    }
}

bool GameSession::initializeMap(const maps::MapCatalog& maps,
                                const maps::MapValidationCatalogs& catalogs,
                                const maps::RuntimeWorldBuilder& builder,
                                const simulation::MapId& mapId,
                                const simulation::SpawnId& spawnId,
                                std::string& error) {
    auto candidate = std::make_unique<maps::MapSession>(maps, catalogs, builder, handles_,
                                                         worldState_);
    const auto activated = candidate->activate(mapId, spawnId);
    if (!activated.changed) { error = activated.error; return false; }
    pendingSceneId_.reset();
    player_.relocate(activated.spawn.position, activated.spawn.facing);
    mapSession_ = std::move(candidate);
    mapEnteredPending_ = true;
    return true;
}

void GameSession::tick(const simulation::PlayerCommand& command) {
    events_.clear();
    worldLogicEventCursor_ = 0;
    if (mapEnteredPending_ && mapSession_ && mapSession_->world()) {
        events_.emit(simulation::MapEntered{mapSession_->world()->id()});
        mapEnteredPending_ = false;
    }
    gameplay::tickInvulnerability(player_.combatant());
    if (!mapSession_ || !mapSession_->world() || !mapSession_->data()) { return; }
    if (handleDialogueCommand(command)) {
        consumeQuestEvents(); resolvePendingQuestRewards();
        return;
    }
    if (sceneController_.active()) {
        sceneController_.advance(events_);
        consumeWorldLogic();
        consumeQuestEvents();
        resolvePendingQuestRewards();
        return;
    }
    if (shopOverlay_.open()) {
        if (command.actions.toggleInventoryPressed) { shopOverlay_.close(); }
        else if (shopCatalog_ && playerItems_) {
            if (const auto* shop = shopCatalog_->find(shopOverlay_.activeShopId())) {
                static_cast<void>(gameplay::routeShopCommand(shopOverlay_, command, *shop,
                                                              *playerItems_, shopTransactionService_));
            } else { shopOverlay_.close(); }
        }
        resolvePendingQuestRewards();
        return;
    }
    if (bankOverlay_.open()) {
        if (command.actions.toggleInventoryPressed) { bankOverlay_.toggle(); }
        else { static_cast<void>(gameplay::routeBankCommand(bankOverlay_, command, *playerItems_)); }
        resolvePendingQuestRewards();
        return;
    }
    if (playerItems_) {
        const auto inventoryResult = gameplay::routeInventoryCommand(
            inventoryOverlay_, command, *playerItems_, *itemCatalog_, player_.health());
        if (inventoryResult.equipmentChanged) { refreshDerivedPlayerStats(); }
        if (inventoryResult.consumedTick) {
            consumeQuestEvents(); resolvePendingQuestRewards();
            return;
        }
    }
    if (playerItems_ && command.actions.quickSlotPressed >= 0) {
        static_cast<void>(playerItems_->useQuickSlot(
            static_cast<std::size_t>(command.actions.quickSlotPressed), *itemCatalog_,
            player_.health()));
    }
    const auto& map = mapSession_->world()->map();
    const auto objectCollisions = mapSession_->world()->objectCollisionBounds();
    const auto previousAction = player_.actionState();
    player_.update(command, map.collision(), map.tileSize(), objectCollisions);
    regionTracker_.update(mapSession_->world()->id(), mapSession_->data()->regions,
                          player_.feetPosition(), events_);
    mapSession_->world()->updatePressureActivations(player_.feetPosition(), events_);
    // Combat is optional for the small logical map fixtures used by Session
    // tests. A fully bootstrapped game configures it before the first tick.
    if (projectiles_ && attackCatalog_ && projectileCatalog_ && behaviorCatalog_ &&
        swordDefinition_ && bowDefinition_) {
        if (previousAction == gameplay::PlayerActionState::none &&
            (player_.actionState() == gameplay::PlayerActionState::swordAttack ||
             player_.actionState() == gameplay::PlayerActionState::bowAttack)) {
            startPlayerAttack();
        }
        advancePlayerAttack();
        updateEnemies();
        resolvePlayerSword();
        resolveEnemyContacts();
        auto targets = combatTargets();
        std::vector<gameplay::CombatResolution> resolutions;
        projectiles_->update(map.collision(), map.tileSize(), targets, combat_, events_, resolutions,
                             objectCollisions);
        for (const auto& resolution : resolutions) { applyResolution(resolution); }
        resolveDefeatRewards();
        removeDefeatedEnemies();
    }
    // Interaction events are part of the same event-processing cycle as region,
    // encounter and map-entry events, so authored objectOpened rules see them
    // without a one-tick delay.
    if (command.actions.interactPressed) { interactWithWorld(); }
    std::vector<simulation::PersistentInstanceId> aliveParticipants;
    aliveParticipants.reserve(mapSession_->world()->enemies().size());
    for (const auto& enemy : mapSession_->world()->enemies()) {
        aliveParticipants.push_back(enemy.persistentId);
    }
    encounters_.evaluate(mapSession_->data()->encounters, mapSession_->world()->id(),
                         aliveParticipants, worldState_.encounters, events_);
    consumeWorldLogic();
    static_cast<void>(startPendingScene());
    consumeWorldLogic();
    resolveEncounterRewards();
    if (sceneController_.active()) {
        consumeQuestEvents();
        resolvePendingQuestRewards();
        return;
    }
    mapSession_->beginTick();
    static_cast<void>(mapSession_->requestTransition(player_.collisionBody()));
    if (mapSession_->pending()) {
        const auto transition = mapSession_->commitPending();
        if (transition.changed) {
            if (sceneController_.active()) sceneController_.abort(events_, "scene aborted by map transition");
            clearCombatTransients();
            closeDialogue();
            player_.relocate(transition.spawn.position, transition.spawn.facing);
            events_.emit(simulation::MapEntered{mapSession_->world()->id()});
            regionTracker_.update(mapSession_->world()->id(), mapSession_->data()->regions,
                                  player_.feetPosition(), events_);
            // Pressure activation is derived from the newly spawned player position.
            // Evaluate it before the same world-logic pass so a plate at a map
            // entry can drive authored rules without a one-tick delay.
            mapSession_->world()->updatePressureActivations(player_.feetPosition(), events_);
            std::vector<simulation::PersistentInstanceId> newMapAlive;
            newMapAlive.reserve(mapSession_->world()->enemies().size());
            for (const auto& enemy : mapSession_->world()->enemies()) {
                newMapAlive.push_back(enemy.persistentId);
            }
            encounters_.evaluate(mapSession_->data()->encounters, mapSession_->world()->id(),
                                 newMapAlive, worldState_.encounters, events_);
            consumeWorldLogic();
            static_cast<void>(startPendingScene());
            consumeWorldLogic();
            resolveEncounterRewards();
            if (sceneController_.active()) {
                consumeQuestEvents();
                resolvePendingQuestRewards();
                return;
            }
            mapEnteredPending_ = false;
        }
    }
    collectNearbyPickups();
    updateObjects();
    consumeQuestEvents();
    resolvePendingQuestRewards();
}

void GameSession::resolveDefeatRewards() {
    if (rewardCatalog_ == nullptr || pickupDefinitions_ == nullptr || mapSession_ == nullptr || mapSession_->world() == nullptr) {
        return;
    }
    std::vector<simulation::EntityDefeated> defeatEvents;
    for (const auto& event : events_.events()) {
        if (const auto* defeated = std::get_if<simulation::EntityDefeated>(&event)) {
            defeatEvents.push_back(*defeated);
        }
    }
    std::vector<simulation::EntityHandle> resolved;
    for (const auto& defeated : defeatEvents) {
        if (defeated.attacker != player_.entityHandle()) { continue; }
        if (std::find(resolved.begin(), resolved.end(), defeated.target) != resolved.end()) { continue; }
        const auto enemy = std::find_if(mapSession_->world()->enemies().begin(),
            mapSession_->world()->enemies().end(), [&](const auto& value) {
                return value.instance.handle() == defeated.target;
            });
        if (enemy == mapSession_->world()->enemies().end() ||
            !enemy->instance.definition().rewardProfileId) { continue; }
        const auto* profile = rewardCatalog_->find(*enemy->instance.definition().rewardProfileId);
        if (profile == nullptr) { continue; }
        const auto resolution = rewardResolver_.resolve(
            *profile, {mapSession_->world()->id(), enemy->persistentId});
        const auto gain = progression_.grantExperience(resolution.experience);
        if (gain.granted != 0) {
            events_.emit(simulation::ExperienceGranted{
                player_.entityHandle(), enemy->instance.definition().id, gain.granted,
                progression_.totalExperience(), gain.previousLevel, gain.newLevel});
        }
        for (const auto& drop : resolution.loot) {
            const auto definition = std::find_if(pickupDefinitions_->begin(), pickupDefinitions_->end(),
                [&](const auto& value) { return value.id == drop.pickupDefinitionId; });
            if (definition == pickupDefinitions_->end()) { continue; }
            for (std::uint32_t count = 0; count < drop.count; ++count) {
                mapSession_->world()->pickups().push_back({
                    {}, true, gameplay::WorldPickup{handles_.create(), *definition,
                                                        enemy->instance.feetPosition()}});
            }
        }
        resolved.push_back(defeated.target);
    }
}

void GameSession::removeDefeatedEnemies() {
    auto& enemies = mapSession_->world()->enemies();
    for (std::size_t index = 0; index < enemies.size();) {
        if (enemies[index].instance.state() != gameplay::creatures::BehaviorState::dead) {
            ++index;
            continue;
        }
        static_cast<void>(handles_.destroy(enemies[index].instance.handle()));
        enemies.erase(enemies.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

const maps::RuntimeWorld& GameSession::world() const noexcept { return *mapSession_->world(); }

const maps::MapData& GameSession::mapData() const {
    if (!mapSession_ || !mapSession_->data()) {
        throw std::logic_error("GameSession has no active map data");
    }
    return *mapSession_->data();
}

bool GameSession::restoreMap(const simulation::MapId& mapId,
                             const save::SessionWorldState& state, std::string& error) {
    if (!mapSession_) { error = "GameSession has no map session"; return false; }
    const auto restored = mapSession_->restore(mapId, state);
    if (!restored.changed) { error = restored.error; return false; }
    pendingSceneId_.reset();
    clearCombatTransients();
    closeDialogue();
    player_.relocate(restored.spawn.position, restored.spawn.facing);
    mapEnteredPending_ = true;
    return true;
}

} // namespace underworld::game
