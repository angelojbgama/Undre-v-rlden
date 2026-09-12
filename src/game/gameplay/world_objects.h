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
// Runtime representation of an authored pixel mask. The content compiler turns
// mask pixels into deterministic compact AABBs, so movement never samples image pixels.
struct ObjectCollisionDefinition final { std::vector<world::AabbI> regions; };
struct ObjectOcclusionDefinition final {
    std::uint32_t width{};
    std::uint32_t height{};
    core::PointI origin{};
    std::vector<std::uint8_t> cells;
};
struct ObjectDestructibleDefinition final {
    int maximumHealth{};
    world::AabbI hurtbox{};
    // Matches the current crate.break presentation clip (7 frames x 4 ticks),
    // while remaining authoritative logical gameplay data.
    std::uint32_t destructionDurationTicks{28};
    std::uint32_t damageDurationTicks{8};
    // Reuses the RPG reward resolver used by enemies. Missing profile means no drop.
    std::optional<simulation::DefinitionId> rewardProfileId{};
    // Zelda-like props disappear after their break sequence by default. Set true
    // only for authored rubble/remains that should persist visually.
    bool leaveDestroyedResidue{false};
};
struct ObjectBankAccessDefinition final {};
enum class DoorState { locked, closed, open };
struct ObjectDoorDefinition final {
    DoorState initialState{DoorState::closed};
    world::AabbI blockingBounds{};
    // Compatibility for old authored doors. New Studio doors use the generic collision component.
    bool hasBlockingBounds{true};
};
enum class ObjectActivationMode { interactToggle, playerPressure };
struct ObjectActivationDefinition final {
    ObjectActivationMode mode{ObjectActivationMode::interactToggle};
    bool initialActive{};
    std::optional<world::AabbI> activationBounds{};
    [[nodiscard]] bool operator==(const ObjectActivationDefinition&) const noexcept = default;
};

struct WorldObjectDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualSetId{};
    std::optional<ObjectInteractionDefinition> interactable{};
    std::optional<ObjectContainerDefinition> container{};
    std::optional<ObjectDestructibleDefinition> destructible{};
    std::optional<ObjectBankAccessDefinition> bankAccess{};
    std::optional<ObjectDoorDefinition> door{};
    std::optional<ObjectActivationDefinition> activation{};
    std::optional<ObjectCollisionDefinition> collision{};
    // Presentation metadata kept separate from collision. The anchor is relative
    // to the object's world position; only Y participates in depth sorting.
    core::PointI depthAnchor{};
    std::optional<ObjectOcclusionDefinition> occlusion{};
};

class WorldObjectCatalog final {
public:
    void add(WorldObjectDefinition definition);
    [[nodiscard]] const WorldObjectDefinition* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const WorldObjectDefinition& require(
        const simulation::DefinitionId& id) const;
    [[nodiscard]] const std::unordered_map<simulation::DefinitionId, WorldObjectDefinition,
                                           simulation::DefinitionIdHash>& values() const noexcept {
        return definitions_;
    }

private:
    std::unordered_map<simulation::DefinitionId, WorldObjectDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

enum class WorldObjectState { idle, damaged, opened, destroying, destroyed };

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
    [[nodiscard]] bool hasActivation() const noexcept { return definition_->activation.has_value(); }
    [[nodiscard]] bool activationActive() const noexcept { return activationActive_; }
    [[nodiscard]] bool setActivationActive(bool active) noexcept;
    [[nodiscard]] bool toggleActivation() noexcept;
    [[nodiscard]] std::optional<world::AabbI> interactionArea() const noexcept;
    [[nodiscard]] ItemContainer* contents() noexcept;
    [[nodiscard]] const ItemContainer* contents() const noexcept;
    [[nodiscard]] CombatantState* combatant() noexcept;
    [[nodiscard]] const CombatantState* combatant() const noexcept;
    [[nodiscard]] Hurtbox hurtbox() const noexcept;
    [[nodiscard]] CombatTargetRef combatTarget();
    [[nodiscard]] bool open() noexcept;
    [[nodiscard]] bool syncDamageState() noexcept;
    void advanceDamageTick() noexcept;
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
    int observedHealth_{};
    std::uint32_t damageTicksRemaining_{};
    std::uint32_t destructionTicksRemaining_{};
    DoorState doorState_{DoorState::closed};
    bool activationActive_{};
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
