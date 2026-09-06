#pragma once

#include "game/gameplay/items.h"
#include "game/gameplay/player_items.h"
#include "game/gameplay/rpg/player_progression.h"

#include <cstdint>
#include <vector>

namespace underworld::game::gameplay::rpg {

struct RewardItemGrantDefinition final { simulation::DefinitionId itemId{}; std::uint32_t quantity{1}; };
struct RewardGrantDefinition final { simulation::DefinitionId id{}; std::uint64_t experience{}; std::uint64_t gold{}; std::vector<RewardItemGrantDefinition> items; };

class RewardGrantCatalog final {
public:
    void add(RewardGrantDefinition definition);
    [[nodiscard]] const RewardGrantDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const RewardGrantDefinition& require(const simulation::DefinitionId& id) const;
private:
    std::vector<RewardGrantDefinition> definitions_;
};

struct RewardGrantResult final {
    bool applied{};
    bool blockedByStorage{};
    ExperienceGainResult experience{};
    std::uint64_t goldGranted{};
};

class RewardGrantService final {
public:
    [[nodiscard]] RewardGrantResult grant(const RewardGrantDefinition& definition,
                                          PlayerProgressionState& progression,
                                          PlayerItems& items) const;
};

inline constexpr std::uint32_t maximumRewardItems = 64;

} // namespace underworld::game::gameplay::rpg
