#include "game/audit/audit_snapshot.h"

#include "game/audit/audit_session.h"

#include <sstream>

namespace underworld::game::audit {
namespace {

void writeActorArray(std::ostringstream& out, const char* name,
                     const std::vector<AuditActor>& actors) {
    out << '"' << name << "\":[";
    for (std::size_t index = 0; index < actors.size(); ++index) {
        if (index != 0) { out << ','; }
        const auto& actor = actors[index];
        out << "{\"instance\":" << actor.instanceId
            << ",\"definition\":\"" << escapeJsonString(actor.definitionId)
            << "\",\"x\":" << actor.x << ",\"y\":" << actor.y
            << ",\"health\":" << actor.health
            << ",\"maximumHealth\":" << actor.maximumHealth
            << ",\"state\":\"" << escapeJsonString(actor.state) << "\"}";
    }
    out << ']';
}

void writeQuestArray(std::ostringstream& out, const std::vector<AuditQuest>& quests) {
    out << "\"quests\":[";
    for (std::size_t index = 0; index < quests.size(); ++index) {
        if (index != 0) { out << ','; }
        const auto& quest = quests[index];
        out << "{\"id\":\"" << escapeJsonString(quest.questId)
            << "\",\"status\":\"" << escapeJsonString(quest.status)
            << "\",\"objectives\":[";
        for (std::size_t objectiveIndex = 0; objectiveIndex < quest.objectives.size();
             ++objectiveIndex) {
            if (objectiveIndex != 0) { out << ','; }
            const auto& objective = quest.objectives[objectiveIndex];
            out << "{\"id\":\"" << escapeJsonString(objective.objectiveId)
                << "\",\"current\":" << objective.currentCount << '}';
        }
        out << "]}";
    }
    out << ']';
}

} // namespace

std::string serializeAuditSnapshot(const GameAuditSnapshot& snapshot) {
    std::ostringstream out;
    out << "{\"tick\":" << snapshot.tick
        << ",\"map\":\"" << escapeJsonString(snapshot.currentMap)
        << "\",\"spawn\":\"" << escapeJsonString(snapshot.currentSpawn)
        << "\",\"player\":{\"x\":" << snapshot.playerX
        << ",\"y\":" << snapshot.playerY
        << ",\"facing\":\"" << escapeJsonString(snapshot.playerFacing)
        << "\",\"motion\":\"" << escapeJsonString(snapshot.playerMotion)
        << "\",\"action\":\"" << escapeJsonString(snapshot.playerAction)
        << "\",\"health\":" << snapshot.playerHealth
        << ",\"maximumHealth\":" << snapshot.playerMaximumHealth << "}"
        << ",\"gold\":" << snapshot.gold
        << ",\"inventoryOpen\":" << (snapshot.inventoryOpen ? "true" : "false")
        << ",\"inventory\":[";
    for (std::size_t index = 0; index < snapshot.inventory.size(); ++index) {
        if (index != 0) { out << ','; }
        const auto& slot = snapshot.inventory[index];
        out << "{\"slot\":" << slot.slot << ",\"item\":\""
            << escapeJsonString(slot.itemId) << "\",\"quantity\":" << slot.quantity << '}';
    }
    out << "],\"quickSlots\":[";
    for (std::size_t index = 0; index < snapshot.quickSlots.size(); ++index) {
        if (index != 0) { out << ','; }
        const auto& slot = snapshot.quickSlots[index];
        out << "{\"slot\":" << slot.slot << ",\"item\":\""
            << escapeJsonString(slot.itemId) << "\"}";
    }
    out << "] ,";
    writeActorArray(out, "enemies", snapshot.enemies);
    out << ',';
    writeActorArray(out, "npcs", snapshot.npcs);
    out << ',';
    writeActorArray(out, "objects", snapshot.objects);
    out << ",\"pickups\":[";
    for (std::size_t index = 0; index < snapshot.pickups.size(); ++index) {
        if (index != 0) { out << ','; }
        const auto& pickup = snapshot.pickups[index];
        out << "{\"instance\":" << pickup.instanceId
            << ",\"definition\":\"" << escapeJsonString(pickup.definitionId)
            << "\",\"x\":" << pickup.x << ",\"y\":" << pickup.y
            << ",\"quantity\":" << pickup.quantity << '}';
    }
    out << "],\"dialogue\":{\"active\":" << (snapshot.dialogue.active ? "true" : "false")
        << ",\"id\":\"" << escapeJsonString(snapshot.dialogue.dialogueId)
        << "\",\"node\":\"" << escapeJsonString(snapshot.dialogue.nodeId)
        << "\",\"page\":" << snapshot.dialogue.pageIndex
        << ",\"pageCount\":" << snapshot.dialogue.pageCount
        << ",\"choicesVisible\":" << (snapshot.dialogue.choicesVisible ? "true" : "false")
        << ",\"selectedChoice\":" << snapshot.dialogue.selectedChoice << "},";
    writeQuestArray(out, snapshot.quests);
    out << ",\"dialogueFlags\":[";
    for (std::size_t index = 0; index < snapshot.dialogueFlags.size(); ++index) {
        if (index != 0) { out << ','; }
        out << '"' << escapeJsonString(snapshot.dialogueFlags[index]) << '"';
    }
    out << "],\"activeProjectiles\":" << snapshot.activeProjectileCount
        << ",\"lastEvent\":\"" << escapeJsonString(snapshot.lastEvent) << "\"}";
    return out.str();
}

} // namespace underworld::game::audit
