#pragma once
#include "engine/simulation/player_command.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/rpg/shops.h"
#include <cstddef>
#include <optional>

namespace underworld::game::gameplay {
enum class ShopOverlayMode { buy, sell };
class ShopOverlayState final {
public:
    static constexpr std::size_t columns = 10;
    static constexpr std::size_t inventoryRows = 3;
    void open(const rpg::ShopDefinition& shop) noexcept;
    void close() noexcept;
    [[nodiscard]] bool open() const noexcept { return open_; }
    [[nodiscard]] const simulation::DefinitionId& activeShopId() const noexcept { return activeShopId_; }
    [[nodiscard]] ShopOverlayMode mode() const noexcept { return mode_; }
    [[nodiscard]] std::size_t buySelection() const noexcept { return buySelection_; }
    [[nodiscard]] std::size_t inventorySelection() const noexcept { return inventorySelection_; }
    [[nodiscard]] const std::optional<rpg::ShopTransactionStatus>& feedback() const noexcept { return feedback_; }
    void moveBuySelection(const rpg::ShopDefinition&, int delta) noexcept;
    void moveInventorySelection(int x, int y) noexcept;
    void switchMode(const rpg::ShopDefinition&) noexcept;
    void setFeedback(rpg::ShopTransactionStatus status) noexcept { feedback_ = status; }
private:
    bool open_{};
    simulation::DefinitionId activeShopId_{};
    ShopOverlayMode mode_{ShopOverlayMode::buy};
    std::size_t buySelection_{};
    std::size_t inventorySelection_{};
    std::optional<rpg::ShopTransactionStatus> feedback_{};
};
struct ShopCommandResult final { bool consumedTick{}; std::optional<rpg::ShopTransactionResult> transaction{}; };
[[nodiscard]] ShopCommandResult routeShopCommand(ShopOverlayState&, const simulation::PlayerCommand&,
    const rpg::ShopDefinition&, PlayerItems&, const rpg::ShopTransactionService&);
} // namespace underworld::game::gameplay
