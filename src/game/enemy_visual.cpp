#include "game/enemy_visual.h"

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

void validateClips(const DirectionalAnimationClips& clips, const char* message) {
    for (const auto& clip : clips) {
        if (!clip) { throw std::invalid_argument(message); }
    }
}

} // namespace

void EnemyVisualCatalog::add(EnemyVisualSet visualSet) {
    if (visualSet.id.empty()) {
        throw std::invalid_argument("enemy visual set requires a stable id");
    }
    validateClips(visualSet.idle, "enemy visual set requires idle clips");
    validateClips(visualSet.walk, "enemy visual set requires resolved move clips");
    validateClips(visualSet.death, "enemy visual set requires resolved death clips");
    if (visualSet.hurt) validateClips(*visualSet.hurt, "enemy visual hurt clips are invalid");
    if (visualSet.dead) validateClips(*visualSet.dead, "enemy visual dead clips are invalid");
    for (const auto& [actionId, clips] : visualSet.attacks) {
        if (actionId.empty()) {
            throw std::invalid_argument("enemy visual action requires a stable id");
        }
        validateClips(clips, "enemy visual action requires directional clips");
    }
    const auto [position, inserted] = sets_.emplace(visualSet.id, std::move(visualSet));
    static_cast<void>(position);
    if (!inserted) { throw std::logic_error("duplicate enemy visual set id"); }
}

const EnemyVisualSet* EnemyVisualCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = sets_.find(id);
    return found == sets_.end() ? nullptr : &found->second;
}

const EnemyVisualSet& EnemyVisualCatalog::require(
    const simulation::DefinitionId& id) const {
    const EnemyVisualSet* result = find(id);
    if (result == nullptr) { throw std::out_of_range("enemy visual set was not found"); }
    return *result;
}

std::vector<simulation::DefinitionId> EnemyVisualCatalog::ids() const {
    std::vector<simulation::DefinitionId> result;
    result.reserve(sets_.size());
    for (const auto& [id, visualSet] : sets_) {
        static_cast<void>(visualSet);
        result.push_back(id);
    }
    return result;
}

EnemyVisualInstance::EnemyVisualInstance(simulation::EntityHandle handle,
                                         const EnemyVisualSet& visualSet)
    : handle_(handle), visualSet_(&visualSet) {
    if (!handle) { throw std::invalid_argument("enemy visual requires a valid handle"); }
}

void EnemyVisualInstance::update(const gameplay::creatures::EnemyInstance& enemy,
                                 std::uint64_t ticks) {
    if (enemy.handle() != handle_) {
        throw std::invalid_argument("enemy visual was updated with a different handle");
    }
    const auto state = enemy.state();
    const auto facing = enemy.facing();
    simulation::DefinitionId actionId;
    const DirectionalAnimationClips* clips = nullptr;
    if (state == gameplay::creatures::BehaviorState::dead) {
        clips = visualSet_->dead ? &*visualSet_->dead : &visualSet_->death;
    } else if (state == gameplay::creatures::BehaviorState::attack) {
        if (enemy.activeAttack()) {
            actionId = enemy.activeAttack()->definition->visualActionId;
            const auto found = visualSet_->attacks.find(actionId);
            if (found != visualSet_->attacks.end()) clips = &found->second;
        }
        if (!clips) clips = &visualSet_->idle;
    } else if (state == gameplay::creatures::BehaviorState::chase ||
               state == gameplay::creatures::BehaviorState::wander) {
        clips = &visualSet_->walk;
    } else {
        clips = &visualSet_->idle;
    }
    if (!initialized_ || state != state_ || facing != facing_ || actionId != actionId_) {
        animator_.play((*clips)[directionIndex(facing)]);
        initialized_ = true;
    }
    state_ = state;
    facing_ = facing;
    actionId_ = std::move(actionId);
    flipX_ = facing == gameplay::FacingDirection::right;
    animator_.updateTicks(ticks, markerEvents_);
}

std::vector<render::AnimationMarkerEvent> EnemyVisualInstance::consumeMarkerEvents() {
    std::vector<render::AnimationMarkerEvent> result;
    result.swap(markerEvents_);
    return result;
}

} // namespace underworld::game
