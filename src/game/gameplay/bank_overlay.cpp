#include "game/gameplay/bank_overlay.h"

#include <algorithm>

namespace underworld::game::gameplay {

void BankOverlayState::toggle() noexcept {
    open_ = !open_;
    if (open_) { focus_ = BankOverlayFocus::inventory; }
}

void BankOverlayState::moveSelection(int x, int y) noexcept {
    if (focus_ == BankOverlayFocus::inventory) {
        if (y > 0 && inventorySelection_ / columns == inventoryRows - 1) {
            focus_ = BankOverlayFocus::bank;
            bankSelection_ = inventorySelection_ % columns;
            return;
        }
        const int column = static_cast<int>(inventorySelection_ % columns);
        const int row = static_cast<int>(inventorySelection_ / columns);
        inventorySelection_ = static_cast<std::size_t>(std::clamp(row + std::clamp(y, -1, 1), 0, static_cast<int>(inventoryRows - 1))) * columns + static_cast<std::size_t>(std::clamp(column + std::clamp(x, -1, 1), 0, static_cast<int>(columns - 1)));
    } else if (focus_ == BankOverlayFocus::bank) {
        if (y < 0 && bankSelection_ / columns == 0) {
            focus_ = BankOverlayFocus::inventory;
            inventorySelection_ = (inventoryRows - 1) * columns + bankSelection_ % columns;
        } else if (y > 0 && bankSelection_ / columns == bankRows - 1) {
            focus_ = BankOverlayFocus::gold;
        } else {
            const int column = static_cast<int>(bankSelection_ % columns);
            const int row = static_cast<int>(bankSelection_ / columns);
            bankSelection_ = static_cast<std::size_t>(std::clamp(row + std::clamp(y, -1, 1), 0, static_cast<int>(bankRows - 1))) * columns + static_cast<std::size_t>(std::clamp(column + std::clamp(x, -1, 1), 0, static_cast<int>(columns - 1)));
        }
    } else {
        if (y < 0) { focus_ = BankOverlayFocus::bank; }
        else if (x != 0) { goldSelection_ = x < 0 ? BankGoldSelection::carried : BankGoldSelection::stored; }
    }
}

BankCommandResult routeBankCommand(BankOverlayState& overlay,
                                   const simulation::PlayerCommand& command,
                                   PlayerItems& items) {
    if (command.actions.toggleInventoryPressed) { overlay.toggle(); }
    if (!overlay.open()) { return {}; }
    overlay.moveSelection(command.movement.x, command.movement.y);
    BankCommandResult result{true, 0, 0};
    if (!command.actions.primaryAttackPressed) { return result; }
    if (overlay.focus() == BankOverlayFocus::inventory) {
        const auto& selected = items.inventory().items().slot(overlay.inventorySelection());
        if (selected) result.itemsMoved = items.inventory().items().transferSlotTo(
            items.bank().items(), overlay.inventorySelection(), selected->quantity);
    } else if (overlay.focus() == BankOverlayFocus::bank) {
        const auto& selected = items.bank().items().slot(overlay.bankSelection());
        if (selected) result.itemsMoved = items.bank().items().transferSlotTo(
            items.inventory().items(), overlay.bankSelection(), selected->quantity);
    } else if (overlay.goldSelection() == BankGoldSelection::carried) {
        result.goldMoved = items.bank().depositGold(items.wallet(), items.wallet().gold());
    } else {
        result.goldMoved = items.bank().withdrawGold(items.wallet(), items.bank().gold());
    }
    return result;
}

} // namespace underworld::game::gameplay
