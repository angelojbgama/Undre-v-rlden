#pragma once

#include "engine/platform/input_state.h"
#include "game/ui/ui_presenter.h"

#include <functional>
#include <vector>

namespace underworld::game::ui {

// UI-5 shell state: opens/closes the authored menu screen, moves keyboard
// focus across its actionable nodes and dispatches their actions. Pure
// presentation state — gameplay actions flow to the shell's sink and become
// intents for the current tick's command; the gameplay stays authoritative.
class UiRuntime final {
public:
    using ActionSink = std::function<void(ActionId)>;

    void setMenuScreen(const ScreenDefinition* screen) noexcept;
    void setActionSink(ActionSink sink);

    [[nodiscard]] bool menuOpen() const noexcept { return open_; }
    [[nodiscard]] const NodeDefinition* focusedNode() const noexcept;

    // Processes one input tick. Edges derive from the previous snapshot, so
    // this must run exactly once per tick; `interactionAllowed` keeps the menu
    // from opening over dialogue/scenes while still advancing the snapshot.
    void update(const platform::InputState& input, bool interactionAllowed);

    void render(const UiBindingResolver& resolver, const UiVisualContext& context,
                render::Renderer2D& renderer) const;

private:
    void rebuildFocusables();
    void activateFocused();

    const ScreenDefinition* menu_{};
    bool open_{};
    std::size_t focusIndex_{};
    std::vector<const NodeDefinition*> focusables_;
    platform::InputState previous_{};
    ActionSink sink_;
};

} // namespace underworld::game::ui
