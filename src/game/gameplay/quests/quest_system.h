#pragma once

#include "engine/simulation/events.h"
#include "game/gameplay/quests/quest_state.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace underworld::game::gameplay::quests {

class QuestSystem final {
public:
    QuestSystem(const QuestCatalog& catalog, QuestStateStore& state)
        : catalog_(&catalog), state_(&state) {}

    [[nodiscard]] bool start(const simulation::DefinitionId& questId);
    void consume(const simulation::EventBuffer& events);
    void consume(std::span<const simulation::SimulationEvent> events);

private:
    void consume(const simulation::EntityDamaged&) noexcept {}
    void consume(const simulation::ProjectileImpact&) noexcept {}
    void consume(const simulation::EntityDefeated& event);
    void consume(const simulation::PickupCollected& event);
    void consume(const simulation::NpcTalked& event);
    void consume(const simulation::MapEntered& event);
    void consume(const simulation::ObjectOpened& event);
    void consume(const simulation::ItemDelivered& event);

    void advance(QuestObjectiveKind kind,
                 const simulation::DefinitionId& targetId,
                 std::uint32_t amount);
    void advanceByValue(QuestObjectiveKind kind, std::string_view targetValue,
                        std::uint32_t amount);

    const QuestCatalog* catalog_{};
    QuestStateStore* state_{};
};

} // namespace underworld::game::gameplay::quests
