#include "game/ui/ui_screens.h"

#include <algorithm>
#include <stdexcept>

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

} // namespace underworld::game::ui
