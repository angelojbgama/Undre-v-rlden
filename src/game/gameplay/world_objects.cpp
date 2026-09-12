#include "game/gameplay/world_objects.h"

#include "game/gameplay/combat_system.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace underworld::game::gameplay {
namespace {

void validate(const WorldObjectDefinition& definition) {
    // A definition with only a visual is a valid scenery object. Optional gameplay
    // capabilities add interaction, collision or state without being mandatory.
    if (definition.id.empty() || definition.visualSetId.empty()) {
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
         definition.destructible->hurtbox.height <= 0 ||
         definition.destructible->destructionDurationTicks == 0 ||
         definition.destructible->damageDurationTicks == 0)) {
        throw std::invalid_argument("object destructible capability is invalid");
    }
    if (definition.bankAccess && (!definition.interactable || definition.container || definition.destructible)) {
        throw std::invalid_argument("bank access requires an interactable non-container object");
    }
    if (definition.door && (definition.door->blockingBounds.width <= 0 ||
                            definition.door->blockingBounds.height <= 0)) {
        throw std::invalid_argument("door blocking bounds must be positive");
    }
    if (definition.activation) {
        if (definition.activation->mode == ObjectActivationMode::interactToggle) {
            if (!definition.interactable || definition.activation->activationBounds) {
                throw std::invalid_argument("interact-toggle activation requires an interactable object and no pressure bounds");
            }
        } else if (definition.activation->mode == ObjectActivationMode::playerPressure) {
            if (!definition.activation->activationBounds ||
                definition.activation->activationBounds->width <= 0 ||
                definition.activation->activationBounds->height <= 0 ||
                definition.activation->initialActive) {
                throw std::invalid_argument("player-pressure activation requires positive bounds and inactive initial state");
            }
        } else {
            throw std::invalid_argument("unknown object activation mode");
        }
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
            false, definition.id});
        observedHealth_ = combatant_->health.current;
    }
    if (definition.door) { doorState_ = definition.door->initialState; }
    if (definition.activation && definition.activation->mode == ObjectActivationMode::interactToggle) {
        activationActive_ = definition.activation->initialActive;
    }
}

bool WorldObjectInstance::setDoorState(DoorState state) noexcept {
    if (!definition_->door || doorState_ == state) { return false; }
    doorState_ = state;
    return true;
}

bool WorldObjectInstance::interactDoor() noexcept {
    if (!definition_->door || doorState_ == DoorState::locked) { return false; }
    return setDoorState(DoorState::open);
}

bool WorldObjectInstance::setActivationActive(bool active) noexcept {
    if (!definition_->activation || activationActive_ == active) {
        return false;
    }
    activationActive_ = active;
    return true;
}

bool WorldObjectInstance::toggleActivation() noexcept {
    if (!definition_->activation || definition_->activation->mode != ObjectActivationMode::interactToggle) {
        return false;
    }
    return setActivationActive(!activationActive_);
}

std::optional<world::AabbI> WorldObjectInstance::interactionArea() const noexcept {
    if ((!definition_->interactable && !definition_->door) || state_ == WorldObjectState::destroyed) {
        return std::nullopt;
    }
    const auto bounds = definition_->interactable ? definition_->interactable->bounds :
                                                     definition_->door->blockingBounds;
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
            (state_ == WorldObjectState::idle || state_ == WorldObjectState::damaged) &&
                !combatant_->health.depleted()};
}
CombatTargetRef WorldObjectInstance::combatTarget() {
    if (!combatant_) { throw std::logic_error("object is not destructible"); }
    return {*combatant_, hurtbox()};
}
bool WorldObjectInstance::open() noexcept {
    if (!definition_->interactable || state_ != WorldObjectState::idle) { return false; }
    state_ = WorldObjectState::opened;
    return true;
}
bool WorldObjectInstance::syncDamageState() noexcept {
    if (!combatant_ || combatant_->health.current >= observedHealth_) { return false; }
    observedHealth_ = combatant_->health.current;
    if (combatant_->health.depleted() ||
        (state_ != WorldObjectState::idle && state_ != WorldObjectState::damaged)) {
        return false;
    }
    state_ = WorldObjectState::damaged;
    damageTicksRemaining_ = definition_->destructible->damageDurationTicks;
    return true;
}
void WorldObjectInstance::advanceDamageTick() noexcept {
    if (state_ != WorldObjectState::damaged) { return; }
    if (damageTicksRemaining_ > 0) { --damageTicksRemaining_; }
    if (damageTicksRemaining_ == 0) { state_ = WorldObjectState::idle; }
}
bool WorldObjectInstance::syncDestructionState() noexcept {
    if (combatant_ && combatant_->health.depleted() &&
        (state_ == WorldObjectState::idle || state_ == WorldObjectState::damaged)) {
        state_ = WorldObjectState::destroying;
        damageTicksRemaining_ = 0;
        destructionTicksRemaining_ = definition_->destructible->destructionDurationTicks;
        return true;
    }
    return false;
}
void WorldObjectInstance::advanceDestructionTick() noexcept {
    if (state_ == WorldObjectState::destroying && destructionTicksRemaining_ > 0) {
        --destructionTicksRemaining_;
    }
}
bool WorldObjectInstance::destructionComplete() const noexcept {
    return state_ == WorldObjectState::destroying && destructionTicksRemaining_ == 0;
}
bool WorldObjectInstance::completeDestruction(simulation::EntityHandlePool& handles) noexcept {
    if (state_ != WorldObjectState::destroying) { return false; }
    state_ = WorldObjectState::destroyed;
    return handles.destroy(handle_);
}

WorldObjectInstance WorldObjectFactory::create(
    simulation::EntityHandlePool& handles, const simulation::DefinitionId& definitionId,
    core::WorldPointI position,
    std::span<const ItemStack> initialContents) const {
    return WorldObjectInstance(handles.create(), objects_.require(definitionId), position,
                               items_, initialContents);
}

WorldObjectInstance WorldObjectFactory::create(
    const simulation::DefinitionId& definitionId, core::WorldPointI position,
    std::span<const ItemStack> initialContents) const {
    if (legacyHandles_ == nullptr) { throw std::logic_error("WorldObjectFactory requires a handle pool"); }
    return create(*legacyHandles_, definitionId, position, initialContents);
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
    if (selected->isDoor()) {
        return selected->interactDoor() ? ObjectInteractionResult{selected->handle(), 0} :
                                          ObjectInteractionResult{};
    }
    if (!selected->open()) { return {}; }
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
