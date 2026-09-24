#pragma once

#include "engine/render/animation.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/entity_handle.h"
#include "game/gameplay/creatures/creature_engine.h"

#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace underworld::game {

using DirectionalAnimationClips =
    std::array<std::shared_ptr<const render::AnimationClip>, 3>; // down, up, side-left

struct EnemyVisualSet final {
    EnemyVisualSet() = default;
    EnemyVisualSet(simulation::DefinitionId visualId, DirectionalAnimationClips idleClips,
                   DirectionalAnimationClips moveClips, DirectionalAnimationClips deathClips,
                   std::unordered_map<simulation::DefinitionId, DirectionalAnimationClips,
                                      simulation::DefinitionIdHash> actionClips)
        : id(std::move(visualId)), idle(std::move(idleClips)), walk(std::move(moveClips)),
          death(std::move(deathClips)), attacks(std::move(actionClips)) {}

    simulation::DefinitionId id{};
    DirectionalAnimationClips idle{};
    // Authored optional states are resolved to these runtime fallbacks.
    DirectionalAnimationClips walk{};
    DirectionalAnimationClips death{};
    std::optional<DirectionalAnimationClips> hurt{};
    std::optional<DirectionalAnimationClips> dead{};
    std::unordered_map<simulation::DefinitionId, DirectionalAnimationClips,
                       simulation::DefinitionIdHash> attacks{};
};

class EnemyVisualCatalog final {
public:
    void add(EnemyVisualSet visualSet);
    [[nodiscard]] const EnemyVisualSet* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const EnemyVisualSet& require(
        const simulation::DefinitionId& id) const;
    [[nodiscard]] std::vector<simulation::DefinitionId> ids() const;

private:
    std::unordered_map<simulation::DefinitionId, EnemyVisualSet,
                       simulation::DefinitionIdHash> sets_;
};

constexpr std::uint32_t kFlashBlinkPeriod = 4;

class EnemyVisualInstance final {
public:
    EnemyVisualInstance(simulation::EntityHandle handle, const EnemyVisualSet& visualSet);

    void update(const gameplay::creatures::EnemyInstance& enemy,
                std::uint64_t ticks = 1);
    [[nodiscard]] simulation::EntityHandle handle() const noexcept { return handle_; }
    [[nodiscard]] const simulation::DefinitionId& visualSetId() const noexcept {
        return visualSet_->id;
    }
    [[nodiscard]] const render::Animator& animator() const noexcept { return animator_; }
    [[nodiscard]] bool flipX() const noexcept { return flipX_; }
    // Presentation-only damage feedback, derived from the combatant's
    // invulnerability window: latched on a fresh hit and gone when the
    // window closes. Never persists and never reaches saves.
    [[nodiscard]] bool flashing() const noexcept { return flashTicks_ > 0; }
    // Alternates while latched so the flash reads as a blink, not a light
    // (elapsed-frame based: the first two ticks of the window are lit).
    [[nodiscard]] bool flashFrame() const noexcept {
        const std::uint32_t elapsed = flashTicks_ % kFlashBlinkPeriod;
        return flashing() && (kFlashBlinkPeriod - elapsed) % kFlashBlinkPeriod < 2;
    }
    [[nodiscard]] std::vector<render::AnimationMarkerEvent> consumeMarkerEvents();

private:
    simulation::EntityHandle handle_{};
    const EnemyVisualSet* visualSet_{};
    render::Animator animator_{};
    gameplay::creatures::BehaviorState state_{gameplay::creatures::BehaviorState::idle};
    gameplay::FacingDirection facing_{gameplay::FacingDirection::down};
    simulation::DefinitionId actionId_{};
    std::vector<render::AnimationMarkerEvent> markerEvents_;
    std::uint32_t hurtTicks_{};   // while > 0 the authored hurt clip holds
    std::uint32_t flashTicks_{};  // while > 0 the tint blink is on
    bool invulnerable_{};         // previous-tick invulnerability for edge detect
    bool flipX_{};
    bool initialized_{};
};

} // namespace underworld::game
