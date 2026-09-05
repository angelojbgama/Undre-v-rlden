#pragma once

#include "engine/simulation/player_command.h"
#include "game/gameplay/dialogue/dialogue_model.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::game::gameplay::dialogue {

enum class DialogueSessionState { closed, text, choices };

class DialogueSession final {
public:
    DialogueSession(const DialogueCatalog& catalog, DialogueFlagSet& flags)
        : catalog_(&catalog), flags_(&flags) {}

    // Returns false and leaves the current session unchanged when the definition cannot
    // be opened. Invalid authored content is therefore reported at the boundary rather
    // than becoming a renderer-visible silent failure.
    [[nodiscard]] bool begin(const simulation::DefinitionId& dialogueId,
                             std::string& error);
    void close() noexcept;

    // A dialogue consumes the complete command while open, including movement. This
    // keeps player simulation and inventory routing outside the dialogue overlay.
    [[nodiscard]] bool handleCommand(const simulation::PlayerCommand& command);

    [[nodiscard]] bool isOpen() const noexcept { return state_ != DialogueSessionState::closed; }
    [[nodiscard]] DialogueSessionState state() const noexcept { return state_; }
    [[nodiscard]] std::string_view speaker() const noexcept;
    [[nodiscard]] std::string_view currentPage() const noexcept;
    [[nodiscard]] std::size_t pageIndex() const noexcept { return pageIndex_; }
    [[nodiscard]] std::size_t pageCount() const noexcept;
    [[nodiscard]] bool choicesVisible() const noexcept {
        return state_ == DialogueSessionState::choices;
    }
    [[nodiscard]] std::size_t choiceCount() const noexcept;
    [[nodiscard]] std::size_t selectedChoice() const noexcept { return selectedChoice_; }
    [[nodiscard]] std::string_view choiceLabel(std::size_t index) const noexcept;

private:
    void enterNode(const simulation::DefinitionId& nodeId);
    void showChoices() noexcept;

    const DialogueCatalog* catalog_{};
    DialogueFlagSet* flags_{};
    const DialogueDefinition* dialogue_{};
    const DialogueNode* node_{};
    DialogueSessionState state_{DialogueSessionState::closed};
    std::size_t pageIndex_{};
    std::size_t selectedChoice_{};
    std::vector<std::size_t> availableChoiceIndices_;
};

} // namespace underworld::game::gameplay::dialogue
