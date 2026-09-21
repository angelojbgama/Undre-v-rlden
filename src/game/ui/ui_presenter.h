#pragma once

#include "engine/render/bitmap_font.h"
#include "engine/render/renderer_2d.h"
#include "game/presentation/visual_content_loader.h"
#include "game/ui/ui_screens.h"

#include <optional>

namespace underworld::game::ui {

// One repeater instance (UI-4): the identity/visual/amount the template reads
// through the context.* binding paths.
struct UiCollectionContext final {
    std::optional<simulation::DefinitionId> itemId;
    std::optional<simulation::DefinitionId> icon;
    std::int64_t amount{};
    std::int64_t index{};
};

// Value source for the registered binding paths. Implemented over the
// GameViewModel snapshot in the game layer and over synthetic preview data in
// tests; the UI never reaches into gameplay types directly.
class UiBindingResolver {
public:
    [[nodiscard]] virtual std::optional<std::int64_t> number(BindingPath path) const = 0;
    [[nodiscard]] virtual std::optional<simulation::DefinitionId> id(BindingPath path) const = 0;
    // Collection source for repeaters; empty for non-collection paths.
    [[nodiscard]] virtual std::vector<UiCollectionContext> collection(BindingPath) const {
        return {};
    }
    // Derived per-instance values (overlay.inventory.slotSelected composes
    // focus+selection+context index in the game adapter); defaults to number().
    [[nodiscard]] virtual std::optional<std::int64_t> contextualNumber(
        BindingPath path, std::int64_t) const {
        return number(path);
    }

protected:
    UiBindingResolver() = default;
    ~UiBindingResolver() = default;
    UiBindingResolver(const UiBindingResolver&) = delete;
    UiBindingResolver& operator=(const UiBindingResolver&) = delete;
};

// Wraps the base resolver so repeater template bindings resolve against the
// current instance: context.* paths come from the entry, derived contextual
// paths flow through contextualNumber with the instance index.
class UiContextResolver final : public UiBindingResolver {
public:
    UiContextResolver(const UiBindingResolver& base, const UiCollectionContext& context)
        noexcept : base_(&base), context_(context) {}

    [[nodiscard]] std::optional<std::int64_t> number(BindingPath path) const override;
    [[nodiscard]] std::optional<simulation::DefinitionId> id(BindingPath path) const override;
    [[nodiscard]] std::vector<UiCollectionContext> collection(BindingPath path) const override {
        return base_->collection(path);
    }
    [[nodiscard]] virtual std::optional<std::int64_t> contextualNumber(
        BindingPath path, std::int64_t) const {
        return number(path);
    }

private:
    const UiBindingResolver* base_;
    UiCollectionContext context_;
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
                    const UiVisualContext& context, render::Renderer2D& renderer,
                    core::PointI offset = {0, 0}) const;
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
