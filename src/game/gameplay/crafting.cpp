#include "game/gameplay/crafting.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace underworld::game::gameplay {
namespace {

bool validIngredients(const std::vector<CraftingIngredient>& ingredients,
                      std::size_t minimum, std::size_t maximum) noexcept {
    if (ingredients.size() < minimum || ingredients.size() > maximum) { return false; }
    std::unordered_set<std::string> seen;
    for (const auto& ingredient : ingredients) {
        if (ingredient.itemId.empty() || ingredient.quantity == 0) { return false; }
        if (!seen.emplace(std::string(ingredient.itemId.value())).second) { return false; }
    }
    return true;
}

bool validRecipe(const CraftingRecipeDefinition& recipe) noexcept {
    if (recipe.id.empty()) { return false; }
    return validIngredients(recipe.inputs, minimumRecipeInputs, maximumRecipeInputs) &&
           validIngredients(recipe.outputs, minimumRecipeOutputs, maximumRecipeOutputs);
}

bool knownItems(const CraftingRecipeDefinition& recipe, const ItemCatalog& catalog) noexcept {
    const auto known = [&catalog](const CraftingIngredient& ingredient) {
        return catalog.find(ingredient.itemId) != nullptr;
    };
    return std::all_of(recipe.inputs.begin(), recipe.inputs.end(), known) &&
           std::all_of(recipe.outputs.begin(), recipe.outputs.end(), known);
}

} // namespace

void CraftingCatalog::add(CraftingRecipeDefinition definition) {
    if (definition.id.empty()) throw std::invalid_argument("crafting recipe id is empty");
    if (find(definition.id)) throw std::logic_error("duplicate crafting recipe id");
    if (!validRecipe(definition)) throw std::invalid_argument("crafting recipe definition is invalid");
    definitions_.push_back(std::move(definition));
}

const CraftingRecipeDefinition* CraftingCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto it = std::find_if(definitions_.begin(), definitions_.end(),
                                 [&](const auto& value) { return value.id == id; });
    return it == definitions_.end() ? nullptr : &*it;
}

const CraftingRecipeDefinition& CraftingCatalog::require(const simulation::DefinitionId& id) const {
    const auto* value = find(id);
    if (!value) throw std::out_of_range("crafting recipe not found");
    return *value;
}

CraftingResult CraftingService::simulate(const CraftingRecipeDefinition& recipe,
                                         const ItemContainer& inventory,
                                         std::uint32_t crafts,
                                         ItemContainer* commitTarget) {
    CraftingResult result;
    result.recipeId = recipe.id;
    result.craftsPerformed = commitTarget != nullptr ? crafts : 0;
    if (crafts == 0 || !validRecipe(recipe) || !knownItems(recipe, inventory.catalog())) {
        result.status = CraftingStatus::invalidRecipe;
        return result;
    }

    struct Total final { simulation::DefinitionId itemId; std::uint64_t quantity; };
    auto accumulate = [crafts](const std::vector<CraftingIngredient>& ingredients) {
        std::vector<Total> totals;
        totals.reserve(ingredients.size());
        for (const auto& ingredient : ingredients) {
            totals.push_back({ingredient.itemId,
                              static_cast<std::uint64_t>(ingredient.quantity) * crafts});
        }
        return totals;
    };
    const auto inputs = accumulate(recipe.inputs);
    const auto outputs = accumulate(recipe.outputs);
    constexpr auto uint32Max = std::numeric_limits<std::uint32_t>::max();
    for (const auto* sides : {&inputs, &outputs}) {
        for (const auto& total : *sides) {
            if (total.quantity > uint32Max) { result.status = CraftingStatus::invalidRecipe; return result; }
        }
    }

    for (const auto& input : inputs) {
        if (inventory.count(input.itemId) < input.quantity) {
            result.status = CraftingStatus::missingIngredients;
            return result;
        }
    }

    // Replay the whole transaction on a detached container so that consuming
    // ingredients frees slots for the outputs; the real container is only
    // replaced once every step succeeded.
    ItemContainer staged(inventory.capacity(), inventory.catalog());
    staged.restoreSlots(inventory.slots());
    result.consumed.reserve(inputs.size());
    for (const auto& input : inputs) {
        const auto removed = staged.remove(input.itemId, static_cast<std::uint32_t>(input.quantity));
        if (removed != input.quantity) {
            result.status = CraftingStatus::missingIngredients;
            return result;
        }
        result.consumed.push_back({input.itemId, static_cast<std::uint32_t>(input.quantity)});
    }
    result.produced.reserve(outputs.size());
    for (const auto& output : outputs) {
        const auto added = staged.add(output.itemId, static_cast<std::uint32_t>(output.quantity));
        if (added.remainder != 0) {
            result.status = CraftingStatus::inventoryFull;
            return result;
        }
        result.produced.push_back({output.itemId, static_cast<std::uint32_t>(output.quantity)});
    }
    if (commitTarget != nullptr) { commitTarget->restoreSlots(staged.slots()); }
    return result;
}

CraftingResult CraftingService::canCraft(const CraftingRecipeDefinition& recipe,
                                         const ItemContainer& inventory,
                                         std::uint32_t crafts) const {
    return simulate(recipe, inventory, crafts, nullptr);
}

CraftingResult CraftingService::craft(const CraftingRecipeDefinition& recipe,
                                      ItemContainer& inventory,
                                      std::uint32_t crafts) const {
    return simulate(recipe, inventory, crafts, &inventory);
}

std::uint32_t CraftingService::maxCraftable(const CraftingRecipeDefinition& recipe,
                                            const ItemContainer& inventory) const {    if (!validRecipe(recipe)) { return 0; }
    std::uint64_t bound = std::numeric_limits<std::uint32_t>::max();
    for (const auto& input : recipe.inputs) {
        bound = std::min<std::uint64_t>(bound, inventory.count(input.itemId) / input.quantity);
        if (bound == 0) { return 0; }
    }
    if (bound > std::numeric_limits<std::uint32_t>::max()) { bound = std::numeric_limits<std::uint32_t>::max(); }
    // Space feasibility is not analytically monotone with stack merging, so
    // scan upward from the ingredient bound until the staged transaction fails.
    std::uint32_t best = 0;
    for (std::uint64_t crafts = 1; crafts <= bound; ++crafts) {
        if (!simulate(recipe, inventory, static_cast<std::uint32_t>(crafts), nullptr)) { break; }
        best = static_cast<std::uint32_t>(crafts);
    }
    return best;
}

void CraftingHistory::record(const simulation::DefinitionId& recipeId, std::uint32_t crafts) {
    if (recipeId.empty() || crafts == 0) { return; }
    const auto found = std::find_if(records_.begin(), records_.end(),
                                    [&](const auto& value) { return value.first == recipeId; });
    if (found == records_.end()) { records_.push_back({recipeId, crafts}); }
    else { found->second += crafts; }
}

void CraftingHistory::restore(
    std::vector<std::pair<simulation::DefinitionId, std::uint32_t>> records) {
    records_.clear();
    for (auto& value : records) { record(value.first, value.second); }
}

std::uint32_t CraftingHistory::count(const simulation::DefinitionId& recipeId) const noexcept {
    const auto found = std::find_if(records_.begin(), records_.end(),
                                    [&](const auto& value) { return value.first == recipeId; });
    return found == records_.end() ? 0 : found->second;
}

} // namespace underworld::game::gameplay
