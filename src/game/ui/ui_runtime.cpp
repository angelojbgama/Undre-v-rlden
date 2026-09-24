#include "game/ui/ui_runtime.h"

#include "engine/render/sprite.h"

#include <algorithm>

namespace underworld::game::ui {
namespace {

void collectFocusables(const NodeDefinition& node,
                       std::vector<const NodeDefinition*>& out) {
    if (!node.actions.empty()) { out.push_back(&node); }
    for (const auto& child : node.children) {
        collectFocusables(child, out);
    }
}

} // namespace

void UiRuntime::setMenuScreen(const ScreenDefinition* screen) noexcept {
    menu_ = screen;
    open_ = false;
    active_ = nullptr;
    focusables_.clear();
    focusIndex_ = 0;
}

void UiRuntime::setSavesScreen(const ScreenDefinition* screen) noexcept {
    saves_ = screen;
}

void UiRuntime::setHelpScreen(const ScreenDefinition* screen) noexcept {
    help_ = screen;
}

void UiRuntime::setTitleScreen(const ScreenDefinition* screen) noexcept {
    title_ = screen;
    titleMode_ = screen != nullptr;
    open_ = titleMode_;
    active_ = screen;
    focusables_.clear();
    focusIndex_ = 0;
    rebuildFocusables();
}

void UiRuntime::exitTitleMode() noexcept {
    titleMode_ = false;
    open_ = false;
    active_ = nullptr;
    focusables_.clear();
    focusIndex_ = 0;
}

void UiRuntime::setGameOverScreen(const ScreenDefinition* screen) noexcept {
    gameover_ = screen;
}

void UiRuntime::enterGameOver() noexcept {
    if (gameover_ == nullptr || gameOverMode_) { return; }
    titleMode_ = false;  // death overrides a still-open title (defensive)
    gameOverMode_ = true;
    open_ = true;
    active_ = gameover_;
    focusables_.clear();
    focusIndex_ = 0;
    rebuildFocusables();
}

void UiRuntime::exitGameOver() noexcept {
    gameOverMode_ = false;
    open_ = false;
    active_ = nullptr;
    focusables_.clear();
    focusIndex_ = 0;
}

void UiRuntime::setActionSink(ActionSink sink) {
    sink_ = std::move(sink);
}

const NodeDefinition* UiRuntime::focusedNode() const noexcept {
    if (!open_ || focusIndex_ >= focusables_.size()) { return nullptr; }
    return focusables_[focusIndex_];
}

void UiRuntime::rebuildFocusables() {
    focusables_.clear();
    focusIndex_ = 0;
    const auto* screen = active_ != nullptr ? active_ : menu_;
    if (screen != nullptr && open_) {
        collectFocusables(screen->root, focusables_);
    }
}

void UiRuntime::update(const platform::InputState& input, bool interactionAllowed) {
    const bool menuEdge = input.menuPressed && !previous_.menuPressed;
    // The title/game-over shells have no gameplay to fall back to, so the
    // menu toggle is inert there and only navigation/activation runs.
    if (!titleMode_ && !gameOverMode_ && interactionAllowed && menuEdge) {
        open_ = !open_ && menu_ != nullptr;
        active_ = open_ ? menu_ : nullptr;
        rebuildFocusables();
        focusIndex_ = 0;
    }
    if (open_ && interactionAllowed) {
        const bool up = input.moveUp && !previous_.moveUp;
        const bool down = input.moveDown && !previous_.moveDown;
        if (!focusables_.empty()) {
            if (up) {
                focusIndex_ = (focusIndex_ + focusables_.size() - 1) % focusables_.size();
            }
            if (down) {
                focusIndex_ = (focusIndex_ + 1) % focusables_.size();
            }
        }
        const bool activate = input.interactPressed && !previous_.interactPressed;
        if (activate) {
            activateFocused();
        }
    }
    previous_ = input;
}

void UiRuntime::activateFocused() {
    const auto* node = focusedNode();
    if (node == nullptr || node->actions.empty()) { return; }
    const ActionId action = node->actions.front().action;
    if (action == ActionId::screenClose) {
        if (gameOverMode_) {
            // The game-over shell offers retry only; close has no target.
            return;
        }
        if (titleMode_) {
            // BACK from the saves screen returns to the title; the title
            // itself has nothing to close into.
            if (active_ == saves_) {
                active_ = title_;
                rebuildFocusables();
                focusIndex_ = 0;
            }
            return;
        }
        open_ = false;
        active_ = nullptr;
        return;
    }
    if (action == ActionId::screenOpenSaves) {
        active_ = saves_ != nullptr ? saves_ : menu_;
        rebuildFocusables();
        focusIndex_ = 0;
        return;
    }
    if (action == ActionId::screenOpenHelp) {
        active_ = help_ != nullptr ? help_ : menu_;
        rebuildFocusables();
        focusIndex_ = 0;
        return;
    }
    if (sink_) {
        sink_(action);
    }
}

void UiRuntime::render(const UiBindingResolver& resolver, const UiVisualContext& context,
                       render::Renderer2D& renderer) const {
    if (!open_) { return; }
    const auto* screen = active_ != nullptr ? active_ : menu_;
    if (screen == nullptr) { return; }
    UiPresenter presenter;
    presenter.render(*screen, resolver, context, renderer);
}

} // namespace underworld::game::ui
