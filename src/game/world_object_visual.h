#pragma once

#include "engine/render/animation.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/entity_handle.h"
#include "game/gameplay/world_objects.h"

#include <memory>
#include <unordered_map>

namespace underworld::game {

struct WorldObjectVisualSet final {
    simulation::DefinitionId id{};
    std::shared_ptr<const render::AnimationClip> idle{};
    std::shared_ptr<const render::AnimationClip> opened{};
    std::shared_ptr<const render::AnimationClip> destroying{};
};

class WorldObjectVisualCatalog final {
public:
    void add(WorldObjectVisualSet set);
    [[nodiscard]] const WorldObjectVisualSet& require(
        const simulation::DefinitionId& id) const;

private:
    std::unordered_map<simulation::DefinitionId, WorldObjectVisualSet,
                       simulation::DefinitionIdHash> sets_;
};

class WorldObjectVisualInstance final {
public:
    WorldObjectVisualInstance(simulation::EntityHandle handle,
                              const WorldObjectVisualSet& set);
    void update(const gameplay::WorldObjectInstance& object, std::uint64_t ticks = 1);
    [[nodiscard]] simulation::EntityHandle handle() const noexcept { return handle_; }
    [[nodiscard]] const simulation::DefinitionId& visualSetId() const noexcept {
        return set_->id;
    }
    [[nodiscard]] const render::Animator& animator() const noexcept { return animator_; }
    [[nodiscard]] bool finished() const noexcept { return animator_.finished(); }

private:
    simulation::EntityHandle handle_{};
    const WorldObjectVisualSet* set_{};
    gameplay::WorldObjectState state_{gameplay::WorldObjectState::idle};
    render::Animator animator_{};
    bool initialized_{};
};

} // namespace underworld::game
