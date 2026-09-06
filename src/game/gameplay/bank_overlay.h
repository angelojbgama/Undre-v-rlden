#pragma once

#include "engine/simulation/player_command.h"
#include "game/gameplay/player_items.h"

namespace underworld::game::gameplay {

enum class BankOverlayFocus { inventory, bank, gold };
enum class BankGoldSelection { carried, stored };

class BankOverlayState final {
public:
    static constexpr std::size_t columns = 10;
    static constexpr std::size_t inventoryRows = 3;
    static constexpr std::size_t bankRows = 5;

    void toggle() noexcept;
    void close() noexcept { open_ = false; }
    [[nodiscard]] bool open() const noexcept { return open_; }
    [[nodiscard]] BankOverlayFocus focus() const noexcept { return focus_; }
    [[nodiscard]] std::size_t inventorySelection() const noexcept { return inventorySelection_; }
    [[nodiscard]] std::size_t bankSelection() const noexcept { return bankSelection_; }
    [[nodiscard]] BankGoldSelection goldSelection() const noexcept { return goldSelection_; }
    void moveSelection(int x, int y) noexcept;

private:
    bool open_{};
    BankOverlayFocus focus_{BankOverlayFocus::inventory};
    std::size_t inventorySelection_{};
    std::size_t bankSelection_{};
    BankGoldSelection goldSelection_{BankGoldSelection::carried};
};

struct BankCommandResult final { bool consumedTick{}; std::uint32_t itemsMoved{}; std::uint64_t goldMoved{}; };
[[nodiscard]] BankCommandResult routeBankCommand(BankOverlayState& overlay,
                                                  const simulation::PlayerCommand& command,
                                                  PlayerItems& items);

} // namespace underworld::game::gameplay
