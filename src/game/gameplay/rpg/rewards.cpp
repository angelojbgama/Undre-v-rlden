#include "game/gameplay/rpg/rewards.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game::gameplay::rpg {
namespace {

std::uint64_t mix(std::uint64_t value, std::uint64_t input) noexcept {
    value ^= input + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    return value;
}

std::uint64_t stableHash(std::string_view value) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void validate(const RewardProfileDefinition& definition) {
    if (definition.id.empty()) { throw std::invalid_argument("reward profile id is empty"); }
    if (definition.loot.size() > maximumLootEntriesPerProfile) {
        throw std::invalid_argument("reward profile has too many loot entries");
    }
    for (const auto& entry : definition.loot) {
        if (entry.pickupDefinitionId.empty() || entry.chanceBasisPoints > 10000 ||
            entry.minimumCount == 0 || entry.minimumCount > entry.maximumCount ||
            entry.maximumCount > maximumDropCountPerEntry) {
            throw std::invalid_argument("reward loot entry is invalid");
        }
    }
}

} // namespace

void RewardProfileCatalog::add(RewardProfileDefinition definition) {
    validate(definition);
    const auto found = std::find_if(definitions_.begin(), definitions_.end(),
        [&](const auto& value) { return value.id == definition.id; });
    if (found != definitions_.end()) { throw std::logic_error("duplicate reward profile id"); }
    definitions_.push_back(std::move(definition));
}

const RewardProfileDefinition* RewardProfileCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto found = std::find_if(definitions_.begin(), definitions_.end(),
        [&](const auto& value) { return value.id == id; });
    return found == definitions_.end() ? nullptr : &*found;
}

const RewardProfileDefinition& RewardProfileCatalog::require(const simulation::DefinitionId& id) const {
    const auto* result = find(id);
    if (result == nullptr) { throw std::out_of_range("reward profile was not found"); }
    return *result;
}

RewardResolution RewardResolver::resolve(const RewardProfileDefinition& profile,
                                         const RewardRollContext& context) const {
    RewardResolution result{profile.experience, {}};
    std::uint64_t seed = stableHash(profile.id.value());
    seed = mix(seed, stableHash(context.mapId.value()));
    seed = mix(seed, context.sourceInstanceId.value);
    for (std::size_t index = 0; index < profile.loot.size(); ++index) {
        const auto& entry = profile.loot[index];
        auto entrySeed = mix(seed, static_cast<std::uint64_t>(index));
        if (entry.chanceBasisPoints != 10000 &&
            (entry.chanceBasisPoints == 0 || entrySeed % 10000 >= entry.chanceBasisPoints)) {
            continue;
        }
        const auto range = static_cast<std::uint64_t>(entry.maximumCount) - entry.minimumCount + 1;
        const auto count = static_cast<std::uint32_t>(entry.minimumCount + mix(entrySeed, 1) % range);
        result.loot.push_back({entry.pickupDefinitionId, count});
    }
    return result;
}

} // namespace underworld::game::gameplay::rpg
