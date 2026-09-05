#pragma once

#include "game/gameplay/quests/quest_model.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace underworld::game::gameplay::quests {

enum class QuestStatus { inactive, active, completed };

struct QuestObjectiveProgress final {
    simulation::DefinitionId objectiveId{};
    std::uint32_t currentCount{};
    [[nodiscard]] bool operator==(const QuestObjectiveProgress&) const noexcept = default;
};

struct QuestProgress final {
    simulation::DefinitionId questId{};
    QuestStatus status{QuestStatus::inactive};
    std::vector<QuestObjectiveProgress> objectives;
    [[nodiscard]] bool operator==(const QuestProgress&) const noexcept = default;
};

class QuestStateStore final {
public:
    // A quest absent from the store has the inactive status. Starting it creates
    // an independent progress record initialized from the immutable definition.
    [[nodiscard]] bool start(const QuestDefinition& definition);
    [[nodiscard]] bool reset(const simulation::DefinitionId& questId) noexcept;

    [[nodiscard]] QuestStatus status(
        const simulation::DefinitionId& questId) const noexcept;
    [[nodiscard]] const QuestProgress* find(
        const simulation::DefinitionId& questId) const noexcept;
    [[nodiscard]] const QuestProgress& require(
        const simulation::DefinitionId& questId) const;
    [[nodiscard]] std::size_t size() const noexcept { return progress_.size(); }

    // Progress is deliberately explicit about the definition it belongs to. This
    // keeps objective limits in immutable content and avoids copying definitions
    // into runtime state.
    [[nodiscard]] bool setObjectiveProgress(const QuestDefinition& definition,
                                            const simulation::DefinitionId& objectiveId,
                                            std::uint32_t count);
    [[nodiscard]] bool advanceObjective(const QuestDefinition& definition,
                                        const simulation::DefinitionId& objectiveId,
                                        std::uint32_t amount = 1);

private:
    [[nodiscard]] QuestProgress* findMutable(
        const simulation::DefinitionId& questId) noexcept;
    [[nodiscard]] QuestObjectiveProgress* findObjectiveProgressMutable(
        QuestProgress& progress, const simulation::DefinitionId& objectiveId) noexcept;
    static void refreshStatus(const QuestDefinition& definition, QuestProgress& progress);

    std::unordered_map<simulation::DefinitionId, QuestProgress,
                       simulation::DefinitionIdHash> progress_;
};

[[nodiscard]] const QuestObjectiveProgress* findObjectiveProgress(
    const QuestProgress& progress,
    const simulation::DefinitionId& objectiveId) noexcept;

} // namespace underworld::game::gameplay::quests
