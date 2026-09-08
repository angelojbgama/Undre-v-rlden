#include "editor/visual_authoring.h"

#include <algorithm>
#include <cmath>

namespace underworld::editor {

core::PointI anchorForPreset(AnchorPreset preset, core::RectI source) noexcept {
    switch (preset) {
    case AnchorPreset::topLeft: return {0, 0};
    case AnchorPreset::center: return {source.width / 2, source.height / 2};
    case AnchorPreset::bottomCenter: return {source.width / 2, std::max(0, source.height - 1)};
    case AnchorPreset::origin: return {0, 0};
    }
    return {};
}

core::PointI anchorFromPreviewPointer(const VisualPreviewTransform& transform,
                                      core::PointI pointer, core::RectI source) noexcept {
    const auto imagePoint = previewScreenToImage(transform, pointer);
    return {imagePoint.x - source.x, imagePoint.y - source.y};
}

core::PointI offsetFromPreviewDrag(const VisualPreviewTransform& transform,
                                   core::PointI start, core::PointI current,
                                   core::PointI initial) noexcept {
    if (transform.scale <= 0.0) return initial;
    return {initial.x + static_cast<int>(std::lround((current.x - start.x) / transform.scale)),
            initial.y + static_cast<int>(std::lround((current.y - start.y) / transform.scale))};
}

core::RectI clampPreviewSource(core::RectI source, core::PointI imageSize) noexcept {
    source.width = std::max(0, std::min(source.width, imageSize.x));
    source.height = std::max(0, std::min(source.height, imageSize.y));
    source.x = std::clamp(source.x, 0, std::max(0, imageSize.x - source.width));
    source.y = std::clamp(source.y, 0, std::max(0, imageSize.y - source.height));
    return source;
}

core::RectI snapPreviewSource(core::RectI source, core::PointI origin, core::PointI cell,
                              core::PointI imageSize) noexcept {
    if (cell.x <= 0 || cell.y <= 0) return clampPreviewSource(source, imageSize);
    const auto snap = [](int value, int base, int step) {
        return base + static_cast<int>(std::lround(static_cast<double>(value - base) / step)) * step;
    };
    source.x = snap(source.x, origin.x, cell.x);
    source.y = snap(source.y, origin.y, cell.y);
    source.width = std::max(cell.x, snap(source.width, 0, cell.x));
    source.height = std::max(cell.y, snap(source.height, 0, cell.y));
    return clampPreviewSource(source, imageSize);
}

} // namespace underworld::editor
