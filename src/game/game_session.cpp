#include "game/game_session.h"

#include "game/save/save_data.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace underworld::game {

GameSession::GameSession(simulation::PlayerId playerId,
                         const gameplay::rpg::PlayerProgressionDefinition& progression,
                         core::WorldPointI initialPosition)
    : player_(playerId, handles_.create(), initialPosition, progression.baseStats.maximumHealth),
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
    return {save::capturePlayer(player_, *playerItems_, mapSession_->world()->id()),
            {progression_.definition().id, progression_.totalExperience()}, worldState_,
            dialogueFlags_, questState_};
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
    const save::SaveData previous = captureSaveData();
    if (!restoreMap(data.player.currentMapId, data.world, error)) { return false; }
    if (!save::applyPlayer(data.player, player_, *playerItems_, *itemCatalog_, error) ||
        !restoreNarrativeState(data.dialogueFlags, data.quests.snapshot(), error) ||
        !progression_.restoreExperience(data.progression.totalExperience)) {
        std::string rollbackError;
        static_cast<void>(restoreMap(previous.player.currentMapId, previous.world,
                                     rollbackError));
        static_cast<void>(save::applyPlayer(previous.player, player_, *playerItems_,
                                            *itemCatalog_, rollbackError));
        static_cast<void>(restoreNarrativeState(previous.dialogueFlags,
                                                previous.quests.snapshot(), rollbackError));
        static_cast<void>(progression_.restoreExperience(previous.progression.totalExperience));
        return false;
    }
    clearCombatTransients();
    closeDialogue();
    error.clear();
    return true;
}

void GameSession::closeDialogue() noexcept {
    if (dialogue_) { dialogue_->close(); }
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
            activeSword_ = {
                playerAttack_->definition->meleeHitboxes->forFacing(
                    playerAttack_->lockedFacing).at(player_.feetPosition()),
                playerAttack_->key, gameplay::Faction::player,
                playerAttack_->definition->damage,
                direction.x * playerAttack_->definition->damage.knockbackPixels,
                direction.y * playerAttack_->definition->damage.knockbackPixels, true};
        } else if (event.kind == gameplay::AttackTimelineEventKind::deactivateHitbox) {
            activeSword_.enabled = false;
        } else if (event.kind == gameplay::AttackTimelineEventKind::spawnProjectile) {
            const auto& definition = projectileCatalog_->require(
                *playerAttack_->definition->projectileDefinitionId);
            const auto offset = definition.spawnOffsets.forFacing(playerAttack_->lockedFacing);
            static_cast<void>(projectiles_->spawn(
                playerAttack_->key, gameplay::Faction::player, definition.id,
                gameplay::addOffset(player_.feetPosition(), offset),
                playerAttack_->lockedFacing, playerAttack_->definition->damage));
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
        enemy.instance.applyKnockback(resolution.requestedKnockbackX,
                                      resolution.requestedKnockbackY,
                                      mapSession_->world()->map().collision(),
                                      mapSession_->world()->map().tileSize());
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
        enemy.applyKnockback(-knockbackX, -knockbackY, map.collision(), map.tileSize());
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
    for (auto& persistent : enemies) {
        auto& enemy = persistent.instance;
        const auto& profile = behaviorCatalog_->require(enemy.definition().behaviorProfileId);
        const std::optional<gameplay::AttackKey> previousAttack = enemy.activeAttack()
            ? std::optional<gameplay::AttackKey>{enemy.activeAttack()->key} : std::nullopt;
        static_cast<void>(enemyBehavior_.update(
            enemy, player_.entityHandle(), player_.feetPosition(),
            !player_.health().depleted(), profile, *attackCatalog_,
            map.collision(), map.tileSize()));
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

void GameSession::updateObjects() {
    auto& objects = mapSession_->world()->objects();
    bool changed = false;
    for (std::size_t index = 0; index < objects.size();) {
        auto& object = objects[index].instance;
        static_cast<void>(object.syncDestructionState());
        if (auto* combatant = object.combatant()) {
            gameplay::tickInvulnerability(*combatant);
        }
        object.advanceDestructionTick();
        if (object.destructionComplete()) {
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
    if (!object.open()) { return; }
    if (auto* contents = object.contents()) {
        for (std::size_t index = 0; index < contents->capacity(); ++index) {
            const auto slot = contents->slot(index);
            if (slot) {
                static_cast<void>(contents->transferTo(
                    playerItems_->inventory().items(), slot->itemId, slot->quantity));
            }
        }
    }
    events_.emit(simulation::ObjectOpened{
        player_.entityHandle(), object.handle(), object.definition().id});
    captureWorldState();
}

bool GameSession::handleDialogueCommand(const simulation::PlayerCommand& command) {
    if (!dialogue_ || !dialogue_->isOpen()) { return false; }
    static_cast<void>(dialogue_->handleCommand(command));
    applyDialogueActions();
    return true;
}

void GameSession::applyDialogueActions() {
    if (!dialogue_) { return; }
    for (const auto& action : dialogue_->takeActions()) {
        if (action.kind == gameplay::dialogue::DialogueActionKind::setFlag) {
            static_cast<void>(dialogueFlags_.set(action.targetId));
        } else if (action.kind == gameplay::dialogue::DialogueActionKind::clearFlag) {
            static_cast<void>(dialogueFlags_.clear(action.targetId));
        } else if (questSystem_) {
            static_cast<void>(questSystem_->start(action.targetId));
        }
    }
}

void GameSession::consumeQuestEvents() {
    if (questSystem_) { questSystem_->consume(events_); }
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
    player_.relocate(activated.spawn.position, activated.spawn.facing);
    mapSession_ = std::move(candidate);
    return true;
}

void GameSession::tick(const simulation::PlayerCommand& command) {
    events_.clear();
    gameplay::tickInvulnerability(player_.combatant());
    if (!mapSession_ || !mapSession_->world() || !mapSession_->data()) { return; }
    if (handleDialogueCommand(command)) {
        consumeQuestEvents();
        return;
    }
    if (playerItems_ && gameplay::routeInventoryCommand(
            inventoryOverlay_, command, *playerItems_, *itemCatalog_, player_.health())) {
        consumeQuestEvents();
        return;
    }
    if (playerItems_ && command.actions.quickSlotPressed >= 0) {
        static_cast<void>(playerItems_->useQuickSlot(
            static_cast<std::size_t>(command.actions.quickSlotPressed), *itemCatalog_,
            player_.health()));
    }
    const auto& map = mapSession_->world()->map();
    const auto previousAction = player_.actionState();
    player_.update(command, map.collision(), map.tileSize());
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
        projectiles_->update(map.collision(), map.tileSize(), targets, combat_, events_, resolutions);
        for (const auto& resolution : resolutions) { applyResolution(resolution); }
        resolveDefeatRewards();
        removeDefeatedEnemies();
    }
    mapSession_->beginTick();
    static_cast<void>(mapSession_->requestTransition(player_.collisionBody()));
    if (mapSession_->pending()) {
        const auto transition = mapSession_->commitPending();
        if (transition.changed) {
            clearCombatTransients();
            closeDialogue();
            player_.relocate(transition.spawn.position, transition.spawn.facing);
            events_.emit(simulation::MapEntered{mapSession_->world()->id()});
        }
    }
    if (command.actions.interactPressed) { interactWithWorld(); }
    collectNearbyPickups();
    updateObjects();
    consumeQuestEvents();
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
    clearCombatTransients();
    closeDialogue();
    player_.relocate(restored.spawn.position, restored.spawn.facing);
    return true;
}

} // namespace underworld::game
