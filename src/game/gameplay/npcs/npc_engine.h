#pragma once

#include "engine/core/coordinates.h"
#include "engine/core/color_rgba8.h"
#include "engine/simulation/definition_id.h"
#include "engine/simulation/entity_handle.h"
#include "engine/simulation/persistent_id.h"
#include "game/gameplay/combat_types.h"
#include "game/gameplay/facing_direction.h"

#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace underworld::game::gameplay::npcs {

struct NpcVisualSet final {
    simulation::DefinitionId id{};
    core::ColorRGBA8 markerColor{};
};

class NpcVisualCatalog final {
public:
    void add(NpcVisualSet visual);
    [[nodiscard]] const NpcVisualSet* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const NpcVisualSet& require(
        const simulation::DefinitionId& id) const;

private:
    std::unordered_map<simulation::DefinitionId, NpcVisualSet,
                       simulation::DefinitionIdHash> visuals_;
};

struct NpcDefinition final {
    simulation::DefinitionId id{};
    simulation::DefinitionId visualSetId{};
    InteractionArea interaction{};
    simulation::DefinitionId defaultDialogueId{};
    std::vector<std::string> tags;
};

class NpcCatalog final {
public:
    void add(NpcDefinition definition);
    [[nodiscard]] const NpcDefinition* find(
        const simulation::DefinitionId& id) const noexcept;
    [[nodiscard]] const NpcDefinition& require(
        const simulation::DefinitionId& id) const;

private:
    std::unordered_map<simulation::DefinitionId, NpcDefinition,
                       simulation::DefinitionIdHash> definitions_;
};

class NpcInstance final {
public:
    NpcInstance(const NpcInstance&) = delete;
    NpcInstance& operator=(const NpcInstance&) = delete;
    NpcInstance(NpcInstance&&) noexcept = default;
    NpcInstance& operator=(NpcInstance&&) noexcept = default;

    [[nodiscard]] simulation::EntityHandle handle() const noexcept { return handle_; }
    [[nodiscard]] const NpcDefinition& definition() const noexcept { return *definition_; }
    [[nodiscard]] core::WorldPointI position() const noexcept { return position_; }
    [[nodiscard]] FacingDirection facing() const noexcept { return facing_; }
    [[nodiscard]] InteractionArea interactionArea() const noexcept;

private:
    friend class NpcFactory;
    NpcInstance(simulation::EntityHandle handle, const NpcDefinition& definition,
                core::WorldPointI position, FacingDirection facing);

    simulation::EntityHandle handle_{};
    const NpcDefinition* definition_{};
    core::WorldPointI position_{};
    FacingDirection facing_{FacingDirection::down};
};

class NpcFactory final {
public:
    NpcFactory(simulation::EntityHandlePool& handles, const NpcCatalog& npcs)
        : handles_(handles), npcs_(npcs) {}

    [[nodiscard]] NpcInstance create(const simulation::DefinitionId& definitionId,
                                     core::WorldPointI position,
                                     FacingDirection facing = FacingDirection::down) const;

private:
    simulation::EntityHandlePool& handles_;
    const NpcCatalog& npcs_;
};

struct NpcInteractionResult final {
    simulation::EntityHandle npc{};
};

[[nodiscard]] NpcInteractionResult interactNearest(
    core::WorldPointI playerFeet, world::AabbI playerInteraction,
    std::span<NpcInstance> npcs);

// RuntimeWorld stores persistent wrappers around instances. Keep this adapter small and
// header-only so the NPC domain does not depend on the map runtime type.
template<class PersistentNpc>
[[nodiscard]] NpcInteractionResult interactNearest(
    core::WorldPointI playerFeet, world::AabbI playerInteraction,
    std::vector<PersistentNpc>& persistentNpcs) {
    NpcInteractionResult result{};
    std::int64_t bestDistance = 0;
    for (auto& persistent : persistentNpcs) {
        auto& npc = persistent.instance;
        const auto area = npc.interactionArea();
        const auto& bounds = area.bounds;
        const bool overlaps = playerInteraction.x < bounds.x + bounds.width &&
            playerInteraction.x + playerInteraction.width > bounds.x &&
            playerInteraction.y < bounds.y + bounds.height &&
            playerInteraction.y + playerInteraction.height > bounds.y;
        if (!area.enabled || !overlaps) { continue; }
        const auto dx = static_cast<std::int64_t>(playerFeet.x) - npc.position().x;
        const auto dy = static_cast<std::int64_t>(playerFeet.y) - npc.position().y;
        const auto distance = dx * dx + dy * dy;
        if (!result.npc || distance < bestDistance ||
            (distance == bestDistance && npc.handle().index < result.npc.index)) {
            result.npc = npc.handle();
            bestDistance = distance;
        }
    }
    return result;
}

[[nodiscard]] const simulation::DefinitionId& guardNpcId();
[[nodiscard]] const simulation::DefinitionId& scholarNpcId();
[[nodiscard]] NpcDefinition makeGuardNpcDefinition();
[[nodiscard]] NpcDefinition makeScholarNpcDefinition();

} // namespace underworld::game::gameplay::npcs
