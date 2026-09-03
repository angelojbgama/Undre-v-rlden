#include "engine/simulation/entity_handle.h"

#include <limits>
#include <stdexcept>

namespace underworld::simulation {

EntityHandle EntityHandlePool::create() {
    if (!free_.empty()) {
        const std::uint32_t index = free_.back();
        free_.pop_back();
        Slot& slot = slots_[index];
        slot.alive = true;
        return {index, slot.generation};
    }
    if (slots_.size() >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("entity handle pool exhausted");
    }
    const auto index = static_cast<std::uint32_t>(slots_.size());
    slots_.push_back({1, true});
    return {index, 1};
}

bool EntityHandlePool::destroy(EntityHandle handle) noexcept {
    if (!valid(handle)) {
        return false;
    }
    Slot& slot = slots_[handle.index];
    slot.alive = false;
    if (slot.generation == std::numeric_limits<std::uint32_t>::max()) {
        slot.generation = 0; // Retire the slot rather than revive a stale generation.
        return true;
    }
    ++slot.generation;
    free_.push_back(handle.index);
    return true;
}

bool EntityHandlePool::valid(EntityHandle handle) const noexcept {
    return handle.generation != 0 && handle.index < slots_.size() &&
           slots_[handle.index].alive && slots_[handle.index].generation == handle.generation;
}

} // namespace underworld::simulation
