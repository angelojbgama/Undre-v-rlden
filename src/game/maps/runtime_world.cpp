#include "game/maps/runtime_world.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>

namespace underworld::game::maps {

namespace {
DoorPhysicalState stableDoorPhysicalState(
    gameplay::DoorState state) noexcept {
    return state == gameplay::DoorState::open
        ? DoorPhysicalState::open
        : DoorPhysicalState::closed;
}
std::optional<simulation::DefinitionId> resolveDoorOpeningAnimation(const gameplay::WorldObjectDefinition& def,const MapValidationCatalogs& catalogs) noexcept {
    if (
        !catalogs.animationCollisions
        || !catalogs.objectVisuals
    ) {
        return std::nullopt;
    }

    const auto* v = catalogs.objectVisuals->find(
        def.visualSetId);

    if (!v) {
        return std::nullopt;
    }
    std::optional<simulation::DefinitionId> selected;
    bool ambiguous = false;
    const auto consider = [&](const simulation::DefinitionId& id) {
        if (!catalogs.animationCollisions->find(id)) {
            return;
        }

        if (!selected) {
            selected = id;
            return;
        }

        if (*selected != id) {
            ambiguous = true;
        }
    };
    consider(v->idleAnimationId);

    for (const auto* c : {
            &v->doorLockedAnimationId,
            &v->doorClosedAnimationId,
            &v->doorOpenAnimationId
        }) {
        if (*c) {
            consider(**c);
        }
    }
    return ambiguous
        ? std::nullopt
        : selected;
}
void configureDoorTimeline(RuntimeDoor& door,const gameplay::WorldObjectDefinition& def,const MapValidationCatalogs& catalogs) noexcept {
    door.physicalState = stableDoorPhysicalState(
        door.state);

    const auto id = resolveDoorOpeningAnimation(
        def,
        catalogs);

    if (!id || !catalogs.animationCollisions) {
        return;
    }

    const auto* p = catalogs.animationCollisions->find(
        *id);

    if (!p || p->durationTicks == 0) {
        return;
    }

    door.openingAnimationId = *id;
    door.openingDurationTicks = p->durationTicks;
}
void syncRuntimeDoorObjectState(
    std::vector<PersistentObject>& objects,
    simulation::PersistentInstanceId id,
    gameplay::DoorState state) noexcept {
    for (auto& o : objects) {
        if (o.persistentId == id) {
            static_cast<void>(
                o.instance.setDoorState(state));
            return;
        }
    }
}
} // namespace

std::vector<world::AabbI> RuntimeWorld::objectCollisionBounds() const {
    std::vector<world::AabbI> result;

    for (const auto& object : objects_) {
        const auto& definition =
            object.instance.definition();

        const gameplay::ObjectCollisionDefinition*
            collision = nullptr;

        bool animatedCollisionOwnsFrame = false;

        if (
            animationCollisions_ != nullptr
            && object.instance.isDoor()
        ) {
            const auto* runtimeDoor =
                door(object.persistentId);

            if (
                runtimeDoor != nullptr
                && runtimeDoor->openingAnimationId
                && animationCollisions_->find(
                    *runtimeDoor->openingAnimationId) != nullptr
            ) {
                // Profile existence means the authored animation owns collision.
                // An empty sampled frame is therefore intentional "no collision",
                // not a reason to fall back to definition.collision.
                animatedCollisionOwnsFrame = true;

                const auto frameIndex =
                    doorAnimationFrameIndex(
                        object.persistentId);

                if (frameIndex) {
                    collision =
                        animationCollisions_->sample(
                            *runtimeDoor->openingAnimationId,
                            *frameIndex);
                }
            }
        }

        if (!animatedCollisionOwnsFrame) {
            if (!definition.collision) {
                continue;
            }

            if (
                object.instance.isDoor()
                && object.instance.doorState()
                    == gameplay::DoorState::open
            ) {
                continue;
            }

            collision =
                &*definition.collision;
        }

        if (collision == nullptr) {
            continue;
        }

        result.reserve(
            result.size()
            + collision->regions.size());

        for (const auto& region : collision->regions) {
            result.push_back({
                object.instance.position().x + region.x,
                object.instance.position().y + region.y,
                region.width,
                region.height
            });
        }
    }

    return result;
}

std::vector<world::AabbI> RuntimeWorld::movementCollisionBounds() const {
    std::vector<world::AabbI> result = tileCollisionBounds_;

    const auto objects =
        objectCollisionBounds();

    result.reserve(
        result.size() +
        objects.size());

    result.insert(
        result.end(),
        objects.begin(),
        objects.end());

    return result;
}

void RuntimeWorld::addDestroyedObjectResidue(
    simulation::PersistentInstanceId persistentId, simulation::DefinitionId visualSetId,
    core::WorldPointI position) {
    if (!persistentId || visualSetId.empty()) {
        throw std::invalid_argument("destroyed object residue identity is invalid");
    }
    const auto found = std::find_if(destroyedObjectResidues_.begin(),
                                    destroyedObjectResidues_.end(),
                                    [&](const DestroyedObjectResidue& residue) {
                                        return residue.persistentId == persistentId;
                                    });
    if (found != destroyedObjectResidues_.end()) {
        *found = {persistentId, std::move(visualSetId), position};
        return;
    }
    destroyedObjectResidues_.push_back({persistentId, std::move(visualSetId), position});
}

bool RuntimeWorld::setDoorState(simulation::PersistentInstanceId id,
                                gameplay::DoorState state) noexcept {
    const auto found = std::find_if(
        doors_.begin(),
        doors_.end(),
        [&](const RuntimeDoor& door) {
            return door.id == id;
        });

    if (found == doors_.end()) {
        return false;
    }
    found->state = state;
    found->physicalState = stableDoorPhysicalState(
        state);
    found->openingTick = 0;
    for (const auto& cell : found->cells) {
        map_.collision().setSolid(
            cell.x,
            cell.y,
            state == gameplay::DoorState::open
                ? cell.baseSolid
                : true);
    }
    syncRuntimeDoorObjectState(
        objects_,
        id,
        state);

    return true;
}

const RuntimeDoor* RuntimeWorld::door(
    simulation::PersistentInstanceId id) const noexcept {
    const auto found = std::find_if(
        doors_.begin(),
        doors_.end(),
        [&](const RuntimeDoor& candidate) {
            return candidate.id == id;
        });

    return found == doors_.end()
        ? nullptr
        : &*found;
}

std::optional<gameplay::DoorState> RuntimeWorld::doorState(
    simulation::PersistentInstanceId id) const noexcept {
    const auto* runtimeDoor = door(id);

    return runtimeDoor == nullptr
        ? std::nullopt
        : std::optional{runtimeDoor->state};
}

std::optional<DoorPhysicalState> RuntimeWorld::doorPhysicalState(
    simulation::PersistentInstanceId id) const noexcept {
    const auto* d = door(id);
    return d
        ? std::optional{d->physicalState}
        : std::nullopt;
}
std::optional<std::size_t> RuntimeWorld::doorAnimationFrameIndex(simulation::PersistentInstanceId id) const noexcept {
    const auto* d = door(id);

    if (
        !d
        || !d->openingAnimationId
        || !animationCollisions_
    ) {
        return std::nullopt;
    }

    const auto* p = animationCollisions_->find(
        *d->openingAnimationId);

    if (!p || p->frames.empty()) {
        return std::nullopt;
    }
    if (d->physicalState == DoorPhysicalState::closed) {
        return std::size_t{0};
    }

    if (d->physicalState == DoorPhysicalState::open) {
        return p->frames.size() - 1;
    }

    return animationCollisions_->frameIndexAtTick(
        *d->openingAnimationId,
        d->openingTick);
}

bool RuntimeWorld::interactDoor(
    simulation::PersistentInstanceId id,
    gameplay::ItemContainer& inventory) noexcept {
    const auto found = std::find_if(
        doors_.begin(),
        doors_.end(),
        [&](const RuntimeDoor& d) {
            return d.id == id;
        });
    if (
        found == doors_.end()
        || found->state == gameplay::DoorState::open
        || found->physicalState == DoorPhysicalState::opening
    ) {
        return false;
    }
    if (found->state == gameplay::DoorState::locked) {
        if (
            !found->requiredItemId
            || inventory.count(*found->requiredItemId) == 0
        ) {
            return false;
        }

        if (
            found->consumeItem
            && !inventory.consume(*found->requiredItemId)
        ) {
            return false;
        }

        found->state = gameplay::DoorState::closed;
        syncRuntimeDoorObjectState(
            objects_,
            id,
            gameplay::DoorState::closed);
    }
    if (
        found->openingAnimationId
        && found->openingDurationTicks > 0
    ) {
        found->physicalState = DoorPhysicalState::opening;
        found->openingTick = 0;
        return true;
    }
    return setDoorState(
        id,
        gameplay::DoorState::open);
}

bool RuntimeWorld::advanceDoorTransitions(std::uint64_t ticks) noexcept {
    if (ticks == 0) {
        return false;
    }

    bool completed = false;
    for (auto& d : doors_) {
        if (d.physicalState != DoorPhysicalState::opening) {
            continue;
        }

        const auto remaining =
            d.openingDurationTicks > d.openingTick
                ? d.openingDurationTicks - d.openingTick
                : 0;
        if (remaining == 0 || ticks >= remaining) {
            d.openingTick = d.openingDurationTicks;
            d.physicalState = DoorPhysicalState::open;
            d.state = gameplay::DoorState::open;

            for (const auto& cell : d.cells) {
                map_.collision().setSolid(
                    cell.x,
                    cell.y,
                    cell.baseSolid);
            }

            syncRuntimeDoorObjectState(
                objects_,
                d.id,
                gameplay::DoorState::open);

            completed = true;
        } else {
            d.openingTick += ticks;
        }
    }
    return completed;
}

bool RuntimeWorld::setObjectActivation(simulation::PersistentInstanceId id, bool active) noexcept {
    const auto found = std::find_if(objects_.begin(), objects_.end(),
        [&](const PersistentObject& object) { return object.persistentId == id; });
    return found != objects_.end() && found->instance.setActivationActive(active);
}

std::optional<bool> RuntimeWorld::objectActivation(
    simulation::PersistentInstanceId id) const noexcept {
    const auto found = std::find_if(objects_.begin(), objects_.end(),
        [&](const PersistentObject& object) { return object.persistentId == id; });
    if (found == objects_.end() || !found->instance.hasActivation()) return std::nullopt;
    return found->instance.activationActive();
}

bool RuntimeWorld::toggleObjectActivation(simulation::PersistentInstanceId id) noexcept {
    const auto found = std::find_if(objects_.begin(), objects_.end(),
        [&](const PersistentObject& object) { return object.persistentId == id; });
    return found != objects_.end() && found->instance.toggleActivation();
}

void RuntimeWorld::updatePressureActivations(core::WorldPointI playerFeet,
                                             simulation::EventBuffer& events) noexcept {
    for (auto& object : objects_) {
        const auto& activation = object.instance.definition().activation;
        if (!activation || activation->mode != gameplay::ObjectActivationMode::playerPressure ||
            !activation->activationBounds) continue;
        const auto bounds = *activation->activationBounds;
        const auto left = static_cast<std::int64_t>(object.instance.position().x) + bounds.x;
        const auto top = static_cast<std::int64_t>(object.instance.position().y) + bounds.y;
        const auto right = left + bounds.width;
        const auto bottom = top + bounds.height;
        const bool active = static_cast<std::int64_t>(playerFeet.x) >= left &&
            static_cast<std::int64_t>(playerFeet.x) < right &&
            static_cast<std::int64_t>(playerFeet.y) >= top &&
            static_cast<std::int64_t>(playerFeet.y) < bottom;
        if (object.instance.setActivationActive(active)) {
            events.emit(simulation::ObjectActivationChanged{id_, object.persistentId, active});
        }
    }
}

RuntimeWorldBuildResult RuntimeWorldBuilder::build(
    const MapData& data, simulation::EntityHandlePool& handles,
    const simulation::SpawnId& spawnId) const {
    const auto validation = validateMapData(data, &catalogs_);
    if (!validation) { return {nullptr, validation.error}; }
    if (!data.npcs.empty() && !npcFactory_) {
        return {nullptr, "map contains NPC placements but no NPC factory is configured"};
    }
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
        world::RuntimeMap runtime(
            static_cast<int>(data.width),
            static_cast<int>(data.height),
            static_cast<int>(data.tileSize));

        std::vector<world::AabbI>
            tileCollisionBounds;

        const int runtimeTileSize =
            static_cast<int>(data.tileSize);

        for (const auto& sourceLayer : data.layers) {
            auto& layer = runtime.layer(runtime.addLayer(sourceLayer.name, sourceLayer.visible));
            for (std::uint32_t y = 0; y < data.height; ++y) {
                for (std::uint32_t x = 0; x < data.width; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y) * data.width + x;
                    if (!sourceLayer.cells[index]) { continue; }
                    const auto& source =
                        data.tileReferences[
                            *sourceLayer.cells[index]];

                    const auto runtimeTilesetId =
                        tilesets_.requireRuntimeId(
                            source.tilesetId);

                    layer.set(
                        static_cast<int>(x),
                        static_cast<int>(y),
                        world::TileRef{
                            {
                                runtimeTilesetId,
                                source.sourceIndex
                            },
                            source.flags
                        });

                    const auto* tileset =
                        tilesets_.definition(
                            runtimeTilesetId);

                    if (tileset == nullptr) {
                        throw std::logic_error(
                            "runtime tileset definition is unavailable");
                    }

                    const auto* collision =
                        tileset->collisionFor(
                            source.sourceIndex);

                    if (collision == nullptr) {
                        continue;
                    }

                    const int worldX =
                        static_cast<int>(x) *
                        runtimeTileSize;

                    const int worldY =
                        static_cast<int>(y) *
                        runtimeTileSize;

                    for (auto region : collision->regions) {
                        region =
                            transformTileCollisionRegion(
                                region,
                                runtimeTileSize,
                                source.flags);

                        region.x += worldX;
                        region.y += worldY;

                        tileCollisionBounds.push_back(
                            region);
                    }
                }
            }
        }
        // Static tile collision no longer comes from MapData::collision.
        //
        // A placed tile is physically solid only where its tileset
        // Pixel Collision definition contains solid pixels.
        //
        // RuntimeMap::collision remains available for world boundaries
        // and dynamic/legacy cell blockers such as old door content.
        result = std::make_unique<RuntimeWorld>(
            data.id,
            std::move(runtime),
            *spawn);

        result->animationCollisions_ = catalogs_.animationCollisions;
        result->tileCollisionBounds_ =
            std::move(tileCollisionBounds);

        result->enemies_.reserve(data.enemies.size());
        for (const auto& placement : data.enemies) {
            result->enemies_.push_back({placement.id, enemyFactory_.create(handles,
                placement.definitionId, placement.position, placement.facing)});
        }
        result->npcs_.reserve(data.npcs.size());
        for (const auto& placement : data.npcs) {
            result->npcs_.push_back({placement.id, npcFactory_->create(handles,
                placement.definitionId, placement.position, placement.facing)});
        }
        result->objects_.reserve(data.objects.size());
        for (const auto& placement : data.objects) {
            result->objects_.push_back({
                placement.id,
                placement.persistence,
                placement.transition,
                objectFactory_.create(
                    handles,
                    placement.definitionId,
                    placement.position,
                    placement.initialContents)
            });
        }
        for (const auto& placement : data.objects) {
            const auto* definition =
                catalogs_.objects
                    ? catalogs_.objects->find(
                        placement.definitionId)
                    : nullptr;

            if (!definition || !definition->door) {
                continue;
            }

            const auto initialState =
                placement.door
                    ? placement.door->initialState
                    : definition->door->initialState;

            const auto requiredItemId =
                placement.door
                    ? placement.door->requiredItemId
                    : std::optional<simulation::DefinitionId>{};

            const bool consumeItem =
                placement.door &&
                placement.door->consumeItem;

            const auto runtimeObject =
                std::find_if(
                    result->objects_.begin(),
                    result->objects_.end(),
                    [&](const PersistentObject& candidate) {
                        return candidate.persistentId ==
                            placement.id;
                    });

            if (runtimeObject == result->objects_.end()) {
                throw std::logic_error(
                    "runtime door object is unavailable");
            }

            static_cast<void>(
                runtimeObject->instance.setDoorState(
                    initialState));

            // New authored collision masks supersede the legacy
            // tile-cell door blocker. Old content without a
            // collision component keeps the previous behaviour.
            if (definition->collision ||
                !definition->door->hasBlockingBounds) {
                result->doors_.push_back({
                    placement.id,
                    initialState,
                    initialState,
                    requiredItemId,
                    consumeItem,
                    {}
                });
                configureDoorTimeline(result->doors_.back(), *definition, catalogs_);
                continue;
            }
            const auto bounds = definition->door->blockingBounds;
            const int tileSize = static_cast<int>(data.tileSize);
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
                throw std::invalid_argument("door blocking bounds are outside the map");
            }
            RuntimeDoor door{
                placement.id,
                initialState,
                initialState,
                requiredItemId,
                consumeItem,
                {}
            };
            for (auto y = firstY; y <= lastY; ++y) {
                for (auto x = firstX; x <= lastX; ++x) {
                    if (std::any_of(result->doors_.begin(), result->doors_.end(),
                        [&](const RuntimeDoor& other) {
                            return std::any_of(other.cells.begin(), other.cells.end(),
                                [&](const RuntimeDoorCell& cell) { return cell.x == x && cell.y == y; });
                        })) {
                        throw std::invalid_argument("overlapping dynamic door collision cells");
                    }
                    door.cells.push_back({static_cast<int>(x), static_cast<int>(y),
                                          result->map_.collision().isSolid(static_cast<int>(x),
                                                                            static_cast<int>(y))});
                }
            }
            result->doors_.push_back(std::move(door));
            configureDoorTimeline(result->doors_.back(), *definition, catalogs_);
            const auto& added = result->doors_.back();
            for (const auto& cell : added.cells) {
                result->map_.collision().setSolid(cell.x, cell.y,
                    added.state != gameplay::DoorState::open);
            }
        }
        result->pickupDefinitions_.reserve(data.pickups.size());
        for (const auto& placement : data.pickups) {
            result->pickupDefinitions_.push_back({placement.definitionId, placement.visualId,
                                                  placement.collectionBounds, placement.payload});
        }
        result->pickups_.reserve(data.pickups.size());
        for (std::size_t index = 0; index < data.pickups.size(); ++index) {
            result->pickups_.push_back({data.pickups[index].id, false, gameplay::WorldPickup{
                handles.create(), result->pickupDefinitions_[index], data.pickups[index].position}});
        }
        return {std::move(result), {}};
    } catch (const std::exception& exception) {
        if (result) {
            for (auto& enemy : result->enemies_) {
                static_cast<void>(handles.destroy(enemy.instance.handle()));
            }
            for (auto& npc : result->npcs_) {
                static_cast<void>(handles.destroy(npc.instance.handle()));
            }
            for (auto& object : result->objects_) {
                static_cast<void>(handles.destroy(object.instance.handle()));
            }
            for (auto& pickup : result->pickups_) {
                static_cast<void>(handles.destroy(pickup.instance.handle()));
            }
        }
        return {nullptr, exception.what()};
    }
}

RuntimeWorldBuildResult RuntimeWorldBuilder::build(
    const MapData& data, const simulation::SpawnId& spawnId) const {
    if (legacyHandles_ == nullptr) {
        return {nullptr, "RuntimeWorldBuilder requires a handle pool"};
    }
    return build(data, *legacyHandles_, spawnId);
}

} // namespace underworld::game::maps
