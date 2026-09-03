#include "game/gameplay/attack_definitions.h"

#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay {
namespace {

std::size_t facingIndex(FacingDirection facing) noexcept {
    switch (facing) {
    case FacingDirection::down: return 0;
    case FacingDirection::up: return 1;
    case FacingDirection::left: return 2;
    case FacingDirection::right: return 3;
    }
    return 0;
}

void validate(const ProjectileDefinition& definition) {
    if (definition.id.empty() || definition.visualId.empty() ||
        definition.speedPixelsPerTick <= 0 || definition.lifetimeTicks == 0 ||
        definition.hitboxWidth <= 0 || definition.hitboxHeight <= 0) {
        throw std::invalid_argument("projectile definition is incomplete or invalid");
    }
}

void validate(const AttackDefinition& definition) {
    if (definition.id.empty() || definition.visualActionId.empty() ||
        definition.damage.amount <= 0 || definition.damage.knockbackPixels < 0 ||
        definition.totalTicks == 0 || definition.maximumRangePixels < 0 ||
        definition.minimumRangePixels < 0 ||
        definition.minimumRangePixels > definition.maximumRangePixels) {
        throw std::invalid_argument("attack definition is incomplete or invalid");
    }
    if (definition.kind == AttackKind::meleeHitbox) {
        if (!definition.meleeHitboxes || definition.projectileDefinitionId) {
            throw std::invalid_argument("melee attack requires only directional hitboxes");
        }
        for (const auto& box : definition.meleeHitboxes->values) {
            if (!box.valid()) {
                throw std::invalid_argument("melee attack contains an invalid hitbox");
            }
        }
    } else if (!definition.projectileDefinitionId || definition.meleeHitboxes) {
        throw std::invalid_argument("projectile attack requires only a projectile definition id");
    }
}

template <typename Map, typename Definition>
void addUnique(Map& map, Definition definition, const char* duplicateMessage) {
    validate(definition);
    const auto [position, inserted] = map.emplace(definition.id, std::move(definition));
    static_cast<void>(position);
    if (!inserted) {
        throw std::logic_error(duplicateMessage);
    }
}

const simulation::DefinitionId swordId{"attack.player.sword"};
const simulation::DefinitionId bowId{"attack.player.bow"};
const simulation::DefinitionId arrowId{"projectile.player.arrow"};

} // namespace

const DirectionalBoxDefinition& DirectionalBoxes::forFacing(
    FacingDirection facing) const noexcept {
    return values[facingIndex(facing)];
}

core::WorldPointI DirectionalOffsets::forFacing(FacingDirection facing) const noexcept {
    return values[facingIndex(facing)];
}

void AttackCatalog::add(AttackDefinition definition) {
    addUnique(definitions_, std::move(definition), "duplicate attack definition id");
}

const AttackDefinition* AttackCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}

const AttackDefinition& AttackCatalog::require(const simulation::DefinitionId& id) const {
    const AttackDefinition* definition = find(id);
    if (definition == nullptr) {
        throw std::out_of_range("attack definition was not found");
    }
    return *definition;
}

void ProjectileCatalog::add(ProjectileDefinition definition) {
    addUnique(definitions_, std::move(definition), "duplicate projectile definition id");
}

const ProjectileDefinition* ProjectileCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}

const ProjectileDefinition& ProjectileCatalog::require(
    const simulation::DefinitionId& id) const {
    const ProjectileDefinition* definition = find(id);
    if (definition == nullptr) {
        throw std::out_of_range("projectile definition was not found");
    }
    return *definition;
}

const simulation::DefinitionId& playerSwordAttackId() { return swordId; }
const simulation::DefinitionId& playerBowAttackId() { return bowId; }
const simulation::DefinitionId& playerArrowProjectileId() { return arrowId; }

AttackDefinition makePlayerSwordAttackDefinition() {
    DirectionalBoxes boxes{{{
        {-10, -1, 20, 18},
        {-10, -27, 20, 19},
        {-27, -18, 21, 18},
        {6, -18, 21, 18},
    }}};
    return {playerSwordAttackId(), AttackKind::meleeHitbox, {1, 8}, 24, 0, 0, 27,
            simulation::DefinitionId{"visual.player.sword"}, boxes, std::nullopt};
}

AttackDefinition makePlayerBowAttackDefinition() {
    return {playerBowAttackId(), AttackKind::projectile, {1, 6}, 16, 0, 0, 512,
            simulation::DefinitionId{"visual.player.bow"}, std::nullopt,
            playerArrowProjectileId()};
}

ProjectileDefinition makePlayerArrowProjectileDefinition() {
    return {playerArrowProjectileId(), simulation::DefinitionId{"visual.projectile.player.arrow"},
            FacingDirection::up, 4, 120, 6, 6,
            {{{{0, 3}, {0, -20}, {-12, -10}, {12, -10}}}}};
}

core::WorldPointI directionVector(FacingDirection facing) noexcept {
    switch (facing) {
    case FacingDirection::down: return {0, 1};
    case FacingDirection::up: return {0, -1};
    case FacingDirection::left: return {-1, 0};
    case FacingDirection::right: return {1, 0};
    }
    return {};
}

core::WorldPointI addOffset(core::WorldPointI feet, core::WorldPointI offset) noexcept {
    return {feet.x + offset.x, feet.y + offset.y};
}

} // namespace underworld::game::gameplay
