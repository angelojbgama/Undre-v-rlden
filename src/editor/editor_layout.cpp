#include "editor/editor_layout.h"

#include <algorithm>
#include <cstdint>

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

core::RectI inset(core::RectI bounds, int padding) noexcept {
    const int safePadding = std::max(0, padding);
    const int diameter = safePadding * 2;
    return {bounds.x + safePadding, bounds.y + safePadding,
            std::max(0, bounds.width - diameter), std::max(0, bounds.height - diameter)};
}

std::vector<core::RectI> equalColumns(core::RectI bounds, std::size_t count, int spacing) {
    std::vector<core::RectI> result;
    if (count == 0) return result;
    result.reserve(count);
    const int safeSpacing = std::max(0, spacing);
    const auto gaps = count > 1 ? static_cast<std::int64_t>(count - 1) * safeSpacing : 0;
    const auto available = std::max<std::int64_t>(0, static_cast<std::int64_t>(bounds.width) - gaps);
    const auto baseWidth = available / static_cast<std::int64_t>(count);
    const auto remainder = available % static_cast<std::int64_t>(count);
    int x = bounds.x;
    for (std::size_t index = 0; index < count; ++index) {
        const int columnWidth = static_cast<int>(baseWidth +
            (static_cast<std::int64_t>(index) < remainder ? 1 : 0));
        result.push_back({x, bounds.y, columnWidth, std::max(0, bounds.height)});
        x += columnWidth + safeSpacing;
    }
    return result;
}

EditorSplitLayout splitFixedLeft(core::RectI bounds, int fixedWidth, int spacing) noexcept {
    const int safeSpacing = std::max(0, spacing);
    const int firstWidth = std::clamp(fixedWidth, 0, std::max(0, bounds.width));
    const int secondX = bounds.x + firstWidth + std::min(safeSpacing,
                                                          std::max(0, bounds.width - firstWidth));
    return {{bounds.x, bounds.y, firstWidth, std::max(0, bounds.height)},
            {secondX, bounds.y,
             std::max(0, bounds.width - (secondX - bounds.x)), std::max(0, bounds.height)}};
}

EditorSplitLayout splitFixedRight(core::RectI bounds, int fixedWidth, int spacing) noexcept {
    const int safeSpacing = std::max(0, spacing);
    const int secondWidth = std::clamp(fixedWidth, 0, std::max(0, bounds.width));
    const int secondX = bounds.x + std::max(0, bounds.width - secondWidth);
    const int firstWidth = std::max(0, secondX - bounds.x -
        std::min(safeSpacing, std::max(0, secondX - bounds.x)));
    const int firstEnd = bounds.x + firstWidth;
    const int actualSecondX = std::min(secondX, firstEnd + safeSpacing);
    return {{bounds.x, bounds.y, firstWidth, std::max(0, bounds.height)},
            {actualSecondX, bounds.y,
             std::max(0, bounds.x + bounds.width - actualSecondX), std::max(0, bounds.height)}};
}

EditorLayerRowLayout makeLayerRowLayout(core::RectI row) noexcept {
    const int width = std::max(0, row.width);
    const int gap = EditorLayoutMetrics::layerControlGap;
    const int dragWidth = std::min(EditorLayoutMetrics::layerDragHandleWidth, width);
    const int lockWidth = std::min(EditorLayoutMetrics::layerLockWidth,
                                   std::max(0, width - dragWidth));
    const int visibilityWidth = std::min(EditorLayoutMetrics::layerVisibilityWidth,
                                         std::max(0, width - dragWidth - lockWidth));
    const int lockX = row.x + width - lockWidth;
    const int visibilityX = std::max(row.x + dragWidth,
                                     lockX - std::min(gap, std::max(0, lockX - row.x - dragWidth)) -
                                         visibilityWidth);
    const int dragEnd = row.x + dragWidth;
    const int nameX = std::min(row.x + width, dragEnd + gap);
    const int nameEnd = std::max(nameX, visibilityX - gap);
    return {row,
            {row.x, row.y, dragWidth, row.height},
            {nameX, row.y, std::max(0, nameEnd - nameX), row.height},
            {visibilityX, row.y, visibilityWidth, row.height},
            {lockX, row.y, lockWidth, row.height}};
}

std::optional<LayerDropPreview> layerDropPreview(core::PointI pointer, core::RectI listBounds,
                                                 std::size_t layerCount,
                                                 std::size_t sourceIndex, int scrollOffset,
                                                 int rowHeight) noexcept {
    if (layerCount == 0 || sourceIndex >= layerCount || rowHeight <= 0 || listBounds.empty() ||
        pointer.x < listBounds.x || pointer.x >= listBounds.x + listBounds.width ||
        pointer.y < listBounds.y || pointer.y >= listBounds.y + listBounds.height) {
        return std::nullopt;
    }
    const auto contentY = static_cast<std::int64_t>(pointer.y - listBounds.y) +
                          std::max(0, scrollOffset);
    const auto rawIndex = contentY / rowHeight;
    const auto hoveredIndex = std::min<std::size_t>(layerCount - 1,
        static_cast<std::size_t>(std::max<std::int64_t>(0, rawIndex)));
    const bool after = contentY % rowHeight >= rowHeight / 2;
    const std::size_t insertionIndex = hoveredIndex + (after ? 1U : 0U);
    // MoveLayerCommand expects the destination index after the source is removed.
    const std::size_t targetIndex = std::min(layerCount - 1,
        insertionIndex > sourceIndex ? insertionIndex - 1U : insertionIndex);
    const auto indicatorY64 = static_cast<std::int64_t>(listBounds.y) +
        static_cast<std::int64_t>(hoveredIndex + (after ? 1U : 0U)) * rowHeight -
        std::max(0, scrollOffset);
    return std::optional<LayerDropPreview>{LayerDropPreview{
        hoveredIndex, targetIndex, static_cast<int>(indicatorY64), after}};
}

int clampScroll(int requested, int contentHeight, int viewportHeight) noexcept {
    return std::clamp(requested, 0,
                      std::max(0, contentHeight - std::max(0, viewportHeight)));
}

int autoScrollLayerList(int currentScroll, core::PointI pointer, core::RectI listBounds,
                        int contentHeight, int step, int edge) noexcept {
    if (listBounds.empty()) return 0;
    const int safeStep = std::max(0, step);
    const int safeEdge = std::max(1, edge);
    int requested = currentScroll;
    if (pointer.y < listBounds.y + safeEdge) requested -= safeStep;
    else if (pointer.y >= listBounds.y + listBounds.height - safeEdge) requested += safeStep;
    return clampScroll(requested, contentHeight, listBounds.height);
}

core::RectI placeTooltip(core::RectI anchor, int tooltipWidth, int tooltipHeight,
                         core::RectI canvas) noexcept {
    const int width = std::min(std::max(0, tooltipWidth), std::max(0, canvas.width));
    const int height = std::min(std::max(0, tooltipHeight), std::max(0, canvas.height));
    if (width == 0 || height == 0) return {canvas.x, canvas.y, width, height};
    int x = anchor.x;
    if (x + width > canvas.x + canvas.width) x = anchor.x + anchor.width - width;
    x = std::clamp(x, canvas.x, canvas.x + canvas.width - width);
    int y = anchor.y - height - 2;
    if (y < canvas.y) y = anchor.y + anchor.height + 2;
    y = std::clamp(y, canvas.y, canvas.y + canvas.height - height);
    return {x, y, width, height};
}

} // namespace underworld::editor
