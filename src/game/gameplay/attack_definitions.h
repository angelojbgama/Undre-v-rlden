#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/definition_id.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/facing_direction.h"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace underworld::game::gameplay {

struct DirectionalBoxDefinition final {
    int offsetX{};
    int offsetY{};
    int width{};
    int height{};

    [[nodiscard]] world::AabbI at(core::WorldPointI feet) const noexcept {
        return {feet.x + offsetX, feet.y + offsetY, width, height};
    }
    [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0; }
};

struct DirectionalBoxes final {
    std::array<DirectionalBoxDefinition, 4> values{}; // down, up, left, right
    [[nodiscard]] const DirectionalBoxDefinition& forFacing(FacingDirection facing) const noexcept;
};

struct DirectionalOffsets final {
    std::array<core::WorldPointI, 4> values{}; // down, up, left, right
    [[nodiscard]] core::WorldPointI forFacing(FacingDirection facing) const noexcept;
};

struct ProjectileDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualId{};
    FacingDirection canonicalFacing{FacingDirection::up};
    int speedPixelsPerTick{};
    std::uint32_t lifetimeTicks{};
    int hitboxWidth{};
    int hitboxHeight{};
    DirectionalOffsets spawnOffsets{};
};

struct AttackDefinition final {
    simulation::DefinitionId id{};
    AttackKind kind{AttackKind::meleeHitbox};
    DamageSpec damage{};
    std::uint32_t totalTicks{};
    std::uint32_t cooldownTicks{};
    int minimumRangePixels{};
    int maximumRangePixels{};
    simulation::DefinitionId visualActionId{};
    std::optional<DirectionalBoxes> meleeHitboxes{};
    std::optional<simulation::DefinitionId> projectileDefinitionId{};
};

class AttackCatalog final {
public:
    void add(AttackDefinition definition);
    [[nodiscard]] const AttackDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const AttackDefinition& require(const simulation::DefinitionId& id) const;

private:
    std::unordered_map<simulation::DefinitionId, AttackDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

class ProjectileCatalog final {
public:
    void add(ProjectileDefinition definition);
    [[nodiscard]] const ProjectileDefinition* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const ProjectileDefinition& require(
        const simulation::DefinitionId& id) const;

private:
    std::unordered_map<simulation::DefinitionId, ProjectileDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

[[nodiscard]] const simulation::DefinitionId& playerSwordAttackId();
[[nodiscard]] const simulation::DefinitionId& playerBowAttackId();
[[nodiscard]] const simulation::DefinitionId& playerArrowProjectileId();
[[nodiscard]] AttackDefinition makePlayerSwordAttackDefinition();
[[nodiscard]] AttackDefinition makePlayerBowAttackDefinition();
[[nodiscard]] ProjectileDefinition makePlayerArrowProjectileDefinition();

[[nodiscard]] core::WorldPointI directionVector(FacingDirection facing) noexcept;
[[nodiscard]] core::WorldPointI addOffset(core::WorldPointI feet,
                                          core::WorldPointI offset) noexcept;
[[nodiscard]] std::uint8_t clockwiseQuarterTurns(FacingDirection canonical,
                                                  FacingDirection target) noexcept;

} // namespace underworld::game::gameplay
