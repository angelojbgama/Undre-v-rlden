#include "game/gameplay/dialogue/dialogue_flags.h"

#include <algorithm>
#include <utility>

namespace underworld::game::gameplay::dialogue {

bool DialogueFlagSet::isSet(const simulation::DefinitionId& flag) const noexcept {
    return std::binary_search(flags_.begin(), flags_.end(), flag,
                              [](const auto& left, const auto& right) {
                                  return left.value() < right.value();
                              });
}

bool DialogueFlagSet::set(simulation::DefinitionId flag) {
    if (flag.empty()) { return false; }
    const auto position = std::lower_bound(flags_.begin(), flags_.end(), flag,
                                           [](const auto& left, const auto& right) {
                                               return left.value() < right.value();
                                           });
    if (position != flags_.end() && position->value() == flag.value()) { return false; }
    flags_.insert(position, std::move(flag));
    return true;
}

bool DialogueFlagSet::clear(const simulation::DefinitionId& flag) noexcept {
    const auto position = std::lower_bound(flags_.begin(), flags_.end(), flag,
                                           [](const auto& left, const auto& right) {
                                               return left.value() < right.value();
                                           });
    if (position == flags_.end() || position->value() != flag.value()) { return false; }
    flags_.erase(position);
    return true;
}

bool DialogueFlagSet::restore(std::span<const simulation::DefinitionId> flags) {
    for (std::size_t index = 0; index < flags.size(); ++index) {
        if (flags[index].empty() || (index != 0 &&
            flags[index - 1].value() >= flags[index].value())) {
            return false;
        }
    }
    flags_.assign(flags.begin(), flags.end());
    return true;
}

} // namespace underworld::game::gameplay::dialogue
