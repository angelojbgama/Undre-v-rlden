#pragma once

#include "engine/simulation/definition_id.h"
#include "game/gameplay/items.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/bank_overlay.h"
#include "game/gameplay/crafting.h"
#include "game/gameplay/crafting_overlay.h"
#include "game/gameplay/rpg/equipment.h"
#include "game/gameplay/rpg/shops.h"
#include "game/gameplay/shop_overlay.h"
#include "game/gameplay/quests/quest_model.h"
#include "game/gameplay/quests/quest_state.h"
#include "game/ui/ui_presenter.h"
#include "game/ui/ui_screens.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace underworld::game::gameplay { class Player; }
namespace underworld::game::gameplay::dialogue { class DialogueSession; }

namespace underworld::game {

struct ItemSlotView final {
    std::optional<simulation::DefinitionId> itemId{};
    std::optional<simulation::DefinitionId> visualId{};
    std::uint64_t quantity{};
};
struct QuestObjectiveView final {
    std::string description;
    std::uint32_t current{};
    std::uint32_t required{1};
};
struct QuestJournalView final {
    simulation::DefinitionId questId{};
    std::string title;
    bool completed{};
    std::vector<QuestObjectiveView> objectives;
};
struct ShopOfferView final {
    simulation::DefinitionId itemId{};
    std::optional<simulation::DefinitionId> visualId{};
    std::optional<std::uint64_t> playerBuyPrice{};
    std::optional<std::uint64_t> playerSellPrice{};
};
struct CraftingIngredientView final {
    simulation::DefinitionId itemId{};
    std::optional<simulation::DefinitionId> visualId{};
    std::uint64_t ownedQuantity{};
    std::uint32_t requiredQuantity{};
};
struct CraftingOutputView final {
    simulation::DefinitionId itemId{};
    std::optional<simulation::DefinitionId> visualId{};
    std::uint32_t quantity{};
};
struct CraftingRecipeView final {
    simulation::DefinitionId recipeId{};
    std::vector<CraftingIngredientView> inputs;
    std::vector<CraftingOutputView> outputs;
    bool craftable{};
    std::uint32_t maxCraftable{};
    // Quest-gated recipes: known only after the unlock quest completes. The
    // book still lists unknown ones, but the presentation shows silhouettes
    // instead of the real item art until then.
    bool known{};
    bool revealSilhouette{};
    std::optional<simulation::DefinitionId> unlockQuestId{};
    std::uint32_t craftedCount{};
};

struct GameViewModel final {
    int playerHealth{};
    int playerMaximumHealth{};
    std::uint64_t gold{};
    // Ammo readout for the player's projectile attack (authored
    // AttackDefinition::ammo); empty when the attack spends nothing.
    ItemSlotView ammo{};
    std::array<ItemSlotView, gameplay::QuickSlotBindings::slotCount> quickSlots{};
    std::array<ItemSlotView, gameplay::PlayerInventory::slotCount> inventory{};
    bool inventoryOpen{};
    std::size_t inventorySelection{};
    gameplay::InventoryOverlayFocus inventoryFocus{gameplay::InventoryOverlayFocus::inventory};
    gameplay::rpg::EquipmentSlot equipmentSelection{gameplay::rpg::EquipmentSlot::armor};
    ItemSlotView armor{};
    ItemSlotView accessory{};
    int derivedMaximumHealth{};
    int playerAttackDamageBonus{};
    bool bankOpen{};
    gameplay::BankOverlayFocus bankFocus{gameplay::BankOverlayFocus::inventory};
    std::size_t bankSelection{};
    gameplay::BankGoldSelection bankGoldSelection{gameplay::BankGoldSelection::carried};
    std::array<ItemSlotView, gameplay::PlayerBank::slotCount> bank{};
    std::uint64_t bankGold{};
    bool shopOpen{};
    simulation::DefinitionId activeShopId{};
    gameplay::ShopOverlayMode shopMode{gameplay::ShopOverlayMode::buy};
    std::size_t shopBuySelection{};
    std::size_t shopInventorySelection{};
    std::vector<ShopOfferView> shopOffers;
    std::optional<gameplay::rpg::ShopTransactionStatus> shopFeedback{};
    bool craftingOpen{};
    gameplay::CraftingTab craftingTab{gameplay::CraftingTab::craft};
    std::size_t craftingSelection{};
    std::uint32_t craftingQuantity{};
    std::optional<gameplay::CraftingStatus> craftingFeedback{};
    std::vector<CraftingRecipeView> craftingRecipes;
    // Quest journal read model: started quests in store order, titles and
    // objective limits resolved from the immutable catalog.
    std::vector<QuestJournalView> journal;
    // Selected-recipe detail for the crafting tab, composed at frame build.
    std::string craftingIngredient0Text;
    std::string craftingIngredient1Text;
    std::string craftingIngredient2Text;
    std::string craftingIngredient3Text;
    std::string craftingOutputText;
    std::string craftingQtyText;
    std::string craftingFeedbackText;
    // Save slot readout for the saves screen: filled by the shell (file
    // existence), labels composed for the authored text bindings.
    struct SaveSlotView final {
        bool exists{};
        std::string label;
    };
    std::array<SaveSlotView, 3> saveSlots;
    // Dialogue read model: composed at frame build from the live session —
    // wrapped page lines and numbered choice lines ready for string bindings.
    bool dialogueOpen{};
    std::string dialogueSpeaker;
    std::string dialoguePageText;
    std::vector<std::string> dialoguePageLines;
    std::vector<std::string> dialogueChoiceLines;
    std::size_t dialogueSelectedChoice{};
    bool dialogueChoicesVisible{};
};

[[nodiscard]] GameViewModel buildGameViewModel(
    const gameplay::Player& player, const gameplay::PlayerItems& items,
    const gameplay::ItemCatalog& catalog,
    const gameplay::InventoryOverlayState& overlay,
    const gameplay::BankOverlayState& bankOverlay,
    const gameplay::rpg::PlayerDerivedStats& derivedStats,
    const gameplay::ShopOverlayState& shopOverlay,
    const gameplay::rpg::ShopCatalog& shops);
[[nodiscard]] GameViewModel buildGameViewModel(
    const gameplay::Player&, const gameplay::PlayerItems&, const gameplay::ItemCatalog&,
    const gameplay::InventoryOverlayState&, const gameplay::BankOverlayState&,
    const gameplay::rpg::PlayerDerivedStats&);
[[nodiscard]] GameViewModel buildGameViewModel(
    const gameplay::Player& player, const gameplay::PlayerItems& items,
    const gameplay::ItemCatalog& catalog,
    const gameplay::InventoryOverlayState& overlay,
    const gameplay::BankOverlayState& bankOverlay,
    const gameplay::rpg::PlayerDerivedStats& derivedStats,
    const gameplay::ShopOverlayState& shopOverlay,
    const gameplay::rpg::ShopCatalog& shops,
    const gameplay::CraftingOverlayState& craftingTab,
    const gameplay::CraftingCatalog& crafting,
    const gameplay::CraftingKnowledge& craftingKnowledge,
    const gameplay::CraftingHistory& craftedRecipes);

// Composes the selected-recipe detail lines for the crafting tab.
void buildCraftingDetails(GameViewModel& view);

// Fills the journal read model from the quest state and catalog.
void buildQuestJournal(GameViewModel& view,
                       const gameplay::quests::QuestStateStore& quests,
                       const gameplay::quests::QuestCatalog& catalog);

// Word-wraps text into at most maximumLines lines of maximumColumns cells,
// breaking on spaces and forced newlines. Shared by the dialogue read model
// and the legacy fallback renderer so both wrap identically.
[[nodiscard]] std::vector<std::string> wrapTextLines(std::string_view text,
                                                     std::size_t maximumColumns,
                                                     std::size_t maximumLines);

// Composes the dialogue read model (speaker, page counter, wrapped page
// lines, numbered choice lines) from the live session for screen.dialogue.
void buildDialogueDetails(GameViewModel& view,
                          const gameplay::dialogue::DialogueSession& session);

// Maps the GameViewModel snapshot onto the UI Engine binding registry
// (docs/UI_ENGINE.md). Read-only presentation adapter: number and id reads
// only, never gameplay mutations.
class GameViewModelBindings final : public ui::UiBindingResolver {
public:
    explicit GameViewModelBindings(const GameViewModel& view) noexcept : view_(&view) {}

    [[nodiscard]] std::optional<std::int64_t> number(ui::BindingPath path) const override;
    [[nodiscard]] std::optional<simulation::DefinitionId> id(ui::BindingPath path) const override;
    [[nodiscard]] std::optional<std::string> string(ui::BindingPath path) const override;
    [[nodiscard]] std::vector<ui::UiCollectionContext> collection(
        ui::BindingPath path) const override;
    [[nodiscard]] std::optional<std::int64_t> contextualNumber(
        ui::BindingPath path, std::int64_t contextIndex) const override;

private:
    const GameViewModel* view_;
};

} // namespace underworld::game
