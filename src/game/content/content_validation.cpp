#include "game/content/content_validation.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace underworld::game::content {
namespace {

using Severity = ContentDiagnosticSeverity;

void error(ContentValidationReport& report, ContentKind kind,
           simulation::DefinitionId id, std::string code, std::string message,
           std::string field = {}) {
    report.diagnostics.push_back({Severity::error, std::move(code), std::move(message),
                                  kind, std::move(id), std::move(field)});
}

template<class Range, class IdFn>
std::unordered_set<std::string> ids(const Range& values, ContentValidationReport& report,
                                    ContentKind kind, IdFn idFn) {
    std::unordered_set<std::string> result;
    for (const auto& value : values) {
        const auto id = idFn(value);
        if (id.empty()) {
            error(report, kind, id, "empty_id", "definition id must not be empty", "id");
        } else if (!result.emplace(std::string(id.value())).second) {
            error(report, kind, id, "duplicate_id", "definition id is duplicated", "id");
        }
    }
    return result;
}

bool contains(const std::unordered_set<std::string>& values,
              const simulation::DefinitionId& id) {
    return values.contains(std::string(id.value()));
}

void validateAttack(const AuthoredAttack& value, ContentValidationReport& report) {
    if (value.visualActionId.empty() || value.damage.amount <= 0 || value.damage.knockbackPixels < 0)
        error(report, ContentKind::attack, value.id, "invalid_value", "attack visual and damage values are invalid", "definition");
    if (value.totalTicks == 0) error(report, ContentKind::attack, value.id, "invalid_value", "total ticks must be positive", "totalTicks");
    if (value.minimumRangePixels < 0 || value.maximumRangePixels < value.minimumRangePixels)
        error(report, ContentKind::attack, value.id, "invalid_range", "attack range is invalid", "range");
    std::uint32_t previous{};
    bool first = true;
    for (const auto& event : value.timeline) {
        if (event.tick >= value.totalTicks || (!first && event.tick < previous))
            error(report, ContentKind::attack, value.id, "invalid_timeline", "timeline must be ordered and inside totalTicks", "timeline");
        previous = event.tick;
        first = false;
    }
    if (value.kind == gameplay::AttackKind::meleeHitbox && !value.meleeHitboxes)
        error(report, ContentKind::attack, value.id, "invalid_value", "melee attack requires hitboxes", "meleeHitboxes");
    if (value.kind == gameplay::AttackKind::projectile && !value.projectileDefinitionId)
        error(report, ContentKind::attack, value.id, "invalid_value", "projectile attack requires a projectile definition", "projectileDefinitionId");
}

} // namespace

bool ContentValidationReport::hasErrors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& value) {
        return value.severity == ContentDiagnosticSeverity::error;
    });
}

ContentValidationReport ContentValidator::validate(const AuthoredContentPack& pack) const {
    ContentValidationReport report;
    const auto tilesets = ids(pack.tilesets, report, ContentKind::tileset,
                              [](const auto& value) { return value.id; });
    const auto projectiles = ids(pack.projectiles, report, ContentKind::projectile,
                                 [](const auto& value) { return value.id; });
    const auto attacks = ids(pack.attacks, report, ContentKind::attack,
                             [](const auto& value) { return value.id; });
    const auto behaviors = ids(pack.behaviors, report, ContentKind::behavior,
                               [](const auto& value) { return value.id; });
    const auto enemies = ids(pack.enemies, report, ContentKind::enemy,
                             [](const auto& value) { return value.id; });
    const auto items = ids(pack.items, report, ContentKind::item,
                           [](const auto& value) { return value.id; });
    const auto objects = ids(pack.objects, report, ContentKind::object,
                             [](const auto& value) { return value.id; });
    const auto pickups = ids(pack.pickups, report, ContentKind::pickup,
                             [](const auto& value) { return value.id; });
    const auto npcs = ids(pack.npcs, report, ContentKind::npc,
                          [](const auto& value) { return value.id; });
    const auto dialogues = ids(pack.dialogues, report, ContentKind::dialogue,
                               [](const auto& value) { return value.id; });
    const auto quests = ids(pack.quests, report, ContentKind::quest,
                            [](const auto& value) { return value.id; });
    const auto tileSemantics = ids(pack.tileSemantics, report, ContentKind::tileSemantic,
                                   [](const auto& value) { return value.id; });
    const auto stamps = ids(pack.stamps, report, ContentKind::stamp,
                            [](const auto& value) { return value.id; });
    const auto progressions = ids(pack.playerProgressions, report, ContentKind::playerProgression,
                                  [](const auto& value) { return value.id; });
    const auto rewards = ids(pack.rewardProfiles, report, ContentKind::rewardProfile,
                             [](const auto& value) { return value.id; });
    const auto grants = ids(pack.rewardGrants, report, ContentKind::rewardGrant,
                            [](const auto& value) { return value.id; });
    const auto shops = ids(pack.shops, report, ContentKind::shop,
                           [](const auto& value) { return value.id; });

    for (const auto& value : pack.tilesets) {
        if (value.displayName.empty() || value.relativeAssetPath.empty() || value.tileSize == 0 ||
            value.columns == 0 || value.rows == 0 ||
            value.columns > std::numeric_limits<std::uint32_t>::max() / value.rows)
            error(report, ContentKind::tileset, value.id, "invalid_value", "tileset dimensions and path must be valid", "tileset");
    }
    for (const auto& value : pack.projectiles) {
        if (value.visualId.empty() || value.speedPixelsPerTick <= 0 || value.lifetimeTicks == 0 ||
            value.hitboxWidth <= 0 || value.hitboxHeight <= 0)
            error(report, ContentKind::projectile, value.id, "invalid_value", "projectile values must be positive", "projectile");
    }
    for (const auto& value : pack.attacks) {
        validateAttack(value, report);
        if (value.projectileDefinitionId && !contains(projectiles, *value.projectileDefinitionId))
            error(report, ContentKind::attack, value.id, "unknown_reference", "projectile definition does not exist", "projectileDefinitionId");
    }
    for (const auto& value : pack.behaviors) {
        if (value.detectionRangePixels < 0 || value.disengageRangePixels < value.detectionRangePixels)
            error(report, ContentKind::behavior, value.id, "invalid_range", "behavior ranges are invalid", "range");
    }
    for (const auto& value : pack.enemies) {
        if (value.visualSetId.empty() || value.maximumHealth <= 0 || value.movementSpeedSubpixelsPerTick < 0 ||
            !value.collisionBody.valid() || !value.hurtbox.valid())
            error(report, ContentKind::enemy, value.id, "invalid_value", "enemy body and stats are invalid", "definition");
        if (!contains(behaviors, value.behaviorProfileId))
            error(report, ContentKind::enemy, value.id, "unknown_reference", "behavior profile does not exist", "behaviorProfileId");
        for (const auto& attack : value.attackIds) if (!contains(attacks, attack))
            error(report, ContentKind::enemy, value.id, "unknown_reference", "attack definition does not exist", "attackIds");
        if (value.rewardProfileId && !contains(rewards, *value.rewardProfileId))
            error(report, ContentKind::enemy, value.id, "unknown_reference", "reward profile does not exist", "rewardProfileId");
    }
    for (const auto& value : pack.rewardProfiles) {
        if (value.loot.size() > gameplay::rpg::maximumLootEntriesPerProfile)
            error(report, ContentKind::rewardProfile, value.id, "invalid_range", "reward profile has too many loot entries", "loot");
        for (const auto& entry : value.loot) {
            if (entry.pickupDefinitionId.empty() || !contains(pickups, entry.pickupDefinitionId))
                error(report, ContentKind::rewardProfile, value.id, "unknown_reference", "loot pickup definition does not exist", "pickupDefinitionId");
            if (entry.chanceBasisPoints > 10000 || entry.minimumCount == 0 ||
                entry.minimumCount > entry.maximumCount || entry.maximumCount > gameplay::rpg::maximumDropCountPerEntry)
                error(report, ContentKind::rewardProfile, value.id, "invalid_range", "loot chance or count is invalid", "loot");
        }
    }
    for (const auto& value : pack.rewardGrants) {
        if (value.experience == 0 && value.gold == 0 && value.items.empty()) error(report, ContentKind::rewardGrant, value.id, "empty_grant", "reward grant must contain a reward", "definition");
        if (value.items.size() > gameplay::rpg::maximumRewardItems) error(report, ContentKind::rewardGrant, value.id, "invalid_range", "reward grant has too many item entries", "items");
        std::unordered_set<std::string> grantItems;
        for (const auto& item : value.items) {
            if (item.itemId.empty() || !contains(items, item.itemId)) error(report, ContentKind::rewardGrant, value.id, "unknown_reference", "reward item does not exist", "items");
            if (item.quantity == 0) error(report, ContentKind::rewardGrant, value.id, "invalid_quantity", "reward item quantity must be positive", "items");
            if (!grantItems.emplace(std::string(item.itemId.value())).second) error(report, ContentKind::rewardGrant, value.id, "duplicate_item", "reward grant item is duplicated", "items");
        }
    }
    for (const auto& value : pack.shops) {
        if (value.offers.empty()) error(report, ContentKind::shop, value.id, "empty_shop", "shop must contain offers", "offers");
        if (value.offers.size() > gameplay::rpg::maximumShopOffers) error(report, ContentKind::shop, value.id, "invalid_range", "shop has too many offers", "offers");
        std::unordered_set<std::string> offerItems;
        for (const auto& offer : value.offers) {
            if (offer.itemId.empty() || !contains(items, offer.itemId)) error(report, ContentKind::shop, value.id, "unknown_reference", "shop offer item does not exist", "itemId");
            if (!offer.playerBuyPrice && !offer.playerSellPrice) error(report, ContentKind::shop, value.id, "empty_offer", "shop offer must provide a buy or sell price", "offer");
            if (!offerItems.emplace(std::string(offer.itemId.value())).second) error(report, ContentKind::shop, value.id, "duplicate_offer", "shop item offer is duplicated", "itemId");
        }
    }
    for (const auto& value : pack.items) {
        if (value.visualId.empty() || value.stackLimit == 0 ||
            (value.category == gameplay::ItemCategory::equipment && value.stackLimit != 1))
            error(report, ContentKind::item, value.id, "invalid_stack_limit", "item stack limit is invalid", "stackLimit");
        if (value.category == gameplay::ItemCategory::equipment) {
            if (!value.equipment || value.use || value.equipment->modifiers.maximumHealthBonus < 0 ||
                value.equipment->modifiers.playerAttackDamageBonus < 0 ||
                value.equipment->modifiers.maximumHealthBonus > 1000 ||
                value.equipment->modifiers.playerAttackDamageBonus > 1000)
                error(report, ContentKind::item, value.id, "invalid_equipment", "equipment metadata or modifiers are invalid", "equipment");
        } else if (value.equipment) {
            error(report, ContentKind::item, value.id, "invalid_equipment", "non-equipment item has equipment metadata", "equipment");
        }
    }
    for (const auto& value : pack.objects) {
        if (value.id.empty() || value.visualSetId.empty() ||
            (!value.interactable && !value.container && !value.destructible))
            error(report, ContentKind::object, value.id, "invalid_value", "object must have valid visual and capability data", "definition");
        if (value.interactable && (value.interactable->bounds.width <= 0 || value.interactable->bounds.height <= 0)) error(report, ContentKind::object, value.id, "invalid_value", "interaction bounds are invalid", "interactable");
        if (value.container && value.container->capacity == 0) error(report, ContentKind::object, value.id, "invalid_value", "container capacity must be positive", "container");
        if (value.destructible && (value.destructible->maximumHealth <= 0 || value.destructible->hurtbox.width <= 0 || value.destructible->hurtbox.height <= 0 || value.destructible->destructionDurationTicks == 0))
            error(report, ContentKind::object, value.id, "invalid_value", "destructible values are invalid", "destructible");
        if (value.bankAccess && (!value.interactable || value.container || value.destructible))
            error(report, ContentKind::object, value.id, "invalid_bank_access", "bank access requires interaction and cannot be a container or destructible", "bankAccess");
    }
    for (const auto& value : pack.pickups) {
        if (value.visualId.empty() || value.collectionBounds.width <= 0 || value.collectionBounds.height <= 0) error(report, ContentKind::pickup, value.id, "invalid_value", "pickup visual and collection bounds are invalid", "definition");
        if (const auto* item = std::get_if<AuthoredItemPickup>(&value.payload)) {
            if (!contains(items, item->itemId) || item->quantity == 0)
                error(report, ContentKind::pickup, value.id, "unknown_reference", "item pickup references an invalid item or quantity", "payload");
        } else if (const auto* health = std::get_if<AuthoredHealthPickup>(&value.payload); health && health->amount <= 0) {
            error(report, ContentKind::pickup, value.id, "invalid_value", "health pickup amount must be positive", "payload");
        } else if (const auto* currency = std::get_if<AuthoredCurrencyPickup>(&value.payload); currency && currency->amount == 0) {
            error(report, ContentKind::pickup, value.id, "invalid_value", "currency pickup amount must be positive", "payload");
        }
    }
    for (const auto& value : pack.npcs) {
        if (value.visualSetId.empty() || value.defaultDialogueId.empty()) error(report, ContentKind::npc, value.id, "invalid_value", "NPC visual and dialogue references are required", "definition");
        if (!contains(dialogues, value.defaultDialogueId)) error(report, ContentKind::npc, value.id, "unknown_reference", "dialogue definition does not exist", "defaultDialogueId");
    }
    for (const auto& value : pack.dialogues) {
        if (value.entryNodeId.empty() || value.nodes.empty()) error(report, ContentKind::dialogue, value.id, "invalid_value", "dialogue requires an entry node and nodes", "nodes");
        std::unordered_set<std::string> nodeIds;
        for (const auto& node : value.nodes) {
            if (node.id.empty() || !nodeIds.emplace(std::string(node.id.value())).second) error(report, ContentKind::dialogue, value.id, "duplicate_id", "dialogue node id is empty or duplicated", "nodes");
            if (!node.nextNodeId.empty() && !std::any_of(value.nodes.begin(), value.nodes.end(), [&](const auto& other) { return other.id == node.nextNodeId; })) error(report, ContentKind::dialogue, value.id, "invalid_node_target", "dialogue next node does not exist", "nextNodeId");
            for (const auto& choice : node.choices) {
                if (!choice.targetNodeId.empty() && !std::any_of(value.nodes.begin(), value.nodes.end(), [&](const auto& other) { return other.id == choice.targetNodeId; })) error(report, ContentKind::dialogue, value.id, "invalid_node_target", "dialogue choice target does not exist", "targetNodeId");
                for (const auto& condition : choice.conditions) if (condition.flagId.empty()) error(report, ContentKind::dialogue, value.id, "invalid_value", "dialogue condition flag is empty", "conditions");
                for (const auto& action : choice.actions) {
                    if (action.targetId.empty()) error(report, ContentKind::dialogue, value.id, "invalid_value", "dialogue action target is empty", "actions");
                    if ((action.kind == gameplay::dialogue::DialogueActionKind::startQuest && !contains(quests, action.targetId)) ||
                        (action.kind == gameplay::dialogue::DialogueActionKind::openShop && !contains(shops, action.targetId)))
                        error(report, ContentKind::dialogue, value.id, "unknown_reference", "dialogue action target does not exist", "actions");
                }
            }
        }
        if (!contains(nodeIds, value.entryNodeId)) error(report, ContentKind::dialogue, value.id, "invalid_node_target", "dialogue entry node does not exist", "entryNodeId");
    }
    for (const auto& value : pack.quests) {
        if (value.title.empty() || value.objectives.empty()) error(report, ContentKind::quest, value.id, "invalid_value", "quest requires a title and objectives", "objectives");
        std::unordered_set<std::string> objectiveIds;
        for (const auto& objective : value.objectives) {
            if (objective.id.empty() || !objectiveIds.emplace(std::string(objective.id.value())).second) error(report, ContentKind::quest, value.id, "duplicate_id", "quest objective id is empty or duplicated", "objectives");
            if (objective.requiredCount == 0 || objective.targetId.empty()) error(report, ContentKind::quest, value.id, "invalid_value", "quest objective target and required count are required", "objectives");
            const bool known = (objective.kind == gameplay::quests::QuestObjectiveKind::kill && contains(enemies, objective.targetId)) ||
                (objective.kind == gameplay::quests::QuestObjectiveKind::talk && contains(npcs, objective.targetId)) ||
                (objective.kind == gameplay::quests::QuestObjectiveKind::open && contains(objects, objective.targetId)) ||
                (objective.kind == gameplay::quests::QuestObjectiveKind::deliver && contains(items, objective.targetId)) ||
                (objective.kind == gameplay::quests::QuestObjectiveKind::pickup && (contains(pickups, objective.targetId) || contains(items, objective.targetId)));
            if (objective.kind != gameplay::quests::QuestObjectiveKind::enter && !known) error(report, ContentKind::quest, value.id, "unknown_reference", "quest objective target does not exist", "targetId");
        }
        if (value.rewardGrantId && !contains(grants, *value.rewardGrantId)) error(report, ContentKind::quest, value.id, "unknown_reference", "quest reward grant does not exist", "rewardGrantId");
    }
    for (const auto& value : pack.tileSemantics) {
        const auto tileset = std::find_if(pack.tilesets.begin(), pack.tilesets.end(), [&](const auto& other) { return other.id == value.tilesetId; });
        if (!contains(tilesets, value.tilesetId) || tileset == pack.tilesets.end() || value.sourceIndex >= tileset->columns * tileset->rows) error(report, ContentKind::tileSemantic, value.id, "invalid_tile_reference", "semantic tile references an invalid tileset index", "sourceIndex");
    }
    for (const auto& value : pack.stamps) {
        if (value.width == 0 || value.height == 0 || value.cells.empty()) error(report, ContentKind::stamp, value.id, "invalid_stamp_cell", "stamp dimensions and cells are required", "cells");
        std::unordered_set<std::string> cells;
        for (const auto& cell : value.cells) {
            if (cell.x < 0 || cell.y < 0 || static_cast<std::uint32_t>(cell.x) >= value.width || static_cast<std::uint32_t>(cell.y) >= value.height || !contains(tileSemantics, cell.tileId) || !cells.emplace(std::to_string(cell.x) + ":" + std::to_string(cell.y)).second)
                error(report, ContentKind::stamp, value.id, "invalid_stamp_cell", "stamp cell is outside bounds, duplicated or unknown", "cells");
        }
    }
    for (const auto& value : pack.playerProgressions) {
        if (value.baseStats.maximumHealth <= 0 || value.cumulativeExperienceThresholds.empty() ||
            value.cumulativeExperienceThresholds.front() != 0 ||
            !std::is_sorted(value.cumulativeExperienceThresholds.begin(), value.cumulativeExperienceThresholds.end()) ||
            std::adjacent_find(value.cumulativeExperienceThresholds.begin(), value.cumulativeExperienceThresholds.end()) != value.cumulativeExperienceThresholds.end())
            error(report, ContentKind::playerProgression, value.id, "invalid_curve", "progression health and cumulative thresholds are invalid", "cumulativeExperienceThresholds");
    }
    for (const auto& value : pack.authoringDescriptors) {
        if (value.definitionId.empty() || value.displayName.empty()) error(report, ContentKind::authoringDescriptor, value.definitionId, "invalid_value", "authoring descriptor requires id and display name", "descriptor");
        const bool known = (value.category == AuthoringCategory::enemy && contains(enemies, value.definitionId)) || (value.category == AuthoringCategory::object && contains(objects, value.definitionId)) || (value.category == AuthoringCategory::pickup && contains(pickups, value.definitionId)) || (value.category == AuthoringCategory::npc && contains(npcs, value.definitionId)) || (value.category == AuthoringCategory::item && contains(items, value.definitionId)) || (value.category == AuthoringCategory::rewardProfile && contains(rewards, value.definitionId)) || (value.category == AuthoringCategory::rewardGrant && contains(grants, value.definitionId)) || (value.category == AuthoringCategory::shop && contains(shops, value.definitionId));
        if (!known) error(report, ContentKind::authoringDescriptor, value.definitionId, "unknown_reference", "descriptor target does not exist in its category", "definitionId");
    }
    return report;
}

} // namespace underworld::game::content
