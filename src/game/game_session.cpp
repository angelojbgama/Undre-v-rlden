#include "game/game_session.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace underworld::game {

GameSession::GameSession(simulation::EntityHandlePool& handles, simulation::PlayerId playerId,
                         core::WorldPointI initialPosition)
    : handles_(handles), player_(playerId, handles.create(), initialPosition) {}

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

bool GameSession::initializeMap(const maps::MapCatalog& maps,
                                const maps::MapValidationCatalogs& catalogs,
                                const maps::RuntimeWorldBuilder& builder,
                                simulation::EntityHandlePool& handles,
                                const simulation::MapId& mapId,
                                const simulation::SpawnId& spawnId,
                                std::string& error) {
    auto candidate = std::make_unique<maps::MapSession>(maps, catalogs, builder, handles,
                                                         worldState_);
    const auto activated = candidate->activate(mapId, spawnId);
    if (!activated.changed) { error = activated.error; return false; }
    player_.relocate(activated.spawn.position, activated.spawn.facing);
    mapSession_ = std::move(candidate);
    return true;
}

void GameSession::tick(const simulation::PlayerCommand& command) {
    events_.clear();
    if (!mapSession_ || !mapSession_->world() || !mapSession_->data()) { return; }
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
        removeDefeatedEnemies();
    }
    mapSession_->beginTick();
    static_cast<void>(mapSession_->requestTransition(player_.collisionBody()));
    if (!mapSession_->pending()) { return; }
    const auto transition = mapSession_->commitPending();
    if (transition.changed) {
        player_.relocate(transition.spawn.position, transition.spawn.facing);
        events_.emit(simulation::MapEntered{mapSession_->world()->id()});
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

maps::RuntimeWorld& GameSession::worldForRuntime() noexcept { return *mapSession_->world(); }

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
    player_.relocate(restored.spawn.position, restored.spawn.facing);
    return true;
}

} // namespace underworld::game
