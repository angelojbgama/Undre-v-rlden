#include "game/gameplay/creatures/creature_engine.h"

#include "engine/core/coordinates.h"
#include "engine/world/collision_grid.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay::creatures {
namespace {

const simulation::DefinitionId soldierBehavior{"behavior.soldier.melee"};
const simulation::DefinitionId skullBehavior{"behavior.skull.ranged"};
const simulation::DefinitionId soldierEnemy{"enemy.evil_soldier"};
const simulation::DefinitionId skullEnemy{"enemy.skull"};
const simulation::DefinitionId soldierVisual{"visual.enemy.evil_soldier"};
const simulation::DefinitionId skullVisual{"visual.enemy.skull"};
const simulation::DefinitionId soldierSword{"attack.soldier.sword"};
const simulation::DefinitionId skullArrowAttack{"attack.skull.arrow"};
const simulation::DefinitionId skullArrowProjectile{"projectile.skull.arrow"};

std::int64_t squaredDistance(core::WorldPointI left, core::WorldPointI right) noexcept {
    const std::int64_t dx = static_cast<std::int64_t>(right.x) - left.x;
    const std::int64_t dy = static_cast<std::int64_t>(right.y) - left.y;
    return dx * dx + dy * dy;
}

bool withinRange(core::WorldPointI left, core::WorldPointI right, int range) noexcept {
    return squaredDistance(left, right) <= static_cast<std::int64_t>(range) * range;
}

int pixelCoordinate(std::int64_t value) {
    const std::int64_t pixels = core::floorDiv(
        value, static_cast<int>(EnemyInstance::subpixelsPerPixel));
    if (pixels < std::numeric_limits<int>::min() ||
        pixels > std::numeric_limits<int>::max()) {
        throw std::overflow_error("enemy position is outside integer world coordinates");
    }
    return static_cast<int>(pixels);
}

std::int64_t subpixelCoordinate(int value) noexcept {
    return static_cast<std::int64_t>(value) * EnemyInstance::subpixelsPerPixel;
}

void setFacingFromIntent(FacingDirection& facing, int x, int y) noexcept {
    if (y < 0) { facing = FacingDirection::up; }
    else if (y > 0) { facing = FacingDirection::down; }
    else if (x < 0) { facing = FacingDirection::left; }
    else if (x > 0) { facing = FacingDirection::right; }
}

FacingDirection facingToward(core::WorldPointI from, core::WorldPointI to) noexcept {
    const int dx = (to.x > from.x ? 1 : 0) - (to.x < from.x ? 1 : 0);
    const int dy = (to.y > from.y ? 1 : 0) - (to.y < from.y ? 1 : 0);
    FacingDirection result = FacingDirection::down;
    setFacingFromIntent(result, dx, dy);
    return result;
}

void validate(const BehaviorProfile& profile) {
    if (profile.id.empty() || profile.detectionRangePixels <= 0 ||
        profile.disengageRangePixels <= profile.detectionRangePixels ||
        profile.idleDurationTicks == 0 || profile.wanderDurationTicks == 0) {
        throw std::invalid_argument("behavior profile is incomplete or invalid");
    }
}

void validate(const EnemyDefinition& definition) {
    if (definition.id.empty() || definition.visualSetId.empty() ||
        definition.behaviorProfileId.empty() || definition.faction != Faction::enemy ||
        definition.maximumHealth <= 0 || definition.movementSpeedSubpixelsPerTick <= 0 ||
        !definition.collisionBody.valid() || !definition.hurtbox.valid() ||
        definition.attackIds.empty()) {
        throw std::invalid_argument("enemy definition is incomplete or invalid");
    }
}

template <typename Map, typename Definition>
void addUnique(Map& map, Definition definition, const char* duplicateMessage) {
    validate(definition);
    const auto [position, inserted] = map.emplace(definition.id, std::move(definition));
    static_cast<void>(position);
    if (!inserted) { throw std::logic_error(duplicateMessage); }
}

} // namespace

void BehaviorCatalog::add(BehaviorProfile profile) {
    addUnique(profiles_, std::move(profile), "duplicate behavior profile id");
}

const BehaviorProfile* BehaviorCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto found = profiles_.find(id);
    return found == profiles_.end() ? nullptr : &found->second;
}

const BehaviorProfile& BehaviorCatalog::require(const simulation::DefinitionId& id) const {
    const BehaviorProfile* profile = find(id);
    if (profile == nullptr) { throw std::out_of_range("behavior profile was not found"); }
    return *profile;
}

void EnemyCatalog::add(EnemyDefinition definition) {
    addUnique(definitions_, std::move(definition), "duplicate enemy definition id");
}

const EnemyDefinition* EnemyCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}

const EnemyDefinition& EnemyCatalog::require(const simulation::DefinitionId& id) const {
    const EnemyDefinition* definition = find(id);
    if (definition == nullptr) { throw std::out_of_range("enemy definition was not found"); }
    return *definition;
}

EnemyInstance::EnemyInstance(simulation::EntityHandle handle,
                             const EnemyDefinition& definition,
                             core::WorldPointI feet, FacingDirection facing,
                             const BehaviorProfile& profile)
    : definition_(&definition),
      combatant_{handle, definition.faction, Health{definition.maximumHealth}, 0, false},
      positionX_(subpixelCoordinate(feet.x)), positionY_(subpixelCoordinate(feet.y)),
      facing_(facing), stateTimer_(profile.idleDurationTicks) {
    cooldowns_.reserve(definition.attackIds.size());
    for (const simulation::DefinitionId& attackId : definition.attackIds) {
        cooldowns_.push_back({attackId, 0});
    }
}

core::WorldPointI EnemyInstance::feetPosition() const {
    return {pixelCoordinate(positionX_), pixelCoordinate(positionY_)};
}

world::AabbI EnemyInstance::collisionBody() const {
    return definition_->collisionBody.at(feetPosition());
}

Hurtbox EnemyInstance::hurtbox() const noexcept {
    return {definition_->hurtbox.at(feetPosition()),
            state_ != BehaviorState::dead && !combatant_.health.depleted()};
}

CombatTargetRef EnemyInstance::combatTarget() noexcept {
    return {combatant_, hurtbox()};
}

AttackCooldown* EnemyInstance::cooldownFor(
    const simulation::DefinitionId& attackId) noexcept {
    const auto found = std::find_if(cooldowns_.begin(), cooldowns_.end(),
        [&](const AttackCooldown& cooldown) { return cooldown.attackId == attackId; });
    return found == cooldowns_.end() ? nullptr : &*found;
}

const AttackCooldown* EnemyInstance::cooldownFor(
    const simulation::DefinitionId& attackId) const noexcept {
    const auto found = std::find_if(cooldowns_.begin(), cooldowns_.end(),
        [&](const AttackCooldown& cooldown) { return cooldown.attackId == attackId; });
    return found == cooldowns_.end() ? nullptr : &*found;
}

void EnemyInstance::move(int intentX, int intentY,
                         const world::CollisionGrid& collision, int tileSize) {
    if (state_ == BehaviorState::attack || state_ == BehaviorState::dead) { return; }
    intentX = std::clamp(intentX, -1, 1);
    intentY = std::clamp(intentY, -1, 1);
    setFacingFromIntent(facing_, intentX, intentY);
    std::int64_t speed = definition_->movementSpeedSubpixelsPerTick;
    if (intentX != 0 && intentY != 0) {
        speed = (speed * diagonalScaleNumerator + diagonalScaleDenominator / 2) /
                diagonalScaleDenominator;
    }
    const std::int64_t targetX = positionX_ + speed * intentX;
    const std::int64_t targetY = positionY_ + speed * intentY;
    const core::WorldPointI oldFeet = feetPosition();
    const core::WorldPointI desired{pixelCoordinate(targetX), pixelCoordinate(targetY)};
    world::AabbI body = collisionBody();
    const world::MovementResult movement = world::moveAgainstSolidTiles(
        collision, body, desired.x - oldFeet.x, desired.y - oldFeet.y, tileSize);
    const core::WorldPointI resolved{
        body.x - definition_->collisionBody.offsetX,
        body.y - definition_->collisionBody.offsetY};
    positionX_ = movement.blockedX ? subpixelCoordinate(resolved.x) : targetX;
    positionY_ = movement.blockedY ? subpixelCoordinate(resolved.y) : targetY;
}

void EnemyInstance::applyKnockback(int deltaX, int deltaY,
                                   const world::CollisionGrid& collision, int tileSize) {
    world::AabbI body = collisionBody();
    [[maybe_unused]] const world::MovementResult movement = world::moveAgainstSolidTiles(
        collision, body, deltaX, deltaY, tileSize);
    positionX_ = subpixelCoordinate(body.x - definition_->collisionBody.offsetX);
    positionY_ = subpixelCoordinate(body.y - definition_->collisionBody.offsetY);
}

EnemyFactory::EnemyFactory(simulation::EntityHandlePool& handles,
                           const EnemyCatalog& enemies,
                           const BehaviorCatalog& behaviors,
                           const AttackCatalog& attacks,
                           const ProjectileCatalog& projectiles,
                           std::span<const simulation::DefinitionId> availableVisualSets)
    : handles_(handles), enemies_(enemies), behaviors_(behaviors), attacks_(attacks),
      projectiles_(projectiles),
      availableVisualSets_(availableVisualSets.begin(), availableVisualSets.end()) {}

EnemyInstance EnemyFactory::create(const simulation::DefinitionId& definitionId,
                                   core::WorldPointI feet, FacingDirection facing) const {
    const EnemyDefinition& definition = enemies_.require(definitionId);
    const BehaviorProfile& profile = behaviors_.require(definition.behaviorProfileId);
    if (std::find(availableVisualSets_.begin(), availableVisualSets_.end(),
                  definition.visualSetId) == availableVisualSets_.end()) {
        throw std::invalid_argument("enemy definition references an unavailable visual set");
    }
    for (const simulation::DefinitionId& attackId : definition.attackIds) {
        const AttackDefinition& attack = attacks_.require(attackId);
        if (attack.kind == AttackKind::projectile &&
            (!attack.projectileDefinitionId ||
             projectiles_.find(*attack.projectileDefinitionId) == nullptr)) {
            throw std::invalid_argument("enemy attack references an unavailable projectile");
        }
    }
    return EnemyInstance(handles_.create(), definition, feet, facing, profile);
}

const AttackDefinition* EnemyBehaviorSystem::selectAttack(
    const EnemyInstance& enemy, core::WorldPointI targetFeet,
    const AttackCatalog& attacks) const noexcept {
    const std::int64_t distance = squaredDistance(enemy.feetPosition(), targetFeet);
    for (const simulation::DefinitionId& attackId : enemy.definition().attackIds) {
        const AttackDefinition* attack = attacks.find(attackId);
        const AttackCooldown* cooldown = enemy.cooldownFor(attackId);
        if (attack == nullptr || cooldown == nullptr || cooldown->remainingTicks != 0) {
            continue;
        }
        const std::int64_t minimum = static_cast<std::int64_t>(attack->minimumRangePixels) *
                                     attack->minimumRangePixels;
        const std::int64_t maximum = static_cast<std::int64_t>(attack->maximumRangePixels) *
                                     attack->maximumRangePixels;
        if (distance >= minimum && distance <= maximum) { return attack; }
    }
    return nullptr;
}

BehaviorUpdate EnemyBehaviorSystem::update(
    EnemyInstance& enemy, simulation::EntityHandle playerHandle,
    core::WorldPointI playerFeet, bool playerAlive, const BehaviorProfile& profile,
    const AttackCatalog& attacks, const world::CollisionGrid& collision,
    int tileSize) const {
    BehaviorUpdate result{};
    tickInvulnerability(enemy.combatant_);
    for (AttackCooldown& cooldown : enemy.cooldowns_) {
        if (cooldown.remainingTicks > 0) { --cooldown.remainingTicks; }
    }
    if (enemy.combatant_.health.depleted()) {
        if (enemy.state_ != BehaviorState::dead) {
            enemy.state_ = BehaviorState::dead;
            enemy.target_ = {};
            enemy.activeAttack_.reset();
            enemy.stateTimer_ = 0;
            result.stateChanged = true;
        }
        return result;
    }

    const bool detected = playerAlive && withinRange(
        enemy.feetPosition(), playerFeet, profile.detectionRangePixels);
    const bool disengaged = !playerAlive || !withinRange(
        enemy.feetPosition(), playerFeet, profile.disengageRangePixels);

    if (enemy.state_ == BehaviorState::idle) {
        if (detected) {
            enemy.state_ = BehaviorState::chase;
            enemy.target_ = playerHandle;
            result.stateChanged = true;
        } else if (enemy.stateTimer_ > 1) {
            --enemy.stateTimer_;
        } else {
            enemy.state_ = BehaviorState::wander;
            enemy.stateTimer_ = profile.wanderDurationTicks;
            ++enemy.wanderCycle_;
            result.stateChanged = true;
        }
    } else if (enemy.state_ == BehaviorState::wander) {
        if (detected) {
            enemy.state_ = BehaviorState::chase;
            enemy.target_ = playerHandle;
            result.stateChanged = true;
        } else {
            const std::uint32_t direction =
                (enemy.handle().index + enemy.wanderCycle_) % 4U;
            constexpr int dx[4]{0, 1, 0, -1};
            constexpr int dy[4]{-1, 0, 1, 0};
            enemy.move(dx[direction], dy[direction], collision, tileSize);
            if (enemy.stateTimer_ > 1) {
                --enemy.stateTimer_;
            } else {
                enemy.state_ = BehaviorState::idle;
                enemy.stateTimer_ = profile.idleDurationTicks;
                result.stateChanged = true;
            }
        }
    } else if (enemy.state_ == BehaviorState::chase) {
        if (disengaged) {
            enemy.state_ = BehaviorState::idle;
            enemy.target_ = {};
            enemy.stateTimer_ = profile.idleDurationTicks;
            result.stateChanged = true;
        } else if (const AttackDefinition* attack = selectAttack(enemy, playerFeet, attacks)) {
            enemy.state_ = BehaviorState::attack;
            enemy.facing_ = facingToward(enemy.feetPosition(), playerFeet);
            enemy.activeAttack_ = ActiveAttack{
                attack, {enemy.handle(), enemy.nextAttackInstance_++}, enemy.facing_, false};
            result.stateChanged = true;
            result.attackStarted = true;
        } else {
            const auto feet = enemy.feetPosition();
            const int dx = (playerFeet.x > feet.x ? 1 : 0) - (playerFeet.x < feet.x ? 1 : 0);
            const int dy = (playerFeet.y > feet.y ? 1 : 0) - (playerFeet.y < feet.y ? 1 : 0);
            enemy.move(dx, dy, collision, tileSize);
        }
    }
    return result;
}

void EnemyBehaviorSystem::finishAttack(EnemyInstance& enemy,
                                       const BehaviorProfile& profile) const {
    if (!enemy.activeAttack_) { return; }
    if (AttackCooldown* cooldown = enemy.cooldownFor(
            enemy.activeAttack_->definition->id)) {
        cooldown->remainingTicks = enemy.activeAttack_->definition->cooldownTicks;
    }
    enemy.activeAttack_.reset();
    enemy.state_ = enemy.combatant_.health.depleted()
                       ? BehaviorState::dead : BehaviorState::chase;
    if (enemy.state_ == BehaviorState::dead) {
        enemy.target_ = {};
        enemy.stateTimer_ = 0;
    } else {
        enemy.stateTimer_ = profile.idleDurationTicks;
    }
}

const char* behaviorStateName(BehaviorState state) noexcept {
    switch (state) {
    case BehaviorState::idle: return "IDLE";
    case BehaviorState::wander: return "WANDER";
    case BehaviorState::chase: return "CHASE";
    case BehaviorState::attack: return "ATTACK";
    case BehaviorState::dead: return "DEAD";
    }
    return "UNKNOWN";
}

const simulation::DefinitionId& soldierBehaviorId() { return soldierBehavior; }
const simulation::DefinitionId& skullBehaviorId() { return skullBehavior; }
const simulation::DefinitionId& soldierEnemyId() { return soldierEnemy; }
const simulation::DefinitionId& skullEnemyId() { return skullEnemy; }
const simulation::DefinitionId& soldierVisualId() { return soldierVisual; }
const simulation::DefinitionId& skullVisualId() { return skullVisual; }
const simulation::DefinitionId& soldierSwordAttackId() { return soldierSword; }
const simulation::DefinitionId& skullArrowAttackId() { return skullArrowAttack; }
const simulation::DefinitionId& skullArrowProjectileId() { return skullArrowProjectile; }

BehaviorProfile makeSoldierBehaviorProfile() {
    return {soldierBehavior, 100, 132, 60, 90};
}

BehaviorProfile makeSkullBehaviorProfile() {
    return {skullBehavior, 150, 184, 75, 90};
}

EnemyDefinition makeSoldierEnemyDefinition() {
    return {soldierEnemy, soldierVisual, soldierBehavior, Faction::enemy, 3, 256,
            {-5, -8, 10, 8}, {-7, -22, 14, 22}, {soldierSword}};
}

EnemyDefinition makeSkullEnemyDefinition() {
    return {skullEnemy, skullVisual, skullBehavior, Faction::enemy, 3, 192,
            {-5, -8, 10, 8}, {-7, -22, 14, 22}, {skullArrowAttack}};
}

AttackDefinition makeSoldierSwordAttackDefinition() {
    DirectionalBoxes boxes{{{
        {-10, -1, 20, 18}, {-10, -27, 20, 19},
        {-27, -18, 21, 18}, {6, -18, 21, 18},
    }}};
    return {soldierSword, AttackKind::meleeHitbox, {1, 7}, 24, 45, 0, 27,
            simulation::DefinitionId{"visual.action.soldier.sword"}, boxes,
            std::nullopt};
}

AttackDefinition makeSkullArrowAttackDefinition() {
    return {skullArrowAttack, AttackKind::projectile, {1, 5}, 16, 60, 0, 120,
            simulation::DefinitionId{"visual.action.skull.arrow"}, std::nullopt,
            skullArrowProjectile};
}

ProjectileDefinition makeSkullArrowProjectileDefinition() {
    return {skullArrowProjectile,
            simulation::DefinitionId{"visual.projectile.skull.arrow"},
            FacingDirection::right, 4, 120, 6, 6,
            {{{{0, 3}, {0, -20}, {-12, -10}, {12, -10}}}}};
}

} // namespace underworld::game::gameplay::creatures
