#include "game/world_object_visual.h"

#include <stdexcept>
#include <utility>

namespace underworld::game {

void WorldObjectVisualCatalog::add(WorldObjectVisualSet set) {
    if (set.id.empty() || !set.idle) {
        throw std::invalid_argument("world object visual set requires id and idle clip");
    }
    const auto [position, inserted] = sets_.emplace(set.id, std::move(set));
    static_cast<void>(position);
    if (!inserted) { throw std::logic_error("duplicate world object visual set id"); }
}
const WorldObjectVisualSet& WorldObjectVisualCatalog::require(
    const simulation::DefinitionId& id) const {
    const auto found = sets_.find(id);
    if (found == sets_.end()) { throw std::out_of_range("world object visual set not found"); }
    return found->second;
}

WorldObjectVisualInstance::WorldObjectVisualInstance(
    simulation::EntityHandle handle, const WorldObjectVisualSet& set)
    : handle_(handle), set_(&set) {
    if (!handle) { throw std::invalid_argument("object visual requires valid handle"); }
}

void WorldObjectVisualInstance::update(const gameplay::WorldObjectInstance& object,
                                       std::uint64_t ticks) {
    if (object.handle() != handle_) {
        throw std::invalid_argument("object visual updated with different handle");
    }
    const auto state = object.state();
    if (!initialized_ || state != state_) {
        const auto* clip = &set_->idle;
        if (state == gameplay::WorldObjectState::opened && set_->opened) {
            clip = &set_->opened;
        } else if (state == gameplay::WorldObjectState::destroying && set_->destroying) {
            clip = &set_->destroying;
        }
        animator_.play(*clip);
        initialized_ = true;
        state_ = state;
    }
    animator_.updateTicks(ticks);
}

} // namespace underworld::game
