#include "game/gameplay/dialogue/dialogue_session.h"

#include <algorithm>
#include <exception>

namespace underworld::game::gameplay::dialogue {

bool DialogueSession::begin(const simulation::DefinitionId& dialogueId,
                            std::string& error) {
    try {
        const auto& definition = catalog_->require(dialogueId);
        const auto* entry = findNode(definition, definition.entryNodeId);
        if (!entry) {
            error = "dialogue entry node is unavailable";
            return false;
        }
        dialogue_ = &definition;
        node_ = entry;
        pageIndex_ = 0;
        selectedChoice_ = 0;
        state_ = DialogueSessionState::text;
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

void DialogueSession::close() noexcept {
    dialogue_ = nullptr;
    node_ = nullptr;
    state_ = DialogueSessionState::closed;
    pageIndex_ = 0;
    selectedChoice_ = 0;
}

bool DialogueSession::handleCommand(const simulation::PlayerCommand& command) {
    if (!isOpen()) { return false; }
    if (command.actions.secondaryAttackPressed) {
        close();
        return true;
    }

    if (choicesVisible()) {
        if (command.movement.y != 0 && choiceCount() != 0) {
            const auto delta = command.movement.y < 0 ? -1 : 1;
            const auto next = static_cast<int>(selectedChoice_) + delta;
            selectedChoice_ = static_cast<std::size_t>(std::clamp(
                next, 0, static_cast<int>(choiceCount() - 1)));
        }
        if (command.actions.primaryAttackPressed || command.actions.interactPressed) {
            enterNode(node_->choices[selectedChoice_].targetNodeId);
        }
        return true;
    }

    if (!command.actions.primaryAttackPressed && !command.actions.interactPressed) {
        return true;
    }
    if (pageIndex_ + 1 < node_->pages.size()) {
        ++pageIndex_;
    } else if (!node_->choices.empty()) {
        showChoices();
    } else if (!node_->nextNodeId.empty()) {
        enterNode(node_->nextNodeId);
    } else {
        close();
    }
    return true;
}

std::string_view DialogueSession::speaker() const noexcept {
    return node_ ? std::string_view{node_->speaker} : std::string_view{};
}

std::string_view DialogueSession::currentPage() const noexcept {
    if (!node_ || pageIndex_ >= node_->pages.size()) { return {}; }
    return node_->pages[pageIndex_];
}

std::size_t DialogueSession::pageCount() const noexcept {
    return node_ ? node_->pages.size() : 0;
}

std::size_t DialogueSession::choiceCount() const noexcept {
    return node_ && choicesVisible() ? node_->choices.size() : 0;
}

std::string_view DialogueSession::choiceLabel(std::size_t index) const noexcept {
    if (!node_ || index >= node_->choices.size()) { return {}; }
    return node_->choices[index].label;
}

void DialogueSession::enterNode(const simulation::DefinitionId& nodeId) {
    node_ = &requireNode(*dialogue_, nodeId);
    pageIndex_ = 0;
    selectedChoice_ = 0;
    state_ = DialogueSessionState::text;
}

void DialogueSession::showChoices() noexcept {
    state_ = DialogueSessionState::choices;
    selectedChoice_ = 0;
}

} // namespace underworld::game::gameplay::dialogue
