#pragma once

#include "editor/visual_preview.h"

namespace underworld::editor {

enum class AnchorPreset {
    topLeft,
    center,
    bottomCenter,
    origin,
};

[[nodiscard]] core::PointI anchorForPreset(AnchorPreset preset, core::RectI source) noexcept;
[[nodiscard]] core::PointI anchorFromPreviewPointer(
    const VisualPreviewTransform& transform, core::PointI pointer,
    core::RectI source) noexcept;
[[nodiscard]] core::PointI offsetFromPreviewDrag(
    const VisualPreviewTransform& transform, core::PointI start,
    core::PointI current, core::PointI initial) noexcept;
[[nodiscard]] core::RectI clampPreviewSource(core::RectI source, core::PointI imageSize) noexcept;
[[nodiscard]] core::RectI snapPreviewSource(core::RectI source, core::PointI origin,
                                             core::PointI cell, core::PointI imageSize) noexcept;

} // namespace underworld::editor
