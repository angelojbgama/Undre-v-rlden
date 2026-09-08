#include "editor/editor_ui.h"

#include "editor/editor_text_layout.h"

#include "engine/core/color_rgba8.h"
#include "engine/core/utf8.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/renderer_2d.h"

#include <algorithm>

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
    if (font_) {
        const auto localized = localization_.localize(text);
        render::drawText(renderer_, *font_, localized, x, y);
    }
}

void EditorUiContext::labelRaw(std::string_view text, int x, int y) const {
    if (font_) render::drawText(renderer_, *font_, text, x, y);
}

void EditorUiContext::labelInRect(core::RectI bounds, std::string_view text, bool fromEnd) const {
    if (!font_) return;
    const auto localized = localization_.localize(text);
    render::drawText(renderer_, *font_, fitText(localized, std::max(0, bounds.width - 8), fromEnd),
                     bounds.x + 4, bounds.y + (bounds.height - 9) / 2);
}

void EditorUiContext::labelRawInRect(core::RectI bounds, std::string_view text, bool fromEnd) const {
    if (!font_) return;
    render::drawText(renderer_, *font_, fitText(text, std::max(0, bounds.width - 8), fromEnd),
                     bounds.x + 4, bounds.y + (bounds.height - 9) / 2);
}

bool EditorUiContext::button(core::RectI bounds, std::string_view text, bool active) const {
    const bool hovered = pointerInside(bounds);
    renderer_.fillRect(bounds, active ? activeColor : (hovered ? borderColor : buttonColor));
    labelInRect(bounds, text);
    return hovered && input_.pointer.leftPressed;
}

bool EditorUiContext::buttonRaw(core::RectI bounds, std::string_view text, bool active) const {
    const bool hovered = pointerInside(bounds);
    renderer_.fillRect(bounds, active ? activeColor : (hovered ? borderColor : buttonColor));
    labelRawInRect(bounds, text);
    return hovered && input_.pointer.leftPressed;
}

bool EditorUiContext::toggle(core::RectI bounds, std::string_view text, bool value) const {
    return button(bounds, text, value);
}

bool EditorUiContext::textField(core::RectI bounds, std::string& value, bool active,
                               std::size_t maximumLength) const {
    const bool hovered = pointerInside(bounds);
    renderer_.fillRect(bounds, active ? activeColor : (hovered ? borderColor : buttonColor));
    if (font_) render::drawText(renderer_, *font_, fitText(value, std::max(0, bounds.width - 8), active, font_->advance()),
                                bounds.x + 4, bounds.y + (bounds.height - 9) / 2);
    if (active) {
        std::size_t offset = 0;
        while (offset < input_.textInput.size() && core::utf8CodepointCount(value) < maximumLength) {
            const std::size_t before = offset;
            const auto codepoint = core::decodeUtf8Codepoint(input_.textInput, offset);
            if (offset == before) ++offset;
            if (codepoint && *codepoint >= 32U && *codepoint != 127U && *codepoint != '\n' &&
                *codepoint != '\r' && *codepoint != '\t') {
                static_cast<void>(core::appendUtf8Codepoint(value, *codepoint));
            }
        }
        if (input_.backspacePressed) static_cast<void>(core::eraseLastUtf8Codepoint(value));
    }
    return hovered && input_.pointer.leftPressed;
}

bool EditorUiContext::pointerInside(core::RectI bounds) const noexcept {
    return input_.pointer.x >= bounds.x && input_.pointer.y >= bounds.y &&
           input_.pointer.x < bounds.x + bounds.width &&
           input_.pointer.y < bounds.y + bounds.height;
}

} // namespace underworld::editor
