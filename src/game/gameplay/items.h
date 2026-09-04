#pragma once

#include "engine/simulation/definition_id.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace underworld::game::gameplay {

enum class ItemCategory { consumable, equipment, key, misc };
enum class ItemUseKind { restoreHealth };

struct ItemUseDefinition final {
    ItemUseKind kind{ItemUseKind::restoreHealth};
    int amount{};
};

struct ItemDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualId{};
    ItemCategory category{ItemCategory::misc};
    std::uint32_t stackLimit{};
    std::optional<ItemUseDefinition> use{};
};

class ItemCatalog final {
public:
    void add(ItemDefinition definition);
    [[nodiscard]] const ItemDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const ItemDefinition& require(const simulation::DefinitionId& id) const;

private:
    std::unordered_map<simulation::DefinitionId, ItemDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

struct ItemStack final {
    simulation::DefinitionId itemId{};
    std::uint32_t quantity{};
};

struct AddResult final {
    std::uint32_t accepted{};
    std::uint32_t remainder{};
};

class ItemContainer final {
public:
    ItemContainer(std::size_t capacity, const ItemCatalog& catalog);

    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }
    [[nodiscard]] const std::optional<ItemStack>& slot(std::size_t index) const;
    [[nodiscard]] AddResult add(const simulation::DefinitionId& itemId,
                                std::uint32_t quantity);
    [[nodiscard]] AddResult canAdd(const simulation::DefinitionId& itemId,
                                   std::uint32_t quantity) const;
    [[nodiscard]] std::uint32_t remove(const simulation::DefinitionId& itemId,
                                       std::uint32_t quantity);
    [[nodiscard]] bool consume(const simulation::DefinitionId& itemId);
    [[nodiscard]] std::uint64_t count(const simulation::DefinitionId& itemId) const noexcept;
    [[nodiscard]] std::uint32_t transferTo(ItemContainer& destination,
                                           const simulation::DefinitionId& itemId,
                                           std::uint32_t quantity);

private:
    const ItemCatalog* catalog_{};
    std::vector<std::optional<ItemStack>> slots_;
};

class PlayerInventory final {
public:
    static constexpr std::size_t slotCount = 30;
    explicit PlayerInventory(const ItemCatalog& catalog) : items_(slotCount, catalog) {}
    [[nodiscard]] ItemContainer& items() noexcept { return items_; }
    [[nodiscard]] const ItemContainer& items() const noexcept { return items_; }

private:
    ItemContainer items_;
};

class Wallet final {
public:
    [[nodiscard]] std::uint64_t gold() const noexcept { return gold_; }
    // Saturates at uint64 max and returns the amount that could not be credited.
    [[nodiscard]] std::uint64_t addGold(std::uint64_t amount) noexcept;

private:
    std::uint64_t gold_{};
};

[[nodiscard]] const simulation::DefinitionId& lifePotionItemId();
[[nodiscard]] ItemDefinition makeLifePotionDefinition();

} // namespace underworld::game::gameplay
