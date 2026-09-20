#pragma once

#include "engine/simulation/definition_id.h"
#include "game/gameplay/items.h"
#include "game/gameplay/quests/quest_state.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace underworld::game::gameplay {

// Crafting is authored content, not item metadata: a recipe combines existing
// items from the ItemCatalog into one or more items. Recipes reference items;
// ItemDefinition never knows about recipes.
struct CraftingIngredient final {
    simulation::DefinitionId itemId{};
    std::uint32_t quantity{};
};

struct CraftingRecipeDefinition final {
    simulation::DefinitionId id{};
    std::vector<CraftingIngredient> inputs;
    std::vector<CraftingIngredient> outputs;
    // Quest-gated recipes stay hidden behind a silhouette until the authored
    // quest reaches the completed status; empty means always known.
    std::optional<simulation::DefinitionId> unlockQuestId{};
};

inline constexpr std::size_t minimumRecipeInputs = 2;
inline constexpr std::size_t maximumRecipeInputs = 4;
inline constexpr std::size_t minimumRecipeOutputs = 1;
inline constexpr std::size_t maximumRecipeOutputs = 4;

class CraftingCatalog final {
public:
    void add(CraftingRecipeDefinition definition);
    [[nodiscard]] const CraftingRecipeDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const CraftingRecipeDefinition& require(const simulation::DefinitionId& id) const;
    [[nodiscard]] const std::vector<CraftingRecipeDefinition>& values() const noexcept { return definitions_; }

private:
    std::vector<CraftingRecipeDefinition> definitions_;
};

enum class CraftingStatus {
    success, recipeNotFound, missingIngredients, inventoryFull, invalidRecipe, recipeLocked
};

struct CraftingResult final {
    CraftingStatus status{CraftingStatus::success};
    simulation::DefinitionId recipeId{};
    std::uint32_t craftsPerformed{};
    std::vector<CraftingIngredient> consumed;
    std::vector<CraftingIngredient> produced;
    [[nodiscard]] explicit operator bool() const noexcept { return status == CraftingStatus::success; }
};

// Derives which recipes the player currently knows. Quest-gated recipes are
// known exactly when their authored quest is completed, so unlocks are derived
// persistent quest state and never saved separately.
class CraftingKnowledge final {
public:
    explicit CraftingKnowledge(const quests::QuestStateStore& quests) noexcept : quests_(&quests) {}
    [[nodiscard]] bool known(const CraftingRecipeDefinition& recipe) const noexcept {
        return !recipe.unlockQuestId ||
               quests_->status(*recipe.unlockQuestId) == quests::QuestStatus::completed;
    }

private:
    const quests::QuestStateStore* quests_{};
};

// Recipes the player actually crafted, with per-recipe counters. This is the
// only crafting state that is saved (DSAV CRFT): it cannot be derived from
// quest state because ordinary recipes become "made" through play.
class CraftingHistory final {
public:
    void record(const simulation::DefinitionId& recipeId, std::uint32_t crafts);
    void restore(std::vector<std::pair<simulation::DefinitionId, std::uint32_t>> records);
    [[nodiscard]] std::uint32_t count(const simulation::DefinitionId& recipeId) const noexcept;
    [[nodiscard]] const std::vector<std::pair<simulation::DefinitionId, std::uint32_t>>&
        values() const noexcept { return records_; }

private:
    std::vector<std::pair<simulation::DefinitionId, std::uint32_t>> records_;
};

// Transactional crafting. Every operation is simulated on a detached copy of
// the container first: the real inventory is only mutated after every input
// removal and every output placement succeeded. Consuming ingredients can
// therefore free the slots that the outputs then occupy.
class CraftingService final {
public:
    // Simulates `crafts` crafts without touching the container.
    [[nodiscard]] CraftingResult canCraft(const CraftingRecipeDefinition& recipe,
                                          const ItemContainer& inventory,
                                          std::uint32_t crafts = 1) const;
    // Applies `crafts` crafts atomically; the container is unchanged on failure.
    [[nodiscard]] CraftingResult craft(const CraftingRecipeDefinition& recipe,
                                       ItemContainer& inventory,
                                       std::uint32_t crafts = 1) const;
    // Largest craft count the inventory can pay for and absorb.
    [[nodiscard]] std::uint32_t maxCraftable(const CraftingRecipeDefinition& recipe,
                                             const ItemContainer& inventory) const;

private:
    static CraftingResult simulate(const CraftingRecipeDefinition& recipe,
                                   const ItemContainer& inventory,
                                   std::uint32_t crafts,
                                   ItemContainer* commitTarget);
};

} // namespace underworld::game::gameplay
