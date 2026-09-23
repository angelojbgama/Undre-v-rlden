#pragma once

#include "engine/core/color_rgba8.h"
#include "engine/core/geometry.h"
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
enum class ComponentKind {
    group, panel, image, animatedImage, text, meter,
    // UI-4: repeated collections. repeater resolves a collection binding and
    // instantiates its children per entry (grid stride via columns/cell);
    // slot renders one item cell driven by context bindings.
    slot, repeater
};
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
    // UI-4 collection + context paths: the repeater source resolves to the
    // 30 inventory slots; context paths resolve against the current repeater
    // instance; the derived selection flag composes focus+selection+index in
    // the GameViewModel adapter so authored states stay single-condition.
    playerInventorySlots,
    playerAmmoPresent,
    playerQuickSlotSlots,
    questsJournal,
    contextQuestTitle,
    contextQuestCompleted,
    saveSlot1Label,
    saveSlot2Label,
    saveSlot3Label,
    playerArmorIcon,
    playerAccessoryIcon,
    overlayEquipmentArmorSelected,
    overlayEquipmentAccessorySelected,
    playerDerivedMaxHealth,
    playerAttackDamageBonus,
    bankSlots,
    overlayBankInventorySelected,
    overlayBankStorageSelected,
    bankGoldStored,
    shopOffers,
    contextOfferLine,
    overlayShopModeSell,
    overlayShopBuySelected,
    overlayShopSellSelected,
    overlayShopSellItemName,
    overlayShopFeedbackPresent,
    craftingRecipesPath,
    contextCraftLine,
    contextBookLine,
    overlayCraftingRowSelected,
    overlayCraftingBookRowSelected,
    overlayCraftingTabCraft,
    overlayCraftingTabBook,
    craftingIngredient0,
    craftingIngredient1,
    craftingIngredient2,
    craftingIngredient3,
    craftingOutputLine,
    craftingQtyLine,
    craftingFeedbackLine,
    contextIndex,
    contextItemId,
    contextItemIcon,
    contextItemAmount,
    overlayInventorySlotSelected,
    // Dialogue read model (screen.dialogue): speaker/page header strings are
    // composed at frame build; the page lines and the numbered choice lines
    // are repeater collections, the selection flag composes session state
    // with the context index, and choicesVisible gates the advance hint.
    dialogueSpeaker,
    dialoguePageText,
    dialoguePageLines,
    contextPageLine,
    dialogueChoices,
    contextChoiceLine,
    overlayDialogueChoiceSelected,
    dialogueChoicesVisible,
    // Shell readout for the saves screen: 1 while the title shell uses the
    // screen as the start/new-game picker (SAVE buttons stay hidden there).
    savesStartMode,
    // Transient HUD notification (level up, quest started/completed) fed by
    // the shell from domain events; text plus its presence gate.
    hudNotification,
    hudNotificationPresent,
};

struct BindingPathEntry final {
    BindingPath id{};
    std::string_view path;
};

inline constexpr std::array<BindingPathEntry, 75> bindingPathTable{{
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
    {BindingPath::playerInventorySlots, "player.inventory.slots"},
    {BindingPath::playerAmmoPresent, "player.ammo.present"},
    {BindingPath::playerQuickSlotSlots, "player.quickSlots.slots"},
    {BindingPath::questsJournal, "quests.journal"},
    {BindingPath::contextQuestTitle, "context.quest.title"},
    {BindingPath::contextQuestCompleted, "context.quest.completed"},
    {BindingPath::saveSlot1Label, "saves.slot.1.label"},
    {BindingPath::saveSlot2Label, "saves.slot.2.label"},
    {BindingPath::saveSlot3Label, "saves.slot.3.label"},
    {BindingPath::playerArmorIcon, "player.armor.icon"},
    {BindingPath::playerAccessoryIcon, "player.accessory.icon"},
    {BindingPath::overlayEquipmentArmorSelected, "overlay.equipment.armorSelected"},
    {BindingPath::overlayEquipmentAccessorySelected, "overlay.equipment.accessorySelected"},
    {BindingPath::playerDerivedMaxHealth, "player.derivedMaxHealth"},
    {BindingPath::playerAttackDamageBonus, "player.attackDamageBonus"},
    {BindingPath::bankSlots, "bank.slots"},
    {BindingPath::overlayBankInventorySelected, "overlay.bank.inventorySelected"},
    {BindingPath::overlayBankStorageSelected, "overlay.bank.storageSelected"},
    {BindingPath::bankGoldStored, "bank.goldStored"},
    {BindingPath::shopOffers, "shop.offers"},
    {BindingPath::contextOfferLine, "context.offer.line"},
    {BindingPath::overlayShopModeSell, "overlay.shop.modeSell"},
    {BindingPath::overlayShopBuySelected, "overlay.shop.buySelected"},
    {BindingPath::overlayShopSellSelected, "overlay.shop.sellSelected"},
    {BindingPath::overlayShopSellItemName, "overlay.shop.sellItemName"},
    {BindingPath::overlayShopFeedbackPresent, "overlay.shop.feedbackPresent"},
    {BindingPath::craftingRecipesPath, "crafting.recipes"},
    {BindingPath::contextCraftLine, "context.craft.line"},
    {BindingPath::contextBookLine, "context.book.line"},
    {BindingPath::overlayCraftingRowSelected, "overlay.crafting.rowSelected"},
    {BindingPath::overlayCraftingBookRowSelected, "overlay.crafting.bookRowSelected"},
    {BindingPath::overlayCraftingTabCraft, "overlay.crafting.tabCraft"},
    {BindingPath::overlayCraftingTabBook, "overlay.crafting.tabBook"},
    {BindingPath::craftingIngredient0, "crafting.ingredient0"},
    {BindingPath::craftingIngredient1, "crafting.ingredient1"},
    {BindingPath::craftingIngredient2, "crafting.ingredient2"},
    {BindingPath::craftingIngredient3, "crafting.ingredient3"},
    {BindingPath::craftingOutputLine, "crafting.outputLine"},
    {BindingPath::craftingQtyLine, "crafting.qtyLine"},
    {BindingPath::craftingFeedbackLine, "crafting.feedbackLine"},
    {BindingPath::contextIndex, "context.index"},
    {BindingPath::contextItemId, "context.item.id"},
    {BindingPath::contextItemIcon, "context.item.icon"},
    {BindingPath::contextItemAmount, "context.item.amount"},
    {BindingPath::overlayInventorySlotSelected, "overlay.inventory.slotSelected"},
    {BindingPath::dialogueSpeaker, "dialogue.speaker"},
    {BindingPath::dialoguePageText, "dialogue.page.text"},
    {BindingPath::dialoguePageLines, "dialogue.pageLines"},
    {BindingPath::contextPageLine, "context.pageLine"},
    {BindingPath::dialogueChoices, "dialogue.choices"},
    {BindingPath::contextChoiceLine, "context.choiceLine"},
    {BindingPath::overlayDialogueChoiceSelected, "overlay.dialogue.choiceSelected"},
    {BindingPath::dialogueChoicesVisible, "dialogue.choices.visible"},
    {BindingPath::savesStartMode, "saves.startMode"},
    {BindingPath::hudNotification, "hud.notification"},
    {BindingPath::hudNotificationPresent, "hud.notification.present"},
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
    // Presentation-level action: the UI shell closes the active menu itself.
    screenClose,
    // Presentation-level navigation: opens the authored saves screen.
    screenOpenSaves,
    // Slot-indexed save/load (the shell picks the slot, then replays the
    // regular save/load intent for the tick).
    gameSaveSlot1,
    gameSaveSlot2,
    gameSaveSlot3,
    gameLoadSlot1,
    gameLoadSlot2,
    gameLoadSlot3,
    // Death recovery: the shell rebuilds the current map at its start spawn
    // with the player healed (session progress survives).
    gameRetry,
};

struct ActionEntry final {
    ActionId id{};
    std::string_view name;
};

inline constexpr std::array<ActionEntry, 17> actionTable{{
    {ActionId::gameSave, "game.save"},
    {ActionId::gameLoad, "game.load"},
    {ActionId::inventoryToggle, "inventory.toggle"},
    {ActionId::craftingToggle, "crafting.toggle"},
    {ActionId::quickSlot1, "quickSlot.1"},
    {ActionId::quickSlot2, "quickSlot.2"},
    {ActionId::quickSlot3, "quickSlot.3"},
    {ActionId::quickSlot4, "quickSlot.4"},
    {ActionId::screenClose, "screen.close"},
    {ActionId::screenOpenSaves, "screen.open.saves"},
    {ActionId::gameSaveSlot1, "save.slot.1"},
    {ActionId::gameSaveSlot2, "save.slot.2"},
    {ActionId::gameSaveSlot3, "save.slot.3"},
    {ActionId::gameLoadSlot1, "load.slot.1"},
    {ActionId::gameLoadSlot2, "load.slot.2"},
    {ActionId::gameLoadSlot3, "load.slot.3"},
    {ActionId::gameRetry, "game.retry"},
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

// Solid-rect fallback for empty segments in segmented meters. Drawn at
// (segment.x + offsetX, segment.y + offsetY) with the authored size, so legacy
// HUD placeholders (inset rectangles) can be reproduced pixel-exactly.
struct MeterEmptyRect final {
    core::ColorRGBA8 color{};
    int offsetX{};
    int offsetY{};
    int width{};
    int height{};
};

struct MeterDefinition final {
    MeterMode mode{MeterMode::segmented};
    std::uint32_t segmentValue{1};
    MeterSprites sprites{};
    int spacing{0};
    std::optional<MeterEmptyRect> emptyRect;
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
    std::optional<core::ColorRGBA8> background;
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

// Authored 9-slice frame: the border inset renders 1:1 in the four corners,
// stretches along one axis on the edges and fills the remaining center.
struct BackgroundImageDefinition final {
    simulation::DefinitionId sprite{};
    int border{};
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
    // Slot visuals: background fills the slot cell; icon/count offsets mirror
    // the authored cell-local placement. Optional panel/group background
    // fills the container box (the overlay panel rectangles).
    std::optional<core::ColorRGBA8> background;
    // backgroundImage replaces the flat color with a 9-slice frame sprite.
    std::optional<BackgroundImageDefinition> backgroundImage;
    core::PointI iconOffset{0, 0};
    core::PointI countOffset{0, 0};
    // Repeater grid: instances are placed at origin + (i % columns) *
    // cellWidth, (i / columns) * cellHeight.
    int columns{1};
    int cellWidth{0};
    int cellHeight{0};
    // Slots hide the stack count at 1 by default (inventory semantics);
    // quickslots author countAlways to mirror the legacy always-on number.
    bool countAlways{false};
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

[[nodiscard]] std::string_view uiComponentName(ComponentKind component) noexcept;
[[nodiscard]] bool isContainerComponent(ComponentKind component) noexcept;
[[nodiscard]] bool componentAcceptsProperty(ComponentKind component, std::string_view property);

// Deterministic JSON manifest of the registries for the Studio UI Composer
// pickers (docs/UI_ENGINE.md, block UI-3). Python keeps a mirrored table in
// tools/content_studio/services/ui_registry.py; a Studio test runs the
// ui_manifest tool and fails loudly when the two drift apart.
[[nodiscard]] std::string emitUiManifestJson();

} // namespace underworld::game::ui
