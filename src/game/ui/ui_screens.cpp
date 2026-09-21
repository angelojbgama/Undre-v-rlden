#include "game/ui/ui_screens.h"

#include "engine/data/json.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace underworld::game::ui {

std::optional<BindingPath> findBindingPath(std::string_view path) {
    for (const auto& entry : bindingPathTable) {
        if (entry.path == path) return entry.id;
    }
    return std::nullopt;
}

std::string_view bindingPathName(BindingPath path) {
    for (const auto& entry : bindingPathTable) {
        if (entry.id == path) return entry.path;
    }
    return {};
}

std::optional<ActionId> findAction(std::string_view name) {
    for (const auto& entry : actionTable) {
        if (entry.name == name) return entry.id;
    }
    return std::nullopt;
}

std::string_view actionName(ActionId action) {
    for (const auto& entry : actionTable) {
        if (entry.id == action) return entry.name;
    }
    return {};
}

void ScreenCatalog::add(ScreenDefinition definition) {
    definitions_.push_back(std::move(definition));
}

const ScreenDefinition* ScreenCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto found = std::find_if(definitions_.begin(), definitions_.end(),
                                    [&id](const ScreenDefinition& definition) {
                                        return definition.id == id;
                                    });
    return found == definitions_.end() ? nullptr : &*found;
}

const ScreenDefinition& ScreenCatalog::require(const simulation::DefinitionId& id) const {
    const auto* value = find(id);
    if (!value) throw std::out_of_range("ui screen not found");
    return *value;
}

std::string_view uiComponentName(ComponentKind component) noexcept {
    switch (component) {
        case ComponentKind::group: return "group";
        case ComponentKind::panel: return "panel";
        case ComponentKind::image: return "image";
        case ComponentKind::animatedImage: return "animatedImage";
        case ComponentKind::text: return "text";
        case ComponentKind::meter: return "meter";
    }
    return {};
}

bool isContainerComponent(ComponentKind component) noexcept {
    return component == ComponentKind::group || component == ComponentKind::panel;
}

bool componentAcceptsProperty(ComponentKind component, std::string_view property) {
    switch (component) {
        case ComponentKind::meter:
            return property == "value" || property == "maximum";
        case ComponentKind::text:
            return property == "text";
        default:
            return false;
    }
}

std::string emitUiManifestJson() {
    using engine::data::JsonArray;
    using engine::data::JsonObject;
    using engine::data::JsonValue;
    const auto text = [](std::string value) {
        return JsonValue{{}, std::move(value)};
    };
    const auto strings = [text](const auto& values) {
        JsonArray array;
        for (const auto& value : values) {
            array.push_back(text(std::string(value)));
        }
        return JsonValue{{}, std::move(array)};
    };
    const auto object = [](JsonObject value) {
        return JsonValue{{}, std::move(value)};
    };
    const auto array = [](JsonArray value) {
        return JsonValue{{}, std::move(value)};
    };
    JsonArray componentProperties;
    for (const ComponentKind component :
         {ComponentKind::group, ComponentKind::panel, ComponentKind::image,
          ComponentKind::animatedImage, ComponentKind::text, ComponentKind::meter}) {
        JsonArray properties;
        for (const char* property : {"value", "maximum", "text"}) {
            if (componentAcceptsProperty(component, property)) {
                properties.push_back(text(std::string(property)));
            }
        }
        JsonObject entry;
        entry.emplace_back("component", text(std::string(uiComponentName(component))));
        entry.emplace_back("properties", array(std::move(properties)));
        componentProperties.push_back(object(std::move(entry)));
    }
    JsonObject root;
    root.emplace_back("components", strings(std::array<std::string_view, 6>{
                                        "group", "panel", "image", "animatedImage", "text",
                                        "meter"}));
    root.emplace_back("componentProperties", array(std::move(componentProperties)));
    root.emplace_back("anchors", strings(std::array<std::string_view, 9>{
                                     "topLeft", "topCenter", "topRight", "centerLeft", "center",
                                     "centerRight", "bottomLeft", "bottomCenter", "bottomRight"}));
    root.emplace_back("meterModes", strings(std::array<std::string_view, 3>{
                                        "segmented", "fillHorizontal", "fillVertical"}));
    root.emplace_back("conditionOperators", strings(std::array<std::string_view, 3>{
                                                "equal", "lessOrEqual", "greaterOrEqual"}));
    root.emplace_back("screenKinds", strings(std::array<std::string_view, 3>{
                                         "hud", "overlay", "screen"}));
    JsonArray bindings;
    for (const auto& entry : bindingPathTable) {
        bindings.push_back(text(std::string(entry.path)));
    }
    root.emplace_back("bindings", array(std::move(bindings)));
    JsonArray actions;
    for (const auto& entry : actionTable) {
        actions.push_back(text(std::string(entry.name)));
    }
    root.emplace_back("actions", array(std::move(actions)));
    return engine::data::writeJson(JsonValue{{}, std::move(root)});
}

} // namespace underworld::game::ui
