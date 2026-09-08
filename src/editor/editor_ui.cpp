#include "editor/editor_ui.h"

#include "editor/editor_layout.h"
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
constexpr core::ColorRGBA8 tooltipColor{23, 27, 34, 255};
}

void EditorUiContext::panel(core::RectI bounds) const {
    renderer_.fillRect(bounds, panelColor);
    renderer_.fillRect({bounds.x, bounds.y, bounds.width, 1}, borderColor);
    renderer_.fillRect({bounds.x, bounds.y + bounds.height - 1, bounds.width, 1}, borderColor);
    renderer_.fillRect({bounds.x, bounds.y, 1, bounds.height}, borderColor);
    renderer_.fillRect({bounds.x + bounds.width - 1, bounds.y, 1, bounds.height}, borderColor);
}

void EditorUiContext::fillRect(core::RectI bounds, core::ColorRGBA8 color) const noexcept {
    renderer_.fillRect(bounds, color);
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

bool EditorUiContext::buttonWithIcon(core::RectI bounds, EditorIcon icon,
                                     std::string_view text, bool active,
                                     std::optional<EditorTextId> tooltipId) const {
    const bool hovered = pointerInside(bounds);
    renderer_.fillRect(bounds, active ? activeColor : (hovered ? borderColor : buttonColor));
    const int iconWidth = std::min(EditorIconMetrics::size + 6, std::max(0, bounds.width));
    drawEditorIcon(renderer_, icon, {bounds.x + 3, bounds.y, iconWidth, bounds.height},
                   active, false, hovered);
    labelInRect({bounds.x + iconWidth, bounds.y,
                 std::max(0, bounds.width - iconWidth), bounds.height}, text);
    if (tooltipId && hovered) tooltip(bounds, *tooltipId);
    return hovered && input_.pointer.leftPressed;
}

bool EditorUiContext::iconButton(core::RectI bounds, EditorIcon icon, bool active,
                                 std::optional<EditorTextId> tooltipId) const {
    const bool hovered = pointerInside(bounds);
    renderer_.fillRect(bounds, active ? activeColor : (hovered ? borderColor : buttonColor));
    drawEditorIcon(renderer_, icon, bounds, active, false, hovered);
    if (tooltipId && hovered) tooltip(bounds, *tooltipId);
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

void EditorUiContext::tooltip(core::RectI bounds, EditorTextId textId) const {
    tooltipRaw(bounds, localization_.text(textId));
}

void EditorUiContext::tooltipRaw(core::RectI bounds, std::string_view text) const {
    if (!pointerInside(bounds) || text.empty() || tooltip_) return;
    tooltip_ = TooltipRequest{bounds, std::string(text)};
}

void EditorUiContext::drawTooltip() const {
    if (!tooltip_ || !font_ || canvas_.empty()) return;
    const int padding = 4;
    const int height = font_->lineHeight() + padding * 2;
    const int maxWidth = std::max(0, canvas_.width - padding * 2);
    const int requestedWidth = measureTextWidth(tooltip_->text, font_->advance()) + padding * 2;
    const int width = std::min(maxWidth, std::max(24, requestedWidth));
    if (width <= 0) return;
    const auto bounds = placeTooltip(tooltip_->anchor, width, height,
                                     inset(canvas_, padding));
    renderer_.fillRect(bounds, tooltipColor);
    renderer_.fillRect({bounds.x, bounds.y, bounds.width, 1}, borderColor);
    renderer_.fillRect({bounds.x, bounds.y + bounds.height - 1, bounds.width, 1}, borderColor);
    renderer_.fillRect({bounds.x, bounds.y, 1, bounds.height}, borderColor);
    renderer_.fillRect({bounds.x + bounds.width - 1, bounds.y, 1, bounds.height}, borderColor);
    labelRawInRect(bounds, tooltip_->text);
}

} // namespace underworld::editor
