#pragma once

#include "engine/render/animation.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/entity_handle.h"
#include "game/gameplay/creatures/creature_engine.h"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace underworld::game {

using DirectionalAnimationClips =
    std::array<std::shared_ptr<const render::AnimationClip>, 3>; // down, up, side-left

struct EnemyVisualSet final {
    simulation::DefinitionId id{};
    DirectionalAnimationClips idle{};
    DirectionalAnimationClips walk{};
    DirectionalAnimationClips death{};
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
    [[nodiscard]] std::vector<render::AnimationMarkerEvent> consumeMarkerEvents();

private:
    simulation::EntityHandle handle_{};
    const EnemyVisualSet* visualSet_{};
    render::Animator animator_{};
    gameplay::creatures::BehaviorState state_{gameplay::creatures::BehaviorState::idle};
    gameplay::FacingDirection facing_{gameplay::FacingDirection::down};
    simulation::DefinitionId actionId_{};
    std::vector<render::AnimationMarkerEvent> markerEvents_;
    bool flipX_{};
    bool initialized_{};
};

} // namespace underworld::game
