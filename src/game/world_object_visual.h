#pragma once

#include "engine/render/animation.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/entity_handle.h"
#include "engine/simulation/persistent_id.h"
#include "game/gameplay/world_objects.h"

#include <memory>
#include <optional>
#include <unordered_map>

namespace underworld::game {

struct WorldObjectVisualSet final {
    simulation::DefinitionId id{};
    std::shared_ptr<const render::AnimationClip> idle{};
    std::shared_ptr<const render::AnimationClip> opened{};
    std::shared_ptr<const render::AnimationClip> destroying{};
    std::shared_ptr<const render::AnimationClip> activationInactive{};
    std::shared_ptr<const render::AnimationClip> activationActive{};
    std::shared_ptr<const render::AnimationClip> doorLocked{};
    std::shared_ptr<const render::AnimationClip> doorClosed{};
    std::shared_ptr<const render::AnimationClip> doorOpen{};
    std::shared_ptr<const render::AnimationClip> destroyed{};
};

class WorldObjectVisualCatalog final {
public:
    void add(WorldObjectVisualSet set);
    [[nodiscard]] const WorldObjectVisualSet* find(
        const simulation::DefinitionId& id) const noexcept;
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
    std::optional<gameplay::DoorState> doorState_;
    std::optional<bool> activation_;
    render::Animator animator_{};
    bool initialized_{};
};

// A destroyed prop no longer owns a live gameplay entity. This presentation
// record is intentionally separate so EntityHandle generation/lifetime rules
// remain unchanged while authored residue can still be rendered and restored.
class WorldObjectResidueVisualInstance final {
public:
    WorldObjectResidueVisualInstance(simulation::PersistentInstanceId persistentId,
                                     simulation::DefinitionId visualSetId,
                                     core::WorldPointI position,
                                     const WorldObjectVisualSet& set);
    [[nodiscard]] simulation::PersistentInstanceId persistentId() const noexcept {
        return persistentId_;
    }
    [[nodiscard]] const simulation::DefinitionId& visualSetId() const noexcept {
        return set_->id;
    }
    [[nodiscard]] core::WorldPointI position() const noexcept { return position_; }
    [[nodiscard]] const render::Animator& animator() const noexcept { return animator_; }

private:
    simulation::PersistentInstanceId persistentId_{};
    core::WorldPointI position_{};
    const WorldObjectVisualSet* set_{};
    render::Animator animator_{};
};

} // namespace underworld::game
