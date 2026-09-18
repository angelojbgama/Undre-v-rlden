#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/definition_id.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/facing_direction.h"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

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

enum class AttackTimelineEventKind {
    activateHitbox,
    deactivateHitbox,
    spawnProjectile,
    playEffect,
};

struct AttackTimelineEvent final {
    std::uint32_t tick{};
    AttackTimelineEventKind kind{AttackTimelineEventKind::activateHitbox};
    // playEffect payload: authored animation to play and the offset from
    // the caster's feet point. Ignored by other event kinds.
    simulation::DefinitionId effectAnimationId{};
    core::WorldPointI effectOffset{};
    [[nodiscard]] constexpr bool operator==(const AttackTimelineEvent&) const noexcept = default;
};

struct DirectionalAnimations final {
    std::array<std::optional<simulation::DefinitionId>, 4> values{}; // down, up, left, right
    [[nodiscard]] std::optional<simulation::DefinitionId> forFacing(
        FacingDirection facing) const noexcept;
    [[nodiscard]] bool any() const noexcept;
};

enum class ProjectileRenderLayer {
    world, // drawn behind actors (below the player sprite)
    actor, // drawn in front of actors (default)
};

// Per-direction render layer authored on the projectile. Unset facings
// fall back to ProjectileDefinition::renderLayer.
struct DirectionalRenderLayers final {
    std::array<std::optional<ProjectileRenderLayer>, 4> values{}; // down, up, left, right
    [[nodiscard]] std::optional<ProjectileRenderLayer> forFacing(
        FacingDirection facing) const noexcept;
    [[nodiscard]] bool any() const noexcept;
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
    ProjectileRenderLayer renderLayer{ProjectileRenderLayer::actor};
    DirectionalRenderLayers renderLayers{};
    // When set, the projectile renders this authored animation in flight
    // (frame pacing authored on the animation) instead of the static sprite.
    simulation::DefinitionId animationId{};
    // Optional travel cap in pixels (0 = unlimited: flies until collision
    // or lifetime expiry).
    int maximumDistancePixels{};
    // Optional animations for how the projectile finishes: collision plays
    // impactAnimationId; reaching the lifetime/distance limit plays
    // expireAnimationId with its LAST FRAME HELD in place (e.g. an arrow
    // stuck in the ground). Empty = default impact behavior / nothing.
    simulation::DefinitionId impactAnimationId{};
    simulation::DefinitionId expireAnimationId{};
    // Per-direction override of expireAnimationId (e.g. an arrow stuck in
    // the ground looks different per flight direction).
    DirectionalAnimations expireAnimations{};
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
    std::vector<AttackTimelineEvent> timeline{};
    struct CollisionSample final {
        std::uint32_t tick{};
        std::array<std::vector<DirectionalBoxDefinition>, 4> regions{};
        // Distinguishes "this facing is explicitly empty at this tick" from
        // "this sample was authored only for another facing".
        std::array<bool, 4> authored{};
    };
    std::vector<CollisionSample> collisionSamples{};

    [[nodiscard]] bool hasCollisionSamples(
        FacingDirection facing) const noexcept;
    [[nodiscard]] const CollisionSample* collisionSampleAt(
        std::uint32_t tick, FacingDirection facing) const noexcept;
};

// Gameplay attack timing is measured in fixed ticks, independently from any
// visual animation. An execution starts at elapsedTicks == 0. Each call to
// advance() completes one simulation tick, increments elapsedTicks, and emits
// all events at that tick exactly once. The execution is finished when
// elapsedTicks reaches definition->totalTicks; events at totalTicks are
// rejected by AttackCatalog validation.
struct AttackExecution final {
    const AttackDefinition* definition{};
    AttackKey key{};
    FacingDirection lockedFacing{FacingDirection::down};
    bool meleeHitboxActive{};
    std::uint32_t elapsedTicks{};
    std::size_t nextTimelineEvent{};
    bool finished{};

    void advance(std::vector<AttackTimelineEvent>& events);
};

class AttackCatalog final {
public:
    void add(AttackDefinition definition);
    [[nodiscard]] const AttackDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const AttackDefinition& require(const simulation::DefinitionId& id) const;
    [[nodiscard]] const std::unordered_map<simulation::DefinitionId, AttackDefinition,
                                           simulation::DefinitionIdHash>& values() const noexcept {
        return definitions_;
    }

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
    [[nodiscard]] const std::unordered_map<simulation::DefinitionId, ProjectileDefinition,
                                           simulation::DefinitionIdHash>& values() const noexcept {
        return definitions_;
    }

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
