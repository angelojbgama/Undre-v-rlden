#pragma once

#include "game/gameplay/items.h"

namespace underworld::game::gameplay {

class PlayerBank final {
public:
    static constexpr std::size_t columns = 10;
    static constexpr std::size_t rows = 5;
    static constexpr std::size_t slotCount = columns * rows;

    explicit PlayerBank(const ItemCatalog& catalog) : items_(slotCount, catalog) {}

    [[nodiscard]] ItemContainer& items() noexcept { return items_; }
    [[nodiscard]] const ItemContainer& items() const noexcept { return items_; }
    [[nodiscard]] std::uint64_t gold() const noexcept { return gold_; }
    [[nodiscard]] std::uint32_t depositItem(ItemContainer& inventory,
                                             const simulation::DefinitionId& itemId,
                                             std::uint32_t quantity);
    [[nodiscard]] std::uint32_t withdrawItem(ItemContainer& inventory,
                                              const simulation::DefinitionId& itemId,
                                              std::uint32_t quantity);
    [[nodiscard]] std::uint64_t depositGold(Wallet& wallet,
                                             std::uint64_t amount) noexcept;
    [[nodiscard]] std::uint64_t withdrawGold(Wallet& wallet,
                                              std::uint64_t amount) noexcept;
    void restoreGold(std::uint64_t amount) noexcept { gold_ = amount; }

private:
    ItemContainer items_;
    std::uint64_t gold_{};
};

} // namespace underworld::game::gameplay
