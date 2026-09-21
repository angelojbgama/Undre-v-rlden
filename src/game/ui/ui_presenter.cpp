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

core::PointI addOffset(core::PointI position, core::PointI offset) noexcept {
    return {position.x + offset.x, position.y + offset.y};
}

std::optional<simulation::DefinitionId> boundId(const NodeDefinition& node,
                                                std::string_view property,
                                                const UiBindingResolver& resolver) {
    for (const auto& binding : node.bindings) {
        if (binding.property == property) return resolver.id(binding.source);
    }
    return std::nullopt;
}

std::optional<BindingPath> boundPath(const NodeDefinition& node, std::string_view property) {
    for (const auto& binding : node.bindings) {
        if (binding.property == property) return binding.source;
    }
    return std::nullopt;
}

// Resolved per-frame visual deltas of a node's authored states.
struct NodeVisual final {
    bool visible{true};
    std::optional<simulation::DefinitionId> sprite;
    std::optional<core::ColorRGBA8> tint;
    std::optional<core::ColorRGBA8> background;
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
    // The authored layout visibility is the base; state deltas override it,
    // so a node can start hidden and be revealed by a condition.
    visual.visible = node.layout.visible;
    for (const auto& state : node.states) {
        if (state.condition && !conditionMatches(*state.condition, resolver)) continue;
        if (state.visual.visible) visual.visible = *state.visual.visible;
        if (state.visual.sprite) visual.sprite = state.visual.sprite;
        if (state.visual.tint) visual.tint = state.visual.tint;
        if (state.visual.background) visual.background = state.visual.background;
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

std::optional<std::int64_t> UiContextResolver::number(BindingPath path) const {
    switch (path) {
        case BindingPath::contextItemAmount:
            return context_.amount;
        case BindingPath::contextIndex:
            return context_.index;
        case BindingPath::overlayInventorySlotSelected:
            return base_->contextualNumber(path, context_.index);
        default:
            return base_->number(path);
    }
}

std::optional<simulation::DefinitionId> UiContextResolver::id(BindingPath path) const {
    switch (path) {
        case BindingPath::contextItemId:
            return context_.itemId;
        case BindingPath::contextItemIcon:
            return context_.icon;
        default:
            return base_->id(path);
    }
}

void UiPresenter::render(const ScreenDefinition& screen, const UiBindingResolver& resolver,
                         const UiVisualContext& context, render::Renderer2D& renderer) const {
    renderNode(screen.root, resolver, context, renderer);
}

void UiPresenter::renderNode(const NodeDefinition& node, const UiBindingResolver& resolver,
                             const UiVisualContext& context, render::Renderer2D& renderer,
                             core::PointI offset) const {
    const NodeVisual visual = resolveVisual(node, resolver);
    if (!visual.visible) return;
    if (context.focusedNode == &node &&
        (node.layout.width.has_value() || node.layout.height.has_value())) {
        const int boxWidth = node.layout.width.value_or(24);
        const int boxHeight = node.layout.height.value_or(16);
        const auto box = addOffset(resolveNodePosition(node.layout, {boxWidth, boxHeight}), offset);
        renderer.fillRect({box.x - 1, box.y - 1, boxWidth + 2, 1}, {240, 240, 240, 255});
        renderer.fillRect({box.x - 1, box.y + boxHeight, boxWidth + 2, 1}, {240, 240, 240, 255});
        renderer.fillRect({box.x - 1, box.y - 1, 1, boxHeight + 2}, {240, 240, 240, 255});
        renderer.fillRect({box.x + boxWidth, box.y - 1, 1, boxHeight + 2}, {240, 240, 240, 255});
    }
    switch (node.component) {
        case ComponentKind::image: {
            // Icons may be authored (sprite) or bound to a dynamic id
            // (player.ammo.icon): a missing bound id simply draws nothing.
            auto spriteId = boundId(node, "icon", resolver);
            if (!spriteId) spriteId = visual.sprite ? visual.sprite : node.spriteId;
            if (!spriteId) spriteId = node.spriteId;
            if (!spriteId) break;
            const auto* sprite = context.staticSprites.find(*spriteId);
            if (!sprite) break;
            const auto position = addOffset(resolveNodePosition(node.layout, spriteFrameSize(*sprite)), offset);
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
            const auto position = addOffset(resolveNodePosition(node.layout, {0, 0}), offset);
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
                    addOffset(resolveNodePosition(node.layout,
                                                       {static_cast<int>(*maximum) * stride,
                                                        segment.y}),
                              offset);
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
                addOffset(resolveNodePosition(node.layout, {source.width, source.height}),
                          offset);
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
        case ComponentKind::slot: {
            const int width = node.layout.width.value_or(0);
            const int height = node.layout.height.value_or(0);
            if (width <= 0 || height <= 0) break;
            const auto position = addOffset(resolveNodePosition(node.layout, {width, height}), offset);
            auto background = node.background;
            if (visual.background) background = visual.background;
            if (background) {
                renderer.fillRect({position.x, position.y, width, height}, *background);
            }
            const auto iconId = boundId(node, "icon", resolver);
            if (!iconId) break;
            const auto* icon = context.staticSprites.find(*iconId);
            if (!icon) break;
            render::drawSprite(renderer, *icon->sheet, icon->frame,
                               {position.x + node.iconOffset.x + icon->frame.anchor.x,
                                position.y + node.iconOffset.y + icon->frame.anchor.y});
            if (const auto count = boundNumber(node, "count", resolver);
                count && (node.countAlways || *count > 1)) {
                render::drawText(renderer, context.font, std::to_string(*count),
                                 position.x + node.countOffset.x,
                                 position.y + node.countOffset.y);
            }
            break;
        }
        case ComponentKind::repeater: {
            const auto source = boundPath(node, "source");
            if (!source) break;
            if (node.columns <= 0 || node.cellWidth <= 0 || node.cellHeight <= 0) break;
            const auto entries = resolver.collection(*source);
            const auto origin = addOffset(resolveNodePosition(node.layout, {0, 0}), offset);
            std::int64_t index = 0;
            for (const auto& entry : entries) {
                const core::PointI cell{
                    origin.x + static_cast<int>(index % node.columns) * node.cellWidth,
                    origin.y + static_cast<int>(index / node.columns) * node.cellHeight};
                const UiContextResolver contextResolver{resolver, entry};
                for (const auto& child : node.children) {
                    renderNode(child, contextResolver, context, renderer, cell);
                }
                ++index;
            }
            break;
        }
        case ComponentKind::group:
        case ComponentKind::panel:
            if (node.background && node.layout.width && node.layout.height) {
                const auto size =
                    core::PointI{*node.layout.width, *node.layout.height};
                const auto position =
                    addOffset(resolveNodePosition(node.layout, size), offset);
                renderer.fillRect({position.x, position.y, size.x, size.y}, *node.background);
            }
            for (const auto& child : node.children) {
                renderNode(child, resolver, context, renderer, offset);
            }
            break;
        case ComponentKind::animatedImage:
            // Animated clips need per-node playback state; they join with the
            // first authored use of an animated HUD/menu element.
            break;
    }
}

} // namespace underworld::game::ui
