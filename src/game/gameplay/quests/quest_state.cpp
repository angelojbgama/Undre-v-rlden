#include "game/gameplay/quests/quest_state.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay::quests {

namespace {

void requireMatchingQuest(const QuestDefinition& definition,
                          const QuestProgress& progress) {
    if (definition.id != progress.questId) {
        throw std::invalid_argument("quest progress belongs to another quest definition");
    }
}

} // namespace

bool QuestStateStore::start(const QuestDefinition& definition) {
    if (definition.id.empty() || definition.objectives.empty()) {
        return false;
    }
    if (progress_.find(definition.id) != progress_.end()) {
        return false;
    }

    QuestProgress progress{definition.id, QuestStatus::active, {}};
    progress.objectives.reserve(definition.objectives.size());
    for (const auto& objective : definition.objectives) {
        progress.objectives.push_back({objective.id, 0});
    }
    progress_.emplace(definition.id, std::move(progress));
    return true;
}

bool QuestStateStore::reset(const simulation::DefinitionId& questId) noexcept {
    return progress_.erase(questId) != 0;
}

QuestStatus QuestStateStore::status(
    const simulation::DefinitionId& questId) const noexcept {
    const auto* progress = find(questId);
    return progress == nullptr ? QuestStatus::inactive : progress->status;
}

const QuestProgress* QuestStateStore::find(
    const simulation::DefinitionId& questId) const noexcept {
    const auto found = progress_.find(questId);
    return found == progress_.end() ? nullptr : &found->second;
}

QuestProgress* QuestStateStore::findMutable(
    const simulation::DefinitionId& questId) noexcept {
    const auto found = progress_.find(questId);
    return found == progress_.end() ? nullptr : &found->second;
}

const QuestProgress& QuestStateStore::require(
    const simulation::DefinitionId& questId) const {
    const auto* progress = find(questId);
    if (progress == nullptr) { throw std::out_of_range("quest progress not found"); }
    return *progress;
}

std::vector<simulation::DefinitionId> QuestStateStore::activeQuestIds() const {
    std::vector<simulation::DefinitionId> result;
    result.reserve(progress_.size());
    for (const auto& [id, progress] : progress_) {
        if (progress.status == QuestStatus::active) { result.push_back(id); }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.value() < right.value();
    });
    return result;
}

QuestObjectiveProgress* QuestStateStore::findObjectiveProgressMutable(
    QuestProgress& progress, const simulation::DefinitionId& objectiveId) noexcept {
    for (auto& objective : progress.objectives) {
        if (objective.objectiveId == objectiveId) { return &objective; }
    }
    return nullptr;
}

const QuestObjectiveProgress* findObjectiveProgress(
    const QuestProgress& progress,
    const simulation::DefinitionId& objectiveId) noexcept {
    for (const auto& objective : progress.objectives) {
        if (objective.objectiveId == objectiveId) { return &objective; }
    }
    return nullptr;
}

void QuestStateStore::refreshStatus(const QuestDefinition& definition,
                                    QuestProgress& progress) {
    requireMatchingQuest(definition, progress);
    const bool allComplete = std::all_of(
        definition.objectives.begin(), definition.objectives.end(), [&](const auto& definitionObjective) {
            const auto* progressObjective = findObjectiveProgress(progress, definitionObjective.id);
            return progressObjective != nullptr &&
                   progressObjective->currentCount >= definitionObjective.requiredCount;
        });
    progress.status = allComplete ? QuestStatus::completed : QuestStatus::active;
}

bool QuestStateStore::setObjectiveProgress(const QuestDefinition& definition,
                                           const simulation::DefinitionId& objectiveId,
                                           std::uint32_t count) {
    auto* progress = findMutable(definition.id);
    if (progress == nullptr || progress->status == QuestStatus::completed) { return false; }
    const auto* definitionObjective = findObjective(definition, objectiveId);
    auto* progressObjective = findObjectiveProgressMutable(*progress, objectiveId);
    if (definitionObjective == nullptr || progressObjective == nullptr) { return false; }

    const auto clamped = std::min(count, definitionObjective->requiredCount);
    if (progressObjective->currentCount == clamped) { return false; }
    progressObjective->currentCount = clamped;
    refreshStatus(definition, *progress);
    return true;
}

bool QuestStateStore::advanceObjective(const QuestDefinition& definition,
                                       const simulation::DefinitionId& objectiveId,
                                       std::uint32_t amount) {
    auto* progress = findMutable(definition.id);
    if (progress == nullptr || progress->status == QuestStatus::completed || amount == 0) {
        return false;
    }
    const auto* definitionObjective = findObjective(definition, objectiveId);
    auto* progressObjective = findObjectiveProgressMutable(*progress, objectiveId);
    if (definitionObjective == nullptr || progressObjective == nullptr) { return false; }

    const auto available = definitionObjective->requiredCount - progressObjective->currentCount;
    const auto accepted = std::min(amount, available);
    if (accepted == 0) { return false; }
    progressObjective->currentCount += accepted;
    refreshStatus(definition, *progress);
    return true;
}

} // namespace underworld::game::gameplay::quests
