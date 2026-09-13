#include "game/player_visual.h"

#include <stdexcept>
#include <utility>

namespace underworld::game {

namespace {

std::size_t directionIndex(gameplay::FacingDirection facing) noexcept {
    switch (facing) {
    case gameplay::FacingDirection::down: return 0;
    case gameplay::FacingDirection::up: return 1;
    case gameplay::FacingDirection::left:
    case gameplay::FacingDirection::right: return 2;
    }
    return 0;
}

PlayerDirectionalClips actionClips(
    const PlayerVisualSet& set, std::string_view actionId) {
    const auto found = set.actions.find(std::string(actionId));
    return found == set.actions.end()
        ? PlayerDirectionalClips{}
        : found->second;
}

} // namespace

void PlayerVisualSetCatalog::add(PlayerVisualSet set) {
    if (set.id.empty()) {
        throw std::invalid_argument("Player runtime visual requires id");
    }
    const auto [unused, inserted] = sets_.emplace(set.id, std::move(set));
    static_cast<void>(unused);
    if (!inserted) {
        throw std::logic_error("duplicate Player runtime visual");
    }
}

const PlayerVisualSet* PlayerVisualSetCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = sets_.find(id);
    return found == sets_.end() ? nullptr : &found->second;
}

const PlayerVisualSet& PlayerVisualSetCatalog::require(
    const simulation::DefinitionId& id) const {
    const auto* value = find(id);
    if (!value) throw std::out_of_range("Player runtime visual not found");
    return *value;
}

PlayerVisual::PlayerVisual(const PlayerVisualSet& visualSet)
    : PlayerVisual(
          visualSet.idle,
          visualSet.walk,
          actionClips(visualSet, "sword"),
          actionClips(visualSet, "bow"),
          visualSet.hurt ? *visualSet.hurt : DirectionalClips{}) {
    authoredSideCanonicalLeft_ = true;
}

PlayerVisual::PlayerVisual(DirectionalClips idleClips, DirectionalClips walkClips,
                           DirectionalClips swordClips, DirectionalClips bowClips,
                           DirectionalClips hurtClips)
    : idleClips_(std::move(idleClips)), walkClips_(std::move(walkClips)),
      swordClips_(std::move(swordClips)), bowClips_(std::move(bowClips)),
      hurtClips_(std::move(hurtClips)) {
    for (const auto& clip : idleClips_) {
        if (!clip) {
            throw std::invalid_argument("player visual requires every idle direction clip");
        }
    }
    for (const auto& clip : walkClips_) {
        if (!clip) {
            throw std::invalid_argument("player visual requires every walk direction clip");
        }
    }
    const bool hasSword = swordClips_[0] != nullptr;
    const bool hasBow = bowClips_[0] != nullptr;
    for (const auto& clip : swordClips_) {
        if ((clip != nullptr) != hasSword) {
            throw std::invalid_argument("player visual sword clips must be all present or absent");
        }
    }
    for (const auto& clip : bowClips_) {
        if ((clip != nullptr) != hasBow) {
            throw std::invalid_argument("player visual bow clips must be all present or absent");
        }
    }
    const bool hasHurt = hurtClips_[0] != nullptr;
    for (const auto& clip : hurtClips_) {
        if ((clip != nullptr) != hasHurt) {
            throw std::invalid_argument("player visual hurt clips must be all present or absent");
        }
    }
}

const std::shared_ptr<const render::AnimationClip>& PlayerVisual::selectedClip(
    gameplay::PlayerMotionState motion, gameplay::FacingDirection facing,
    gameplay::PlayerActionState action) const noexcept {
    if (action == gameplay::PlayerActionState::swordAttack && swordClips_[0]) {
        return swordClips_[directionIndex(facing)];
    }
    if (action == gameplay::PlayerActionState::bowAttack && bowClips_[0]) {
        return bowClips_[directionIndex(facing)];
    }
    if (action == gameplay::PlayerActionState::hurt && hurtClips_[0]) {
        return hurtClips_[directionIndex(facing)];
    }
    const auto& clips =
        motion == gameplay::PlayerMotionState::walk ? walkClips_ : idleClips_;
    return clips[directionIndex(facing)];
}

void PlayerVisual::update(gameplay::PlayerMotionState motion,
                          gameplay::FacingDirection facing,
                          gameplay::PlayerActionState action,
                          std::uint64_t ticks) {
    if (!initialized_ || motion != motion_ || facing != facing_ || action != action_) {
        animator_.play(selectedClip(motion, facing, action));
        initialized_ = true;
    }
    motion_ = motion;
    facing_ = facing;
    action_ = action;

    if (authoredSideCanonicalLeft_) {
        flipX_ = facing == gameplay::FacingDirection::right;
    } else if (action == gameplay::PlayerActionState::swordAttack) {
        flipX_ = facing == gameplay::FacingDirection::left;
    } else {
        flipX_ = facing == gameplay::FacingDirection::right;
    }

    animator_.updateTicks(ticks, markerEvents_);
}

std::vector<render::AnimationMarkerEvent> PlayerVisual::consumeMarkerEvents() {
    std::vector<render::AnimationMarkerEvent> result;
    result.swap(markerEvents_);
    return result;
}

} // namespace underworld::game
