#include "game/gameplay/world_objects.h"

#include "game/gameplay/combat_system.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay {
namespace {

void validate(const WorldObjectDefinition& definition) {
    if (definition.id.empty() || definition.visualSetId.empty() ||
        (!definition.interactable && !definition.container && !definition.destructible)) {
        throw std::invalid_argument("world object definition is incomplete");
    }
    if (definition.interactable && (definition.interactable->bounds.width <= 0 ||
                                    definition.interactable->bounds.height <= 0)) {
        throw std::invalid_argument("object interaction area must be positive");
    }
    if (definition.container && definition.container->capacity == 0) {
        throw std::invalid_argument("object container capacity must be positive");
    }
    if (definition.destructible &&
        (definition.destructible->maximumHealth <= 0 ||
         definition.destructible->hurtbox.width <= 0 ||
         definition.destructible->hurtbox.height <= 0)) {
        throw std::invalid_argument("object destructible capability is invalid");
    }
}

bool handleBefore(simulation::EntityHandle left, simulation::EntityHandle right) noexcept {
    return left.index < right.index ||
           (left.index == right.index && left.generation < right.generation);
}

std::int64_t distanceSquared(core::WorldPointI left, core::WorldPointI right) noexcept {
    const std::int64_t x = static_cast<std::int64_t>(left.x) - right.x;
    const std::int64_t y = static_cast<std::int64_t>(left.y) - right.y;
    return x * x + y * y;
}

} // namespace

void WorldObjectCatalog::add(WorldObjectDefinition definition) {
    validate(definition);
    const auto [position, inserted] = definitions_.emplace(definition.id, std::move(definition));
    static_cast<void>(position);
    if (!inserted) { throw std::logic_error("duplicate world object definition id"); }
}
const WorldObjectDefinition* WorldObjectCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}
const WorldObjectDefinition& WorldObjectCatalog::require(
    const simulation::DefinitionId& id) const {
    const auto* definition = find(id);
    if (definition == nullptr) { throw std::out_of_range("world object definition not found"); }
    return *definition;
}

WorldObjectInstance::WorldObjectInstance(
    simulation::EntityHandle handle, const WorldObjectDefinition& definition,
    core::WorldPointI position, const ItemCatalog& items,
    std::span<const ItemStack> initialContents)
    : handle_(handle), definition_(&definition), position_(position) {
    if (!handle) { throw std::invalid_argument("world object requires a valid handle"); }
    if (definition.container) {
        contents_.emplace(definition.container->capacity, items);
        for (const ItemStack& stack : initialContents) {
            const AddResult result = contents_->add(stack.itemId, stack.quantity);
            if (result.remainder != 0) {
                throw std::invalid_argument("initial object contents exceed container capacity");
            }
        }
    } else if (!initialContents.empty()) {
        throw std::invalid_argument("non-container object cannot have contents");
    }
    if (definition.destructible) {
        combatant_.emplace(CombatantState{
            handle, Faction::environment, Health{definition.destructible->maximumHealth}, 0,
            false});
    }
}

std::optional<world::AabbI> WorldObjectInstance::interactionArea() const noexcept {
    if (!definition_->interactable || state_ == WorldObjectState::destroyed) {
        return std::nullopt;
    }
    const auto bounds = definition_->interactable->bounds;
    return world::AabbI{position_.x + bounds.x, position_.y + bounds.y,
                        bounds.width, bounds.height};
}
ItemContainer* WorldObjectInstance::contents() noexcept {
    return contents_ ? &*contents_ : nullptr;
}
const ItemContainer* WorldObjectInstance::contents() const noexcept {
    return contents_ ? &*contents_ : nullptr;
}
CombatantState* WorldObjectInstance::combatant() noexcept {
    return combatant_ ? &*combatant_ : nullptr;
}
const CombatantState* WorldObjectInstance::combatant() const noexcept {
    return combatant_ ? &*combatant_ : nullptr;
}
Hurtbox WorldObjectInstance::hurtbox() const noexcept {
    if (!definition_->destructible || !combatant_) { return {{}, false}; }
    const auto bounds = definition_->destructible->hurtbox;
    return {{position_.x + bounds.x, position_.y + bounds.y, bounds.width, bounds.height},
            state_ == WorldObjectState::idle && !combatant_->health.depleted()};
}
CombatTargetRef WorldObjectInstance::combatTarget() {
    if (!combatant_) { throw std::logic_error("object is not destructible"); }
    return {*combatant_, hurtbox()};
}
void WorldObjectInstance::open() noexcept {
    if (definition_->interactable) { state_ = WorldObjectState::opened; }
}
bool WorldObjectInstance::syncDestructionState() noexcept {
    if (combatant_ && combatant_->health.depleted() && state_ == WorldObjectState::idle) {
        state_ = WorldObjectState::destroying;
        return true;
    }
    return false;
}
bool WorldObjectInstance::completeDestruction(simulation::EntityHandlePool& handles) noexcept {
    if (state_ != WorldObjectState::destroying) { return false; }
    state_ = WorldObjectState::destroyed;
    return handles.destroy(handle_);
}

WorldObjectInstance WorldObjectFactory::create(
    const simulation::DefinitionId& definitionId, core::WorldPointI position,
    std::span<const ItemStack> initialContents) const {
    return WorldObjectInstance(handles_.create(), objects_.require(definitionId), position,
                               items_, initialContents);
}

ObjectInteractionResult interactNearest(
    core::WorldPointI playerFeet, world::AabbI playerInteraction,
    ItemContainer& playerInventory, std::span<WorldObjectInstance> objects) {
    WorldObjectInstance* selected{};
    std::int64_t selectedDistance{};
    for (WorldObjectInstance& object : objects) {
        const auto area = object.interactionArea();
        if (!area || !overlaps(playerInteraction, *area)) { continue; }
        const auto distance = distanceSquared(playerFeet, object.position());
        if (selected == nullptr || distance < selectedDistance ||
            (distance == selectedDistance && handleBefore(object.handle(), selected->handle()))) {
            selected = &object;
            selectedDistance = distance;
        }
    }
    if (selected == nullptr) { return {}; }
    selected->open();
    std::uint64_t transferred{};
    if (ItemContainer* contents = selected->contents()) {
        for (std::size_t index = 0; index < contents->capacity(); ++index) {
            const auto slot = contents->slot(index);
            if (!slot) { continue; }
            transferred += contents->transferTo(playerInventory, slot->itemId, slot->quantity);
        }
    }
    return {selected->handle(), transferred};
}

} // namespace underworld::game::gameplay
