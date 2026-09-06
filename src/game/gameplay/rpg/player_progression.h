#pragma once

#include "engine/simulation/definition_id.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace underworld::game::gameplay::rpg {

struct PlayerBaseStats final { int maximumHealth{}; };

struct PlayerProgressionDefinition final {
    simulation::DefinitionId id{};
    PlayerBaseStats baseStats{};
    std::vector<std::uint64_t> cumulativeExperienceThresholds;
};

struct ExperienceGainResult final {
    std::uint64_t requested{};
    std::uint64_t granted{};
    std::uint32_t previousLevel{};
    std::uint32_t newLevel{};
    [[nodiscard]] bool leveledUp() const noexcept { return newLevel > previousLevel; }
};

class PlayerProgressionState final {
public:
    explicit PlayerProgressionState(const PlayerProgressionDefinition& definition) noexcept
        : definition_(&definition) {}
    [[nodiscard]] std::uint64_t totalExperience() const noexcept { return totalExperience_; }
    [[nodiscard]] std::uint32_t level() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> nextLevelExperienceThreshold() const noexcept;
    [[nodiscard]] std::uint64_t experienceIntoCurrentLevel() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> experienceNeededForNextLevel() const noexcept;
    [[nodiscard]] ExperienceGainResult grantExperience(std::uint64_t amount) noexcept;
    [[nodiscard]] bool restoreExperience(std::uint64_t totalExperience) noexcept;
    [[nodiscard]] const PlayerProgressionDefinition& definition() const noexcept { return *definition_; }
private:
    const PlayerProgressionDefinition* definition_;
    std::uint64_t totalExperience_{};
};

class PlayerProgressionCatalog final {
public:
    void add(PlayerProgressionDefinition definition);
    [[nodiscard]] const PlayerProgressionDefinition* find(const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const PlayerProgressionDefinition& require(const simulation::DefinitionId& id) const;
private:
    std::vector<PlayerProgressionDefinition> definitions_;
};

[[nodiscard]] const simulation::DefinitionId& defaultPlayerProgressionId() noexcept;

} // namespace underworld::game::gameplay::rpg
