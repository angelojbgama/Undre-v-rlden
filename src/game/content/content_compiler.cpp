#include "game/content/content_compiler.h"
#include "game/gameplay/attack_shapes.h"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace underworld::game::content {
namespace {

TilesetDefinition compileTileset(const AuthoredTileset& v) { return {v.id, v.displayName, v.relativeAssetPath, v.tileSize, v.columns, v.rows}; }
gameplay::ProjectileDefinition compileProjectile(const AuthoredProjectile& v) { return {v.id, v.visualId, v.canonicalFacing, v.speedPixelsPerTick, v.lifetimeTicks, v.hitboxWidth, v.hitboxHeight, v.spawnOffsets}; }
gameplay::AttackDefinition compileAttack(const AuthoredAttack& v) {
    gameplay::AttackDefinition result{v.id, v.kind, v.damage, v.totalTicks, v.cooldownTicks,
                                      v.minimumRangePixels, v.maximumRangePixels,
                                      v.visualActionId, v.meleeHitboxes,
                                      v.projectileDefinitionId, v.timeline, {}};
    for (const auto& authoredDirection : v.shapes) {
        for (const auto& frame : authoredDirection.frames) {
            auto sample = std::find_if(result.collisionSamples.begin(),
                                       result.collisionSamples.end(),
                                       [&](const auto& candidate) {
                                           return candidate.tick == frame.tick;
                                       });
            if (sample == result.collisionSamples.end()) {
                result.collisionSamples.push_back({frame.tick, {}});
                sample = std::prev(result.collisionSamples.end());
            }
            sample->regions[gameplay::facingIndex(authoredDirection.facing)] =
                gameplay::compileAttackShapeMask(frame.width, frame.height, frame.cells);
        }
    }
    std::sort(result.collisionSamples.begin(), result.collisionSamples.end(),
              [](const auto& left, const auto& right) { return left.tick < right.tick; });
    return result;
}
gameplay::creatures::BehaviorProfile compileBehavior(const AuthoredBehaviorProfile& v) { return {v.id, v.detectionRangePixels, v.disengageRangePixels, v.idleDurationTicks, v.wanderDurationTicks}; }
gameplay::creatures::EnemyDefinition compileEnemy(const AuthoredEnemy& v) { return {v.id, v.visualSetId, v.behaviorProfileId, v.faction, v.maximumHealth, v.movementSpeedSubpixelsPerTick, v.collisionBody, v.hurtbox, v.attackIds, v.rewardProfileId}; }
gameplay::ItemDefinition compileItem(const AuthoredItem& v) {
    std::optional<gameplay::rpg::EquipmentDefinition> equipment;
    if (v.equipment) {
        const auto slot = v.equipment->slot == AuthoredEquipmentSlot::armor
                              ? gameplay::rpg::EquipmentSlot::armor
                              : gameplay::rpg::EquipmentSlot::accessory;
        equipment = gameplay::rpg::EquipmentDefinition{
            slot, {v.equipment->modifiers.maximumHealthBonus,
                   v.equipment->modifiers.playerAttackDamageBonus}};
    }
    return {v.id, v.visualId, v.category, v.stackLimit, v.use, equipment};
}
gameplay::WorldObjectDefinition compileObject(const AuthoredWorldObject& v) {
    std::optional<gameplay::ObjectCollisionDefinition> collision;
    if (v.collision) {
        gameplay::ObjectCollisionDefinition compiled;
        const auto boxes = gameplay::compileAttackShapeMask(
            v.collision->width, v.collision->height, v.collision->cells,
            v.collision->origin.x, v.collision->origin.y);
        compiled.regions.reserve(boxes.size());
        for (const auto& box : boxes) {
            compiled.regions.push_back({box.offsetX, box.offsetY, box.width, box.height});
        }
        collision = std::move(compiled);
    }
    return {v.id, v.visualSetId, v.interactable, v.container, v.destructible,
            v.bankAccess ? std::optional<gameplay::ObjectBankAccessDefinition>{gameplay::ObjectBankAccessDefinition{}}
                         : std::nullopt, v.door, v.activation, std::move(collision),
            v.depthAnchor, v.occlusion};
}
gameplay::npcs::NpcVisualSet compileNpcVisual(const AuthoredNpcVisualSet& v) { return {v.id, v.markerColor, v.idle}; }
gameplay::npcs::NpcDefinition compileNpc(const AuthoredNpc& v) { return {v.id, v.visualSetId, v.interaction, v.defaultDialogueId, v.tags}; }

presentation::VisualImageDefinition compileVisualImage(const AuthoredVisualImage& v) { return {v.id, v.root, v.relativePath}; }
presentation::StaticSpriteDefinition compileStaticSprite(const AuthoredStaticSprite& v) { return {v.id, v.imageId, v.source, v.anchor}; }
presentation::AnimationDefinition compileAnimation(const AuthoredAnimation& v) {
    presentation::AnimationDefinition result{v.id, v.imageId, {}, v.loop};
    for (const auto& frame : v.frames) result.frames.push_back({frame.source, frame.anchor, frame.drawOffset, frame.durationTicks, frame.markers});
    return result;
}
presentation::EnemyVisualDefinition compileEnemyVisual(const AuthoredEnemyVisual& v) {
    presentation::EnemyVisualDefinition result{v.id, v.idle, v.move, v.hurt, v.death, v.dead, {}};
    for (const auto& attack : v.attacks) result.attacks.push_back({attack.visualActionId, attack.clips});
    return result;
}
presentation::WorldObjectVisualDefinition compileObjectVisual(const AuthoredWorldObjectVisual& v) {
    return {v.id, v.idleAnimationId, v.openedAnimationId, v.destroyingAnimationId,
            v.activationInactiveAnimationId, v.activationActiveAnimationId,
            v.doorLockedAnimationId, v.doorClosedAnimationId, v.doorOpenAnimationId,
            v.destroyedAnimationId, v.damagedAnimationId};
}

gameplay::PickupPayload compilePayload(const AuthoredPickupPayload& payload) {
    return std::visit([](const auto& value) -> gameplay::PickupPayload {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, AuthoredHealthPickup>) return gameplay::HealthPickup{value.amount};
        else if constexpr (std::is_same_v<T, AuthoredCurrencyPickup>) return gameplay::CurrencyPickup{value.amount};
        else return gameplay::ItemPickup{value.itemId, value.quantity};
    }, payload);
}
gameplay::PickupDefinition compilePickup(const AuthoredPickup& v) { return {v.id, v.visualId, v.collectionBounds, compilePayload(v.payload)}; }

gameplay::dialogue::DialogueDefinition compileDialogue(const AuthoredDialogue& v) {
    gameplay::dialogue::DialogueDefinition result{v.id, v.entryNodeId, {}};
    for (const auto& node : v.nodes) {
        gameplay::dialogue::DialogueNode compiled{node.id, node.speaker, node.pages, node.nextNodeId, {}};
        for (const auto& choice : node.choices) {
            gameplay::dialogue::DialogueChoice compiledChoice{choice.label, choice.targetNodeId, {}, {}};
            for (const auto& condition : choice.conditions) compiledChoice.conditions.push_back({condition.kind, condition.flagId});
            for (const auto& action : choice.actions) compiledChoice.actions.push_back({action.kind, action.targetId});
            compiled.choices.push_back(std::move(compiledChoice));
        }
        result.nodes.push_back(std::move(compiled));
    }
    return result;
}
gameplay::quests::QuestDefinition compileQuest(const AuthoredQuest& v) {
    gameplay::quests::QuestDefinition result{v.id, v.title, {}, v.tags, v.rewardGrantId};
    for (const auto& objective : v.objectives) result.objectives.push_back({objective.id, objective.kind, objective.targetId, objective.requiredCount, objective.description});
    return result;
}
gameplay::rpg::PlayerProgressionDefinition compileProgression(const AuthoredPlayerProgression& v) { return {v.id, {v.baseStats.maximumHealth}, v.cumulativeExperienceThresholds}; }
gameplay::rpg::RewardProfileDefinition compileReward(const AuthoredRewardProfile& v) { gameplay::rpg::RewardProfileDefinition result{v.id, v.experience, {}}; for (const auto& entry : v.loot) result.loot.push_back({entry.pickupDefinitionId, entry.chanceBasisPoints, entry.minimumCount, entry.maximumCount}); return result; }
gameplay::rpg::RewardGrantDefinition compileGrant(const AuthoredRewardGrant& v) { gameplay::rpg::RewardGrantDefinition result{v.id, v.experience, v.gold, {}}; for (const auto& item : v.items) result.items.push_back({item.itemId, item.quantity}); return result; }
gameplay::rpg::ShopDefinition compileShop(const AuthoredShop& v) { gameplay::rpg::ShopDefinition result{v.id, {}}; for (const auto& offer : v.offers) result.offers.push_back({offer.itemId, offer.playerBuyPrice, offer.playerSellPrice}); return result; }
authoring::TileSemanticDefinition compileTileSemantic(const AuthoredTileSemantic& v) { return {v.id, v.tilesetId, v.sourceIndex, v.family, v.role, v.topology, v.north, v.east, v.south, v.west, v.preferredLayer, v.flipXAllowed, v.visualConfidence, v.semanticConfidence, v.gameplayConfidence, v.variantWeight}; }
authoring::StampDefinition compileStamp(const AuthoredStamp& v) {
    authoring::StampDefinition result{v.id, v.displayName, v.width, v.height, {}, v.anchor, v.flipXAllowed, v.atomic, v.confidence};
    for (const auto& cell : v.cells) result.cells.push_back({cell.x, cell.y, cell.tileId});
    return result;
}
presentation::PresentationEffectDefinition compilePresentationEffect(const AuthoredPresentationEffect& v) {
    return {v.id, v.lifetime, v.durationTicks, v.priority, v.cameraShake, v.overlay, v.visionMask, v.fade};
}

} // namespace

ContentCompileResult ContentCompiler::compile(const AuthoredContentPack& authored) const {
    ContentCompileResult result;
    result.report = ContentValidator{}.validate(authored);
    if (result.report.hasErrors()) return result;
    try {
        GameContentRegistry registry;
        for (const auto& value : authored.tilesets) registry.tilesets_.add(compileTileset(value));
        for (const auto& value : authored.projectiles) registry.projectiles_.add(compileProjectile(value));
        for (const auto& value : authored.attacks) registry.attacks_.add(compileAttack(value));
        for (const auto& value : authored.behaviors) registry.behaviors_.add(compileBehavior(value));
        for (const auto& value : authored.enemies) registry.enemies_.add(compileEnemy(value));
        for (const auto& value : authored.rewardProfiles) registry.rewards_.add(compileReward(value));
        for (const auto& value : authored.rewardGrants) registry.rewardGrants_.add(compileGrant(value));
        for (const auto& value : authored.shops) registry.shops_.add(compileShop(value));
        for (const auto& value : authored.items) registry.items_.add(compileItem(value));
        for (const auto& value : authored.objects) registry.objects_.add(compileObject(value));
        for (const auto& value : authored.npcs) registry.npcs_.add(compileNpc(value));
        for (const auto& value : authored.npcVisuals) registry.npcVisuals_.add(compileNpcVisual(value));
        for (const auto& value : authored.dialogues) registry.dialogues_.add(compileDialogue(value));
        for (const auto& value : authored.quests) registry.quests_.add(compileQuest(value));
        for (const auto& value : authored.playerProgressions) registry.progressions_.add(compileProgression(value));
        for (const auto& value : authored.pickups) registry.pickups_.push_back(compilePickup(value));
        registry.authoringDescriptors_ = authored.authoringDescriptors;
        for (const auto& value : authored.tileSemantics) registry.authoringSemantics_.addTile(compileTileSemantic(value));
        for (const auto& value : authored.stamps) registry.authoringSemantics_.addStamp(compileStamp(value));
        for (const auto& value : authored.presentationEffects) registry.presentationEffects_.add(compilePresentationEffect(value));
        for (const auto& value : authored.visualImages) registry.visualImages_.add(compileVisualImage(value));
        for (const auto& value : authored.staticSprites) registry.staticSprites_.add(compileStaticSprite(value));
        for (const auto& value : authored.animations) registry.animations_.add(compileAnimation(value));
        for (const auto& value : authored.enemyVisuals) registry.enemyVisuals_.add(compileEnemyVisual(value));
        for (const auto& value : authored.objectVisuals) registry.objectVisuals_.add(compileObjectVisual(value));
        result.registry.emplace(std::move(registry));
    } catch (const std::exception& exception) {
        result.report.diagnostics.push_back({ContentDiagnosticSeverity::error, "catalog_rejected", exception.what(), ContentKind::tileset, {}, "registry"});
    }
    return result;
}

ContentCompileResult compileContent(const AuthoredContentPack& authored) { return ContentCompiler{}.compile(authored); }

} // namespace underworld::game::content
