#pragma once

#include "engine/simulation/definition_id.h"
#include "engine/simulation/persistent_id.h"
#include "game/gameplay/world_objects.h"

#include <vector>

namespace underworld::game::maps {

using DoorState = gameplay::DoorState;

enum class WorldTriggerKind { mapEntered, regionEntered, regionExited, encounterStarted,
                              encounterCompleted, objectOpened, objectActivated,
                              objectDeactivated };
enum class WorldConditionKind { flagSet, flagNotSet, encounterCompleted,
                                encounterNotCompleted, doorState, objectActive,
                                objectInactive };
enum class WorldActionKind { setFlag, clearFlag, startEncounter, setDoorState,
                             playPresentationEffect, startScene };

struct WorldTrigger final {
    WorldTriggerKind kind{WorldTriggerKind::mapEntered};
    simulation::DefinitionId definitionTarget{};
    simulation::PersistentInstanceId instanceTarget{};
    [[nodiscard]] bool operator==(const WorldTrigger&) const noexcept = default;
};

struct WorldCondition final {
    WorldConditionKind kind{WorldConditionKind::flagSet};
    simulation::DefinitionId definitionTarget{};
    simulation::PersistentInstanceId instanceTarget{};
    DoorState doorState{DoorState::closed};
    [[nodiscard]] bool operator==(const WorldCondition&) const noexcept = default;
};

struct WorldAction final {
    WorldActionKind kind{WorldActionKind::setFlag};
    simulation::DefinitionId definitionTarget{};
    simulation::PersistentInstanceId instanceTarget{};
    DoorState doorState{DoorState::closed};
    [[nodiscard]] bool operator==(const WorldAction&) const noexcept = default;
};

struct WorldRuleDefinition final {
    simulation::DefinitionId id{};
    WorldTrigger trigger{};
    std::vector<WorldCondition> conditions;
    std::vector<WorldAction> actions;
    bool once{};
    [[nodiscard]] bool operator==(const WorldRuleDefinition&) const noexcept = default;
};

} // namespace underworld::game::maps
