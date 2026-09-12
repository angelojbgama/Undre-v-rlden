#pragma once

#include "engine/simulation/definition_id.h"
#include "game/gameplay/actor_footprint.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace underworld::game::gameplay {

struct PlayerDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualSetId{};
    simulation::DefinitionId progressionId{};
    std::optional<DirectionalActorCollisionShapes> movementCollision{};
};

class PlayerDefinitionCatalog final {
public:
    void add(PlayerDefinition definition) {
        if (definition.id.empty()) {
            throw std::invalid_argument("player definition id must not be empty");
        }
        const auto id = definition.id;
        const auto [unused, inserted] =
            definitions_.emplace(id, std::move(definition));
        static_cast<void>(unused);
        if (!inserted) {
            throw std::invalid_argument(
                "duplicate player definition: " + std::string(id.value()));
        }
    }

    [[nodiscard]] const PlayerDefinition* find(
        const simulation::DefinitionId& id) const noexcept {
        const auto found = definitions_.find(id);
        return found == definitions_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const PlayerDefinition& require(
        const simulation::DefinitionId& id) const {
        const auto* definition = find(id);
        if (!definition) {
            throw std::out_of_range(
                "unknown player definition: " + std::string(id.value()));
        }
        return *definition;
    }

    [[nodiscard]] const auto& values() const noexcept { return definitions_; }

private:
    std::unordered_map<simulation::DefinitionId, PlayerDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

[[nodiscard]] inline const simulation::DefinitionId&
defaultPlayerDefinitionId() {
    static const simulation::DefinitionId id{"player.hero"};
    return id;
}

} // namespace underworld::game::gameplay
