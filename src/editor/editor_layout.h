#pragma once

#include "engine/core/geometry.h"

#include <cstddef>
#include <optional>
#include <vector>

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
    static constexpr int panelPadding = 8;
    static constexpr int controlGap = 4;
    static constexpr int compactButtonHeight = 18;
    static constexpr int layerRowHeight = 20;
    static constexpr int layerControlHeight = 18;
    static constexpr int layerDragHandleWidth = 22;
    static constexpr int layerVisibilityWidth = 20;
    static constexpr int layerLockWidth = 20;
    static constexpr int layerControlGap = 4;
    static constexpr int layerDragThreshold = 4;
    static constexpr int layerAutoScrollEdge = 16;
    static constexpr int layerAutoScrollStep = 8;
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

[[nodiscard]] core::RectI inset(core::RectI bounds, int padding) noexcept;

[[nodiscard]] std::vector<core::RectI> equalColumns(core::RectI bounds,
                                                       std::size_t count,
                                                       int spacing = EditorLayoutMetrics::controlGap);

struct EditorSplitLayout final {
    core::RectI first;
    core::RectI second;
};

[[nodiscard]] EditorSplitLayout splitFixedLeft(core::RectI bounds, int fixedWidth,
                                                int spacing = EditorLayoutMetrics::controlGap) noexcept;
[[nodiscard]] EditorSplitLayout splitFixedRight(core::RectI bounds, int fixedWidth,
                                                 int spacing = EditorLayoutMetrics::controlGap) noexcept;

struct EditorLayerRowLayout final {
    core::RectI row;
    core::RectI dragHandle;
    core::RectI name;
    core::RectI visibility;
    core::RectI lock;
};

[[nodiscard]] EditorLayerRowLayout makeLayerRowLayout(core::RectI row) noexcept;

struct LayerDropPreview final {
    std::size_t hoveredIndex{};
    std::size_t targetIndex{};
    int indicatorY{};
    bool after{};
};

[[nodiscard]] std::optional<LayerDropPreview> layerDropPreview(
    core::PointI pointer, core::RectI listBounds, std::size_t layerCount,
    std::size_t sourceIndex, int scrollOffset,
    int rowHeight = EditorLayoutMetrics::layerRowHeight) noexcept;

[[nodiscard]] int clampScroll(int requested, int contentHeight, int viewportHeight) noexcept;
[[nodiscard]] int autoScrollLayerList(int currentScroll, core::PointI pointer,
                                      core::RectI listBounds, int contentHeight,
                                      int step = EditorLayoutMetrics::layerAutoScrollStep,
                                      int edge = EditorLayoutMetrics::layerAutoScrollEdge) noexcept;

[[nodiscard]] core::RectI placeTooltip(core::RectI anchor, int tooltipWidth,
                                       int tooltipHeight, core::RectI canvas) noexcept;

} // namespace underworld::editor
