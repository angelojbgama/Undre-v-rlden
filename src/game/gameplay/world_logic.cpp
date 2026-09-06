#include "game/gameplay/world_logic.h"

#include <type_traits>
#include <variant>

namespace underworld::game::gameplay {

void WorldLogicSystem::reset() noexcept { fired_.clear(); mapId_ = {}; }

bool WorldLogicSystem::matches(const maps::WorldRuleDefinition& rule,
                               const simulation::SimulationEvent& event,
                               const simulation::MapId& mapId,
                               const dialogue::DialogueFlagSet& flags) const {
    bool trigger = false;
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, simulation::MapEntered>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::mapEntered && value.mapId == mapId;
        } else if constexpr (std::is_same_v<T, simulation::RegionEntered>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::regionEntered &&
                value.mapId == mapId && value.regionId == rule.trigger.target;
        } else if constexpr (std::is_same_v<T, simulation::RegionExited>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::regionExited &&
                value.mapId == mapId && value.regionId == rule.trigger.target;
        } else if constexpr (std::is_same_v<T, simulation::EncounterStarted>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::encounterStarted &&
                value.mapId == mapId && value.encounterId == rule.trigger.target;
        } else if constexpr (std::is_same_v<T, simulation::EncounterCompleted>) {
            trigger = rule.trigger.kind == maps::WorldTriggerKind::encounterCompleted &&
                value.mapId == mapId && value.encounterId == rule.trigger.target;
        }
    }, event);
    if (!trigger) return false;
    for (const auto& condition : rule.conditions) {
        if (condition.kind == maps::WorldConditionKind::flagSet && !flags.isSet(condition.target)) return false;
        if (condition.kind == maps::WorldConditionKind::flagNotSet && flags.isSet(condition.target)) return false;
    }
    return true;
}

void WorldLogicSystem::consume(const std::vector<maps::WorldRuleDefinition>& rules,
                               const simulation::MapId& mapId,
                               dialogue::DialogueFlagSet& flags,
                               simulation::EventBuffer& events) {
    if (mapId_ != mapId) { fired_.clear(); mapId_ = mapId; }
    const auto snapshot = events.events();
    for (const auto& event : snapshot) {
        for (const auto& rule : rules) {
            if (rule.once && fired_.contains(rule.id)) continue;
            if (!matches(rule, event, mapId, flags)) continue;
            for (const auto& action : rule.actions) {
                if (action.kind == maps::WorldActionKind::setFlag) static_cast<void>(flags.set(action.target));
                else if (action.kind == maps::WorldActionKind::clearFlag) static_cast<void>(flags.clear(action.target));
                else if (action.kind == maps::WorldActionKind::startEncounter) {
                    events.emit(simulation::EncounterStarted{mapId, action.target});
                }
            }
            if (rule.once) fired_.insert(rule.id);
        }
    }
}

} // namespace underworld::game::gameplay
