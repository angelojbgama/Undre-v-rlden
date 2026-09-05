#include "game/gameplay/quests/quest_model.h"

#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay::quests {
namespace {

void validate(const QuestDefinition& definition) {
    if (definition.id.empty() || definition.title.empty() || definition.objectives.empty()) {
        throw std::invalid_argument("quest definition is incomplete");
    }
    for (std::size_t index = 0; index < definition.objectives.size(); ++index) {
        const auto& objective = definition.objectives[index];
        if (objective.id.empty() || objective.targetId.empty() ||
            objective.requiredCount == 0) {
            throw std::invalid_argument("quest objective is incomplete");
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (definition.objectives[previous].id == objective.id) {
                throw std::logic_error("duplicate quest objective id");
            }
        }
    }
}

} // namespace

void QuestCatalog::add(QuestDefinition definition) {
    validate(definition);
    const auto [position, inserted] = definitions_.emplace(definition.id, std::move(definition));
    static_cast<void>(position);
    if (!inserted) { throw std::logic_error("duplicate quest definition id"); }
}

const QuestDefinition* QuestCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}

const QuestDefinition& QuestCatalog::require(const simulation::DefinitionId& id) const {
    const auto* definition = find(id);
    if (!definition) { throw std::out_of_range("quest definition not found"); }
    return *definition;
}

const QuestObjectiveDefinition* findObjective(
    const QuestDefinition& quest, const simulation::DefinitionId& objectiveId) noexcept {
    for (const auto& objective : quest.objectives) {
        if (objective.id == objectiveId) { return &objective; }
    }
    return nullptr;
}

const QuestObjectiveDefinition& requireObjective(
    const QuestDefinition& quest, const simulation::DefinitionId& objectiveId) {
    const auto* objective = findObjective(quest, objectiveId);
    if (!objective) { throw std::out_of_range("quest objective not found"); }
    return *objective;
}

const simulation::DefinitionId& scholarQuestId() {
    static const simulation::DefinitionId id{"quest.scholar.path"};
    return id;
}

QuestDefinition makeScholarQuestDefinition() {
    return {scholarQuestId(), "The Scholar's Path",
            {{simulation::DefinitionId{"quest.scholar.talk"}, QuestObjectiveKind::talk,
              simulation::DefinitionId{"npc.scholar"}, 1, "Speak with the scholar."},
             {simulation::DefinitionId{"quest.scholar.kill"}, QuestObjectiveKind::kill,
              simulation::DefinitionId{"enemy.evil_soldier"}, 1, "Defeat an evil soldier."},
             {simulation::DefinitionId{"quest.scholar.pickup"}, QuestObjectiveKind::pickup,
              simulation::DefinitionId{"pickup.heart"}, 1, "Find a heart pickup."}},
            {"story", "scholar"}};
}

} // namespace underworld::game::gameplay::quests
