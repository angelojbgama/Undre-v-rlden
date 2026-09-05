#pragma once

#include "engine/simulation/definition_id.h"
#include "game/gameplay/dialogue/dialogue_flags.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace underworld::game::gameplay::dialogue {

// Dialogue data is renderer- and input-independent. A page is already a unit that
// the future DialogueSession can present and advance without splitting strings at
// runtime.
enum class DialogueConditionKind { flagSet, flagNotSet };
struct DialogueCondition final {
    DialogueConditionKind kind{DialogueConditionKind::flagSet};
    simulation::DefinitionId flagId{};
};

enum class DialogueActionKind { setFlag, clearFlag };
struct DialogueAction final {
    DialogueActionKind kind{DialogueActionKind::setFlag};
    simulation::DefinitionId flagId{};
};

struct DialogueChoice final {
    std::string label;
    simulation::DefinitionId targetNodeId{};
    std::vector<DialogueCondition> conditions;
    std::vector<DialogueAction> actions;
    [[nodiscard]] bool operator==(const DialogueChoice&) const noexcept = default;
};

struct DialogueNode final {
    simulation::DefinitionId id{};
    std::string speaker;
    std::vector<std::string> pages;
    simulation::DefinitionId nextNodeId{};
    std::vector<DialogueChoice> choices;
    [[nodiscard]] bool operator==(const DialogueNode&) const noexcept = default;
};

struct DialogueDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId entryNodeId{};
    std::vector<DialogueNode> nodes;
    [[nodiscard]] bool operator==(const DialogueDefinition&) const noexcept = default;
};

class DialogueCatalog final {
public:
    void add(DialogueDefinition definition);
    [[nodiscard]] const DialogueDefinition* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const DialogueDefinition& require(
        const simulation::DefinitionId& id) const;
    [[nodiscard]] std::size_t size() const noexcept { return definitions_.size(); }

private:
    std::unordered_map<simulation::DefinitionId, DialogueDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

[[nodiscard]] const DialogueNode* findNode(
    const DialogueDefinition& dialogue,
    const simulation::DefinitionId& nodeId) noexcept;

[[nodiscard]] const DialogueNode& requireNode(
    const DialogueDefinition& dialogue,
    const simulation::DefinitionId& nodeId);

[[nodiscard]] const simulation::DefinitionId& guardDialogueId();
[[nodiscard]] const simulation::DefinitionId& scholarDialogueId();
[[nodiscard]] const simulation::DefinitionId& scholarAskedFlagId();
[[nodiscard]] DialogueDefinition makeGuardDialogueDefinition();
[[nodiscard]] DialogueDefinition makeScholarDialogueDefinition();

} // namespace underworld::game::gameplay::dialogue
