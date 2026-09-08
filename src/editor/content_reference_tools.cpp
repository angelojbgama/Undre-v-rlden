#include "editor/content_reference_tools.h"

#include <algorithm>
#include <cctype>

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
    case ContentDefinitionKind::animation:
        if (const auto* value = document.animation(key.id)) {
            return std::to_string(value->frames.size()) + " frames";
        }
        break;
    case ContentDefinitionKind::dialogue:
        if (const auto* value = document.dialogue(key.id)) {
            return std::to_string(value->nodes.size()) + " nodes";
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
