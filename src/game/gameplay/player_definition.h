#pragma once

#include "engine/simulation/definition_id.h"
#include "game/gameplay/actor_footprint.h"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace underworld::game::gameplay {

struct PlayerHurtboxFrameSample final {
    std::uint32_t tick{};
    // nullopt means: this animation frame deliberately falls back to the
    // Player's stable/base Hurtbox.
    std::optional<ActorCollisionShapeDefinition> shape{};
};

struct PlayerHurtboxTimeline final {
    bool authored{};
    bool loop{true};
    std::uint32_t totalTicks{};
    std::vector<PlayerHurtboxFrameSample> samples{};
};

struct DirectionalPlayerHurtboxTimelines final {
    std::array<PlayerHurtboxTimeline, 4> values{}; // down/up/left/right

    [[nodiscard]] const PlayerHurtboxTimeline& forFacing(
        FacingDirection facing) const noexcept {
        switch (facing) {
        case FacingDirection::down: return values[0];
        case FacingDirection::up: return values[1];
        case FacingDirection::left: return values[2];
        case FacingDirection::right: return values[3];
        }
        return values[0];
    }

    [[nodiscard]] bool authored() const noexcept {
        for (const auto& value : values) {
            if (value.authored) return true;
        }
        return false;
    }
};

struct PlayerHurtboxFrameProfile final {
    DirectionalPlayerHurtboxTimelines idle{};
    DirectionalPlayerHurtboxTimelines walk{};
    std::optional<DirectionalPlayerHurtboxTimelines> hurt{};
    // Action ids remain data-driven. Runtime currently understands sword/bow,
    // while future actions can use the same compiled representation.
    std::unordered_map<std::string, DirectionalPlayerHurtboxTimelines> actions;
};

struct PlayerDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualSetId{};
    simulation::DefinitionId progressionId{};
    std::optional<DirectionalActorCollisionShapes> movementCollision{};
    std::optional<ActorCollisionShapeDefinition> hurtbox{};
    std::optional<PlayerHurtboxFrameProfile> hurtboxFrameOverrides{};
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
