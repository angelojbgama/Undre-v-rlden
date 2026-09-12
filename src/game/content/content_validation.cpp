#include "game/content/content_validation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

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

bool safeRelativeAssetPath(const std::string& value) {
    if (value.empty() || value.find('\0') != std::string::npos) return false;
    if (value.front() == '/' || value.front() == '\\' ||
        (value.size() > 1 && std::isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':')) return false;
    std::string component;
    for (const char character : value + '/') {
        if (character == '/' || character == '\\') {
            if (component == ".." || component == "." || component.empty()) return false;
            component.clear();
        } else {
            component.push_back(character);
        }
    }
    return true;
}

bool visualCoordinateOutOfBounds(int value) noexcept {
    return value < -4096 || value > 4096;
}

void validateDirectional(const presentation::DirectionalAnimationRef& clips,
                         const std::unordered_set<std::string>& animations,
                         ContentValidationReport& report, ContentKind kind,
                         const simulation::DefinitionId& id, std::string_view field) {
    const std::optional<simulation::DefinitionId>* values[] = {
        &clips.defaultAnimation, &clips.down, &clips.up, &clips.side};
    bool hasBinding = false;
    for (const auto* value : values) {
        if (!value->has_value()) continue;
        hasBinding = true;
        if (!contains(animations, **value))
            error(report, kind, id, "unknown_reference", "directional animation does not exist", std::string(field));
    }
    if (!hasBinding)
        error(report, kind, id, "missing_binding", "directional animation requires at least one binding", std::string(field));
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

    std::array<bool, 4> seenFacings{};
    for (const auto& direction : value.shapes) {
        const auto facingIndex = [&] {
            switch (direction.facing) {
            case gameplay::FacingDirection::down: return std::size_t{0};
            case gameplay::FacingDirection::up: return std::size_t{1};
            case gameplay::FacingDirection::left: return std::size_t{2};
            case gameplay::FacingDirection::right: return std::size_t{3};
            }
            return std::size_t{0};
        }();
        if (seenFacings[facingIndex]) {
            error(report, ContentKind::attack, value.id, "duplicate_attack_shape_facing",
                  "attack shape facing is duplicated", "shapes");
            continue;
        }
        seenFacings[facingIndex] = true;
        std::unordered_set<std::uint32_t> seenFrames;
        for (const auto& frame : direction.frames) {
            const auto expectedCells = frame.width > 0 &&
                frame.height <= std::numeric_limits<std::size_t>::max() / frame.width
                ? static_cast<std::size_t>(frame.width) * frame.height : 0;
            if (frame.width == 0 || frame.height == 0 || expectedCells == 0 ||
                frame.cells.size() != expectedCells) {
                error(report, ContentKind::attack, value.id, "invalid_attack_shape",
                      "attack shape dimensions do not match its cell mask", "shapes");
            }
            if (frame.tick >= value.totalTicks) {
                error(report, ContentKind::attack, value.id, "invalid_attack_shape_tick",
                      "attack shape tick must be inside totalTicks", "shapes");
            }
            if (!seenFrames.emplace(frame.frameIndex).second) {
                error(report, ContentKind::attack, value.id, "duplicate_attack_shape_frame",
                      "attack shape frame index is duplicated for a facing", "shapes");
            }
        }
    }
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
    const auto npcVisuals = ids(pack.npcVisuals, report, ContentKind::npcVisual,
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
    static_cast<void>(ids(pack.presentationEffects, report, ContentKind::presentationEffect,
                          [](const auto& value) { return value.id; }));
    const auto visualImages = ids(pack.visualImages, report, ContentKind::visualImage,
                                  [](const auto& value) { return value.id; });
    const auto staticSprites = ids(pack.staticSprites, report, ContentKind::staticSprite,
                                   [](const auto& value) { return value.id; });
    const auto animations = ids(pack.animations, report, ContentKind::animation,
                                [](const auto& value) { return value.id; });
    const auto enemyVisuals = ids(pack.enemyVisuals, report, ContentKind::enemyVisual,
                                  [](const auto& value) { return value.id; });
    const auto objectVisuals = ids(pack.objectVisuals, report, ContentKind::objectVisual,
                                   [](const auto& value) { return value.id; });
    const bool hasVisualSchema = !pack.visualImages.empty() || !pack.staticSprites.empty() ||
        !pack.animations.empty() || !pack.enemyVisuals.empty() || !pack.objectVisuals.empty();

    for (const auto& value : pack.visualImages) {
        if (value.root != presentation::VisualAssetRoot::gameAssets &&
            value.root != presentation::VisualAssetRoot::contentWorkspace)
            error(report, ContentKind::visualImage, value.id, "invalid_asset_root",
                  "visual asset root is invalid", "root");
        if (!safeRelativeAssetPath(value.relativePath))
            error(report, ContentKind::visualImage, value.id, "invalid_asset_path",
                  "visual asset path must be a normalized relative path", "relativePath");
    }
    for (const auto& value : pack.staticSprites) {
        if (!contains(visualImages, value.imageId))
            error(report, ContentKind::staticSprite, value.id, "unknown_reference",
                  "visual image does not exist", "imageId");
        if (value.source && (value.source->x < 0 || value.source->y < 0 ||
                             value.source->width <= 0 || value.source->height <= 0))
            error(report, ContentKind::staticSprite, value.id, "invalid_source",
                  "static sprite source rectangle must be positive", "source");
        if (visualCoordinateOutOfBounds(value.anchor.x) ||
            visualCoordinateOutOfBounds(value.anchor.y))
            error(report, ContentKind::staticSprite, value.id, "invalid_anchor",
                  "static sprite anchor is outside bounds", "anchor");
    }
    for (const auto& value : pack.animations) {
        if (!contains(visualImages, value.imageId))
            error(report, ContentKind::animation, value.id, "unknown_reference",
                  "visual image does not exist", "imageId");
        if (value.frames.empty() || value.frames.size() > 4096)
            error(report, ContentKind::animation, value.id, "invalid_frame_count",
                  "animation must contain a bounded non-empty frame list", "frames");
        for (const auto& frame : value.frames) {
            if (frame.source.x < 0 || frame.source.y < 0 || frame.source.width <= 0 ||
                frame.source.height <= 0 || frame.durationTicks == 0 ||
                visualCoordinateOutOfBounds(frame.anchor.x) ||
                visualCoordinateOutOfBounds(frame.anchor.y) ||
                visualCoordinateOutOfBounds(frame.drawOffset.x) ||
                visualCoordinateOutOfBounds(frame.drawOffset.y))
                error(report, ContentKind::animation, value.id, "invalid_frame",
                      "animation frame source and duration must be positive", "frames");
            if (frame.markers.size() > 32)
                error(report, ContentKind::animation, value.id, "invalid_marker_count",
                      "animation frame has too many markers", "frames.markers");
        }
    }
    for (const auto& value : pack.enemyVisuals) {
        validateDirectional(value.idle, animations, report, ContentKind::enemyVisual, value.id, "idle");
        if (value.move) validateDirectional(*value.move, animations, report, ContentKind::enemyVisual, value.id, "move");
        if (value.hurt) validateDirectional(*value.hurt, animations, report, ContentKind::enemyVisual, value.id, "hurt");
        if (value.death) validateDirectional(*value.death, animations, report, ContentKind::enemyVisual, value.id, "death");
        if (value.dead) validateDirectional(*value.dead, animations, report, ContentKind::enemyVisual, value.id, "dead");
        std::unordered_set<std::string> attackIds;
        for (const auto& attack : value.attacks) {
            if (attack.visualActionId.empty() || !attackIds.emplace(std::string(attack.visualActionId.value())).second)
                error(report, ContentKind::enemyVisual, value.id, "duplicate_attack_visual",
                      "enemy attack visual action IDs must be unique", "attacks");
            validateDirectional(attack.clips, animations, report, ContentKind::enemyVisual, value.id, "attacks.clips");
        }
    }
    for (const auto& value : pack.objectVisuals) {
        if (value.idleAnimationId.empty() || !contains(animations, value.idleAnimationId))
            error(report, ContentKind::objectVisual, value.id, "unknown_reference",
                  "idle animation does not exist", "idleAnimationId");
        const std::pair<const std::optional<simulation::DefinitionId>*, const char*> optionalAnimations[] = {
            {&value.openedAnimationId, "openedAnimationId"},
            {&value.damagedAnimationId, "damagedAnimationId"},
            {&value.destroyingAnimationId, "destroyingAnimationId"},
            {&value.activationInactiveAnimationId, "activationInactiveAnimationId"},
            {&value.activationActiveAnimationId, "activationActiveAnimationId"},
            {&value.doorLockedAnimationId, "doorLockedAnimationId"},
            {&value.doorClosedAnimationId, "doorClosedAnimationId"},
            {&value.doorOpenAnimationId, "doorOpenAnimationId"},
            {&value.destroyedAnimationId, "destroyedAnimationId"},
        };
        for (const auto& [optionalId, field] : optionalAnimations) {
            if (*optionalId && !contains(animations, **optionalId))
                error(report, ContentKind::objectVisual, value.id, "unknown_reference",
                      "object animation does not exist", field);
        }
    }

    for (const auto& value : pack.presentationEffects) {
        const bool validLifetime = value.lifetime == presentation::PresentationEffectLifetime::transient ||
                                   value.lifetime == presentation::PresentationEffectLifetime::persistent;
        if (!validLifetime) error(report, ContentKind::presentationEffect, value.id,
                                  "invalid_lifetime", "presentation effect lifetime is invalid", "lifetime");
        if (value.lifetime == presentation::PresentationEffectLifetime::transient &&
            (value.durationTicks == 0 || value.durationTicks > 100000))
            error(report, ContentKind::presentationEffect, value.id, "invalid_duration",
                  "transient presentation effect duration is outside bounds", "durationTicks");
        if (value.lifetime == presentation::PresentationEffectLifetime::persistent && value.fade)
            error(report, ContentKind::presentationEffect, value.id, "invalid_persistent_fade",
                  "persistent presentation effects cannot contain fades", "fade");
        if (value.priority < -1000 || value.priority > 1000)
            error(report, ContentKind::presentationEffect, value.id, "invalid_priority",
                  "presentation effect priority is outside bounds", "priority");
        if (!value.cameraShake && !value.overlay && !value.visionMask && !value.fade)
            error(report, ContentKind::presentationEffect, value.id, "empty_effect",
                  "presentation effect must define at least one primitive", "definition");
        if (value.cameraShake) {
            if (value.cameraShake->amplitudePixels <= 0 || value.cameraShake->amplitudePixels > 64)
                error(report, ContentKind::presentationEffect, value.id, "invalid_amplitude",
                      "camera shake amplitude is outside bounds", "cameraShake.amplitudePixels");
            if (value.lifetime == presentation::PresentationEffectLifetime::persistent)
                error(report, ContentKind::presentationEffect, value.id, "persistent_camera_shake",
                      "persistent presentation effects cannot contain camera shake", "cameraShake");
        }
        if (value.overlay) {
            const auto validMode = value.overlay->mode == presentation::PresentationOverlayMode::constant ||
                value.overlay->mode == presentation::PresentationOverlayMode::linearFadeOut ||
                value.overlay->mode == presentation::PresentationOverlayMode::pulse;
            if (!validMode) error(report, ContentKind::presentationEffect, value.id, "invalid_overlay_mode",
                                   "overlay mode is invalid", "overlay.mode");
            if (value.overlay->mode == presentation::PresentationOverlayMode::pulse &&
                (value.overlay->pulsePeriodTicks == 0 || value.overlay->pulsePeriodTicks > 100000))
                error(report, ContentKind::presentationEffect, value.id, "invalid_pulse_period",
                      "pulse period must be positive and bounded", "overlay.pulsePeriodTicks");
        }
        if (value.visionMask && (value.visionMask->innerRadiusPixels < 0 ||
            value.visionMask->outerRadiusPixels <= 0 ||
            value.visionMask->outerRadiusPixels < value.visionMask->innerRadiusPixels ||
            value.visionMask->outerRadiusPixels > 4096))
            error(report, ContentKind::presentationEffect, value.id, "invalid_vision_mask",
                  "vision mask radii are invalid", "visionMask");
    }

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
        if (hasVisualSchema && !contains(staticSprites, value.visualId))
            error(report, ContentKind::projectile, value.id, "unknown_reference", "projectile static sprite does not exist", "visualId");
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
        if (hasVisualSchema && !contains(enemyVisuals, value.visualSetId))
            error(report, ContentKind::enemy, value.id, "unknown_reference", "enemy visual definition does not exist", "visualSetId");
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
        if (hasVisualSchema && !contains(staticSprites, value.visualId))
            error(report, ContentKind::item, value.id, "unknown_reference", "item static sprite does not exist", "visualId");
    }
    for (const auto& value : pack.objects) {
        // Scenery objects intentionally have no gameplay capability. Their visual is
        // still authored as an ObjectVisual so they can be placed and animated by the
        // same map/runtime path as interactive objects.
        if (value.id.empty() || value.visualSetId.empty())
            error(report, ContentKind::object, value.id, "invalid_value", "object must have a valid id and visual", "definition");
        if (value.collision) {
            const auto expectedCells = value.collision->width > 0 &&
                value.collision->height <= std::numeric_limits<std::size_t>::max() / value.collision->width
                ? static_cast<std::size_t>(value.collision->width) * value.collision->height : 0;
            const auto collisionRight = static_cast<std::int64_t>(value.collision->origin.x) +
                                        value.collision->width;
            const auto collisionBottom = static_cast<std::int64_t>(value.collision->origin.y) +
                                         value.collision->height;
            const bool coordinatesFit =
                value.collision->width <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
                value.collision->height <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
                collisionRight <= std::numeric_limits<int>::max() &&
                collisionBottom <= std::numeric_limits<int>::max();
            if (value.collision->width == 0 || value.collision->height == 0 || expectedCells == 0 ||
                !coordinatesFit || value.collision->cells.size() != expectedCells ||
                std::any_of(value.collision->cells.begin(), value.collision->cells.end(),
                            [](std::uint8_t cell) { return cell > 1; }) ||
                std::none_of(value.collision->cells.begin(), value.collision->cells.end(),
                             [](std::uint8_t cell) { return cell != 0; })) {
                error(report, ContentKind::object, value.id, "invalid_object_collision",
                      "object collision must be a bounded non-empty 0/1 mask with matching dimensions",
                      "collision");
            }
        }
        if (value.interactable && (value.interactable->bounds.width <= 0 || value.interactable->bounds.height <= 0)) error(report, ContentKind::object, value.id, "invalid_value", "interaction bounds are invalid", "interactable");
        if (value.container && value.container->capacity == 0) error(report, ContentKind::object, value.id, "invalid_value", "container capacity must be positive", "container");
        if (value.destructible && (value.destructible->maximumHealth <= 0 || value.destructible->hurtbox.width <= 0 || value.destructible->hurtbox.height <= 0 || value.destructible->destructionDurationTicks == 0 || value.destructible->damageDurationTicks == 0))
            error(report, ContentKind::object, value.id, "invalid_value", "destructible values are invalid", "destructible");
        if (value.door && value.door->hasBlockingBounds &&
            (value.door->blockingBounds.width <= 0 || value.door->blockingBounds.height <= 0))
            error(report, ContentKind::object, value.id, "invalid_value", "door blocking bounds must be positive", "door.blockingBounds");
        if (value.bankAccess && (!value.interactable || value.container || value.destructible))
            error(report, ContentKind::object, value.id, "invalid_bank_access", "bank access requires interaction and cannot be a container or destructible", "bankAccess");
        if (value.activation) {
            if (value.activation->mode == gameplay::ObjectActivationMode::interactToggle) {
                if (!value.interactable || value.activation->activationBounds) {
                    error(report, ContentKind::object, value.id, "invalid_activation", "interact-toggle activation requires interactable and no pressure bounds", "activation");
                }
            } else if (value.activation->mode == gameplay::ObjectActivationMode::playerPressure) {
                if (!value.activation->activationBounds || value.activation->activationBounds->width <= 0 ||
                    value.activation->activationBounds->height <= 0 || value.activation->initialActive) {
                    error(report, ContentKind::object, value.id, "invalid_activation", "player-pressure activation requires positive bounds and inactive initial state", "activation");
                }
            } else {
                error(report, ContentKind::object, value.id, "invalid_activation", "unknown object activation mode", "activation.mode");
            }
        }
        if (hasVisualSchema && !contains(objectVisuals, value.visualSetId))
            error(report, ContentKind::object, value.id, "unknown_reference", "object visual definition does not exist", "visualSetId");
    }
    for (const auto& value : pack.pickups) {
        if (value.visualId.empty() || value.collectionBounds.width <= 0 || value.collectionBounds.height <= 0) error(report, ContentKind::pickup, value.id, "invalid_value", "pickup visual and collection bounds are invalid", "definition");
        if (hasVisualSchema && !contains(staticSprites, value.visualId))
            error(report, ContentKind::pickup, value.id, "unknown_reference", "pickup static sprite does not exist", "visualId");
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
        if (!contains(npcVisuals, value.visualSetId)) error(report, ContentKind::npc, value.id, "unknown_reference", "NPC visual set does not exist", "visualSetId");
        if (!contains(dialogues, value.defaultDialogueId)) error(report, ContentKind::npc, value.id, "unknown_reference", "dialogue definition does not exist", "defaultDialogueId");
    }
    for (const auto& value : pack.npcVisuals) {
        if (value.idle) validateDirectional(*value.idle, animations, report, ContentKind::npcVisual, value.id, "idle");
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
        if (value.variantWeight == 0) error(report, ContentKind::tileSemantic, value.id, "invalid_variant_weight", "semantic variant weight must be greater than zero", "variantWeight");
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
