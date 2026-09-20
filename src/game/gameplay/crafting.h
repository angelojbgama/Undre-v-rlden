#pragma once

#include "engine/simulation/definition_id.h"
#include "game/gameplay/items.h"

#include <cstddef>
#include <cstdint>
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
    success, recipeNotFound, missingIngredients, inventoryFull, invalidRecipe
};

struct CraftingResult final {
    CraftingStatus status{CraftingStatus::success};
    simulation::DefinitionId recipeId{};
    std::uint32_t craftsPerformed{};
    std::vector<CraftingIngredient> consumed;
    std::vector<CraftingIngredient> produced;
    [[nodiscard]] explicit operator bool() const noexcept { return status == CraftingStatus::success; }
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
