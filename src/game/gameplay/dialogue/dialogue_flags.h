#pragma once

#include "engine/simulation/definition_id.h"

#include <span>
#include <vector>

namespace underworld::game::gameplay::dialogue {

class DialogueFlagSet final {
public:
    [[nodiscard]] bool isSet(const simulation::DefinitionId& flag) const noexcept;
    [[nodiscard]] bool set(simulation::DefinitionId flag);
    [[nodiscard]] bool clear(const simulation::DefinitionId& flag) noexcept;
    void clearAll() noexcept { flags_.clear(); }
    [[nodiscard]] const std::vector<simulation::DefinitionId>& values() const noexcept {
        return flags_;
    }

    // Used by the versioned save reader. Input must be non-empty and strictly sorted.
    [[nodiscard]] bool restore(std::span<const simulation::DefinitionId> flags);

private:
    std::vector<simulation::DefinitionId> flags_;
};

} // namespace underworld::game::gameplay::dialogue
