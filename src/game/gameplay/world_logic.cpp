#include "game/gameplay/world_logic.h"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace underworld::game::gameplay {

bool executeWorldAction(const maps::WorldAction& action,
                        const simulation::MapId& mapId,
                        dialogue::DialogueFlagSet& flags,
                        simulation::EventBuffer& events,
                        const WorldLogicRuntime& runtime) {
    const auto& target = action.definitionTarget;
    if (action.kind == maps::WorldActionKind::setFlag) {
        return flags.set(target);
    }
    if (action.kind == maps::WorldActionKind::clearFlag) {
        return flags.clear(target);
    }
    if (action.kind == maps::WorldActionKind::startEncounter) {
        return runtime.startEncounter && runtime.startEncounter(target, events);
    }
    if (action.kind == maps::WorldActionKind::startScene) {
        return runtime.requestScene && runtime.requestScene(target);
    }
    if (action.kind == maps::WorldActionKind::setDoorState) {
        return runtime.setDoorState && action.instanceTarget &&
               runtime.setDoorState(action.instanceTarget, action.doorState);
    }
    if (action.kind == maps::WorldActionKind::playPresentationEffect) {
        events.emit(simulation::PresentationEffectRequested{mapId, target});
        return true;
    }
    return false;
}

void WorldLogicSystem::reset() noexcept {}

bool WorldLogicSystem::matches(const maps::WorldRuleDefinition& rule,
                               const simulation::SimulationEvent& event,
                               const simulation::MapId& mapId,
                               const dialogue::DialogueFlagSet& flags,
                               const WorldLogicRuntime& runtime) const {
    bool trigger = false;
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, simulation::MapEntered>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::mapEntered && value.mapId == mapId;
        } else if constexpr (std::is_same_v<T, simulation::RegionEntered>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::regionEntered &&
                value.mapId == mapId && value.regionId ==
                    rule.trigger.definitionTarget;
        } else if constexpr (std::is_same_v<T, simulation::RegionExited>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::regionExited &&
                value.mapId == mapId && value.regionId ==
                    rule.trigger.definitionTarget;
        } else if constexpr (std::is_same_v<T, simulation::EncounterStarted>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::encounterStarted &&
                value.mapId == mapId && value.encounterId ==
                    rule.trigger.definitionTarget;
        } else if constexpr (std::is_same_v<T, simulation::EncounterCompleted>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::encounterCompleted &&
                value.mapId == mapId && value.encounterId ==
                    rule.trigger.definitionTarget;
        } else if constexpr (std::is_same_v<T, simulation::ObjectOpened>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::objectOpened &&
                (value.mapId.empty() || value.mapId == mapId) &&
                value.objectInstanceId == rule.trigger.instanceTarget;
        } else if constexpr (std::is_same_v<T, simulation::ObjectActivationChanged>) {
            trigger = (rule.trigger.kind == maps::WorldTriggerKind::objectActivated ||
                       rule.trigger.kind == maps::WorldTriggerKind::objectDeactivated) &&
                value.mapId == mapId && value.objectInstanceId == rule.trigger.instanceTarget &&
                ((rule.trigger.kind == maps::WorldTriggerKind::objectActivated && value.active) ||
                 (rule.trigger.kind == maps::WorldTriggerKind::objectDeactivated && !value.active));
        }
    }, event);
    if (!trigger) return false;
    for (const auto& condition : rule.conditions) {
        const auto& target = condition.definitionTarget;
        if (condition.kind == maps::WorldConditionKind::flagSet && !flags.isSet(target)) return false;
        if (condition.kind == maps::WorldConditionKind::flagNotSet && flags.isSet(target)) return false;
        if (condition.kind == maps::WorldConditionKind::encounterCompleted &&
            (!runtime.encounterCompleted || !runtime.encounterCompleted(target))) return false;
        if (condition.kind == maps::WorldConditionKind::encounterNotCompleted &&
            (!runtime.encounterCompleted || runtime.encounterCompleted(target))) return false;
        if (condition.kind == maps::WorldConditionKind::doorState &&
            (!runtime.doorState || !condition.instanceTarget ||
             runtime.doorState(condition.instanceTarget) != condition.doorState)) return false;
        if ((condition.kind == maps::WorldConditionKind::objectActive ||
             condition.kind == maps::WorldConditionKind::objectInactive) &&
            (!runtime.objectActivation || !condition.instanceTarget)) return false;
        if (condition.kind == maps::WorldConditionKind::objectActive) {
            const auto value = runtime.objectActivation(condition.instanceTarget);
            if (!value || !*value) return false;
        }
        if (condition.kind == maps::WorldConditionKind::objectInactive) {
            const auto value = runtime.objectActivation(condition.instanceTarget);
            if (!value || *value) return false;
        }
    }
    return true;
}

bool WorldLogicSystem::consume(const std::vector<maps::WorldRuleDefinition>& rules,
                               const simulation::MapId& mapId,
                               dialogue::DialogueFlagSet& flags,
                               simulation::EventBuffer& events,
                               std::vector<WorldRuleState>& persistentState,
                               const WorldLogicRuntime& runtime,
                               std::size_t firstEventIndex) {
    constexpr std::size_t maximumGeneratedEvents = 4096;
    const std::size_t initialEventCount = events.size();
    bool success = true;
    std::size_t eventIndex = std::min(firstEventIndex, events.size());
    while (eventIndex < events.size()) {
        if (events.size() - initialEventCount > maximumGeneratedEvents) {
            return false;
        }
        const simulation::SimulationEvent event = events.eventAt(eventIndex);
        for (const auto& rule : rules) {
            const auto fired = std::find_if(persistentState.begin(), persistentState.end(),
                [&](const WorldRuleState& value) {
                    return value.mapId == mapId && value.ruleId == rule.id && value.fired;
                });
            if (rule.once && fired != persistentState.end()) continue;
            if (!matches(rule, event, mapId, flags, runtime)) continue;
            // Record one-shot execution before actions can append events. This makes
            // self-referential/generated-event chains idempotent.
            if (rule.once) {
                if (fired == persistentState.end()) persistentState.push_back({mapId, rule.id, true});
                else fired->fired = true;
            }
            for (const auto& action : rule.actions) {
                success = executeWorldAction(action, mapId, flags, events, runtime) && success;
            }
        }
        ++eventIndex;
    }
    return success;
}

} // namespace underworld::game::gameplay
