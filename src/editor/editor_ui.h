#pragma once

#include "engine/core/color_rgba8.h"
#include "engine/core/geometry.h"
#include "editor/editor_icons.h"
#include "editor/editor_localization.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace underworld::render { class BitmapFont; class Renderer2D; }

namespace underworld::editor {

struct PointerState final {
    int x{};
    int y{};
    bool leftDown{};
    bool leftPressed{};
    bool leftReleased{};
    bool middleDown{};
    bool middlePressed{};
    bool middleReleased{};
    int wheelDelta{};
};

struct EditorInputState final {
    PointerState pointer;
    bool shift{};
    bool control{};
    bool alt{};
    bool space{};
    bool deletePressed{};
    bool duplicatePressed{};
    bool undoPressed{};
    bool redoPressed{};
    bool homePressed{};
    bool enterPressed{};
    bool escapePressed{};
    bool backspacePressed{};
    bool focusLost{};
    bool up{};
    bool down{};
    bool left{};
    bool right{};
    bool leftPressed{};
    bool rightPressed{};
    bool endPressed{};
    std::uint64_t previewTicks{};
    std::string textInput;
};

struct TextEditState final {
    const std::string* field{};
    std::size_t cursor{};
};

class EditorUiContext final {
public:
    EditorUiContext(render::Renderer2D& renderer, const render::BitmapFont* font,
                    const EditorInputState& input,
                    const EditorLocalization& localization,
                    core::RectI canvas = {}, TextEditState* textEditState = nullptr) noexcept
        : renderer_(renderer), font_(font), input_(input), localization_(localization),
          canvas_(canvas),
          textEditState_(textEditState ? textEditState : &fallbackTextEditState_) {}

    void panel(core::RectI bounds) const;
    void fillRect(core::RectI bounds, core::ColorRGBA8 color) const noexcept;
    void label(std::string_view text, int x, int y) const;
    void labelRaw(std::string_view text, int x, int y) const;
    void labelInRect(core::RectI bounds, std::string_view text, bool fromEnd = false) const;
    void labelRawInRect(core::RectI bounds, std::string_view text, bool fromEnd = false) const;
    [[nodiscard]] bool button(core::RectI bounds, std::string_view text,
                              bool active = false) const;
    [[nodiscard]] bool buttonRaw(core::RectI bounds, std::string_view text,
                                 bool active = false) const;
    [[nodiscard]] bool buttonWithIcon(core::RectI bounds, EditorIcon icon,
                                      std::string_view text, bool active = false,
                                      std::optional<EditorTextId> tooltipId = std::nullopt) const;
    [[nodiscard]] bool iconButton(core::RectI bounds, EditorIcon icon,
                                  bool active = false,
                                  std::optional<EditorTextId> tooltipId = std::nullopt) const;
    [[nodiscard]] bool toggle(core::RectI bounds, std::string_view text, bool value) const;
    [[nodiscard]] bool textField(core::RectI bounds, std::string& value, bool active,
                                 std::size_t maximumLength = 240) const;
    [[nodiscard]] bool pointerInside(core::RectI bounds) const noexcept;
    void tooltip(core::RectI bounds, EditorTextId textId) const;
    void tooltipRaw(core::RectI bounds, std::string_view text) const;
    void drawTooltip() const;

private:
    struct TooltipRequest final {
        core::RectI anchor;
        std::string text;
    };

    render::Renderer2D& renderer_;
    const render::BitmapFont* font_{};
    const EditorInputState& input_;
    const EditorLocalization& localization_;
    core::RectI canvas_{};
    mutable TextEditState fallbackTextEditState_{};
    TextEditState* textEditState_{};
    mutable std::optional<TooltipRequest> tooltip_;
};

} // namespace underworld::editor
