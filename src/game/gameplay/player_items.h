#pragma once

#include "game/gameplay/combat_types.h"
#include "game/gameplay/items.h"
#include "engine/simulation/player_command.h"

#include <array>
#include <cstddef>
#include <optional>

namespace underworld::game::gameplay {

class QuickSlotBindings final {
public:
    static constexpr std::size_t slotCount = 4;
    void bind(std::size_t index, simulation::DefinitionId itemId);
    void clear(std::size_t index);
    [[nodiscard]] const std::optional<simulation::DefinitionId>& binding(
        std::size_t index) const;

private:
    std::array<std::optional<simulation::DefinitionId>, slotCount> bindings_{};
};

struct ItemUseResult final { bool applied{}; int healthRestored{}; };

[[nodiscard]] ItemUseResult useItem(const simulation::DefinitionId& itemId,
                                    ItemContainer& inventory, const ItemCatalog& catalog,
                                    Health& health);

class PlayerItems final {
public:
    explicit PlayerItems(const ItemCatalog& catalog) : inventory_(catalog) {}
    [[nodiscard]] PlayerInventory& inventory() noexcept { return inventory_; }
    [[nodiscard]] const PlayerInventory& inventory() const noexcept { return inventory_; }
    [[nodiscard]] Wallet& wallet() noexcept { return wallet_; }
    [[nodiscard]] const Wallet& wallet() const noexcept { return wallet_; }
    [[nodiscard]] QuickSlotBindings& quickSlots() noexcept { return quickSlots_; }
    [[nodiscard]] const QuickSlotBindings& quickSlots() const noexcept { return quickSlots_; }
    [[nodiscard]] ItemUseResult useQuickSlot(std::size_t index, const ItemCatalog& catalog,
                                             Health& health);

private:
    PlayerInventory inventory_;
    Wallet wallet_;
    QuickSlotBindings quickSlots_;
};

class InventoryOverlayState final {
public:
    static constexpr std::size_t columns = 10;
    static constexpr std::size_t rows = 3;
    static constexpr std::size_t slotCount = columns * rows;
    void toggle() noexcept { open_ = !open_; }
    void close() noexcept { open_ = false; }
    [[nodiscard]] bool open() const noexcept { return open_; }
    [[nodiscard]] std::size_t selection() const noexcept { return selection_; }
    void moveSelection(int x, int y) noexcept;

private:
    bool open_{};
    std::size_t selection_{};
};

// Returns true when the overlay consumes the tick and gameplay must remain paused.
[[nodiscard]] bool routeInventoryCommand(InventoryOverlayState& overlay,
                                         const simulation::PlayerCommand& command,
                                         PlayerItems& items, const ItemCatalog& catalog,
                                         Health& health);

} // namespace underworld::game::gameplay
