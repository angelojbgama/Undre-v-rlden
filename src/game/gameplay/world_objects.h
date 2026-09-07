#pragma once

#include "engine/core/coordinates.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/entity_handle.h"
#include "engine/world/collision.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/items.h"

#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace underworld::game::gameplay {

struct ObjectInteractionDefinition final { world::AabbI bounds{}; };
struct ObjectContainerDefinition final { std::size_t capacity{}; };
struct ObjectDestructibleDefinition final {
    int maximumHealth{};
    world::AabbI hurtbox{};
    // Matches the current crate.break presentation clip (7 frames x 4 ticks),
    // while remaining authoritative logical gameplay data.
    std::uint32_t destructionDurationTicks{28};
};
struct ObjectBankAccessDefinition final {};
enum class DoorState { locked, closed, open };
struct ObjectDoorDefinition final {
    DoorState initialState{DoorState::closed};
    world::AabbI blockingBounds{};
};

struct WorldObjectDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualSetId{};
    std::optional<ObjectInteractionDefinition> interactable{};
    std::optional<ObjectContainerDefinition> container{};
    std::optional<ObjectDestructibleDefinition> destructible{};
    std::optional<ObjectBankAccessDefinition> bankAccess{};
    std::optional<ObjectDoorDefinition> door{};
};

class WorldObjectCatalog final {
public:
    void add(WorldObjectDefinition definition);
    [[nodiscard]] const WorldObjectDefinition* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const WorldObjectDefinition& require(
        const simulation::DefinitionId& id) const;

private:
    std::unordered_map<simulation::DefinitionId, WorldObjectDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

enum class WorldObjectState { idle, opened, destroying, destroyed };

class WorldObjectInstance final {
public:
    WorldObjectInstance(const WorldObjectInstance&) = delete;
    WorldObjectInstance& operator=(const WorldObjectInstance&) = delete;
    WorldObjectInstance(WorldObjectInstance&&) noexcept = default;
    WorldObjectInstance& operator=(WorldObjectInstance&&) noexcept = default;

    [[nodiscard]] simulation::EntityHandle handle() const noexcept { return handle_; }
    [[nodiscard]] const WorldObjectDefinition& definition() const noexcept {
        return *definition_;
    }
    [[nodiscard]] core::WorldPointI position() const noexcept { return position_; }
    [[nodiscard]] WorldObjectState state() const noexcept { return state_; }
    [[nodiscard]] bool isDoor() const noexcept { return definition_->door.has_value(); }
    [[nodiscard]] DoorState doorState() const noexcept { return doorState_; }
    [[nodiscard]] bool setDoorState(DoorState state) noexcept;
    [[nodiscard]] bool interactDoor() noexcept;
    [[nodiscard]] std::optional<world::AabbI> interactionArea() const noexcept;
    [[nodiscard]] ItemContainer* contents() noexcept;
    [[nodiscard]] const ItemContainer* contents() const noexcept;
    [[nodiscard]] CombatantState* combatant() noexcept;
    [[nodiscard]] const CombatantState* combatant() const noexcept;
    [[nodiscard]] Hurtbox hurtbox() const noexcept;
    [[nodiscard]] CombatTargetRef combatTarget();
    [[nodiscard]] bool open() noexcept;
    [[nodiscard]] bool syncDestructionState() noexcept;
    void advanceDestructionTick() noexcept;
    [[nodiscard]] bool destructionComplete() const noexcept;
    [[nodiscard]] bool completeDestruction(simulation::EntityHandlePool& handles) noexcept;

private:
    friend class WorldObjectFactory;
    WorldObjectInstance(simulation::EntityHandle handle,
                        const WorldObjectDefinition& definition,
                        core::WorldPointI position, const ItemCatalog& items,
                        std::span<const ItemStack> initialContents);

    simulation::EntityHandle handle_{};
    const WorldObjectDefinition* definition_{};
    core::WorldPointI position_{};
    WorldObjectState state_{WorldObjectState::idle};
    std::optional<ItemContainer> contents_{};
    std::optional<CombatantState> combatant_{};
    std::uint32_t destructionTicksRemaining_{};
    DoorState doorState_{DoorState::closed};
};

class WorldObjectFactory final {
public:
    WorldObjectFactory(const WorldObjectCatalog& objects, const ItemCatalog& items)
        : objects_(objects), items_(items) {}
    WorldObjectFactory(simulation::EntityHandlePool& handles,
                       const WorldObjectCatalog& objects, const ItemCatalog& items)
        : objects_(objects), items_(items), legacyHandles_(&handles) {}
    [[nodiscard]] WorldObjectInstance create(
        simulation::EntityHandlePool& handles,
        const simulation::DefinitionId& definitionId, core::WorldPointI position,
        std::span<const ItemStack> initialContents = {}) const;
    [[nodiscard]] WorldObjectInstance create(
        const simulation::DefinitionId& definitionId, core::WorldPointI position,
        std::span<const ItemStack> initialContents = {}) const;

private:
    const WorldObjectCatalog& objects_;
    const ItemCatalog& items_;
    simulation::EntityHandlePool* legacyHandles_{};
};

struct ObjectInteractionResult final {
    simulation::EntityHandle object{};
    std::uint64_t itemsTransferred{};
};

[[nodiscard]] ObjectInteractionResult interactNearest(
    core::WorldPointI playerFeet, world::AabbI playerInteraction,
    ItemContainer& playerInventory, std::span<WorldObjectInstance> objects);

} // namespace underworld::game::gameplay
