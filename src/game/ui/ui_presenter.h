#pragma once

#include "engine/render/bitmap_font.h"
#include "engine/render/renderer_2d.h"
#include "game/presentation/visual_content_loader.h"
#include "game/ui/ui_screens.h"

#include <optional>

namespace underworld::game::ui {

// Value source for the registered binding paths. Implemented over the
// GameViewModel snapshot in the game layer and over synthetic preview data in
// tests; the UI never reaches into gameplay types directly.
class UiBindingResolver {
public:
    [[nodiscard]] virtual std::optional<std::int64_t> number(BindingPath path) const = 0;
    [[nodiscard]] virtual std::optional<simulation::DefinitionId> id(BindingPath path) const = 0;

protected:
    UiBindingResolver() = default;
    ~UiBindingResolver() = default;
    UiBindingResolver(const UiBindingResolver&) = delete;
    UiBindingResolver& operator=(const UiBindingResolver&) = delete;
};

// Runtime visual dependencies of a render pass.
struct UiVisualContext final {
    const presentation::RuntimeStaticSpriteCatalog& staticSprites;
    const render::BitmapFont& font;
};

// Walks an authored screen definition and draws it into the logical
// framebuffer. Presentation only: bindings resolve through the resolver and
// the presenter never mutates gameplay state.
class UiPresenter final {
public:
    void render(const ScreenDefinition& screen, const UiBindingResolver& resolver,
                const UiVisualContext& context, render::Renderer2D& renderer) const;

private:
    void renderNode(const NodeDefinition& node, const UiBindingResolver& resolver,
                    const UiVisualContext& context, render::Renderer2D& renderer) const;
};

// Anchors position the node box inside the 272x224 logical screen. Boxes use
// the authored layout size when present, otherwise the component content size
// (image: sprite frame; meter: segment stride * bound maximum).
[[nodiscard]] core::PointI resolveNodePosition(const LayoutDefinition& layout,
                                               core::PointI contentSize);
[[nodiscard]] std::optional<std::int64_t> boundNumber(const NodeDefinition& node,
                                                      std::string_view property,
                                                      const UiBindingResolver& resolver);

} // namespace underworld::game::ui
