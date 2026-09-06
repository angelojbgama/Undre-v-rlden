#pragma once

#include "game/gameplay/player_items.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace underworld::game::gameplay::rpg {

struct ShopOfferDefinition final {
    simulation::DefinitionId itemId{};
    std::optional<std::uint64_t> playerBuyPrice{};
    std::optional<std::uint64_t> playerSellPrice{};
};

struct ShopDefinition final { simulation::DefinitionId id{}; std::vector<ShopOfferDefinition> offers; };

class ShopCatalog final {
public:
    void add(ShopDefinition definition);
    [[nodiscard]] const ShopDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const ShopDefinition& require(const simulation::DefinitionId& id) const;
private:
    std::vector<ShopDefinition> definitions_;
};

[[nodiscard]] const ShopOfferDefinition* findOffer(const ShopDefinition& shop,
                                                    const simulation::DefinitionId& itemId) noexcept;

enum class ShopTransactionStatus {
    success, offerNotFound, purchaseUnavailable, saleUnavailable,
    insufficientGold, inventoryFull, invalidInventorySlot, walletCapacityExceeded
};

struct ShopTransactionResult final {
    ShopTransactionStatus status{ShopTransactionStatus::success};
    simulation::DefinitionId itemId{};
    std::uint32_t quantity{};
    std::uint64_t gold{};
    [[nodiscard]] explicit operator bool() const noexcept { return status == ShopTransactionStatus::success; }
};

class ShopTransactionService final {
public:
    [[nodiscard]] ShopTransactionResult buyOne(const ShopDefinition& shop,
                                                const simulation::DefinitionId& itemId,
                                                PlayerItems& playerItems) const;
    [[nodiscard]] ShopTransactionResult sellOneFromSlot(const ShopDefinition& shop,
                                                         std::size_t inventorySlot,
                                                         PlayerItems& playerItems) const;
};

inline constexpr std::size_t maximumShopOffers = 128;

} // namespace underworld::game::gameplay::rpg
