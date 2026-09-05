#pragma once

#include "engine/simulation/definition_id.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace underworld::game::gameplay::quests {

enum class QuestObjectiveKind { talk, kill, pickup, enter, open, deliver };

struct QuestObjectiveDefinition final {
    simulation::DefinitionId id{};
    QuestObjectiveKind kind{QuestObjectiveKind::talk};
    simulation::DefinitionId targetId{};
    std::uint32_t requiredCount{1};
    std::string description;
    [[nodiscard]] bool operator==(const QuestObjectiveDefinition&) const noexcept = default;
};

struct QuestDefinition final {
    simulation::DefinitionId id{};
    std::string title;
    std::vector<QuestObjectiveDefinition> objectives;
    std::vector<std::string> tags;
    [[nodiscard]] bool operator==(const QuestDefinition&) const noexcept = default;
};

class QuestCatalog final {
public:
    void add(QuestDefinition definition);
    [[nodiscard]] const QuestDefinition* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const QuestDefinition& require(
        const simulation::DefinitionId& id) const;
    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }

private:
    std::unordered_map<simulation::DefinitionId, QuestDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

[[nodiscard]] const QuestObjectiveDefinition* findObjective(
    const QuestDefinition& quest, const simulation::DefinitionId& objectiveId) noexcept;

[[nodiscard]] const QuestObjectiveDefinition& requireObjective(
    const QuestDefinition& quest, const simulation::DefinitionId& objectiveId);

[[nodiscard]] const simulation::DefinitionId& scholarQuestId();
[[nodiscard]] QuestDefinition makeScholarQuestDefinition();

} // namespace underworld::game::gameplay::quests
