#include "editor/editor_icons.h"

#include "engine/core/color_rgba8.h"
#include "engine/render/renderer_2d.h"

#include <algorithm>

namespace underworld::editor {
namespace {
constexpr core::ColorRGBA8 iconColor{213, 220, 232, 255};
constexpr core::ColorRGBA8 activeIconColor{255, 232, 132, 255};
constexpr core::ColorRGBA8 disabledIconColor{110, 118, 130, 255};
constexpr core::ColorRGBA8 hoverIconColor{240, 246, 255, 255};

void pixel(render::Renderer2D& renderer, core::RectI bounds, int x, int y,
           int width, int height, core::ColorRGBA8 color) noexcept {
    renderer.fillRect({bounds.x + x, bounds.y + y, width, height}, color);
}

void horizontalLine(render::Renderer2D& renderer, core::RectI bounds, int y,
                    int left, int right, core::ColorRGBA8 color) noexcept {
    if (right >= left) pixel(renderer, bounds, left, y, right - left + 1, 1, color);
}

} // namespace

bool isEditorIconSupported(EditorIcon icon) noexcept {
    switch (icon) {
    case EditorIcon::add:
    case EditorIcon::remove:
    case EditorIcon::visibilityOn:
    case EditorIcon::visibilityOff:
    case EditorIcon::lock:
    case EditorIcon::unlock:
    case EditorIcon::dragHandle:
    case EditorIcon::moveUp:
    case EditorIcon::moveDown:
    case EditorIcon::play:
    case EditorIcon::save:
    case EditorIcon::folder:
    case EditorIcon::map:
    case EditorIcon::link:
    case EditorIcon::warning:
        return true;
    }
    return false;
}

void drawEditorIcon(render::Renderer2D& renderer, EditorIcon icon, core::RectI bounds,
                    bool active, bool disabled, bool hovered) noexcept {
    // Editor affordances stay readable even when the bitmap font lacks a glyph.
    if (bounds.empty()) return;
    const auto color = disabled ? disabledIconColor :
        (active ? activeIconColor : (hovered ? hoverIconColor : iconColor));
    const int size = std::min({EditorIconMetrics::size, bounds.width, bounds.height});
    if (size < EditorIconMetrics::size) return;
    const core::RectI iconBounds{bounds.x + (bounds.width - size) / 2,
                                 bounds.y + (bounds.height - size) / 2, size, size};
    const auto p = [&](int x, int y, int width, int height) {
        pixel(renderer, iconBounds, x, y, width, height, color);
    };
    switch (icon) {
    case EditorIcon::add:
        p(6, 2, 1, 9); p(2, 6, 9, 1); break;
    case EditorIcon::remove:
        p(2, 6, 9, 1); break;
    case EditorIcon::visibilityOn:
    case EditorIcon::visibilityOff:
        p(1, 6, 2, 1); p(3, 4, 2, 1); p(5, 3, 3, 1);
        p(8, 4, 2, 1); p(10, 6, 2, 1); p(8, 8, 2, 1);
        p(5, 9, 3, 1); p(3, 8, 2, 1); p(5, 5, 3, 3);
        if (icon == EditorIcon::visibilityOff) {
            for (int offset = 1; offset < 11; ++offset) {
                p(offset, offset, 1, 1);
            }
        }
        break;
    case EditorIcon::lock:
    case EditorIcon::unlock:
        p(3, 6, 7, 6); p(4, 4, 1, 3); p(8, 4, 1, 3);
        horizontalLine(renderer, iconBounds, 3, 5, 7, color);
        if (icon == EditorIcon::unlock) { p(8, 3, 1, 2); p(9, 2, 1, 2); }
        p(6, 8, 1, 3); break;
    case EditorIcon::dragHandle:
        p(3, 2, 2, 2); p(8, 2, 2, 2); p(3, 5, 2, 2); p(8, 5, 2, 2);
        p(3, 8, 2, 2); p(8, 8, 2, 2); break;
    case EditorIcon::moveUp:
        p(6, 2, 1, 9); p(4, 4, 5, 1); p(3, 5, 2, 1); p(8, 5, 2, 1); break;
    case EditorIcon::moveDown:
        p(6, 2, 1, 9); p(4, 8, 5, 1); p(3, 7, 2, 1); p(8, 7, 2, 1); break;
    case EditorIcon::play:
        p(3, 2, 2, 9); p(5, 4, 2, 5); p(7, 5, 2, 3); p(9, 6, 1, 1); break;
    case EditorIcon::save:
        p(2, 2, 9, 9); p(4, 3, 4, 3); p(4, 8, 5, 2); p(5, 9, 3, 1); break;
    case EditorIcon::folder:
        p(1, 4, 11, 7); p(2, 3, 4, 2); p(4, 2, 3, 2); break;
    case EditorIcon::map:
        p(2, 2, 3, 9); p(5, 3, 1, 7); p(6, 2, 3, 9); p(9, 3, 1, 7); p(10, 2, 1, 9); break;
    case EditorIcon::link:
        p(2, 5, 4, 3); p(7, 5, 4, 3); p(5, 6, 3, 1); p(3, 4, 2, 1); p(8, 8, 2, 1); break;
    case EditorIcon::warning:
        p(6, 1, 1, 2); p(5, 3, 3, 1); p(4, 4, 5, 1); p(3, 5, 7, 1);
        p(2, 6, 9, 5); p(6, 7, 1, 2); p(6, 10, 1, 1); break;
    }
}

} // namespace underworld::editor
