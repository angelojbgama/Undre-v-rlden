#include "game/maps/map_data.h"

#include <limits>
#include <unordered_set>

namespace underworld::game::maps {
namespace {

MapValidationResult failure(std::string error) { return {false, std::move(error)}; }

bool validFacing(gameplay::FacingDirection facing) noexcept {
    return facing == gameplay::FacingDirection::down || facing == gameplay::FacingDirection::up ||
           facing == gameplay::FacingDirection::left || facing == gameplay::FacingDirection::right;
}

bool validArea(world::AabbI area) noexcept { return area.width > 0 && area.height > 0; }

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
            if (catalogs->npcVisuals && !catalogs->npcVisuals->find(definition->visualSetId)) {
                return failure("NPC definition references an unknown visual definition");
            }
        }
    }
    for (const auto& object : data.objects) {
        if (!acceptId(object.id)) { return failure("persistent instance id is zero or duplicate"); }
        if (object.definitionId.empty()) { return failure("object placement definition is empty"); }
        const gameplay::WorldObjectDefinition* definition = nullptr;
        if (catalogs && catalogs->objects) {
            definition = catalogs->objects->find(object.definitionId);
            if (!definition) { return failure("object placement references an unknown definition"); }
        }
        if (!object.initialContents.empty() && definition && !definition->container) {
            return failure("non-container object placement has initial contents");
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
    return {true, {}};
}

bool semanticallyEqual(const MapData& a, const MapData& b) noexcept {
    if (!(a.id == b.id) || a.width != b.width || a.height != b.height ||
        a.tileSize != b.tileSize || a.tileReferences != b.tileReferences ||
        a.layers != b.layers || a.collision != b.collision ||
        a.playerSpawns != b.playerSpawns || a.enemies != b.enemies || a.npcs != b.npcs ||
        a.links != b.links ||
        a.objects.size() != b.objects.size() || a.pickups.size() != b.pickups.size()) { return false; }
    for (std::size_t i = 0; i < a.objects.size(); ++i) {
        const auto& x = a.objects[i]; const auto& y = b.objects[i];
        if (!(x.id == y.id) || !(x.definitionId == y.definitionId) || !(x.position == y.position) ||
            x.initialContents.size() != y.initialContents.size()) { return false; }
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
