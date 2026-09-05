#include "game/gameplay/quests/quest_system.h"

#include <algorithm>
#include <limits>
#include <variant>

namespace underworld::game::gameplay::quests {

bool QuestSystem::start(const simulation::DefinitionId& questId) {
    const auto* definition = catalog_->find(questId);
    return definition != nullptr && state_->start(*definition);
}

void QuestSystem::consume(const simulation::EventBuffer& events) {
    consume(events.events());
}

void QuestSystem::consume(std::span<const simulation::SimulationEvent> events) {
    for (const auto& event : events) {
        std::visit([this](const auto& value) { consume(value); }, event);
    }
}

void QuestSystem::consume(const simulation::EntityDefeated& event) {
    if (!event.defeatedDefinitionId.empty()) {
        advance(QuestObjectiveKind::kill, event.defeatedDefinitionId, 1);
    }
}

void QuestSystem::consume(const simulation::PickupCollected& event) {
    const auto amount = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        event.amount, std::numeric_limits<std::uint32_t>::max()));
    if (amount == 0) { return; }
    for (const auto& questId : state_->activeQuestIds()) {
        const auto* definition = catalog_->find(questId);
        if (definition == nullptr) { continue; }
        for (const auto& objective : definition->objectives) {
            if (objective.kind != QuestObjectiveKind::pickup) { continue; }
            const bool pickupMatches = !event.pickupDefinitionId.empty() &&
                objective.targetId == event.pickupDefinitionId;
            const bool itemMatches = event.itemId && objective.targetId == *event.itemId;
            if (pickupMatches || itemMatches) {
                static_cast<void>(state_->advanceObjective(
                    *definition, objective.id, amount));
            }
        }
    }
}

void QuestSystem::consume(const simulation::NpcTalked& event) {
    if (!event.npcDefinitionId.empty()) {
        advance(QuestObjectiveKind::talk, event.npcDefinitionId, 1);
    }
}

void QuestSystem::consume(const simulation::MapEntered& event) {
    if (!event.mapId.empty()) {
        advanceByValue(QuestObjectiveKind::enter, event.mapId.value(), 1);
    }
}

void QuestSystem::consume(const simulation::ObjectOpened& event) {
    if (!event.objectDefinitionId.empty()) {
        advance(QuestObjectiveKind::open, event.objectDefinitionId, 1);
    }
}

void QuestSystem::consume(const simulation::ItemDelivered& event) {
    const auto amount = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        event.amount, std::numeric_limits<std::uint32_t>::max()));
    if (amount != 0 && !event.itemId.empty()) {
        advance(QuestObjectiveKind::deliver, event.itemId, amount);
    }
}

void QuestSystem::advance(QuestObjectiveKind kind,
                          const simulation::DefinitionId& targetId,
                          std::uint32_t amount) {
    for (const auto& questId : state_->activeQuestIds()) {
        const auto* definition = catalog_->find(questId);
        if (definition == nullptr) { continue; }
        for (const auto& objective : definition->objectives) {
            if (objective.kind == kind && objective.targetId == targetId) {
                static_cast<void>(state_->advanceObjective(*definition, objective.id, amount));
            }
        }
    }
}

void QuestSystem::advanceByValue(QuestObjectiveKind kind, std::string_view targetValue,
                                 std::uint32_t amount) {
    for (const auto& questId : state_->activeQuestIds()) {
        const auto* definition = catalog_->find(questId);
        if (definition == nullptr) { continue; }
        for (const auto& objective : definition->objectives) {
            if (objective.kind == kind && objective.targetId.value() == targetValue) {
                static_cast<void>(state_->advanceObjective(*definition, objective.id, amount));
            }
        }
    }
}

} // namespace underworld::game::gameplay::quests
