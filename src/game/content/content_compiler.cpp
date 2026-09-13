#include "game/content/content_compiler.h"
#include "game/gameplay/attack_shapes.h"
#include "game/gameplay/player_definition.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace underworld::game::content {
namespace {

TilesetDefinition compileTileset(const AuthoredTileset& v) { return {v.id, v.displayName, v.relativeAssetPath, v.tileSize, v.columns, v.rows}; }
gameplay::ProjectileDefinition compileProjectile(const AuthoredProjectile& v) { return {v.id, v.visualId, v.canonicalFacing, v.speedPixelsPerTick, v.lifetimeTicks, v.hitboxWidth, v.hitboxHeight, v.spawnOffsets}; }
const AuthoredAnimation* findAnimation(
    const AuthoredContentPack& pack,
    const simulation::DefinitionId& id) {
    const auto found = std::find_if(
        pack.animations.begin(), pack.animations.end(),
        [&](const auto& value) { return value.id == id; });
    return found == pack.animations.end() ? nullptr : &*found;
}

const AuthoredPlayerVisual* findPlayerVisual(
    const AuthoredContentPack& pack,
    const simulation::DefinitionId& id) {
    const auto found = std::find_if(
        pack.playerVisuals.begin(), pack.playerVisuals.end(),
        [&](const auto& value) { return value.id == id; });
    return found == pack.playerVisuals.end() ? nullptr : &*found;
}

const presentation::DirectionalAnimationRef* defaultPlayerSwordClips(
    const AuthoredContentPack& pack) {
    const auto player = std::find_if(
        pack.players.begin(), pack.players.end(),
        [](const auto& value) {
            return value.id == gameplay::defaultPlayerDefinitionId();
        });
    if (player == pack.players.end()) return nullptr;

    const auto visual = std::find_if(
        pack.playerVisuals.begin(), pack.playerVisuals.end(),
        [&](const auto& value) {
            return value.id == player->visualSetId;
        });
    if (visual == pack.playerVisuals.end()) return nullptr;

    const auto action = std::find_if(
        visual->actions.begin(), visual->actions.end(),
        [](const auto& value) { return value.actionId == "sword"; });
    return action == visual->actions.end() ? nullptr : &action->clips;
}

std::optional<simulation::DefinitionId> animationForFacing(
    const presentation::DirectionalAnimationRef& reference,
    gameplay::FacingDirection facing) {
    const std::optional<simulation::DefinitionId>* exact = nullptr;
    switch (facing) {
    case gameplay::FacingDirection::down:
        exact = &reference.down;
        break;
    case gameplay::FacingDirection::up:
        exact = &reference.up;
        break;
    case gameplay::FacingDirection::left:
        exact = reference.left ? &reference.left : &reference.side;
        break;
    case gameplay::FacingDirection::right:
        exact = reference.right ? &reference.right : &reference.side;
        break;
    }
    const std::optional<simulation::DefinitionId>* candidates[] = {
        exact, &reference.defaultAnimation, &reference.down, &reference.up,
        &reference.left, &reference.right, &reference.side};
    for (const auto* candidate : candidates) {
        if (candidate != nullptr && candidate->has_value()) return **candidate;
    }
    return std::nullopt;
}

const AuthoredAnimationFrameMask* frameMask(
    const AuthoredAnimationFrame& frame,
    std::string_view channel) {
    const auto found = std::find_if(
        frame.masks.begin(), frame.masks.end(),
        [&](const auto& value) { return value.channel == channel; });
    return found == frame.masks.end() ? nullptr : &*found;
}

const AuthoredAnimationFrameMask* attackHitboxMask(
    const AuthoredAnimationFrame& frame) {
    return frameMask(frame, "attackHitbox");
}

gameplay::AttackDefinition::CollisionSample& collisionSample(
    std::vector<gameplay::AttackDefinition::CollisionSample>& samples,
    std::uint32_t tick) {
    const auto found = std::find_if(
        samples.begin(), samples.end(),
        [tick](const auto& value) { return value.tick == tick; });
    if (found != samples.end()) return *found;
    samples.push_back({tick, {}, {}});
    return samples.back();
}

void applyDefaultPlayerSwordFrameMasks(
    const AuthoredContentPack& pack,
    gameplay::AttackDefinition& result) {
    if (result.id != gameplay::playerSwordAttackId()) return;
    const auto* clips = defaultPlayerSwordClips(pack);
    if (clips == nullptr) return;

    constexpr std::array facings{
        gameplay::FacingDirection::down,
        gameplay::FacingDirection::up,
        gameplay::FacingDirection::left,
        gameplay::FacingDirection::right,
    };
    std::array<bool, 4> authoredFacings{};
    std::array<std::uint32_t, 4> authoredDurations{};
    std::uint32_t longestDuration = result.totalTicks;

    for (const auto facing : facings) {
        const auto animationId = animationForFacing(*clips, facing);
        if (!animationId) continue;
        const auto* animation = findAnimation(pack, *animationId);
        if (animation == nullptr || animation->frames.empty()) continue;

        const bool hasAttackMasks = std::any_of(
            animation->frames.begin(), animation->frames.end(),
            [](const auto& frame) {
                return attackHitboxMask(frame) != nullptr;
            });
        if (!hasAttackMasks) continue;

        const auto index = gameplay::facingIndex(facing);
        authoredFacings[index] = true;

        // Frame masks are authoritative for this facing. Remove any older
        // authored shape samples only for this direction, leaving the other
        // directions untouched for backwards compatibility.
        for (auto& sample : result.collisionSamples) {
            sample.regions[index].clear();
            sample.authored[index] = false;
        }

        std::uint64_t tick = 0;
        for (const auto& frame : animation->frames) {
            if (tick >= std::numeric_limits<std::uint32_t>::max()) {
                throw std::invalid_argument(
                    "Player sword animation is too long for attack timing");
            }
            auto& sample = collisionSample(
                result.collisionSamples,
                static_cast<std::uint32_t>(tick));
            sample.authored[index] = true;
            sample.regions[index].clear();

            if (const auto* mask = attackHitboxMask(frame)) {
                sample.regions[index] = gameplay::compileAttackShapeMask(
                    mask->width, mask->height, mask->cells,
                    mask->origin.x, mask->origin.y);
            }

            if (frame.durationTicks >
                std::numeric_limits<std::uint32_t>::max() - tick) {
                throw std::invalid_argument(
                    "Player sword animation duration overflows attack timing");
            }
            tick += frame.durationTicks;
        }

        authoredDurations[index] = static_cast<std::uint32_t>(tick);
        longestDuration = std::max(
            longestDuration, authoredDurations[index]);
    }

    if (!std::any_of(
            authoredFacings.begin(), authoredFacings.end(),
            [](bool value) { return value; })) {
        return;
    }

    // When authored animation is longer than the legacy sword timing, the
    // gameplay attack stays alive long enough to reach every authored frame.
    result.totalTicks = longestDuration;

    // A shorter direction gets an explicit empty sample so its last active
    // frame never leaks into the remainder of a longer attack.
    for (std::size_t index = 0; index < authoredFacings.size(); ++index) {
        if (!authoredFacings[index] ||
            authoredDurations[index] >= result.totalTicks) {
            continue;
        }
        auto& sample = collisionSample(
            result.collisionSamples, authoredDurations[index]);
        sample.authored[index] = true;
        sample.regions[index].clear();
    }

    result.collisionSamples.erase(
        std::remove_if(
            result.collisionSamples.begin(),
            result.collisionSamples.end(),
            [](const auto& sample) {
                return !std::any_of(
                    sample.authored.begin(), sample.authored.end(),
                    [](bool value) { return value; });
            }),
        result.collisionSamples.end());

    std::sort(
        result.collisionSamples.begin(), result.collisionSamples.end(),
        [](const auto& left, const auto& right) {
            return left.tick < right.tick;
        });
}

gameplay::AttackDefinition compileAttack(
    const AuthoredAttack& v,
    const AuthoredContentPack& pack) {
    gameplay::AttackDefinition result{v.id, v.kind, v.damage, v.totalTicks, v.cooldownTicks,
                                      v.minimumRangePixels, v.maximumRangePixels,
                                      v.visualActionId, v.meleeHitboxes,
                                      v.projectileDefinitionId, v.timeline, {}};
    for (const auto& authoredDirection : v.shapes) {
        for (const auto& frame : authoredDirection.frames) {
            auto& sample = collisionSample(
                result.collisionSamples, frame.tick);
            const auto index =
                gameplay::facingIndex(authoredDirection.facing);
            sample.regions[index] =
                gameplay::compileAttackShapeMask(
                    frame.width, frame.height, frame.cells);
            sample.authored[index] = true;
        }
    }
    applyDefaultPlayerSwordFrameMasks(pack, result);
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
    for (const auto& frame : v.frames) result.frames.push_back({frame.source, frame.anchor, frame.drawOffset, frame.durationTicks, frame.markers, frame.flipX});
    return result;
}
presentation::EnemyVisualDefinition compileEnemyVisual(const AuthoredEnemyVisual& v) {
    presentation::EnemyVisualDefinition result{v.id, v.idle, v.move, v.hurt, v.death, v.dead, {}};
    for (const auto& attack : v.attacks) result.attacks.push_back({attack.visualActionId, attack.clips});
    return result;
}
presentation::PlayerVisualDefinition compilePlayerVisual(
    const AuthoredPlayerVisual& v) {
    presentation::PlayerVisualDefinition result{
        v.id, v.idle, v.walk, v.hurt, {}
    };
    for (const auto& action : v.actions) {
        result.actions.push_back({action.actionId, action.clips});
    }
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

gameplay::ActorCollisionShapeDefinition compilePlayerMask(
    const AuthoredPixelMask& mask, bool mirrorX) {
    gameplay::ActorCollisionShapeDefinition result;
    const auto boxes = gameplay::compileAttackShapeMask(
        mask.width, mask.height, mask.cells, mask.origin.x, mask.origin.y);
    result.regions.reserve(boxes.size());
    for (const auto& box : boxes) {
        const int offsetX =
            mirrorX ? -(box.offsetX + box.width) : box.offsetX;
        result.regions.push_back(
            {offsetX, box.offsetY, box.width, box.height});
    }
    return result;
}

gameplay::ActorCollisionShapeDefinition compilePlayerFrameMask(
    const AuthoredAnimationFrameMask& mask) {
    gameplay::ActorCollisionShapeDefinition result;
    const auto boxes = gameplay::compileAttackShapeMask(
        mask.width, mask.height, mask.cells,
        mask.origin.x, mask.origin.y);
    result.regions.reserve(boxes.size());
    for (const auto& box : boxes) {
        result.regions.push_back(
            {box.offsetX, box.offsetY, box.width, box.height});
    }
    return result;
}

gameplay::PlayerHurtboxTimeline compileHurtboxTimeline(
    const AuthoredContentPack& pack,
    const presentation::DirectionalAnimationRef& clips,
    gameplay::FacingDirection facing) {
    gameplay::PlayerHurtboxTimeline result;
    const auto animationId = animationForFacing(clips, facing);
    if (!animationId) return result;

    const auto* animation = findAnimation(pack, *animationId);
    if (animation == nullptr || animation->frames.empty()) return result;

    const bool hasOverrides = std::any_of(
        animation->frames.begin(), animation->frames.end(),
        [](const auto& frame) {
            return frameMask(frame, "hurtbox") != nullptr;
        });
    if (!hasOverrides) return result;

    result.authored = true;
    result.loop = animation->loop;
    result.samples.reserve(animation->frames.size());

    std::uint64_t tick = 0;
    for (const auto& frame : animation->frames) {
        if (tick > std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument(
                "Player Hurtbox animation timeline is too long");
        }

        gameplay::PlayerHurtboxFrameSample sample;
        sample.tick = static_cast<std::uint32_t>(tick);
        if (const auto* mask = frameMask(frame, "hurtbox")) {
            sample.shape = compilePlayerFrameMask(*mask);
        }
        result.samples.push_back(std::move(sample));

        if (frame.durationTicks >
            std::numeric_limits<std::uint32_t>::max() - tick) {
            throw std::invalid_argument(
                "Player Hurtbox animation duration overflows");
        }
        tick += frame.durationTicks;
    }

    result.totalTicks = static_cast<std::uint32_t>(tick);
    return result;
}

gameplay::DirectionalPlayerHurtboxTimelines compileDirectionalHurtboxes(
    const AuthoredContentPack& pack,
    const presentation::DirectionalAnimationRef& clips) {
    gameplay::DirectionalPlayerHurtboxTimelines result;
    constexpr std::array facings{
        gameplay::FacingDirection::down,
        gameplay::FacingDirection::up,
        gameplay::FacingDirection::left,
        gameplay::FacingDirection::right,
    };
    for (std::size_t index = 0; index < facings.size(); ++index) {
        result.values[index] =
            compileHurtboxTimeline(pack, clips, facings[index]);
    }
    return result;
}

std::optional<gameplay::PlayerHurtboxFrameProfile>
compilePlayerHurtboxFrameProfile(
    const AuthoredPlayer& player,
    const AuthoredContentPack& pack) {
    const auto* visual = findPlayerVisual(pack, player.visualSetId);
    if (visual == nullptr) return std::nullopt;

    gameplay::PlayerHurtboxFrameProfile result;
    bool anyAuthored = false;

    result.idle = compileDirectionalHurtboxes(pack, visual->idle);
    anyAuthored = result.idle.authored() || anyAuthored;

    result.walk = compileDirectionalHurtboxes(pack, visual->walk);
    anyAuthored = result.walk.authored() || anyAuthored;

    if (visual->hurt) {
        auto hurt = compileDirectionalHurtboxes(pack, *visual->hurt);
        if (hurt.authored()) {
            anyAuthored = true;
            result.hurt = std::move(hurt);
        }
    }

    for (const auto& action : visual->actions) {
        auto timelines =
            compileDirectionalHurtboxes(pack, action.clips);
        if (!timelines.authored()) continue;
        anyAuthored = true;
        result.actions.emplace(
            action.actionId, std::move(timelines));
    }

    if (!anyAuthored) return std::nullopt;
    return result;
}

gameplay::PlayerDefinition compilePlayer(
    const AuthoredPlayer& v,
    const AuthoredContentPack& pack) {
    std::optional<gameplay::DirectionalActorCollisionShapes> movement;
    if (v.movementCollision) {
        gameplay::DirectionalActorCollisionShapes shapes;
        shapes.values[0] = compilePlayerMask(v.movementCollision->down, false);
        shapes.values[1] = compilePlayerMask(v.movementCollision->up, false);
        shapes.values[2] = compilePlayerMask(v.movementCollision->left, false);
        shapes.values[3] = compilePlayerMask(v.movementCollision->right, false);
        movement = std::move(shapes);
    }

    std::optional<gameplay::ActorCollisionShapeDefinition> hurtbox;
    if (v.hurtbox) {
        hurtbox = compilePlayerMask(*v.hurtbox, false);
    }

    auto frameOverrides =
        compilePlayerHurtboxFrameProfile(v, pack);

    return {v.id, v.visualSetId, v.progressionId,
            std::move(movement), std::move(hurtbox),
            std::move(frameOverrides)};
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
        for (const auto& value : authored.attacks) registry.attacks_.add(compileAttack(value, authored));
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
        for (const auto& value : authored.players) registry.players_.add(compilePlayer(value, authored));
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
        for (const auto& value : authored.playerVisuals) registry.playerVisuals_.add(compilePlayerVisual(value));
        for (const auto& value : authored.objectVisuals) registry.objectVisuals_.add(compileObjectVisual(value));
        result.registry.emplace(std::move(registry));
    } catch (const std::exception& exception) {
        result.report.diagnostics.push_back({ContentDiagnosticSeverity::error, "catalog_rejected", exception.what(), ContentKind::tileset, {}, "registry"});
    }
    return result;
}

ContentCompileResult compileContent(const AuthoredContentPack& authored) { return ContentCompiler{}.compile(authored); }

} // namespace underworld::game::content
