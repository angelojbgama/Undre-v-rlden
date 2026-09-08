#pragma once

#include "engine/core/geometry.h"

namespace underworld::editor {

struct EditorPanelWidths final { int left{190}; int right{250}; };

struct EditorLayoutMetrics final {
    static constexpr int defaultLeft = 190;
    static constexpr int defaultRight = 250;
    static constexpr int minimumLeft = 190;
    static constexpr int minimumRight = 250;
    static constexpr int minimumViewport = 240;
    static constexpr int maximumPanel = 600;
    static constexpr int statusHeight = 22;
};

[[nodiscard]] EditorPanelWidths clampPanelWidths(int windowWidth,
                                                  EditorPanelWidths requested) noexcept;

struct EditorShellLayout final {
    core::RectI left;
    core::RectI viewport;
    core::RectI right;
    core::RectI status;
    EditorPanelWidths widths;
};

[[nodiscard]] EditorShellLayout makeEditorShellLayout(
    int windowWidth, int windowHeight, EditorPanelWidths requested) noexcept;

} // namespace underworld::editor
