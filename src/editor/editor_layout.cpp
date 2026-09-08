#include "editor/editor_layout.h"

#include <algorithm>

namespace underworld::editor {

EditorPanelWidths clampPanelWidths(int windowWidth, EditorPanelWidths requested) noexcept {
    const int width = std::max(1, windowWidth);
    const int availablePanels = std::max(0, width - EditorLayoutMetrics::minimumViewport);
    const int leftMinimum = std::min(EditorLayoutMetrics::minimumLeft,
                                     availablePanels / 2);
    const int rightMinimum = std::min(EditorLayoutMetrics::minimumRight,
                                      std::max(0, availablePanels - leftMinimum));
    EditorPanelWidths result;
    result.left = std::clamp(requested.left, leftMinimum,
                             std::min(EditorLayoutMetrics::maximumPanel,
                                      std::max(leftMinimum, availablePanels - rightMinimum)));
    const int rightMaximum = std::max(rightMinimum, availablePanels - result.left);
    result.right = std::clamp(requested.right, rightMinimum,
                              std::min(EditorLayoutMetrics::maximumPanel, rightMaximum));
    if (result.left + result.right > width - 1) {
        result.right = std::max(0, width - 1 - result.left);
        if (result.right < rightMinimum) {
            result.left = std::max(0, width - 1 - rightMinimum);
            result.right = std::max(0, width - 1 - result.left);
        }
    }
    return result;
}

EditorShellLayout makeEditorShellLayout(int windowWidth, int windowHeight,
                                        EditorPanelWidths requested) noexcept {
    const int width = std::max(1, windowWidth);
    const int height = std::max(1, windowHeight);
    const auto panels = clampPanelWidths(width, requested);
    const int centerWidth = std::max(1, width - panels.left - panels.right);
    const int contentHeight = std::max(1, height - EditorLayoutMetrics::statusHeight);
    return {{0, 0, panels.left, contentHeight},
            {panels.left, 0, centerWidth, contentHeight},
            {panels.left + centerWidth, 0, panels.right, contentHeight},
            {0, contentHeight, width, std::min(EditorLayoutMetrics::statusHeight, height)},
            panels};
}

} // namespace underworld::editor
