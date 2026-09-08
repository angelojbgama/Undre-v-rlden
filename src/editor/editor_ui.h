#pragma once

#include "engine/core/geometry.h"
#include "editor/editor_localization.h"

#include <cstdint>
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
    std::uint64_t previewTicks{};
    std::string textInput;
};

class EditorUiContext final {
public:
    EditorUiContext(render::Renderer2D& renderer, const render::BitmapFont* font,
                    const EditorInputState& input,
                    const EditorLocalization& localization) noexcept
        : renderer_(renderer), font_(font), input_(input), localization_(localization) {}

    void panel(core::RectI bounds) const;
    void label(std::string_view text, int x, int y) const;
    void labelRaw(std::string_view text, int x, int y) const;
    [[nodiscard]] bool button(core::RectI bounds, std::string_view text,
                              bool active = false) const;
    [[nodiscard]] bool buttonRaw(core::RectI bounds, std::string_view text,
                                 bool active = false) const;
    [[nodiscard]] bool toggle(core::RectI bounds, std::string_view text, bool value) const;
    [[nodiscard]] bool textField(core::RectI bounds, std::string& value, bool active,
                                 std::size_t maximumLength = 240) const;
    [[nodiscard]] bool pointerInside(core::RectI bounds) const noexcept;

private:
    render::Renderer2D& renderer_;
    const render::BitmapFont* font_{};
    const EditorInputState& input_;
    const EditorLocalization& localization_;
};

} // namespace underworld::editor
