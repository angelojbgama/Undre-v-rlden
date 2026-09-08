#pragma once

#include "engine/core/geometry.h"

namespace underworld::render { class Renderer2D; }

namespace underworld::editor {

enum class EditorIcon {
    add,
    remove,
    visibilityOn,
    visibilityOff,
    lock,
    unlock,
    dragHandle,
    moveUp,
    moveDown,
    play,
    save,
    folder,
    map,
    link,
    warning,
};

struct EditorIconMetrics final {
    static constexpr int size = 13;
};

[[nodiscard]] bool isEditorIconSupported(EditorIcon icon) noexcept;
void drawEditorIcon(render::Renderer2D& renderer, EditorIcon icon, core::RectI bounds,
                    bool active = false, bool disabled = false, bool hovered = false) noexcept;

} // namespace underworld::editor
