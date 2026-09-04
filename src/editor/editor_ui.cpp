#include "editor/editor_ui.h"

#include "engine/core/color_rgba8.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/renderer_2d.h"

namespace underworld::editor {
namespace {
constexpr core::ColorRGBA8 panelColor{30, 34, 42, 255};
constexpr core::ColorRGBA8 borderColor{70, 78, 92, 255};
constexpr core::ColorRGBA8 buttonColor{48, 55, 67, 255};
constexpr core::ColorRGBA8 activeColor{62, 100, 148, 255};
}

void EditorUiContext::panel(core::RectI bounds) const {
    renderer_.fillRect(bounds, panelColor);
    renderer_.fillRect({bounds.x, bounds.y, bounds.width, 1}, borderColor);
    renderer_.fillRect({bounds.x, bounds.y + bounds.height - 1, bounds.width, 1}, borderColor);
    renderer_.fillRect({bounds.x, bounds.y, 1, bounds.height}, borderColor);
    renderer_.fillRect({bounds.x + bounds.width - 1, bounds.y, 1, bounds.height}, borderColor);
}

void EditorUiContext::label(std::string_view text, int x, int y) const {
    if (font_) { render::drawText(renderer_, *font_, text, x, y); }
}

bool EditorUiContext::button(core::RectI bounds, std::string_view text, bool active) const {
    const bool hovered = pointerInside(bounds);
    renderer_.fillRect(bounds, active ? activeColor : (hovered ? borderColor : buttonColor));
    label(text, bounds.x + 4, bounds.y + (bounds.height - 9) / 2);
    return hovered && input_.pointer.leftPressed;
}

bool EditorUiContext::toggle(core::RectI bounds, std::string_view text, bool value) const {
    return button(bounds, text, value);
}

bool EditorUiContext::pointerInside(core::RectI bounds) const noexcept {
    return input_.pointer.x >= bounds.x && input_.pointer.y >= bounds.y &&
           input_.pointer.x < bounds.x + bounds.width &&
           input_.pointer.y < bounds.y + bounds.height;
}

} // namespace underworld::editor
