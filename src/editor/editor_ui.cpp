#include "editor/editor_ui.h"

#include "editor/editor_layout.h"
#include "editor/editor_text_layout.h"

#include "engine/core/color_rgba8.h"
#include "engine/core/utf8.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/renderer_2d.h"

#include <algorithm>
#include <vector>

namespace underworld::editor {
namespace {
constexpr core::ColorRGBA8 panelColor{30, 34, 42, 255};
constexpr core::ColorRGBA8 borderColor{70, 78, 92, 255};
constexpr core::ColorRGBA8 buttonColor{48, 55, 67, 255};
constexpr core::ColorRGBA8 activeColor{62, 100, 148, 255};
constexpr core::ColorRGBA8 tooltipColor{23, 27, 34, 255};

std::vector<std::size_t> codepointBoundaries(std::string_view text) {
    std::vector<std::size_t> result{0};
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t before = offset;
        static_cast<void>(core::decodeUtf8Codepoint(text, offset));
        if (offset == before) ++offset;
        result.push_back(offset);
    }
    return result;
}

struct VisibleTextRange final {
    std::size_t first{};
    std::size_t last{};
};

VisibleTextRange visibleTextRange(std::size_t count, std::size_t cursor,
                                  int maximumCharacters) noexcept {
    if (maximumCharacters <= 0) return {cursor, cursor};
    if (count <= static_cast<std::size_t>(maximumCharacters)) return {0, count};
    const auto capacity = static_cast<std::size_t>(maximumCharacters);
    const auto first = std::min(cursor > capacity ? cursor - capacity : std::size_t{},
                                count - capacity);
    return {first, first + capacity};
}

std::size_t cursorFromPointer(const VisibleTextRange& range, std::size_t count,
                              core::RectI bounds, int pointerX, int advance) noexcept {
    const int relative = pointerX - (bounds.x + 4);
    const auto column = relative <= 0 ? std::size_t{} :
        static_cast<std::size_t>((relative + advance / 2) / advance);
    return std::min(count, range.first + column);
}

void insertTextAtCursor(std::string& value, TextEditState& state,
                        std::string_view text, std::size_t maximumLength) {
    auto boundaries = codepointBoundaries(value);
    state.cursor = std::min(state.cursor, boundaries.size() - 1U);
    std::size_t offset = 0;
    while (offset < text.size() && boundaries.size() - 1U < maximumLength) {
        const std::size_t before = offset;
        const auto codepoint = core::decodeUtf8Codepoint(text, offset);
        if (offset == before) ++offset;
        if (!codepoint || *codepoint < 32U || *codepoint == 127U || *codepoint == '\n' ||
            *codepoint == '\r' || *codepoint == '\t') continue;
        std::string encoded;
        static_cast<void>(core::appendUtf8Codepoint(encoded, *codepoint));
        const auto byteOffset = boundaries[state.cursor];
        value.insert(byteOffset, encoded);
        ++state.cursor;
        boundaries = codepointBoundaries(value);
    }
}

void eraseBeforeCursor(std::string& value, TextEditState& state) {
    const auto boundaries = codepointBoundaries(value);
    state.cursor = std::min(state.cursor, boundaries.size() - 1U);
    if (state.cursor == 0) return;
    value.erase(boundaries[state.cursor - 1U], boundaries[state.cursor] -
                                             boundaries[state.cursor - 1U]);
    --state.cursor;
}

void eraseAtCursor(std::string& value, TextEditState& state) {
    const auto boundaries = codepointBoundaries(value);
    state.cursor = std::min(state.cursor, boundaries.size() - 1U);
    if (state.cursor >= boundaries.size() - 1U) return;
    value.erase(boundaries[state.cursor], boundaries[state.cursor + 1U] -
                                             boundaries[state.cursor]);
}
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
    const bool clicked = hovered && input_.pointer.leftPressed;
    auto& edit = *textEditState_;
    if (clicked || (active && edit.field != &value)) {
        edit.field = &value;
        edit.cursor = core::utf8CodepointCount(value);
    }
    const bool focused = active || clicked;
    const auto boundaries = codepointBoundaries(value);
    edit.cursor = std::min(edit.cursor, boundaries.size() - 1U);
    const int advance = font_ ? std::max(1, font_->advance()) : 7;
    const int innerWidth = std::max(0, bounds.width - 8);
    const auto visible = visibleTextRange(boundaries.size() - 1U, edit.cursor,
                                          innerWidth / advance);
    if (clicked) {
        edit.cursor = cursorFromPointer(visible, boundaries.size() - 1U, bounds,
                                        input_.pointer.x, advance);
    }
    if (focused) {
        edit.cursor = std::min(edit.cursor, boundaries.size() - 1U);
        if (input_.homePressed) edit.cursor = 0;
        if (input_.endPressed) edit.cursor = boundaries.size() - 1U;
        if (input_.leftPressed && edit.cursor > 0) --edit.cursor;
        if (input_.rightPressed && edit.cursor < boundaries.size() - 1U) ++edit.cursor;
        if (input_.backspacePressed) eraseBeforeCursor(value, edit);
        if (input_.deletePressed) eraseAtCursor(value, edit);
        insertTextAtCursor(value, edit, input_.textInput, maximumLength);
    }
    const auto renderedBoundaries = codepointBoundaries(value);
    edit.cursor = std::min(edit.cursor, renderedBoundaries.size() - 1U);
    const auto renderedRange = visibleTextRange(renderedBoundaries.size() - 1U, edit.cursor,
                                                innerWidth / advance);
    renderer_.fillRect(bounds, active ? activeColor : (hovered ? borderColor : buttonColor));
    if (font_) {
        const auto rendered = std::string_view(value).substr(
            renderedBoundaries[renderedRange.first],
            renderedBoundaries[renderedRange.last] - renderedBoundaries[renderedRange.first]);
        render::drawText(renderer_, *font_, rendered, bounds.x + 4,
                         bounds.y + (bounds.height - 9) / 2);
    }
    if (focused) {
        const int caretX = bounds.x + 4 +
            static_cast<int>((edit.cursor - renderedRange.first) *
                             static_cast<std::size_t>(advance));
        renderer_.fillRect({caretX, bounds.y + 3, 1, std::max(1, bounds.height - 6)},
                            core::ColorRGBA8{255, 255, 255, 255});
    }
    return clicked;
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
