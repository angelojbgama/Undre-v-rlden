#pragma once

#include "engine/core/color_rgba8.h"
#include "engine/simulation/definition_id.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::game::ui {

// UI Engine core definitions (docs/UI_ENGINE.md, block UI-1a). Screens are
// authored content: an immutable tree of nodes with layout, bindings, states
// and actions. This module is data only; the presentation runtime that walks
// these definitions arrives in block UI-1b.

enum class ScreenKind { hud, overlay, screen };
enum class ComponentKind { group, panel, image, animatedImage, text, meter };
enum class Anchor {
    topLeft, topCenter, topRight,
    centerLeft, center, centerRight,
    bottomLeft, bottomCenter, bottomRight
};
enum class MeterMode { segmented, fillHorizontal, fillVertical };
enum class ConditionOperator { equal, lessOrEqual, greaterOrEqual };

// Binding Registry (UI-1 scope): canonical read paths over the GameViewModel
// snapshot. Only paths with a concrete consumer enter the table; the overlay
// and collection paths listed in docs/UI_ENGINE.md join with their consumers
// in later blocks. The UI never reaches into gameplay types directly.
enum class BindingPath {
    playerHealthCurrent,
    playerHealthMax,
    playerHealthPercentage,
    playerGold,
    playerAmmoItemId,
    playerAmmoAmount,
    playerAmmoIcon,
    playerQuickSlot0ItemId,
    playerQuickSlot0Amount,
    playerQuickSlot0Icon,
    playerQuickSlot1ItemId,
    playerQuickSlot1Amount,
    playerQuickSlot1Icon,
    playerQuickSlot2ItemId,
    playerQuickSlot2Amount,
    playerQuickSlot2Icon,
    playerQuickSlot3ItemId,
    playerQuickSlot3Amount,
    playerQuickSlot3Icon,
};

struct BindingPathEntry final {
    BindingPath id{};
    std::string_view path;
};

inline constexpr std::array<BindingPathEntry, 19> bindingPathTable{{
    {BindingPath::playerHealthCurrent, "player.health.current"},
    {BindingPath::playerHealthMax, "player.health.max"},
    {BindingPath::playerHealthPercentage, "player.health.percentage"},
    {BindingPath::playerGold, "player.gold"},
    {BindingPath::playerAmmoItemId, "player.ammo.itemId"},
    {BindingPath::playerAmmoAmount, "player.ammo.amount"},
    {BindingPath::playerAmmoIcon, "player.ammo.icon"},
    {BindingPath::playerQuickSlot0ItemId, "player.quickSlots.0.itemId"},
    {BindingPath::playerQuickSlot0Amount, "player.quickSlots.0.amount"},
    {BindingPath::playerQuickSlot0Icon, "player.quickSlots.0.icon"},
    {BindingPath::playerQuickSlot1ItemId, "player.quickSlots.1.itemId"},
    {BindingPath::playerQuickSlot1Amount, "player.quickSlots.1.amount"},
    {BindingPath::playerQuickSlot1Icon, "player.quickSlots.1.icon"},
    {BindingPath::playerQuickSlot2ItemId, "player.quickSlots.2.itemId"},
    {BindingPath::playerQuickSlot2Amount, "player.quickSlots.2.amount"},
    {BindingPath::playerQuickSlot2Icon, "player.quickSlots.2.icon"},
    {BindingPath::playerQuickSlot3ItemId, "player.quickSlots.3.itemId"},
    {BindingPath::playerQuickSlot3Amount, "player.quickSlots.3.amount"},
    {BindingPath::playerQuickSlot3Icon, "player.quickSlots.3.icon"},
}};

[[nodiscard]] std::optional<BindingPath> findBindingPath(std::string_view path);
[[nodiscard]] std::string_view bindingPathName(BindingPath path);

// Action Registry (UI-1 scope): stable action ids that map onto intents the
// PlayerCommand pipeline already carries. An action without a real command
// does not enter the table.
enum class ActionId {
    gameSave,
    gameLoad,
    inventoryToggle,
    craftingToggle,
    quickSlot1,
    quickSlot2,
    quickSlot3,
    quickSlot4,
};

struct ActionEntry final {
    ActionId id{};
    std::string_view name;
};

inline constexpr std::array<ActionEntry, 8> actionTable{{
    {ActionId::gameSave, "game.save"},
    {ActionId::gameLoad, "game.load"},
    {ActionId::inventoryToggle, "inventory.toggle"},
    {ActionId::craftingToggle, "crafting.toggle"},
    {ActionId::quickSlot1, "quickSlot.1"},
    {ActionId::quickSlot2, "quickSlot.2"},
    {ActionId::quickSlot3, "quickSlot.3"},
    {ActionId::quickSlot4, "quickSlot.4"},
}};

[[nodiscard]] std::optional<ActionId> findAction(std::string_view name);
[[nodiscard]] std::string_view actionName(ActionId action);

struct LayoutDefinition final {
    Anchor anchor{Anchor::topLeft};
    int offsetX{};
    int offsetY{};
    std::optional<int> width;
    std::optional<int> height;
    int z{};
    bool visible{true};
};

struct MeterSprites final {
    std::optional<simulation::DefinitionId> fill;
    std::optional<simulation::DefinitionId> full;
    std::optional<simulation::DefinitionId> half;
    std::optional<simulation::DefinitionId> empty;
};

struct MeterDefinition final {
    MeterMode mode{MeterMode::segmented};
    std::uint32_t segmentValue{1};
    MeterSprites sprites{};
};

struct StateCondition final {
    BindingPath source{};
    ConditionOperator op{ConditionOperator::equal};
    std::int64_t value{};
};

struct VisualDelta final {
    std::optional<core::ColorRGBA8> tint;
    std::optional<std::uint8_t> alpha;
    std::optional<simulation::DefinitionId> sprite;
    std::optional<bool> visible;
};

struct StateDefinition final {
    std::string id;
    std::optional<StateCondition> condition;
    VisualDelta visual{};
};

struct BindingDefinition final {
    std::string property;
    BindingPath source{};
};

struct ActionDefinition final {
    std::string event;
    ActionId action{};
};

struct NodeDefinition final {
    std::string id;
    ComponentKind component{ComponentKind::group};
    LayoutDefinition layout{};
    std::optional<simulation::DefinitionId> spriteId;    // image
    std::optional<simulation::DefinitionId> animationId; // animatedImage
    std::string text;                                    // text
    std::optional<MeterDefinition> meter;                // meter
    std::vector<BindingDefinition> bindings;
    std::vector<StateDefinition> states;
    std::vector<ActionDefinition> actions;
    std::vector<NodeDefinition> children;
};

struct ScreenDefinition final {
    simulation::DefinitionId id{};
    ScreenKind kind{ScreenKind::hud};
    NodeDefinition root{};
};

class ScreenCatalog final {
public:
    void add(ScreenDefinition definition);
    [[nodiscard]] const ScreenDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const ScreenDefinition& require(const simulation::DefinitionId& id) const;
    [[nodiscard]] const std::vector<ScreenDefinition>& values() const noexcept { return definitions_; }

private:
    std::vector<ScreenDefinition> definitions_;
};

[[nodiscard]] bool isContainerComponent(ComponentKind component) noexcept;
[[nodiscard]] bool componentAcceptsProperty(ComponentKind component, std::string_view property);

} // namespace underworld::game::ui
