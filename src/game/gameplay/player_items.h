#pragma once

#include "game/gameplay/combat_types.h"
#include "game/gameplay/items.h"
#include "game/gameplay/rpg/equipment.h"
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
enum class InventoryOverlayFocus { inventory, equipment };

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
    [[nodiscard]] rpg::PlayerEquipment& equipment() noexcept { return equipment_; }
    [[nodiscard]] const rpg::PlayerEquipment& equipment() const noexcept { return equipment_; }
    [[nodiscard]] ItemUseResult useQuickSlot(std::size_t index, const ItemCatalog& catalog,
                                             Health& health);

private:
    PlayerInventory inventory_;
    Wallet wallet_;
    QuickSlotBindings quickSlots_;
    rpg::PlayerEquipment equipment_;
};

class InventoryOverlayState final {
public:
    static constexpr std::size_t columns = 10;
    static constexpr std::size_t rows = 3;
    static constexpr std::size_t slotCount = columns * rows;
    void toggle() noexcept;
    void close() noexcept { open_ = false; }
    [[nodiscard]] bool open() const noexcept { return open_; }
    [[nodiscard]] std::size_t selection() const noexcept { return selection_; }
    [[nodiscard]] rpg::EquipmentSlot equipmentSelection() const noexcept { return equipmentSlot_; }
    [[nodiscard]] bool equipmentFocused() const noexcept { return focus_ == InventoryOverlayFocus::equipment; }
    void moveSelection(int x, int y) noexcept;

private:
    bool open_{};
    std::size_t selection_{};
    InventoryOverlayFocus focus_{InventoryOverlayFocus::inventory};
    rpg::EquipmentSlot equipmentSlot_{rpg::EquipmentSlot::armor};
};

struct InventoryCommandResult final { bool consumedTick{}; bool equipmentChanged{}; };
[[nodiscard]] InventoryCommandResult routeInventoryCommand(
    InventoryOverlayState& overlay, const simulation::PlayerCommand& command,
    PlayerItems& items, const ItemCatalog& catalog, Health& health);

} // namespace underworld::game::gameplay
