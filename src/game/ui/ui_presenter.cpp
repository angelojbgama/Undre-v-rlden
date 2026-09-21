#include "game/ui/ui_presenter.h"

#include "engine/core/game_metrics.h"
#include "engine/render/sprite.h"

#include <string>

namespace underworld::game::ui {
namespace {

core::PointI spriteFrameSize(const presentation::RuntimeStaticSprite& sprite) noexcept {
    return {sprite.frame.source.width, sprite.frame.source.height};
}

} // namespace

core::PointI resolveNodePosition(const LayoutDefinition& layout, core::PointI contentSize) {
    const int width = layout.width.value_or(contentSize.x);
    const int height = layout.height.value_or(contentSize.y);
    const int logicalWidth = core::GameMetrics::logicalWidth;
    const int logicalHeight = core::GameMetrics::logicalHeight;
    core::PointI position{layout.offsetX, layout.offsetY};
    switch (layout.anchor) {
        case Anchor::topLeft:
            break;
        case Anchor::topCenter:
            position.x += (logicalWidth - width) / 2;
            break;
        case Anchor::topRight:
            position.x += logicalWidth - width;
            break;
        case Anchor::centerLeft:
            position.y += (logicalHeight - height) / 2;
            break;
        case Anchor::center:
            position.x += (logicalWidth - width) / 2;
            position.y += (logicalHeight - height) / 2;
            break;
        case Anchor::centerRight:
            position.x += logicalWidth - width;
            position.y += (logicalHeight - height) / 2;
            break;
        case Anchor::bottomLeft:
            position.y += logicalHeight - height;
            break;
        case Anchor::bottomCenter:
            position.x += (logicalWidth - width) / 2;
            position.y += logicalHeight - height;
            break;
        case Anchor::bottomRight:
            position.x += logicalWidth - width;
            position.y += logicalHeight - height;
            break;
    }
    return position;
}

std::optional<std::int64_t> boundNumber(const NodeDefinition& node, std::string_view property,
                                        const UiBindingResolver& resolver) {
    for (const auto& binding : node.bindings) {
        if (binding.property == property) return resolver.number(binding.source);
    }
    return std::nullopt;
}

void UiPresenter::render(const ScreenDefinition& screen, const UiBindingResolver& resolver,
                         const UiVisualContext& context, render::Renderer2D& renderer) const {
    renderNode(screen.root, resolver, context, renderer);
}

void UiPresenter::renderNode(const NodeDefinition& node, const UiBindingResolver& resolver,
                             const UiVisualContext& context,
                             render::Renderer2D& renderer) const {
    if (!node.layout.visible) return;
    switch (node.component) {
        case ComponentKind::image: {
            if (!node.spriteId) break;
            const auto* sprite = context.staticSprites.find(*node.spriteId);
            if (!sprite) break;
            const auto position = resolveNodePosition(node.layout, spriteFrameSize(*sprite));
            render::drawSprite(renderer, *sprite->sheet, sprite->frame,
                               {position.x - sprite->frame.anchor.x,
                                position.y - sprite->frame.anchor.y});
            break;
        }
        case ComponentKind::text: {
            std::string text = node.text;
            if (const auto bound = boundNumber(node, "text", resolver)) {
                text = std::to_string(*bound);
            }
            const auto position = resolveNodePosition(node.layout, {0, 0});
            render::drawText(renderer, context.font, text, position.x, position.y);
            break;
        }
        case ComponentKind::meter: {
            if (!node.meter) break;
            const auto& meter = *node.meter;
            // Fill modes join with the first real consumer; segmented meters
            // are the authored HUD shape today.
            if (meter.mode != MeterMode::segmented) break;
            const auto value = boundNumber(node, "value", resolver);
            const auto maximum = boundNumber(node, "maximum", resolver);
            if (!value || !maximum || *maximum <= 0) break;
            const auto* full =
                meter.sprites.full ? context.staticSprites.find(*meter.sprites.full) : nullptr;
            if (!full) break;
            const auto segment = spriteFrameSize(*full);
            const int stride = segment.x + meter.spacing;
            const auto position =
                resolveNodePosition(node.layout, {static_cast<int>(*maximum) * stride, segment.y});
            for (std::int64_t index = 0; index < *maximum; ++index) {
                const int x = position.x + static_cast<int>(index) * stride;
                if (index < *value) {
                    render::drawSprite(renderer, *full->sheet, full->frame,
                                       {x - full->frame.anchor.x,
                                        position.y - full->frame.anchor.y});
                } else if (meter.emptyRect) {
                    renderer.fillRect({x + meter.emptyRect->offsetX,
                                       position.y + meter.emptyRect->offsetY,
                                       meter.emptyRect->width, meter.emptyRect->height},
                                      meter.emptyRect->color);
                }
            }
            break;
        }
        case ComponentKind::group:
        case ComponentKind::panel:
            for (const auto& child : node.children) {
                renderNode(child, resolver, context, renderer);
            }
            break;
        case ComponentKind::animatedImage:
            // Animated clips need per-node playback state; they join with the
            // first authored use of an animated HUD/menu element.
            break;
    }
}

} // namespace underworld::game::ui
