#include "game/ui/ui_presenter.h"

#include "engine/core/game_metrics.h"
#include "engine/render/sprite.h"

#include <algorithm>
#include <string>

namespace underworld::game::ui {
namespace {

core::PointI spriteFrameSize(const presentation::RuntimeStaticSprite& sprite) noexcept {
    return {sprite.frame.source.width, sprite.frame.source.height};
}

// Resolved per-frame visual deltas of a node's authored states.
struct NodeVisual final {
    bool visible{true};
    std::optional<simulation::DefinitionId> sprite;
    std::optional<core::ColorRGBA8> tint;
};

bool conditionMatches(const StateCondition& condition, const UiBindingResolver& resolver) {
    const auto value = resolver.number(condition.source);
    if (!value) return false;
    switch (condition.op) {
        case ConditionOperator::equal:
            return *value == condition.value;
        case ConditionOperator::lessOrEqual:
            return *value <= condition.value;
        case ConditionOperator::greaterOrEqual:
            return *value >= condition.value;
    }
    return false;
}

// States evaluate in authored order and each matching state's deltas overwrite
// the previous value, so later states win deterministically.
NodeVisual resolveVisual(const NodeDefinition& node, const UiBindingResolver& resolver) {
    NodeVisual visual{};
    for (const auto& state : node.states) {
        if (state.condition && !conditionMatches(*state.condition, resolver)) continue;
        if (state.visual.visible) visual.visible = *state.visual.visible;
        if (state.visual.sprite) visual.sprite = state.visual.sprite;
        if (state.visual.tint) visual.tint = state.visual.tint;
    }
    return visual;
}

void drawSpriteRegion(render::Renderer2D& renderer,
                      const presentation::RuntimeStaticSprite& sprite,
                      core::RectI region, int drawX, int drawY,
                      const std::optional<core::ColorRGBA8>& tint) {
    if (tint) {
        renderer.drawImageRegionTinted(sprite.sheet->image(), region, drawX, drawY, *tint);
    } else {
        renderer.drawImageRegion(sprite.sheet->image(), region, drawX, drawY);
    }
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
    const NodeVisual visual = resolveVisual(node, resolver);
    if (!visual.visible) return;
    switch (node.component) {
        case ComponentKind::image: {
            const auto& spriteId = visual.sprite ? visual.sprite : node.spriteId;
            if (!spriteId) break;
            const auto* sprite = context.staticSprites.find(*spriteId);
            if (!sprite) break;
            const auto position = resolveNodePosition(node.layout, spriteFrameSize(*sprite));
            const core::PointI drawAt{position.x - sprite->frame.anchor.x + sprite->frame.drawOffset.x,
                                      position.y - sprite->frame.anchor.y + sprite->frame.drawOffset.y};
            if (visual.tint) {
                render::drawSpriteTinted(renderer, *sprite->sheet, sprite->frame, drawAt, *visual.tint);
            } else {
                render::drawSprite(renderer, *sprite->sheet, sprite->frame, drawAt);
            }
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
            const auto value = boundNumber(node, "value", resolver);
            const auto maximum = boundNumber(node, "maximum", resolver);
            if (!value || !maximum || *maximum <= 0) break;
            const auto clamped = std::clamp<std::int64_t>(*value, 0, *maximum);
            if (meter.mode == MeterMode::segmented) {
                const auto* full =
                    meter.sprites.full ? context.staticSprites.find(*meter.sprites.full) : nullptr;
                if (!full) break;
                const auto segment = spriteFrameSize(*full);
                const int stride = segment.x + meter.spacing;
                const auto position =
                    resolveNodePosition(node.layout,
                                        {static_cast<int>(*maximum) * stride, segment.y});
                for (std::int64_t index = 0; index < *maximum; ++index) {
                    const int x = position.x + static_cast<int>(index) * stride;
                    if (index < clamped) {
                        if (visual.tint) {
                            render::drawSpriteTinted(renderer, *full->sheet, full->frame,
                                                     {x - full->frame.anchor.x,
                                                      position.y - full->frame.anchor.y},
                                                     *visual.tint);
                        } else {
                            render::drawSprite(renderer, *full->sheet, full->frame,
                                               {x - full->frame.anchor.x,
                                                position.y - full->frame.anchor.y});
                        }
                    } else if (meter.emptyRect) {
                        renderer.fillRect({x + meter.emptyRect->offsetX,
                                           position.y + meter.emptyRect->offsetY,
                                           meter.emptyRect->width, meter.emptyRect->height},
                                          meter.emptyRect->color);
                    }
                }
                break;
            }
            // Fill meters: the fill sprite is cropped by the resolved value.
            // Horizontal fills grow left-to-right; vertical fills grow
            // bottom-to-top, so a circular liquid art renders as an orb.
            const auto* fill =
                meter.sprites.fill ? context.staticSprites.find(*meter.sprites.fill) : nullptr;
            if (!fill) break;
            if (clamped == 0) break;
            const auto& source = fill->frame.source;
            const auto position =
                resolveNodePosition(node.layout, {source.width, source.height});
            const int drawX =
                position.x - fill->frame.anchor.x + fill->frame.drawOffset.x;
            const int drawY =
                position.y - fill->frame.anchor.y + fill->frame.drawOffset.y;
            if (meter.mode == MeterMode::fillHorizontal) {
                const int visibleWidth =
                    static_cast<int>((static_cast<std::int64_t>(source.width) * clamped) / *maximum);
                if (visibleWidth <= 0) break;
                drawSpriteRegion(renderer, *fill, {source.x, source.y, visibleWidth, source.height},
                                 drawX, drawY, visual.tint);
            } else if (meter.mode == MeterMode::fillVertical) {
                const int visibleHeight =
                    static_cast<int>((static_cast<std::int64_t>(source.height) * clamped) / *maximum);
                if (visibleHeight <= 0) break;
                drawSpriteRegion(renderer, *fill,
                                 {source.x, source.y + source.height - visibleHeight,
                                  source.width, visibleHeight},
                                 drawX, drawY + source.height - visibleHeight, visual.tint);
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
