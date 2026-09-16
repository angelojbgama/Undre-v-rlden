#pragma once
#include "engine/simulation/definition_id.h"
#include "game/gameplay/world_objects.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
namespace underworld::game::gameplay {
inline constexpr std::string_view objectCollisionMaskChannel{"objectCollision"};
struct AnimationCollisionProfile final {
    simulation::DefinitionId animationId{};
    std::vector<ObjectCollisionDefinition> frames{};
    std::vector<std::uint32_t> frameDurations{};
    std::uint64_t durationTicks{};
};
class AnimationCollisionCatalog final {
public:
    void add(AnimationCollisionProfile profile) {
        if(profile.animationId.empty()||profile.frames.empty()||profile.frames.size()!=profile.frameDurations.size()) throw std::invalid_argument("animation collision profile requires id, frames, and matching durations");
        std::uint64_t duration{};
        for(const auto d:profile.frameDurations){if(d==0||duration>std::numeric_limits<std::uint64_t>::max()-d) throw std::invalid_argument("animation collision profile has an invalid duration");duration+=d;}
        profile.durationTicks=duration;const auto id=profile.animationId;const auto [it,inserted]=profiles_.emplace(id,std::move(profile));static_cast<void>(it);if(!inserted)throw std::logic_error("duplicate animation collision profile id");
    }
    [[nodiscard]] const AnimationCollisionProfile* find(const simulation::DefinitionId& id) const noexcept {const auto it=profiles_.find(id);return it==profiles_.end()?nullptr:&it->second;}
    [[nodiscard]] const AnimationCollisionProfile& require(const simulation::DefinitionId& id) const {const auto* p=find(id);if(!p)throw std::out_of_range("animation collision profile not found");return *p;}
    [[nodiscard]] const ObjectCollisionDefinition* sample(const simulation::DefinitionId& id,std::size_t frame) const noexcept {const auto* p=find(id);return !p||frame>=p->frames.size()?nullptr:&p->frames[frame];}
    [[nodiscard]] std::optional<std::size_t> frameIndexAtTick(const simulation::DefinitionId& id,std::uint64_t tick) const noexcept {
        const auto* p=find(id);if(!p||tick>=p->durationTicks)return std::nullopt;std::uint64_t start{};
        for(std::size_t i=0;i<p->frameDurations.size();++i){const auto end=start+p->frameDurations[i];if(tick<end)return i;start=end;}return std::nullopt;
    }
    [[nodiscard]] std::optional<std::uint64_t> durationTicks(const simulation::DefinitionId& id) const noexcept {const auto* p=find(id);return p?std::optional<std::uint64_t>{p->durationTicks}:std::nullopt;}
private:
    std::unordered_map<simulation::DefinitionId,AnimationCollisionProfile,simulation::DefinitionIdHash> profiles_;
};
} // namespace underworld::game::gameplay
