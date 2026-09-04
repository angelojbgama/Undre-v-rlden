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
};

struct WorldObjectDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualSetId{};
    std::optional<ObjectInteractionDefinition> interactable{};
    std::optional<ObjectContainerDefinition> container{};
    std::optional<ObjectDestructibleDefinition> destructible{};
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
    [[nodiscard]] std::optional<world::AabbI> interactionArea() const noexcept;
    [[nodiscard]] ItemContainer* contents() noexcept;
    [[nodiscard]] const ItemContainer* contents() const noexcept;
    [[nodiscard]] CombatantState* combatant() noexcept;
    [[nodiscard]] const CombatantState* combatant() const noexcept;
    [[nodiscard]] Hurtbox hurtbox() const noexcept;
    [[nodiscard]] CombatTargetRef combatTarget();
    void open() noexcept;
    [[nodiscard]] bool syncDestructionState() noexcept;
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
};

class WorldObjectFactory final {
public:
    WorldObjectFactory(simulation::EntityHandlePool& handles,
                       const WorldObjectCatalog& objects, const ItemCatalog& items)
        : handles_(handles), objects_(objects), items_(items) {}
    [[nodiscard]] WorldObjectInstance create(
        const simulation::DefinitionId& definitionId, core::WorldPointI position,
        std::span<const ItemStack> initialContents = {}) const;

private:
    simulation::EntityHandlePool& handles_;
    const WorldObjectCatalog& objects_;
    const ItemCatalog& items_;
};

struct ObjectInteractionResult final {
    simulation::EntityHandle object{};
    std::uint64_t itemsTransferred{};
};

[[nodiscard]] ObjectInteractionResult interactNearest(
    core::WorldPointI playerFeet, world::AabbI playerInteraction,
    ItemContainer& playerInventory, std::span<WorldObjectInstance> objects);

} // namespace underworld::game::gameplay
