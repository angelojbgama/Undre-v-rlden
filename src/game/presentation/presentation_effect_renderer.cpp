#include "game/presentation/presentation_effect_renderer.h"

#include "engine/render/framebuffer.h"
#include "engine/render/renderer_2d.h"

#include <algorithm>
#include <cstdint>

namespace underworld::game::presentation {
namespace {

void applyOverlay(render::Framebuffer& framebuffer, const ResolvedOverlay& overlay) noexcept {
    render::Renderer2D renderer(framebuffer);
    renderer.fillRect({0, 0, framebuffer.width(), framebuffer.height()}, overlay.color);
}

std::uint8_t maskAlpha(const ResolvedVisionMask& mask, std::int64_t distanceSquared) noexcept {
    const auto inner = static_cast<std::int64_t>(mask.innerRadiusPixels);
    const auto outer = static_cast<std::int64_t>(mask.outerRadiusPixels);
    const auto innerSquared = inner * inner;
    const auto outerSquared = outer * outer;
    if (distanceSquared <= innerSquared) return 0U;
    if (distanceSquared >= outerSquared) return mask.outsideAlpha;
    const auto numerator = distanceSquared - innerSquared;
    const auto denominator = outerSquared - innerSquared;
    return static_cast<std::uint8_t>(
        (static_cast<std::int64_t>(mask.outsideAlpha) * numerator + denominator / 2) /
        denominator);
}

void applyVisionMask(render::Framebuffer& framebuffer, const ResolvedVisionMask& mask,
                     core::LogicalPointI center) noexcept {
    const auto color = mask.color;
    render::Renderer2D renderer(framebuffer);
    for (int y = 0; y < framebuffer.height(); ++y) {
        for (int x = 0; x < framebuffer.width(); ++x) {
            const auto dx = static_cast<std::int64_t>(x - center.x);
            const auto dy = static_cast<std::int64_t>(y - center.y);
            const auto alpha = maskAlpha(mask, dx * dx + dy * dy);
            const auto effectiveAlpha = static_cast<std::uint8_t>(
                (static_cast<std::uint32_t>(alpha) * color.a + 127U) / 255U);
            if (effectiveAlpha == 0U) continue;
            renderer.setPixel(x, y, {color.r, color.g, color.b, effectiveAlpha});
        }
    }
}

} // namespace

void PresentationEffectRenderer::applyWorld(render::Framebuffer& framebuffer,
                                             const PresentationEffectFrame& frame,
                                             core::LogicalPointI visionCenter) noexcept {
    for (const auto& overlay : frame.worldOverlays) applyOverlay(framebuffer, overlay);
    if (frame.visionMask) applyVisionMask(framebuffer, *frame.visionMask, visionCenter);
}

void PresentationEffectRenderer::applyFinal(render::Framebuffer& framebuffer,
                                             const PresentationEffectFrame& frame) noexcept {
    for (const auto& overlay : frame.finalOverlays) applyOverlay(framebuffer, overlay);
    for (const auto& fade : frame.fades) {
        render::Renderer2D renderer(framebuffer);
        renderer.fillRect({0, 0, framebuffer.width(), framebuffer.height()}, fade.color);
    }
}

} // namespace underworld::game::presentation
