#include "game/gameplay/npcs/npc_engine.h"

#include "game/gameplay/dialogue/dialogue_model.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game::gameplay::npcs {
namespace {

void validate(const NpcDefinition& definition) {
    if (definition.id.empty() || definition.visualSetId.empty() ||
        !definition.interaction.enabled || definition.interaction.bounds.width <= 0 ||
        definition.interaction.bounds.height <= 0) {
        throw std::invalid_argument("NPC definition is incomplete");
    }
}

std::int64_t distanceSquared(core::WorldPointI left, core::WorldPointI right) noexcept {
    const std::int64_t x = static_cast<std::int64_t>(left.x) - right.x;
    const std::int64_t y = static_cast<std::int64_t>(left.y) - right.y;
    return x * x + y * y;
}

bool intersects(world::AabbI left, world::AabbI right) noexcept {
    return left.x < right.x + right.width && left.x + left.width > right.x &&
           left.y < right.y + right.height && left.y + left.height > right.y;
}

} // namespace

void NpcVisualCatalog::add(NpcVisualSet visual) {
    if (visual.id.empty()) { throw std::invalid_argument("NPC visual ID is empty"); }
    const auto [position, inserted] = visuals_.emplace(visual.id, visual);
    static_cast<void>(position);
    if (!inserted) { throw std::logic_error("duplicate NPC visual ID"); }
}

const NpcVisualSet* NpcVisualCatalog::find(
    const simulation::DefinitionId& id) const noexcept {
    const auto found = visuals_.find(id);
    return found == visuals_.end() ? nullptr : &found->second;
}

const NpcVisualSet& NpcVisualCatalog::require(const simulation::DefinitionId& id) const {
    const auto* visual = find(id);
    if (!visual) { throw std::out_of_range("NPC visual definition not found"); }
    return *visual;
}

void NpcCatalog::add(NpcDefinition definition) {
    validate(definition);
    const auto [position, inserted] = definitions_.emplace(definition.id, std::move(definition));
    static_cast<void>(position);
    if (!inserted) { throw std::logic_error("duplicate NPC definition id"); }
}

const NpcDefinition* NpcCatalog::find(const simulation::DefinitionId& id) const noexcept {
    const auto found = definitions_.find(id);
    return found == definitions_.end() ? nullptr : &found->second;
}

const NpcDefinition& NpcCatalog::require(const simulation::DefinitionId& id) const {
    const auto* definition = find(id);
    if (!definition) { throw std::out_of_range("NPC definition not found"); }
    return *definition;
}

NpcInstance::NpcInstance(simulation::EntityHandle handle, const NpcDefinition& definition,
                         core::WorldPointI position, FacingDirection facing)
    : handle_(handle), definition_(&definition), position_(position), facing_(facing) {
    if (!handle_) { throw std::invalid_argument("NPC requires a valid handle"); }
}

InteractionArea NpcInstance::interactionArea() const noexcept {
    const auto local = definition_->interaction.bounds;
    return {{position_.x + local.x, position_.y + local.y, local.width, local.height},
            definition_->interaction.enabled};
}

NpcInstance NpcFactory::create(const simulation::DefinitionId& definitionId,
                               core::WorldPointI position, FacingDirection facing) const {
    const auto& definition = npcs_.require(definitionId);
    return NpcInstance{handles_.create(), definition, position, facing};
}

NpcInteractionResult interactNearest(core::WorldPointI playerFeet,
                                     world::AabbI playerInteraction,
                                     std::span<NpcInstance> npcs) {
    NpcInteractionResult result{};
    std::int64_t bestDistance = 0;
    for (auto& npc : npcs) {
        const auto area = npc.interactionArea();
        if (!area.enabled || !intersects(playerInteraction, area.bounds)) { continue; }
        const auto distance = distanceSquared(playerFeet, npc.position());
        if (!result.npc || distance < bestDistance ||
            (distance == bestDistance && npc.handle().index < result.npc.index)) {
            result.npc = npc.handle();
            bestDistance = distance;
        }
    }
    return result;
}

const simulation::DefinitionId& guardNpcId() {
    static const simulation::DefinitionId id{"npc.guard"};
    return id;
}

const simulation::DefinitionId& scholarNpcId() {
    static const simulation::DefinitionId id{"npc.scholar"};
    return id;
}

NpcDefinition makeGuardNpcDefinition() {
    return {guardNpcId(), simulation::DefinitionId{"visual.npc.guard"},
            {{-14, -28, 28, 22}, true}, dialogue::guardDialogueId(), {"npc", "guard"}};
}

NpcDefinition makeScholarNpcDefinition() {
    return {scholarNpcId(), simulation::DefinitionId{"visual.npc.scholar"},
            {{-14, -28, 28, 22}, true}, dialogue::scholarDialogueId(), {"npc", "scholar"}};
}

} // namespace underworld::game::gameplay::npcs
