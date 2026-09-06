#include "game/gameplay/rpg/player_progression.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay::rpg {

std::uint32_t PlayerProgressionState::level() const noexcept {
    const auto it = std::upper_bound(definition_->cumulativeExperienceThresholds.begin(),
                                     definition_->cumulativeExperienceThresholds.end(), totalExperience_);
    return static_cast<std::uint32_t>(std::max<std::ptrdiff_t>(1, it - definition_->cumulativeExperienceThresholds.begin()));
}
std::optional<std::uint64_t> PlayerProgressionState::nextLevelExperienceThreshold() const noexcept {
    const auto current = level();
    return current < definition_->cumulativeExperienceThresholds.size()
        ? std::optional<std::uint64_t>{definition_->cumulativeExperienceThresholds[current]} : std::nullopt;
}
std::uint64_t PlayerProgressionState::experienceIntoCurrentLevel() const noexcept {
    const auto current = level();
    const auto base = definition_->cumulativeExperienceThresholds[current - 1];
    return totalExperience_ - base;
}
std::optional<std::uint64_t> PlayerProgressionState::experienceNeededForNextLevel() const noexcept {
    const auto next = nextLevelExperienceThreshold();
    return next ? std::optional<std::uint64_t>{*next - totalExperience_} : std::nullopt;
}
ExperienceGainResult PlayerProgressionState::grantExperience(std::uint64_t amount) noexcept {
    const auto previous = level();
    const auto available = std::numeric_limits<std::uint64_t>::max() - totalExperience_;
    const auto granted = std::min(amount, available);
    totalExperience_ += granted;
    return {amount, granted, previous, level()};
}
bool PlayerProgressionState::restoreExperience(std::uint64_t value) noexcept {
    totalExperience_ = value;
    return true;
}
void PlayerProgressionCatalog::add(PlayerProgressionDefinition definition) {
    if (definition.id.empty() || definition.baseStats.maximumHealth <= 0 || definition.cumulativeExperienceThresholds.empty()) throw std::logic_error("invalid player progression definition");
    if (!std::is_sorted(definition.cumulativeExperienceThresholds.begin(), definition.cumulativeExperienceThresholds.end()) || definition.cumulativeExperienceThresholds.front() != 0 || std::adjacent_find(definition.cumulativeExperienceThresholds.begin(), definition.cumulativeExperienceThresholds.end()) != definition.cumulativeExperienceThresholds.end()) throw std::logic_error("invalid player progression curve");
    if (find(definition.id)) throw std::logic_error("duplicate player progression definition");
    definitions_.push_back(std::move(definition));
}
const PlayerProgressionDefinition* PlayerProgressionCatalog::find(const simulation::DefinitionId& id) const noexcept { const auto it=std::find_if(definitions_.begin(),definitions_.end(),[&](const auto& v){return v.id==id;});return it==definitions_.end()?nullptr:&*it; }
const PlayerProgressionDefinition& PlayerProgressionCatalog::require(const simulation::DefinitionId& id) const { const auto* v=find(id);if(!v)throw std::out_of_range("unknown player progression definition");return *v; }
const simulation::DefinitionId& defaultPlayerProgressionId() noexcept { static const simulation::DefinitionId id{"progression.player.default"}; return id; }
} // namespace underworld::game::gameplay::rpg
