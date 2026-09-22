#include "game/game_view_model.h"

#include "game/gameplay/dialogue/dialogue_session.h"
#include "game/gameplay/player.h"

namespace underworld::game {
namespace {

ItemSlotView makeSlot(const gameplay::ItemStack& stack,
                      const gameplay::ItemCatalog& catalog) {
    const auto& definition = catalog.require(stack.itemId);
    return {stack.itemId, definition.visualId, stack.quantity};
}

} // namespace

GameViewModel buildGameViewModel(const gameplay::Player& player, const gameplay::PlayerItems& items,
                                 const gameplay::ItemCatalog& catalog,
                                 const gameplay::InventoryOverlayState& overlay,
                                 const gameplay::BankOverlayState& bankOverlay,
                                 const gameplay::rpg::PlayerDerivedStats& derivedStats) {
    static const gameplay::rpg::ShopCatalog noShops;
    static const gameplay::ShopOverlayState noShopOverlay;
    static const gameplay::CraftingCatalog noCrafting;
    static const gameplay::CraftingOverlayState noCraftingTab;
    static const gameplay::quests::QuestStateStore noQuests;
    static const gameplay::CraftingKnowledge noKnowledge{noQuests};
    static const gameplay::CraftingHistory noHistory;
    return buildGameViewModel(player, items, catalog, overlay, bankOverlay, derivedStats,
                              noShopOverlay, noShops, noCraftingTab, noCrafting,
                              noKnowledge, noHistory);
}

GameViewModel buildGameViewModel(const gameplay::Player& player,
                                 const gameplay::PlayerItems& items,
                                 const gameplay::ItemCatalog& catalog,
                                 const gameplay::InventoryOverlayState& overlay,
                                 const gameplay::BankOverlayState& bankOverlay,
                                 const gameplay::rpg::PlayerDerivedStats& derivedStats,
                                 const gameplay::ShopOverlayState& shopOverlay,
                                 const gameplay::rpg::ShopCatalog& shops) {
    static const gameplay::CraftingCatalog noCrafting;
    static const gameplay::CraftingOverlayState noCraftingTab;
    static const gameplay::quests::QuestStateStore noQuests;
    static const gameplay::CraftingKnowledge noKnowledge{noQuests};
    static const gameplay::CraftingHistory noHistory;
    return buildGameViewModel(player, items, catalog, overlay, bankOverlay, derivedStats,
                              shopOverlay, shops, noCraftingTab, noCrafting,
                              noKnowledge, noHistory);
}

GameViewModel buildGameViewModel(const gameplay::Player& player,
                                 const gameplay::PlayerItems& items,
                                 const gameplay::ItemCatalog& catalog,
                                 const gameplay::InventoryOverlayState& overlay,
                                 const gameplay::BankOverlayState& bankOverlay,
                                 const gameplay::rpg::PlayerDerivedStats& derivedStats,
                                 const gameplay::ShopOverlayState& shopOverlay,
                                 const gameplay::rpg::ShopCatalog& shops,
                                 const gameplay::CraftingOverlayState& craftingTab,
                                 const gameplay::CraftingCatalog& crafting,
                                 const gameplay::CraftingKnowledge& craftingKnowledge,
                                 const gameplay::CraftingHistory& craftedRecipes) {
    GameViewModel result;
    result.playerHealth = player.health().current;
    result.playerMaximumHealth = player.health().maximum;
    result.gold = items.wallet().gold();
    result.inventoryOpen = overlay.open();
    result.inventorySelection = overlay.selection();
    result.inventoryFocus = overlay.equipmentFocused()
        ? gameplay::InventoryOverlayFocus::equipment : gameplay::InventoryOverlayFocus::inventory;
    result.equipmentSelection = overlay.equipmentSelection();
    result.derivedMaximumHealth = derivedStats.maximumHealth;
    result.playerAttackDamageBonus = derivedStats.playerAttackDamageBonus;
    result.bankOpen = bankOverlay.open();
    result.bankFocus = bankOverlay.focus();
    result.bankSelection = bankOverlay.bankSelection();
    result.bankGoldSelection = bankOverlay.goldSelection();
    result.bankGold = items.bank().gold();
    result.shopOpen = shopOverlay.open();
    result.shopMode = shopOverlay.mode();
    result.shopBuySelection = shopOverlay.buySelection();
    result.shopInventorySelection = shopOverlay.inventorySelection();
    result.shopFeedback = shopOverlay.feedback();
    result.craftingOpen = overlay.open() && overlay.craftingFocused();
    result.craftingTab = craftingTab.tab();
    result.craftingSelection = craftingTab.selection();
    result.craftingQuantity = craftingTab.craftQuantity();
    result.craftingFeedback = craftingTab.feedback();
    const gameplay::CraftingService craftingService;
    if (result.craftingOpen) {
        result.craftingRecipes.reserve(crafting.values().size());
        for (const auto& recipe : crafting.values()) {
            CraftingRecipeView recipeView;
            recipeView.recipeId = recipe.id;
            recipeView.known = craftingKnowledge.known(recipe);
            recipeView.revealSilhouette = !recipeView.known;
            recipeView.unlockQuestId = recipe.unlockQuestId;
            recipeView.craftedCount = craftedRecipes.count(recipe.id);
            bool craftable = recipeView.known;
            for (const auto& input : recipe.inputs) {
                const auto owned = items.inventory().items().count(input.itemId);
                if (owned < input.quantity) { craftable = false; }
                recipeView.inputs.push_back({input.itemId, catalog.find(input.itemId) != nullptr
                    ? std::optional<simulation::DefinitionId>{catalog.require(input.itemId).visualId}
                    : std::nullopt, owned, input.quantity});
            }
            for (const auto& output : recipe.outputs) {
                recipeView.outputs.push_back({output.itemId, catalog.find(output.itemId) != nullptr
                    ? std::optional<simulation::DefinitionId>{catalog.require(output.itemId).visualId}
                    : std::nullopt, output.quantity});
            }
            recipeView.maxCraftable = recipeView.known
                ? craftingService.maxCraftable(recipe, items.inventory().items()) : 0;
            recipeView.craftable = craftable && recipeView.maxCraftable > 0;
            result.craftingRecipes.push_back(std::move(recipeView));
        }
    }
    if (shopOverlay.open()) {
        result.activeShopId = shopOverlay.activeShopId();
        if (const auto* shop = shops.find(result.activeShopId)) {
            for (const auto& offer : shop->offers) {
                const auto& item = catalog.require(offer.itemId);
                result.shopOffers.push_back({offer.itemId, item.visualId, offer.playerBuyPrice, offer.playerSellPrice});
            }
        }
    }
    const auto makeEquipment = [&](gameplay::rpg::EquipmentSlot slot) {
        const auto& item = items.equipment().item(slot);
        if (!item) { return ItemSlotView{}; }
        return makeSlot(gameplay::ItemStack{*item, 1}, catalog);
    };
    result.armor = makeEquipment(gameplay::rpg::EquipmentSlot::armor);
    result.accessory = makeEquipment(gameplay::rpg::EquipmentSlot::accessory);
    for (std::size_t index = 0; index < result.inventory.size(); ++index) {
        const auto& slot = items.inventory().items().slot(index);
        if (slot) { result.inventory[index] = makeSlot(*slot, catalog); }
    }
    for (std::size_t index = 0; index < result.bank.size(); ++index) {
        const auto& slot = items.bank().items().slot(index);
        if (slot) { result.bank[index] = makeSlot(*slot, catalog); }
    }
    for (std::size_t index = 0; index < result.quickSlots.size(); ++index) {
        const auto& binding = items.quickSlots().binding(index);
        if (!binding) { continue; }
        const auto& definition = catalog.require(*binding);
        result.quickSlots[index] = {*binding, definition.visualId,
            items.inventory().items().count(*binding)};
    }
    return result;
}

std::optional<std::int64_t> GameViewModelBindings::number(ui::BindingPath path) const {
    const auto slotAmount = [this](std::size_t index) -> std::optional<std::int64_t> {
        // An unbound quick slot has no item, so it exposes no value at all.
        if (!view_->quickSlots[index].itemId) { return std::nullopt; }
        return static_cast<std::int64_t>(view_->quickSlots[index].quantity);
    };
    switch (path) {
        case ui::BindingPath::playerHealthCurrent:
            return static_cast<std::int64_t>(view_->playerHealth);
        case ui::BindingPath::playerHealthMax:
            return static_cast<std::int64_t>(view_->playerMaximumHealth);
        case ui::BindingPath::playerHealthPercentage:
            if (view_->playerMaximumHealth <= 0) { return std::nullopt; }
            return (static_cast<std::int64_t>(view_->playerHealth) * 100) / view_->playerMaximumHealth;
        case ui::BindingPath::playerGold:
            return static_cast<std::int64_t>(view_->gold);
        case ui::BindingPath::playerAmmoAmount:
            return static_cast<std::int64_t>(view_->ammo.quantity);
        case ui::BindingPath::playerAmmoPresent:
            return view_->ammo.itemId ? std::optional<std::int64_t>{1}
                                      : std::optional<std::int64_t>{0};
        case ui::BindingPath::overlayEquipmentArmorSelected:
            return view_->inventoryFocus == gameplay::InventoryOverlayFocus::equipment &&
                           view_->equipmentSelection == gameplay::rpg::EquipmentSlot::armor
                       ? std::optional<std::int64_t>{1}
                       : std::optional<std::int64_t>{0};
        case ui::BindingPath::overlayEquipmentAccessorySelected:
            return view_->inventoryFocus == gameplay::InventoryOverlayFocus::equipment &&
                           view_->equipmentSelection == gameplay::rpg::EquipmentSlot::accessory
                       ? std::optional<std::int64_t>{1}
                       : std::optional<std::int64_t>{0};
        case ui::BindingPath::playerDerivedMaxHealth:
            return static_cast<std::int64_t>(view_->derivedMaximumHealth);
        case ui::BindingPath::playerAttackDamageBonus:
            return static_cast<std::int64_t>(view_->playerAttackDamageBonus);
        case ui::BindingPath::bankGoldStored:
            return static_cast<std::int64_t>(view_->bankGold);
        case ui::BindingPath::overlayShopModeSell:
            return view_->shopMode == gameplay::ShopOverlayMode::sell
                       ? std::optional<std::int64_t>{1}
                       : std::optional<std::int64_t>{0};
        case ui::BindingPath::overlayShopFeedbackPresent:
            return view_->shopFeedback ? std::optional<std::int64_t>{1}
                                       : std::optional<std::int64_t>{0};
        case ui::BindingPath::overlayCraftingTabCraft:
            return view_->craftingTab == gameplay::CraftingTab::craft
                       ? std::optional<std::int64_t>{1}
                       : std::optional<std::int64_t>{0};
        case ui::BindingPath::overlayCraftingTabBook:
            return view_->craftingTab == gameplay::CraftingTab::book
                       ? std::optional<std::int64_t>{1}
                       : std::optional<std::int64_t>{0};
        case ui::BindingPath::playerQuickSlot0Amount: return slotAmount(0);
        case ui::BindingPath::playerQuickSlot1Amount: return slotAmount(1);
        case ui::BindingPath::playerQuickSlot2Amount: return slotAmount(2);
        case ui::BindingPath::playerQuickSlot3Amount: return slotAmount(3);
        case ui::BindingPath::dialogueChoicesVisible:
            return view_->dialogueOpen && view_->dialogueChoicesVisible
                       ? std::optional<std::int64_t>{1}
                       : std::optional<std::int64_t>{0};
        default: return std::nullopt;
    }
}

std::vector<ui::UiCollectionContext> GameViewModelBindings::collection(
    ui::BindingPath path) const {
    if (path == ui::BindingPath::dialoguePageLines ||
        path == ui::BindingPath::dialogueChoices) {
        // Both dialogue repeaters are plain text lines composed at frame
        // build; the choice lines already carry their "N: " prefix.
        const auto& lines = path == ui::BindingPath::dialoguePageLines
                                ? view_->dialoguePageLines
                                : view_->dialogueChoiceLines;
        std::vector<ui::UiCollectionContext> entries;
        entries.reserve(lines.size());
        std::int64_t index = 0;
        for (const auto& line : lines) {
            ui::UiCollectionContext entry;
            entry.text = line;
            entry.index = index;
            entries.push_back(std::move(entry));
            ++index;
        }
        return entries;
    }
    if (path == ui::BindingPath::playerQuickSlotSlots) {
        std::vector<ui::UiCollectionContext> entries;
        entries.reserve(view_->quickSlots.size());
        std::int64_t index = 0;
        for (const auto& slot : view_->quickSlots) {
            entries.push_back({slot.itemId, slot.visualId,
                               static_cast<std::int64_t>(slot.quantity), index});
            ++index;
        }
        return entries;
    }
    if (path == ui::BindingPath::craftingRecipesPath) {
        std::vector<ui::UiCollectionContext> entries;
        entries.reserve(view_->craftingRecipes.size());
        std::int64_t index = 0;
        for (const auto& recipe : view_->craftingRecipes) {
            ui::UiCollectionContext entry;
            entry.index = index;
            auto name = std::string(recipe.recipeId.value());
            if (name.size() > 20) { name = name.substr(0, 20); }
            entry.text = name + (recipe.craftable ? " OK" : (recipe.known ? " --" : " ??"));
            auto bookName = name;
            if (bookName.size() > 18) { bookName = bookName.substr(0, 18); }
            const char* marker = recipe.known ? (recipe.craftedCount > 0 ? "*" : "-") : "?";
            entry.text2 = std::string(marker) + " " + bookName;
            entries.push_back(std::move(entry));
            ++index;
        }
        return entries;
    }
    if (path == ui::BindingPath::shopOffers) {
        std::vector<ui::UiCollectionContext> entries;
        std::int64_t index = 0;
        for (const auto& offer : view_->shopOffers) {
            if (!offer.playerBuyPrice) { continue; }
            auto label = std::string(offer.itemId.value());
            if (label.size() > 22) { label = label.substr(0, 22); }
            label += " " + std::to_string(*offer.playerBuyPrice) + "G";
            ui::UiCollectionContext entry;
            entry.text = std::move(label);
            entry.amount = *offer.playerBuyPrice;
            entry.index = index;
            entries.push_back(std::move(entry));
            ++index;
        }
        return entries;
    }
    if (path == ui::BindingPath::bankSlots) {
        std::vector<ui::UiCollectionContext> entries;
        entries.reserve(view_->bank.size());
        std::int64_t index = 0;
        for (const auto& slot : view_->bank) {
            entries.push_back({slot.itemId, slot.visualId,
                               static_cast<std::int64_t>(slot.quantity), index});
            ++index;
        }
        return entries;
    }
    if (path == ui::BindingPath::questsJournal) {
        std::vector<ui::UiCollectionContext> entries;
        entries.reserve(view_->journal.size());
        std::int64_t index = 0;
        for (const auto& quest : view_->journal) {
            ui::UiCollectionContext entry;
            entry.text = quest.title;
            entry.flag = quest.completed;
            entry.index = index;
            entries.push_back(std::move(entry));
            ++index;
        }
        return entries;
    }
    if (path != ui::BindingPath::playerInventorySlots) { return {}; }
    std::vector<ui::UiCollectionContext> entries;
    entries.reserve(view_->inventory.size());
    std::int64_t index = 0;
    for (const auto& slot : view_->inventory) {
        entries.push_back({slot.itemId, slot.visualId,
                           static_cast<std::int64_t>(slot.quantity), index});
        ++index;
    }
    return entries;
}

void buildCraftingDetails(GameViewModel& view) {
    view.craftingIngredient0Text.clear();
    view.craftingIngredient1Text.clear();
    view.craftingIngredient2Text.clear();
    view.craftingIngredient3Text.clear();
    view.craftingOutputText.clear();
    view.craftingQtyText.clear();
    view.craftingFeedbackText.clear();
    if (view.craftingSelection >= view.craftingRecipes.size()) { return; }
    const auto& recipe = view.craftingRecipes[view.craftingSelection];
    const auto itemText = [](const simulation::DefinitionId& id) {
        auto value = std::string(id.value());
        return value.size() > 16 ? value.substr(0, 16) : value;
    };
    for (std::size_t index = 0; index < recipe.inputs.size() && index < 4; ++index) {
        const auto& input = recipe.inputs[index];
        std::string line = std::to_string(input.ownedQuantity) + "/" +
                           std::to_string(input.requiredQuantity) + " " +
                           itemText(input.itemId);
        if (line.size() > 24) { line = line.substr(0, 24); }
        switch (index) {
        case 0: view.craftingIngredient0Text = line; break;
        case 1: view.craftingIngredient1Text = line; break;
        case 2: view.craftingIngredient2Text = line; break;
        case 3: view.craftingIngredient3Text = line; break;
        }
    }
    if (!recipe.outputs.empty()) {
        const auto& output = recipe.outputs.front();
        view.craftingOutputText = "x" + std::to_string(output.quantity) + " " +
                                  itemText(output.itemId);
    }
    view.craftingQtyText = "QTY " + std::to_string(view.craftingQuantity) +
                           " / MAX " + std::to_string(std::max(recipe.maxCraftable, 1U));
    if (view.craftingFeedback) {
        switch (*view.craftingFeedback) {
        case gameplay::CraftingStatus::success: view.craftingFeedbackText = "CRAFTED"; break;
        case gameplay::CraftingStatus::missingIngredients: view.craftingFeedbackText = "NEEDS MATERIALS"; break;
        case gameplay::CraftingStatus::inventoryFull: view.craftingFeedbackText = "INVENTORY FULL"; break;
        case gameplay::CraftingStatus::invalidRecipe: view.craftingFeedbackText = "INVALID RECIPE"; break;
        case gameplay::CraftingStatus::recipeNotFound: view.craftingFeedbackText = "RECIPE MISSING"; break;
        case gameplay::CraftingStatus::recipeLocked: view.craftingFeedbackText = "RECIPE LOCKED"; break;
        }
    }
}

std::optional<std::int64_t> GameViewModelBindings::contextualNumber(
    ui::BindingPath path, std::int64_t contextIndex) const {
    if (path == ui::BindingPath::overlayShopBuySelected) {
        return contextIndex == static_cast<std::int64_t>(view_->shopBuySelection)
                   ? std::optional<std::int64_t>{1}
                   : std::optional<std::int64_t>{0};
    }
    if (path == ui::BindingPath::overlayShopSellSelected) {
        return view_->shopMode == gameplay::ShopOverlayMode::sell &&
                       contextIndex == static_cast<std::int64_t>(view_->shopInventorySelection)
                   ? std::optional<std::int64_t>{1}
                   : std::optional<std::int64_t>{0};
    }
    if (path == ui::BindingPath::overlayInventorySlotSelected ||
        path == ui::BindingPath::overlayBankInventorySelected) {
        // Derived presentation flag: highlighted exactly when the inventory
        // context owns the focus and the context index is the selection.
        return view_->inventoryFocus == gameplay::InventoryOverlayFocus::inventory &&
                       contextIndex == static_cast<std::int64_t>(view_->inventorySelection)
                   ? std::optional<std::int64_t>{1}
                   : std::optional<std::int64_t>{0};
    }
    if (path == ui::BindingPath::overlayBankStorageSelected) {
        return view_->bankFocus == gameplay::BankOverlayFocus::bank &&
                       contextIndex == static_cast<std::int64_t>(view_->bankSelection)
                   ? std::optional<std::int64_t>{1}
                   : std::optional<std::int64_t>{0};
    }
    if (path == ui::BindingPath::overlayDialogueChoiceSelected) {
        return view_->dialogueChoicesVisible &&
                       contextIndex == static_cast<std::int64_t>(view_->dialogueSelectedChoice)
                   ? std::optional<std::int64_t>{1}
                   : std::optional<std::int64_t>{0};
    }
    return number(path);
}

std::optional<std::string> GameViewModelBindings::string(ui::BindingPath path) const {
    if (path == ui::BindingPath::dialogueSpeaker) {
        return view_->dialogueOpen ? std::optional<std::string>{view_->dialogueSpeaker}
                                   : std::nullopt;
    }
    if (path == ui::BindingPath::dialoguePageText) {
        return view_->dialogueOpen ? std::optional<std::string>{view_->dialoguePageText}
                                   : std::nullopt;
    }
    if (path == ui::BindingPath::craftingIngredient0) { return view_->craftingIngredient0Text; }
    if (path == ui::BindingPath::craftingIngredient1) { return view_->craftingIngredient1Text; }
    if (path == ui::BindingPath::craftingIngredient2) { return view_->craftingIngredient2Text; }
    if (path == ui::BindingPath::craftingIngredient3) { return view_->craftingIngredient3Text; }
    if (path == ui::BindingPath::craftingOutputLine) { return view_->craftingOutputText; }
    if (path == ui::BindingPath::craftingQtyLine) { return view_->craftingQtyText; }
    if (path == ui::BindingPath::craftingFeedbackLine) { return view_->craftingFeedbackText; }
    if (path == ui::BindingPath::overlayShopSellItemName) {
        const auto& selected = view_->inventory[view_->shopInventorySelection];
        if (selected.itemId) {
            auto name = std::string(selected.itemId->value());
            if (name.size() > 24) { name = name.substr(0, 24); }
            return name;
        }
        return std::string{"EMPTY SLOT"};
    }
    switch (path) {
        case ui::BindingPath::saveSlot1Label:
            return view_->saveSlots[0].label;
        case ui::BindingPath::saveSlot2Label:
            return view_->saveSlots[1].label;
        case ui::BindingPath::saveSlot3Label:
            return view_->saveSlots[2].label;
        default:
            return std::nullopt;
    }
}

void buildQuestJournal(GameViewModel& view,
                       const gameplay::quests::QuestStateStore& quests,
                       const gameplay::quests::QuestCatalog& catalog) {
    view.journal.clear();
    for (const auto& progress : quests.snapshot()) {
        const auto* definition = catalog.find(progress.questId);
        if (definition == nullptr) { continue; }
        QuestJournalView entry;
        entry.questId = progress.questId;
        entry.title = definition->title;
        entry.completed = progress.status == gameplay::quests::QuestStatus::completed;
        for (const auto& objective : definition->objectives) {
            QuestObjectiveView objectiveView;
            objectiveView.description = objective.description;
            objectiveView.required = objective.requiredCount;
            for (const auto& recorded : progress.objectives) {
                if (recorded.objectiveId == objective.id) {
                    objectiveView.current = recorded.currentCount;
                    break;
                }
            }
            entry.objectives.push_back(std::move(objectiveView));
        }
        view.journal.push_back(std::move(entry));
    }
}

std::vector<std::string> wrapTextLines(const std::string_view text,
                                       const std::size_t maximumColumns,
                                       const std::size_t maximumLines) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t cursor = 0;
    while (cursor < text.size() && lines.size() < maximumLines) {
        while (cursor < text.size() && text[cursor] == ' ') { ++cursor; }
        const auto nextSpace = text.find_first_of(" \n", cursor);
        const auto wordEnd = nextSpace == std::string_view::npos ? text.size() : nextSpace;
        const std::string word{text.substr(cursor, wordEnd - cursor)};
        if (line.empty()) {
            line = word;
        } else if (line.size() + 1U + word.size() <= maximumColumns) {
            line += ' ';
            line += word;
        } else {
            lines.push_back(line);
            line = word;
        }
        cursor = wordEnd;
        if (cursor < text.size() && text[cursor] == '\n') {
            lines.push_back(line);
            line.clear();
            ++cursor;
        } else if (cursor < text.size()) {
            ++cursor;
        }
    }
    if (!line.empty() && lines.size() < maximumLines) {
        lines.push_back(line);
    }
    return lines;
}

void buildDialogueDetails(GameViewModel& view,
                          const gameplay::dialogue::DialogueSession& session) {
    view.dialogueOpen = session.isOpen();
    view.dialogueSpeaker.clear();
    view.dialoguePageText.clear();
    view.dialoguePageLines.clear();
    view.dialogueChoiceLines.clear();
    view.dialogueSelectedChoice = 0;
    view.dialogueChoicesVisible = false;
    if (!session.isOpen()) { return; }
    view.dialogueSpeaker.assign(session.speaker());
    view.dialoguePageText = std::to_string(session.pageIndex() + 1) + "/" +
                            std::to_string(session.pageCount());
    // Same wrap contract as the legacy overlay: 34 columns, two lines.
    view.dialoguePageLines = wrapTextLines(session.currentPage(), 34, 2);
    view.dialogueChoicesVisible = session.choicesVisible();
    view.dialogueSelectedChoice = session.selectedChoice();
    for (std::size_t index = 0; index < session.choiceCount(); ++index) {
        view.dialogueChoiceLines.push_back(std::to_string(index + 1) + ": " +
                                           std::string(session.choiceLabel(index)));
    }
}

std::optional<simulation::DefinitionId> GameViewModelBindings::id(ui::BindingPath path) const {
    const auto slotItem = [this](std::size_t index) -> std::optional<simulation::DefinitionId> {
        return view_->quickSlots[index].itemId;
    };
    const auto slotIcon = [this](std::size_t index) -> std::optional<simulation::DefinitionId> {
        return view_->quickSlots[index].visualId;
    };
    switch (path) {
        case ui::BindingPath::playerAmmoItemId: return view_->ammo.itemId;
        case ui::BindingPath::playerAmmoIcon: return view_->ammo.visualId;
        case ui::BindingPath::playerArmorIcon: return view_->armor.visualId;
        case ui::BindingPath::playerAccessoryIcon: return view_->accessory.visualId;
        case ui::BindingPath::playerQuickSlot0ItemId: return slotItem(0);
        case ui::BindingPath::playerQuickSlot1ItemId: return slotItem(1);
        case ui::BindingPath::playerQuickSlot2ItemId: return slotItem(2);
        case ui::BindingPath::playerQuickSlot3ItemId: return slotItem(3);
        case ui::BindingPath::playerQuickSlot0Icon: return slotIcon(0);
        case ui::BindingPath::playerQuickSlot1Icon: return slotIcon(1);
        case ui::BindingPath::playerQuickSlot2Icon: return slotIcon(2);
        case ui::BindingPath::playerQuickSlot3Icon: return slotIcon(3);
        default: return std::nullopt;
    }
}

} // namespace underworld::game
