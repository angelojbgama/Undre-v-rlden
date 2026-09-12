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
const WorldObjectVisualSet* WorldObjectVisualCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = sets_.find(id);
    return found == sets_.end() ? nullptr : &found->second;
}
const WorldObjectVisualSet& WorldObjectVisualCatalog::require(
    const simulation::DefinitionId& id) const {
    const auto* found = find(id);
    if (!found) { throw std::out_of_range("world object visual set not found"); }
    return *found;
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
    const auto doorState = object.isDoor() ? std::optional{object.doorState()} : std::nullopt;
    const auto activation = object.hasActivation() ? std::optional{object.activationActive()} : std::nullopt;
    const bool stateChanged = !initialized_ || state != state_ || doorState != doorState_ ||
                              activation != activation_;
    if (stateChanged) {
        const auto* clip = &set_->idle;
        if (state == gameplay::WorldObjectState::damaged && set_->damaged) {
            clip = &set_->damaged;
        } else if (state == gameplay::WorldObjectState::destroying && set_->destroying) {
            clip = &set_->destroying;
        } else if (state == gameplay::WorldObjectState::destroyed && set_->destroyed) {
            clip = &set_->destroyed;
        } else if (doorState) {
            if (*doorState == gameplay::DoorState::locked && set_->doorLocked) clip = &set_->doorLocked;
            else if (*doorState == gameplay::DoorState::closed && set_->doorClosed) clip = &set_->doorClosed;
            else if (*doorState == gameplay::DoorState::open && set_->doorOpen) clip = &set_->doorOpen;
        } else if (activation) {
            if (*activation && set_->activationActive) clip = &set_->activationActive;
            else if (!*activation && set_->activationInactive) clip = &set_->activationInactive;
        } else if (state == gameplay::WorldObjectState::opened && set_->opened) {
            clip = &set_->opened;
        }
        animator_.play(*clip);
        initialized_ = true;
        state_ = state;
        doorState_ = doorState;
        activation_ = activation;
        stateTicks_ = 0;
        destructionDurationTicks_ =
            state == gameplay::WorldObjectState::destroying && object.definition().destructible
                ? object.definition().destructible->destructionDurationTicks
                : 0;
    } else {
        stateTicks_ += ticks;
    }
    animator_.updateTicks(ticks);
}

bool WorldObjectVisualInstance::visible() const noexcept {
    if (!initialized_ || state_ != gameplay::WorldObjectState::destroying ||
        destructionDurationTicks_ == 0) {
        return true;
    }
    constexpr std::uint64_t blinkWindowTicks = 12;
    constexpr std::uint64_t blinkHalfPeriodTicks = 2;
    const auto blinkStart = destructionDurationTicks_ > blinkWindowTicks
        ? destructionDurationTicks_ - blinkWindowTicks : 0;
    if (stateTicks_ < blinkStart) return true;
    return ((stateTicks_ - blinkStart) / blinkHalfPeriodTicks) % 2 == 0;
}

WorldObjectResidueVisualInstance::WorldObjectResidueVisualInstance(
    simulation::PersistentInstanceId persistentId, simulation::DefinitionId visualSetId,
    core::WorldPointI position, const WorldObjectVisualSet& set)
    : persistentId_(persistentId), position_(position), set_(&set) {
    if (!persistentId_ || visualSetId != set.id || !set.destroyed) {
        throw std::invalid_argument("destroyed object residue requires a destroyed clip");
    }
    animator_.play(set.destroyed);
}

} // namespace underworld::game
