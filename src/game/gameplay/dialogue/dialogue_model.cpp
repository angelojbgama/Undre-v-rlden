#include "game/gameplay/dialogue/dialogue_model.h"

#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay::dialogue {
namespace {

void validateNodeTargets(const DialogueDefinition& definition,
                         const DialogueNode& node) {
    if (node.id.empty() || node.pages.empty()) {
        throw std::invalid_argument("dialogue node requires an id and at least one page");
    }
    for (const auto& page : node.pages) {
        if (page.empty()) {
            throw std::invalid_argument("dialogue pages cannot be empty");
        }
    }
    if (!node.nextNodeId.empty() && !node.choices.empty()) {
        throw std::invalid_argument("dialogue node cannot have next node and choices");
    }
    if (!node.nextNodeId.empty() && !findNode(definition, node.nextNodeId)) {
        throw std::invalid_argument("dialogue node references an unknown next node");
    }
    for (const auto& choice : node.choices) {
        if (choice.label.empty() || choice.targetNodeId.empty() ||
            !findNode(definition, choice.targetNodeId)) {
            throw std::invalid_argument("dialogue choice is incomplete or targets an unknown node");
        }
        for (const auto& condition : choice.conditions) {
            if (condition.flagId.empty()) {
                throw std::invalid_argument("dialogue condition flag is empty");
            }
        }
        for (const auto& action : choice.actions) {
            if (action.flagId.empty()) {
                throw std::invalid_argument("dialogue action flag is empty");
            }
        }
    }
}

void validate(const DialogueDefinition& definition) {
    if (definition.id.empty() || definition.entryNodeId.empty() || definition.nodes.empty()) {
        throw std::invalid_argument("dialogue definition is incomplete");
    }
    if (!findNode(definition, definition.entryNodeId)) {
        throw std::invalid_argument("dialogue entry node is unknown");
    }
    for (std::size_t i = 0; i < definition.nodes.size(); ++i) {
        if (definition.nodes[i].id.empty()) {
            throw std::invalid_argument("dialogue node id is empty");
        }
        for (std::size_t previous = 0; previous < i; ++previous) {
            if (definition.nodes[previous].id == definition.nodes[i].id) {
                throw std::logic_error("duplicate dialogue node id");
            }
        }
    }
    for (const auto& node : definition.nodes) {
        validateNodeTargets(definition, node);
    }
}

} // namespace

void DialogueCatalog::add(DialogueDefinition definition) {
    validate(definition);
    const auto [position, inserted] = definitions_.emplace(definition.id, std::move(definition));
    static_cast<void>(position);
    if (!inserted) { throw std::logic_error("duplicate dialogue definition id"); }
}

const DialogueDefinition* DialogueCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}

const DialogueDefinition& DialogueCatalog::require(
    const simulation::DefinitionId& id) const {
    const auto* definition = find(id);
    if (!definition) { throw std::out_of_range("dialogue definition not found"); }
    return *definition;
}

const DialogueNode* findNode(const DialogueDefinition& dialogue,
                             const simulation::DefinitionId& nodeId) noexcept {
    for (const auto& node : dialogue.nodes) {
        if (node.id == nodeId) { return &node; }
    }
    return nullptr;
}

const DialogueNode& requireNode(const DialogueDefinition& dialogue,
                                const simulation::DefinitionId& nodeId) {
    const auto* node = findNode(dialogue, nodeId);
    if (!node) { throw std::out_of_range("dialogue node not found"); }
    return *node;
}

const simulation::DefinitionId& guardDialogueId() {
    static const simulation::DefinitionId id{"dialogue.guard.greeting"};
    return id;
}

const simulation::DefinitionId& scholarDialogueId() {
    static const simulation::DefinitionId id{"dialogue.scholar.greeting"};
    return id;
}

const simulation::DefinitionId& scholarAskedFlagId() {
    static const simulation::DefinitionId id{"dialogue.scholar.asked"};
    return id;
}

DialogueDefinition makeGuardDialogueDefinition() {
    const simulation::DefinitionId entry{"guard.entry"};
    const simulation::DefinitionId response{"guard.response"};
    return {guardDialogueId(), entry,
            {{entry, "Guard", {"Halt, traveler.", "The gallery lies beyond the eastern gate."},
              response, {}},
             {response, "Guard", {"Keep your blade ready."}, {}, {}}}};
}

DialogueDefinition makeScholarDialogueDefinition() {
    const simulation::DefinitionId entry{"scholar.entry"};
    const simulation::DefinitionId left{"scholar.left"};
    const simulation::DefinitionId right{"scholar.right"};
    return {scholarDialogueId(), entry,
            {{entry, "Scholar", {"The old stones remember every footstep."}, {},
              {{"Ask about the dungeon", left, {},
                {{DialogueActionKind::setFlag, scholarAskedFlagId()}}},
               {"Say farewell", right,
                {{DialogueConditionKind::flagNotSet, scholarAskedFlagId()}}, {}},
               {"Recall the lesson", left,
                {{DialogueConditionKind::flagSet, scholarAskedFlagId()}}, {}}}},
             {left, "Scholar", {"Study the walls, but trust the path beneath your feet."}, {}, {}},
             {right, "Scholar", {"Then walk carefully, friend."}, {}, {}}}};
}

} // namespace underworld::game::gameplay::dialogue
