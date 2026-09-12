#include "game/maps/map_data.h"

#include "game/gameplay/dialogue/dialogue_model.h"

#include <limits>
#include <string>
#include <unordered_set>

namespace underworld::game::maps {
namespace {

MapValidationResult failure(std::string error, std::string path = {}) {
    return {false, std::move(error), std::move(path)};
}

bool validFacing(gameplay::FacingDirection facing) noexcept {
    return facing == gameplay::FacingDirection::down || facing == gameplay::FacingDirection::up ||
           facing == gameplay::FacingDirection::left || facing == gameplay::FacingDirection::right;
}

bool validArea(world::AabbI area) noexcept { return area.width > 0 && area.height > 0; }

bool areaInsideMap(const MapData& data, world::AabbI area) noexcept {
    if (!validArea(area)) { return false; }
    const auto mapWidth = static_cast<std::int64_t>(data.width) * data.tileSize;
    const auto mapHeight = static_cast<std::int64_t>(data.height) * data.tileSize;
    const auto right = static_cast<std::int64_t>(area.x) + area.width;
    const auto bottom = static_cast<std::int64_t>(area.y) + area.height;
    return area.x >= 0 && area.y >= 0 && right <= mapWidth && bottom <= mapHeight;
}

bool validDoorState(gameplay::DoorState state) noexcept {
    return state == gameplay::DoorState::locked || state == gameplay::DoorState::closed ||
           state == gameplay::DoorState::open;
}

bool validObjectPersistencePolicy(ObjectPersistencePolicy policy) noexcept {
    return policy == ObjectPersistencePolicy::persistent ||
           policy == ObjectPersistencePolicy::resetOnMapEnter;
}

bool equalPayload(const gameplay::PickupPayload& left,
                  const gameplay::PickupPayload& right) noexcept {
    if (left.index() != right.index()) { return false; }
    if (const auto* value = std::get_if<gameplay::HealthPickup>(&left)) {
        return value->amount == std::get<gameplay::HealthPickup>(right).amount;
    }
    if (const auto* value = std::get_if<gameplay::CurrencyPickup>(&left)) {
        return value->amount == std::get<gameplay::CurrencyPickup>(right).amount;
    }
    const auto& a = std::get<gameplay::ItemPickup>(left);
    const auto& b = std::get<gameplay::ItemPickup>(right);
    return a.itemId == b.itemId && a.quantity == b.quantity;
}

bool equalStack(const gameplay::ItemStack& a, const gameplay::ItemStack& b) noexcept {
    return a.itemId == b.itemId && a.quantity == b.quantity;
}

} // namespace

MapValidationResult validateMapData(const MapData& data,
                                    const MapValidationCatalogs* catalogs) {
    if (data.id.empty()) { return failure("map id is empty"); }
    if (data.width == 0 || data.height == 0 || data.tileSize == 0) {
        return failure("map dimensions and tile size must be positive");
    }
    if (data.width > MapLimits::maximumDimension || data.height > MapLimits::maximumDimension) {
        return failure("map dimensions exceed safety limits");
    }
    const std::uint64_t cellCount64 = static_cast<std::uint64_t>(data.width) * data.height;
    if (cellCount64 > std::numeric_limits<std::size_t>::max()) {
        return failure("map cell count overflows this platform");
    }
    const std::size_t cellCount = static_cast<std::size_t>(cellCount64);
    if (data.layers.empty() || data.layers.size() > MapLimits::maximumLayers) {
        return failure("map layer count is invalid");
    }
    if (data.tileReferences.size() > MapLimits::maximumTileReferences) {
        return failure("tile reference count exceeds safety limit");
    }
    std::unordered_set<std::string> layerNames;
    for (const MapTileLayer& layer : data.layers) {
        if (layer.name.empty() || !layerNames.emplace(layer.name).second) {
            return failure("map layer names must be non-empty and unique");
        }
        if (layer.cells.size() != cellCount) { return failure("map layer dimensions mismatch"); }
        for (const auto cell : layer.cells) {
            if (cell && *cell >= data.tileReferences.size()) {
                return failure("map layer references an invalid tile reference");
            }
        }
    }
    if (data.collision.size() != cellCount) { return failure("collision dimensions mismatch"); }
    for (const auto value : data.collision) {
        if (value > 1) { return failure("collision cell value is invalid"); }
    }
    for (const auto& tile : data.tileReferences) {
        if (tile.tilesetId.empty()) { return failure("tileset definition id is empty"); }
        if ((static_cast<std::uint8_t>(tile.flags) & ~static_cast<std::uint8_t>(world::TileFlags::flipX)) != 0) {
            return failure("tile reference contains unsupported flags");
        }
        if (catalogs && catalogs->tilesets) {
            const auto* tileset = catalogs->tilesets->find(tile.tilesetId);
            if (!tileset) { return failure("tile reference references an unknown tileset"); }
            if (tileset->tileSize != data.tileSize) {
                return failure("tile reference tileset tile size does not match map tile size");
            }
            if (tile.sourceIndex >= tileset->tileCount()) {
                return failure("tile reference source index is outside tileset metadata");
            }
        }
    }
    if (data.enemies.size() + data.npcs.size() + data.objects.size() + data.pickups.size() >
        MapLimits::maximumPlacements) { return failure("placement count exceeds safety limit"); }

    std::unordered_set<std::uint64_t> persistentIds;
    const auto acceptId = [&](simulation::PersistentInstanceId id) {
        return id && persistentIds.emplace(id.value).second;
    };
    for (const auto& enemy : data.enemies) {
        if (!acceptId(enemy.id)) { return failure("persistent instance id is zero or duplicate"); }
        if (enemy.definitionId.empty() || !validFacing(enemy.facing)) {
            return failure("enemy placement is invalid");
        }
        if (catalogs && catalogs->enemies && !catalogs->enemies->find(enemy.definitionId)) {
            return failure("enemy placement references an unknown definition");
        }
    }
    for (const auto& npc : data.npcs) {
        if (!acceptId(npc.id)) { return failure("persistent instance id is zero or duplicate"); }
        if (npc.definitionId.empty() || !validFacing(npc.facing)) {
            return failure("NPC placement is invalid");
        }
        if (catalogs && catalogs->npcs) {
            const auto* definition = catalogs->npcs->find(npc.definitionId);
            if (!definition) { return failure("NPC placement references an unknown definition"); }
        }
    }
    for (const auto& object : data.objects) {
        if (!acceptId(object.id)) { return failure("persistent instance id is zero or duplicate"); }
        if (object.definitionId.empty() || !validObjectPersistencePolicy(object.persistence)) {
            return failure("object placement definition or persistence policy is invalid");
        }
        const gameplay::WorldObjectDefinition* definition = nullptr;
        if (catalogs && catalogs->objects) {
            definition = catalogs->objects->find(object.definitionId);
            if (!definition) { return failure("object placement references an unknown definition"); }
        }
        if (!object.initialContents.empty() && definition && !definition->container) {
            return failure("non-container object placement has initial contents");
        }
        if (definition && definition->activation &&
            definition->activation->mode == gameplay::ObjectActivationMode::playerPressure) {
            const auto bounds = *definition->activation->activationBounds;
            const auto worldBounds = world::AabbI{
                object.position.x + bounds.x, object.position.y + bounds.y,
                bounds.width, bounds.height};
            if (!areaInsideMap(data, worldBounds)) {
                return failure("pressure activation bounds are outside the map");
            }
        }
        if (definition && definition->collision) {
            const auto mapWidthPixels = static_cast<std::int64_t>(data.width) * data.tileSize;
            const auto mapHeightPixels = static_cast<std::int64_t>(data.height) * data.tileSize;
            for (const auto& region : definition->collision->regions) {
                const auto left = static_cast<std::int64_t>(object.position.x) + region.x;
                const auto top = static_cast<std::int64_t>(object.position.y) + region.y;
                const auto right = left + region.width;
                const auto bottom = top + region.height;
                if (left < 0 || top < 0 || right > mapWidthPixels || bottom > mapHeightPixels) {
                    return failure("object collision bounds are outside the map");
                }
            }
        }
        for (const auto& stack : object.initialContents) {
            if (stack.itemId.empty() || stack.quantity == 0) {
                return failure("object placement contains an invalid item stack");
            }
            if (catalogs && catalogs->items) {
                const auto* item = catalogs->items->find(stack.itemId);
                if (!item) { return failure("object contents reference an unknown item"); }
                if (stack.quantity > item->stackLimit) {
                    return failure("object contents exceed item stack limit");
                }
            }
        }
    }
    for (const auto& pickup : data.pickups) {
        if (!acceptId(pickup.id)) { return failure("persistent instance id is zero or duplicate"); }
        if (pickup.definitionId.empty() || pickup.visualId.empty() || !validArea(pickup.collectionBounds)) {
            return failure("pickup placement is invalid");
        }
        if (const auto* health = std::get_if<gameplay::HealthPickup>(&pickup.payload)) {
            if (health->amount <= 0) { return failure("health pickup amount must be positive"); }
        } else if (const auto* currency = std::get_if<gameplay::CurrencyPickup>(&pickup.payload)) {
            if (currency->amount == 0) { return failure("currency pickup amount must be positive"); }
        } else {
            const auto& itemPickup = std::get<gameplay::ItemPickup>(pickup.payload);
            if (itemPickup.itemId.empty() || itemPickup.quantity == 0) {
                return failure("item pickup payload is invalid");
            }
            if (catalogs && catalogs->items && !catalogs->items->find(itemPickup.itemId)) {
                return failure("item pickup references an unknown item");
            }
        }
    }
    std::unordered_set<std::string> spawnIds;
    for (const auto& spawn : data.playerSpawns) {
        if (spawn.id.empty() || !spawnIds.emplace(std::string(spawn.id.value())).second ||
            !validFacing(spawn.facing)) { return failure("player spawn id is duplicate or invalid"); }
    }
    std::unordered_set<std::string> linkIds;
    for (const auto& link : data.links) {
        if (link.id.empty() || !linkIds.emplace(link.id).second || !validArea(link.trigger) ||
            link.targetMapId.empty() || link.targetSpawnId.empty()) {
            return failure("map link is invalid or duplicate");
        }
    }
    std::unordered_set<std::string> regionIds;
    for (std::size_t regionIndex = 0; regionIndex < data.regions.size(); ++regionIndex) {
        const auto& region = data.regions[regionIndex];
        const auto regionPath = std::string("regions[") + std::to_string(regionIndex) + "]";
        if (region.id.empty() || !regionIds.emplace(std::string(region.id.value())).second ||
            !areaInsideMap(data, region.bounds)) {
            return failure("map region id or bounds are invalid or outside the map",
                           regionPath + ".bounds");
        }
        if (region.environmentEffectId) {
            if (catalogs && catalogs->presentationEffects) {
                const auto* effect = catalogs->presentationEffects->find(*region.environmentEffectId);
                if (!effect || effect->lifetime != presentation::PresentationEffectLifetime::persistent) {
                    return failure("map region environment effect is unknown or not persistent",
                                   regionPath + ".environmentEffectId");
                }
            }
        }
    }
    std::unordered_set<std::string> ruleIds;
    const auto objectById = [&](simulation::PersistentInstanceId id) {
        return std::find_if(data.objects.begin(), data.objects.end(),
            [&](const auto& value) { return value.id == id; });
    };
    const auto regionExists = [&](const simulation::DefinitionId& id) {
        return std::any_of(data.regions.begin(), data.regions.end(),
                           [&](const auto& value) { return value.id == id; });
    };
    const auto encounterExists = [&](const simulation::DefinitionId& id) {
        return std::any_of(data.encounters.begin(), data.encounters.end(),
                           [&](const auto& value) { return value.id == id; });
    };
    const auto sceneExists = [&](const simulation::DefinitionId& id) {
        return std::any_of(data.scenes.begin(), data.scenes.end(),
                           [&](const auto& value) { return value.id == id; });
    };
    for (std::size_t ruleIndex = 0; ruleIndex < data.worldRules.size(); ++ruleIndex) {
        const auto& rule = data.worldRules[ruleIndex];
        const auto rulePath = std::string("worldRules[") + std::to_string(ruleIndex) + "]";
        const auto triggerPath = rulePath + ".trigger";
        if (rule.id.empty() || !ruleIds.emplace(std::string(rule.id.value())).second) {
            return failure("world rule id is empty or duplicate", rulePath + ".id");
        }
        const auto& triggerTarget = rule.trigger.definitionTarget;
        switch (rule.trigger.kind) {
        case WorldTriggerKind::mapEntered:
            if (!triggerTarget.empty() || rule.trigger.instanceTarget) {
                return failure("map-entered trigger cannot have a target", triggerPath);
            }
            break;
        case WorldTriggerKind::regionEntered:
        case WorldTriggerKind::regionExited:
            if (triggerTarget.empty() || rule.trigger.instanceTarget || !regionExists(triggerTarget)) {
                return failure("world rule references an unknown region", triggerPath);
            }
            break;
        case WorldTriggerKind::encounterStarted:
        case WorldTriggerKind::encounterCompleted:
            if (triggerTarget.empty() || rule.trigger.instanceTarget || !encounterExists(triggerTarget)) {
                return failure("world rule references an unknown encounter", triggerPath);
            }
            break;
        case WorldTriggerKind::objectOpened:
        case WorldTriggerKind::objectActivated:
        case WorldTriggerKind::objectDeactivated:
            if (!triggerTarget.empty() || !rule.trigger.instanceTarget ||
                objectById(rule.trigger.instanceTarget) == data.objects.end()) {
                return failure("world rule references an unknown object instance", triggerPath);
            }
            if (rule.trigger.kind != WorldTriggerKind::objectOpened && catalogs && catalogs->objects) {
                const auto object = objectById(rule.trigger.instanceTarget);
                const auto* definition = catalogs->objects->find(object->definitionId);
                if (!definition || !definition->activation) {
                    return failure("world rule activation target is not activation-capable", triggerPath);
                }
            }
            break;
        }
        for (std::size_t conditionIndex = 0; conditionIndex < rule.conditions.size(); ++conditionIndex) {
            const auto& condition = rule.conditions[conditionIndex];
            const auto conditionPath = rulePath + ".conditions[" + std::to_string(conditionIndex) + "]";
            const auto& target = condition.definitionTarget;
            switch (condition.kind) {
            case WorldConditionKind::flagSet:
            case WorldConditionKind::flagNotSet:
                if (target.empty() || condition.instanceTarget) {
                    return failure("world rule flag condition has an invalid target", conditionPath);
                }
                break;
            case WorldConditionKind::encounterCompleted:
            case WorldConditionKind::encounterNotCompleted:
                if (target.empty() || condition.instanceTarget || !encounterExists(target)) {
                    return failure("world rule condition references an unknown encounter", conditionPath);
                }
                break;
            case WorldConditionKind::doorState: {
                if (!target.empty() || !condition.instanceTarget || !validDoorState(condition.doorState)) {
                    return failure("door condition has an invalid object instance target or state",
                                   conditionPath);
                }
                const auto object = objectById(condition.instanceTarget);
                if (object == data.objects.end()) {
                    return failure("door condition references an unknown object instance", conditionPath);
                }
                if (catalogs != nullptr && catalogs->objects != nullptr) {
                    const auto* definition = catalogs->objects->find(object->definitionId);
                    if (definition == nullptr || !definition->door) {
                        return failure("door condition target is not door-capable", conditionPath);
                    }
                }
                break;
            }
            case WorldConditionKind::objectActive:
            case WorldConditionKind::objectInactive: {
                if (!target.empty() || !condition.instanceTarget) {
                    return failure("object activation condition has an invalid target", conditionPath);
                }
                const auto object = objectById(condition.instanceTarget);
                if (object == data.objects.end()) {
                    return failure("object activation condition references an unknown object instance",
                                   conditionPath);
                }
                if (catalogs && catalogs->objects) {
                    const auto* definition = catalogs->objects->find(object->definitionId);
                    if (!definition || !definition->activation) {
                        return failure("object activation condition target is not activation-capable",
                                       conditionPath);
                    }
                }
                break;
            }
            }
        }
        for (std::size_t actionIndex = 0; actionIndex < rule.actions.size(); ++actionIndex) {
            const auto& action = rule.actions[actionIndex];
            const auto actionPath = rulePath + ".actions[" + std::to_string(actionIndex) + "]";
            const auto& target = action.definitionTarget;
            switch (action.kind) {
            case WorldActionKind::setFlag:
            case WorldActionKind::clearFlag:
                if (target.empty() || action.instanceTarget) {
                    return failure("world rule flag action has an invalid target", actionPath);
                }
                break;
            case WorldActionKind::startEncounter:
                if (target.empty() || action.instanceTarget || !encounterExists(target)) {
                    return failure("world rule action references an unknown encounter", actionPath);
                }
                break;
            case WorldActionKind::startScene:
                if (target.empty() || action.instanceTarget || !sceneExists(target)) {
                    return failure("world rule action references an unknown scene", actionPath);
                }
                break;
            case WorldActionKind::setDoorState: {
                if (!target.empty() || !action.instanceTarget || !validDoorState(action.doorState)) {
                    return failure("door action has an invalid object instance target or state", actionPath);
                }
                const auto object = objectById(action.instanceTarget);
                if (object == data.objects.end()) {
                    return failure("door action references an unknown object instance", actionPath);
                }
                if (catalogs != nullptr && catalogs->objects != nullptr) {
                    const auto* definition = catalogs->objects->find(object->definitionId);
                    if (definition == nullptr || !definition->door) {
                        return failure("door action target is not door-capable", actionPath);
                    }
                }
                break;
            }
            case WorldActionKind::playPresentationEffect: {
                if (target.empty() || action.instanceTarget) {
                    return failure("presentation effect action has an invalid target", actionPath);
                }
                if (catalogs && catalogs->presentationEffects) {
                    const auto* effect = catalogs->presentationEffects->find(target);
                    if (!effect || effect->lifetime != presentation::PresentationEffectLifetime::transient) {
                        return failure("presentation effect action references an unknown or persistent effect",
                                       actionPath);
                    }
                }
                break;
            }
            }
        }
    }
    std::unordered_set<std::string> encounterIds;
    std::unordered_set<std::uint64_t> encounterParticipants;
    for (std::size_t encounterIndex = 0; encounterIndex < data.encounters.size(); ++encounterIndex) {
        const auto& encounter = data.encounters[encounterIndex];
        const auto encounterPath = std::string("encounters[") + std::to_string(encounterIndex) + "]";
        if (encounter.id.empty() || !encounterIds.emplace(std::string(encounter.id.value())).second ||
            encounter.participants.empty()) {
            return failure("encounter id is duplicate or has no participants", encounterPath);
        }
        if (encounter.rewardGrantId && catalogs && catalogs->rewardGrants &&
            !catalogs->rewardGrants->find(*encounter.rewardGrantId)) {
            return failure("encounter references an unknown reward grant", encounterPath + ".rewardGrantId");
        }
        std::unordered_set<std::uint64_t> localParticipants;
        for (std::size_t participantIndex = 0; participantIndex < encounter.participants.size();
             ++participantIndex) {
            const auto participant = encounter.participants[participantIndex];
            if (!participant || !localParticipants.emplace(participant.value).second ||
                !std::any_of(data.enemies.begin(), data.enemies.end(),
                             [&](const auto& enemy) { return enemy.id == participant; }) ||
                !encounterParticipants.emplace(participant.value).second) {
                return failure("encounter participant is invalid, duplicated, or shared",
                               encounterPath + ".participants[" + std::to_string(participantIndex) + "]");
            }
        }
    }
    std::unordered_set<std::string> sceneIds;
    std::vector<simulation::PersistentInstanceId> npcInstances;
    std::vector<simulation::PersistentInstanceId> enemyInstances;
    npcInstances.reserve(data.npcs.size());
    enemyInstances.reserve(data.enemies.size());
    for (const auto& npc : data.npcs) npcInstances.push_back(npc.id);
    for (const auto& enemy : data.enemies) enemyInstances.push_back(enemy.id);
    const auto mapWidthPixels = data.width * static_cast<std::uint32_t>(data.tileSize);
    const auto mapHeightPixels = data.height * static_cast<std::uint32_t>(data.tileSize);
    const auto validateSceneWorldAction = [&](const WorldAction& action,
                                              const std::string& actionPath) -> MapValidationResult {
        const auto& target = action.definitionTarget;
        switch (action.kind) {
        case WorldActionKind::setFlag:
        case WorldActionKind::clearFlag:
            if (target.empty() || action.instanceTarget) {
                return failure("scene world action flag target is invalid", actionPath);
            }
            break;
        case WorldActionKind::startEncounter:
            if (target.empty() || action.instanceTarget || !encounterExists(target)) {
                return failure("scene world action references an unknown encounter", actionPath);
            }
            break;
        case WorldActionKind::startScene:
            return failure("nested scenes are not supported", actionPath);
        case WorldActionKind::setDoorState: {
            if (!target.empty() || !action.instanceTarget || !validDoorState(action.doorState)) {
                return failure("scene door action has an invalid target or state", actionPath);
            }
            const auto object = objectById(action.instanceTarget);
            if (object == data.objects.end()) {
                return failure("scene door action references an unknown object instance", actionPath);
            }
            if (catalogs != nullptr && catalogs->objects != nullptr) {
                const auto* definition = catalogs->objects->find(object->definitionId);
                if (definition == nullptr || !definition->door) {
                    return failure("scene door action target is not door-capable", actionPath);
                }
            }
            break;
        }
        case WorldActionKind::playPresentationEffect:
            if (target.empty() || action.instanceTarget) {
                return failure("scene presentation action has an invalid target", actionPath);
            }
            if (catalogs != nullptr && catalogs->presentationEffects != nullptr) {
                const auto* effect = catalogs->presentationEffects->find(target);
                if (effect == nullptr ||
                    effect->lifetime != presentation::PresentationEffectLifetime::transient) {
                    return failure("scene presentation action references an unknown or persistent effect",
                                   actionPath);
                }
            }
            break;
        }
        return {true, {}, {}};
    };
    for (std::size_t sceneIndex = 0; sceneIndex < data.scenes.size(); ++sceneIndex) {
        const auto& scene = data.scenes[sceneIndex];
        const auto scenePath = "scenes[" + std::to_string(sceneIndex) + "]";
        if (scene.id.empty() || !sceneIds.emplace(std::string(scene.id.value())).second) {
            return failure("scene id is empty or duplicated", scenePath + ".id");
        }
        const auto validation = gameplay::scenes::validateScene(
            scene, mapWidthPixels, mapHeightPixels, npcInstances, enemyInstances);
        if (!validation) return failure(validation.error, scenePath + "." + validation.path);
        for (std::size_t trackIndex = 0; trackIndex < scene.tracks.size(); ++trackIndex) {
            const auto& track = scene.tracks[trackIndex];
            for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
                const auto& clip = track.clips[clipIndex];
                if (clip.kind == gameplay::scenes::SceneClipKind::dialogue &&
                    catalogs != nullptr && catalogs->dialogues != nullptr &&
                    catalogs->dialogues->find(clip.dialogueId) == nullptr) {
                    return failure("scene dialogue clip references an unknown dialogue",
                                   scenePath + ".tracks[" + std::to_string(trackIndex) +
                                       "].clips[" + std::to_string(clipIndex) + ".dialogueId");
                }
                if (clip.kind == gameplay::scenes::SceneClipKind::presentationEffect &&
                    catalogs != nullptr && catalogs->presentationEffects != nullptr &&
                    catalogs->presentationEffects->find(clip.effectId) == nullptr) {
                    return failure("scene presentation clip references an unknown effect",
                                   scenePath + ".tracks[" + std::to_string(trackIndex) +
                                       "].clips[" + std::to_string(clipIndex) + ".effectId");
                }
                if (clip.kind != gameplay::scenes::SceneClipKind::worldEvent) continue;
                const auto actionPath = scenePath + ".tracks[" + std::to_string(trackIndex) +
                    "].clips[" + std::to_string(clipIndex) + "].worldAction";
                const auto actionValidation = validateSceneWorldAction(clip.worldAction, actionPath);
                if (!actionValidation) return actionValidation;
            }
        }
    }
    if (catalogs && catalogs->objects) {
        std::unordered_set<std::uint64_t> doorCells;
        const int tileSize = static_cast<int>(data.tileSize);
        for (const auto& placement : data.objects) {
            const auto* definition = catalogs->objects->find(placement.definitionId);
            if (definition == nullptr || !definition->door || definition->collision ||
                !definition->door->hasBlockingBounds) continue;
            const auto bounds = definition->door->blockingBounds;
            const auto firstX = core::floorDiv(static_cast<std::int64_t>(placement.position.x) + bounds.x,
                                                tileSize);
            const auto firstY = core::floorDiv(static_cast<std::int64_t>(placement.position.y) + bounds.y,
                                                tileSize);
            const auto lastX = core::floorDiv(static_cast<std::int64_t>(placement.position.x) + bounds.x +
                                                  bounds.width - 1, tileSize);
            const auto lastY = core::floorDiv(static_cast<std::int64_t>(placement.position.y) + bounds.y +
                                                  bounds.height - 1, tileSize);
            if (firstX < 0 || firstY < 0 || lastX >= static_cast<std::int64_t>(data.width) ||
                lastY >= static_cast<std::int64_t>(data.height)) {
                return failure("door blocking bounds are outside the map");
            }
            for (auto y = firstY; y <= lastY; ++y) for (auto x = firstX; x <= lastX; ++x) {
                const auto cell = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32U) |
                                  static_cast<std::uint32_t>(x);
                if (!doorCells.emplace(cell).second) return failure("overlapping dynamic door collision cells");
            }
        }
    }
    return {true, {}, {}};
}

bool semanticallyEqual(const MapData& a, const MapData& b) noexcept {
    if (!(a.id == b.id) || a.width != b.width || a.height != b.height ||
        a.tileSize != b.tileSize || a.tileReferences != b.tileReferences ||
        a.layers != b.layers || a.collision != b.collision ||
        a.playerSpawns != b.playerSpawns || a.enemies != b.enemies || a.npcs != b.npcs ||
        a.regions != b.regions || a.worldRules != b.worldRules || a.encounters != b.encounters ||
        a.scenes != b.scenes ||
        a.links != b.links ||
        a.objects.size() != b.objects.size() || a.pickups.size() != b.pickups.size()) { return false; }
    for (std::size_t i = 0; i < a.objects.size(); ++i) {
        const auto& x = a.objects[i]; const auto& y = b.objects[i];
        if (!(x.id == y.id) || !(x.definitionId == y.definitionId) || !(x.position == y.position) ||
            x.persistence != y.persistence || x.initialContents.size() != y.initialContents.size()) { return false; }
        for (std::size_t j = 0; j < x.initialContents.size(); ++j) {
            if (!equalStack(x.initialContents[j], y.initialContents[j])) { return false; }
        }
    }
    for (std::size_t i = 0; i < a.pickups.size(); ++i) {
        const auto& x = a.pickups[i]; const auto& y = b.pickups[i];
        if (!(x.id == y.id) || !(x.definitionId == y.definitionId) || !(x.visualId == y.visualId) ||
            !(x.position == y.position) || x.collectionBounds.x != y.collectionBounds.x ||
            x.collectionBounds.y != y.collectionBounds.y ||
            x.collectionBounds.width != y.collectionBounds.width ||
            x.collectionBounds.height != y.collectionBounds.height ||
            !equalPayload(x.payload, y.payload)) { return false; }
    }
    return true;
}

} // namespace underworld::game::maps
