#pragma once

#include "engine/simulation/definition_id.h"
#include "engine/simulation/persistent_id.h"

#include <cstdint>
#include <vector>

namespace underworld::game::gameplay::rpg {

struct LootEntryDefinition final {
    simulation::DefinitionId pickupDefinitionId{};
    std::uint32_t chanceBasisPoints{};
    std::uint32_t minimumCount{1};
    std::uint32_t maximumCount{1};
};

struct RewardProfileDefinition final {
    simulation::DefinitionId id{};
    std::uint64_t experience{};
    std::vector<LootEntryDefinition> loot;
};

struct LootDrop final {
    simulation::DefinitionId pickupDefinitionId{};
    std::uint32_t count{};
    [[nodiscard]] bool operator==(const LootDrop&) const = default;
};

struct RewardResolution final {
    std::uint64_t experience{};
    std::vector<LootDrop> loot;
};

struct RewardRollContext final {
    simulation::MapId mapId{};
    simulation::PersistentInstanceId sourceInstanceId{};
};

class RewardProfileCatalog final {
public:
    void add(RewardProfileDefinition definition);
    [[nodiscard]] const RewardProfileDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const RewardProfileDefinition& require(const simulation::DefinitionId& id) const;
private:
    std::vector<RewardProfileDefinition> definitions_;
};

class RewardResolver final {
public:
    [[nodiscard]] RewardResolution resolve(const RewardProfileDefinition& profile,
                                           const RewardRollContext& context) const;
};

inline constexpr std::uint32_t maximumLootEntriesPerProfile = 64;
inline constexpr std::uint32_t maximumDropCountPerEntry = 64;

} // namespace underworld::game::gameplay::rpg
