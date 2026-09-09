#include "editor/content_reference_tools.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>

namespace underworld::editor {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool containsInsensitive(std::string_view value, std::string_view query) {
    return lower(std::string(value)).find(lower(std::string(query))) != std::string::npos;
}

std::string optionalId(const std::optional<simulation::DefinitionId>& id) {
    return id ? std::string(id->value()) : "none";
}

std::string animationSummary(const game::content::AuthoredAnimation& value) {
    std::uint64_t duration{};
    for (const auto& frame : value.frames) duration += frame.durationTicks;
    return std::string(value.imageId.value()) + "  " + std::to_string(value.frames.size()) +
           " frames  " + std::to_string(duration) + " ticks";
}

} // namespace

std::string contentDefinitionDisplayName(const ContentWorkspaceDocument& document,
                                          const ContentDefinitionKey& key) {
    switch (key.kind) {
    case ContentDefinitionKind::tileset:
        if (const auto* value = document.tileset(key.id); value && !value->displayName.empty()) return value->displayName;
        break;
    case ContentDefinitionKind::quest:
        if (const auto* value = document.quest(key.id); value && !value->title.empty()) return value->title;
        break;
    case ContentDefinitionKind::authoringDescriptor:
        if (const auto* value = document.authoringDescriptor(key.id); value && !value->displayName.empty()) return value->displayName;
        break;
    default:
        break;
    }
    return std::string(key.id.value());
}

std::string contentDefinitionSummary(const ContentWorkspaceDocument& document,
                                     const ContentDefinitionKey& key) {
    switch (key.kind) {
    case ContentDefinitionKind::item:
        if (const auto* value = document.item(key.id)) {
            return "stack " + std::to_string(value->stackLimit) +
                   (value->use ? "  usable" : "") + (value->equipment ? "  equipment" : "");
        }
        break;
    case ContentDefinitionKind::tileset:
        if (const auto* value = document.tileset(key.id)) {
            return value->relativeAssetPath + "  " + std::to_string(value->tileSize) + " px  " +
                   std::to_string(value->columns) + " x " + std::to_string(value->rows);
        }
        break;
    case ContentDefinitionKind::visualImage:
        if (const auto* value = document.visualImage(key.id)) {
            const char* root = value->root == game::presentation::VisualAssetRoot::gameAssets
                ? "game assets" : "workspace";
            return std::string(root) + "  " + value->relativePath;
        }
        break;
    case ContentDefinitionKind::staticSprite:
        if (const auto* value = document.staticSprite(key.id)) {
            if (!value->source) return std::string(value->imageId.value()) + "  full image";
            const auto& source = *value->source;
            return std::string(value->imageId.value()) + "  " +
                   std::to_string(source.x) + "," + std::to_string(source.y) + "  " +
                   std::to_string(source.width) + " x " + std::to_string(source.height);
        }
        break;
    case ContentDefinitionKind::animation:
        if (const auto* value = document.animation(key.id)) return animationSummary(*value);
        break;
    case ContentDefinitionKind::enemyVisual:
        if (const auto* value = document.enemyVisual(key.id)) {
            return optionalId(value->idle.defaultAnimation) + " idle  " +
                   std::to_string(value->attacks.size()) + " action bindings";
        }
        break;
    case ContentDefinitionKind::objectVisual:
        if (const auto* value = document.objectVisual(key.id)) {
            std::size_t stateCount = 1;
            stateCount += value->openedAnimationId.has_value();
            stateCount += value->destroyingAnimationId.has_value();
            stateCount += value->destroyedAnimationId.has_value();
            stateCount += value->activationInactiveAnimationId.has_value();
            stateCount += value->activationActiveAnimationId.has_value();
            stateCount += value->doorLockedAnimationId.has_value();
            stateCount += value->doorClosedAnimationId.has_value();
            stateCount += value->doorOpenAnimationId.has_value();
            return std::string(value->idleAnimationId.value()) + " idle  " +
                   std::to_string(stateCount) + " state bindings";
        }
        break;
    case ContentDefinitionKind::npcVisual:
        if (const auto* value = document.npcVisual(key.id)) {
            return value->idle ? optionalId(value->idle->defaultAnimation) + " idle"
                               : "no idle binding";
        }
        break;
    case ContentDefinitionKind::projectile:
        if (const auto* value = document.projectile(key.id)) {
            return std::string(value->visualId.value()) + "  " +
                   std::to_string(value->speedPixelsPerTick) + " px/tick  " +
                   std::to_string(value->lifetimeTicks) + " ticks";
        }
        break;
    case ContentDefinitionKind::attack:
        if (const auto* value = document.attack(key.id)) {
            return std::to_string(value->damage.amount) + " damage  " +
                   std::to_string(value->totalTicks) + " ticks  " +
                   std::to_string(value->timeline.size()) + " events";
        }
        break;
    case ContentDefinitionKind::behavior:
        if (const auto* value = document.behavior(key.id)) {
            return std::to_string(value->detectionRangePixels) + " detect  " +
                   std::to_string(value->disengageRangePixels) + " disengage";
        }
        break;
    case ContentDefinitionKind::enemy:
        if (const auto* value = document.enemy(key.id)) {
            return std::string(value->visualSetId.value()) + "  " +
                   std::to_string(value->maximumHealth) + " HP  " +
                   std::to_string(value->attackIds.size()) + " attacks  reward " +
                   optionalId(value->rewardProfileId);
        }
        break;
    case ContentDefinitionKind::object:
        if (const auto* value = document.object(key.id)) {
            std::size_t capabilityCount{};
            capabilityCount += value->interactable.has_value();
            capabilityCount += value->container.has_value();
            capabilityCount += value->destructible.has_value();
            capabilityCount += value->bankAccess.has_value();
            capabilityCount += value->door.has_value();
            capabilityCount += value->activation.has_value();
            return std::string(value->visualSetId.value()) + "  " +
                   std::to_string(capabilityCount) + " capabilities";
        }
        break;
    case ContentDefinitionKind::npc:
        if (const auto* value = document.npc(key.id)) {
            return std::string(value->visualSetId.value()) + "  dialogue " +
                   std::string(value->defaultDialogueId.value()) + "  " +
                   std::to_string(value->tags.size()) + " tags";
        }
        break;
    case ContentDefinitionKind::pickup:
        if (const auto* value = document.pickup(key.id)) {
            if (std::holds_alternative<game::content::AuthoredItemPickup>(value->payload)) {
                const auto& item = std::get<game::content::AuthoredItemPickup>(value->payload);
                return std::string(item.itemId.value()) + " x" + std::to_string(item.quantity);
            }
            if (std::holds_alternative<game::content::AuthoredHealthPickup>(value->payload)) {
                return "health +" + std::to_string(std::get<game::content::AuthoredHealthPickup>(value->payload).amount);
            }
            return "currency +" + std::to_string(std::get<game::content::AuthoredCurrencyPickup>(value->payload).amount);
        }
        break;
    case ContentDefinitionKind::rewardGrant:
        if (const auto* value = document.rewardGrant(key.id)) {
            std::string result = std::to_string(value->experience) + " XP  " +
                std::to_string(value->gold) + " gold";
            if (value->items.empty()) return result + "  no items";
            for (const auto& item : value->items) {
                result += "  |  " + std::string(item.itemId.value()) +
                    " x" + std::to_string(item.quantity);
            }
            return result;
        }
        break;
    case ContentDefinitionKind::rewardProfile:
        if (const auto* value = document.rewardProfile(key.id)) {
            std::string result = std::to_string(value->experience) + " XP";
            if (value->loot.empty()) return result + "  no loot";
            for (const auto& loot : value->loot) {
                result += "  |  " + std::string(loot.pickupDefinitionId.value()) +
                    " " + std::to_string(loot.chanceBasisPoints / 100) + "." +
                    std::to_string(loot.chanceBasisPoints % 100) + "%";
            }
            return result;
        }
        break;
    case ContentDefinitionKind::shop:
        if (const auto* value = document.shop(key.id)) {
            if (value->offers.empty()) return "no offers";
            std::string result;
            for (const auto& offer : value->offers) {
                if (!result.empty()) result += "  |  ";
                result += std::string(offer.itemId.value());
                if (offer.playerBuyPrice) result += " buy " + std::to_string(*offer.playerBuyPrice);
            }
            return result;
        }
        break;
    case ContentDefinitionKind::dialogue:
        if (const auto* value = document.dialogue(key.id)) {
            return std::to_string(value->nodes.size()) + " nodes";
        }
        break;
    case ContentDefinitionKind::tileSemantic:
        if (const auto* value = document.tileSemantic(key.id)) {
            return std::string(value->tilesetId.value()) + "  tile " +
                   std::to_string(value->sourceIndex) + "  " + value->family;
        }
        break;
    case ContentDefinitionKind::stamp:
        if (const auto* value = document.stamp(key.id)) {
            return std::to_string(value->width) + " x " + std::to_string(value->height) +
                   "  " + std::to_string(value->cells.size()) + " cells";
        }
        break;
    case ContentDefinitionKind::playerProgression:
        if (const auto* value = document.playerProgression(key.id)) {
            return std::to_string(value->baseStats.maximumHealth) + " base HP  " +
                   std::to_string(value->cumulativeExperienceThresholds.size()) + " thresholds";
        }
        break;
    case ContentDefinitionKind::presentationEffect:
        if (const auto* value = document.presentationEffect(key.id)) {
            std::size_t componentCount{};
            componentCount += value->cameraShake.has_value();
            componentCount += value->overlay.has_value();
            componentCount += value->visionMask.has_value();
            componentCount += value->fade.has_value();
            return std::to_string(value->durationTicks) + " ticks  " +
                   std::to_string(componentCount) + " components";
        }
        break;
    case ContentDefinitionKind::quest:
        if (const auto* value = document.quest(key.id)) {
            return std::to_string(value->objectives.size()) + " objectives";
        }
        break;
    default:
        break;
    }
    return {};
}

std::vector<ContentReferenceCandidate> contentReferenceCandidates(
    const ContentWorkspaceDocument& document, ContentDefinitionKind kind,
    std::string_view query) {
    std::vector<ContentReferenceCandidate> result;
    for (const auto& key : document.index()) {
        if (key.kind != kind) continue;
        const auto displayName = contentDefinitionDisplayName(document, key);
        const auto summary = contentDefinitionSummary(document, key);
        if (!query.empty() && !containsInsensitive(key.id.value(), query) &&
            !containsInsensitive(displayName, query) && !containsInsensitive(summary, query)) continue;
        result.push_back({key, displayName, summary});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.displayName != right.displayName) return left.displayName < right.displayName;
        return left.key.id.value() < right.key.id.value();
    });
    return result;
}

} // namespace underworld::editor
