#pragma once

#include "engine/simulation/player_command.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace underworld::game::audit {

struct AuditInventorySlot final {
    std::size_t slot{};
    std::string itemId;
    std::uint64_t quantity{};
};

struct AuditQuickSlot final {
    std::size_t slot{};
    std::string itemId;
};

struct AuditActor final {
    std::uint64_t instanceId{};
    std::string definitionId;
    int x{};
    int y{};
    int health{};
    int maximumHealth{};
    std::string state;
};

struct AuditPickup final {
    std::uint64_t instanceId{};
    std::string definitionId;
    int x{};
    int y{};
    std::uint64_t quantity{};
};

struct AuditDialogue final {
    bool active{};
    std::string dialogueId;
    std::string nodeId;
    std::size_t pageIndex{};
    std::size_t pageCount{};
    bool choicesVisible{};
    std::size_t selectedChoice{};
};

struct AuditQuestObjective final {
    std::string objectiveId;
    std::uint32_t currentCount{};
};

struct AuditQuest final {
    std::string questId;
    std::string status;
    std::vector<AuditQuestObjective> objectives;
};

// A value-only diagnostic view. It deliberately contains no EntityHandle, pointers,
// renderer state, or other transient ownership from the running game.
struct GameAuditSnapshot final {
    simulation::Tick tick{};
    std::string currentMap;
    std::string currentSpawn;

    int playerX{};
    int playerY{};
    std::string playerFacing;
    int playerHealth{};
    int playerMaximumHealth{};

    std::uint64_t gold{};
    std::vector<AuditInventorySlot> inventory;
    std::vector<AuditQuickSlot> quickSlots;
    std::vector<AuditActor> enemies;
    std::vector<AuditActor> npcs;
    std::vector<AuditActor> objects;
    std::vector<AuditPickup> pickups;
    AuditDialogue dialogue;
    std::vector<AuditQuest> quests;
    std::vector<std::string> dialogueFlags;
    std::size_t activeProjectileCount{};
};

[[nodiscard]] std::string serializeAuditSnapshot(const GameAuditSnapshot& snapshot);

} // namespace underworld::game::audit
