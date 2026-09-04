#include "game/maps/runtime_world.h"

#include <algorithm>
#include <limits>

namespace underworld::game::maps {

RuntimeWorldBuildResult RuntimeWorldBuilder::build(
    const MapData& data, const simulation::SpawnId& spawnId) const {
    const auto validation = validateMapData(data, &catalogs_);
    if (!validation) { return {nullptr, validation.error}; }
    const auto spawn = std::find_if(data.playerSpawns.begin(), data.playerSpawns.end(),
        [&](const PlayerSpawn& candidate) { return candidate.id == spawnId; });
    if (spawn == data.playerSpawns.end()) { return {nullptr, "requested player spawn does not exist"}; }
    for (const auto& tile : data.tileReferences) {
        if (!tilesets_.mapping().contains(tile.tilesetId)) {
            return {nullptr, "map references an unavailable runtime tileset"};
        }
    }
    std::unique_ptr<RuntimeWorld> result;
    try {
        world::RuntimeMap runtime(static_cast<int>(data.width), static_cast<int>(data.height),
                                  static_cast<int>(data.tileSize));
        for (const auto& sourceLayer : data.layers) {
            auto& layer = runtime.layer(runtime.addLayer(sourceLayer.name, sourceLayer.visible));
            for (std::uint32_t y = 0; y < data.height; ++y) {
                for (std::uint32_t x = 0; x < data.width; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y) * data.width + x;
                    if (!sourceLayer.cells[index]) { continue; }
                    const auto& source = data.tileReferences[*sourceLayer.cells[index]];
                    layer.set(static_cast<int>(x), static_cast<int>(y),
                        world::TileRef{{tilesets_.requireRuntimeId(source.tilesetId), source.sourceIndex}, source.flags});
                }
            }
        }
        for (std::uint32_t y = 0; y < data.height; ++y) {
            for (std::uint32_t x = 0; x < data.width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * data.width + x;
                runtime.collision().setSolid(static_cast<int>(x), static_cast<int>(y), data.collision[index] != 0);
            }
        }
        result = std::make_unique<RuntimeWorld>(data.id, std::move(runtime), *spawn);
        result->enemies_.reserve(data.enemies.size());
        for (const auto& placement : data.enemies) {
            result->enemies_.push_back({placement.id, enemyFactory_.create(
                placement.definitionId, placement.position, placement.facing)});
        }
        result->objects_.reserve(data.objects.size());
        for (const auto& placement : data.objects) {
            result->objects_.push_back({placement.id, objectFactory_.create(
                placement.definitionId, placement.position, placement.initialContents)});
        }
        result->pickupDefinitions_.reserve(data.pickups.size());
        for (const auto& placement : data.pickups) {
            result->pickupDefinitions_.push_back({placement.definitionId, placement.visualId,
                                                  placement.collectionBounds, placement.payload});
        }
        result->pickups_.reserve(data.pickups.size());
        for (std::size_t index = 0; index < data.pickups.size(); ++index) {
            result->pickups_.push_back({data.pickups[index].id, gameplay::WorldPickup{
                handles_.create(), result->pickupDefinitions_[index], data.pickups[index].position}});
        }
        return {std::move(result), {}};
    } catch (const std::exception& exception) {
        if (result) {
            for (auto& enemy : result->enemies_) {
                static_cast<void>(handles_.destroy(enemy.instance.handle()));
            }
            for (auto& object : result->objects_) {
                static_cast<void>(handles_.destroy(object.instance.handle()));
            }
            for (auto& pickup : result->pickups_) {
                static_cast<void>(handles_.destroy(pickup.instance.handle()));
            }
        }
        return {nullptr, exception.what()};
    }
}

} // namespace underworld::game::maps
