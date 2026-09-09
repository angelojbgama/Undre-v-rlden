#include "game/maps/dmap.h"

#include "engine/serialization/byte_io.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace underworld::game::maps {
namespace {
using serialization::ByteReader;
using serialization::ByteWriter;
constexpr std::uint16_t headerSize = 20;
constexpr std::uint32_t emptyCell = std::numeric_limits<std::uint32_t>::max();

struct StringTable final {
    std::vector<std::string> values;
    std::unordered_map<std::string, std::uint32_t> indices;
    [[nodiscard]] std::uint32_t index(std::string_view value) const {
        const auto found = indices.find(std::string(value));
        if (found == indices.end()) { throw std::logic_error("string was not interned"); }
        return found->second;
    }
};

void addString(std::vector<std::string>& values, std::string_view value) {
    if (!value.empty()) { values.emplace_back(value); }
}

StringTable collectStrings(const MapData& data) {
    StringTable table;
    addString(table.values, data.id.value());
    for (const auto& tile : data.tileReferences) { addString(table.values, tile.tilesetId.value()); }
    for (const auto& layer : data.layers) { addString(table.values, layer.name); }
    for (const auto& spawn : data.playerSpawns) { addString(table.values, spawn.id.value()); }
    for (const auto& enemy : data.enemies) { addString(table.values, enemy.definitionId.value()); }
    for (const auto& npc : data.npcs) { addString(table.values, npc.definitionId.value()); }
    for (const auto& object : data.objects) {
        addString(table.values, object.definitionId.value());
        for (const auto& stack : object.initialContents) { addString(table.values, stack.itemId.value()); }
    }
    for (const auto& pickup : data.pickups) {
        addString(table.values, pickup.definitionId.value()); addString(table.values, pickup.visualId.value());
        if (const auto* item = std::get_if<gameplay::ItemPickup>(&pickup.payload)) {
            addString(table.values, item->itemId.value());
        }
    }
    for (const auto& link : data.links) {
        addString(table.values, link.id); addString(table.values, link.targetMapId.value());
        addString(table.values, link.targetSpawnId.value());
    }
    for (const auto& region : data.regions) {
        addString(table.values, region.id.value());
        if (region.environmentEffectId) addString(table.values, region.environmentEffectId->value());
    }
    for (const auto& rule : data.worldRules) {
        addString(table.values, rule.id.value());
        if (!rule.trigger.definitionTarget.empty()) addString(table.values, rule.trigger.definitionTarget.value());
        for (const auto& condition : rule.conditions) {
            if (!condition.definitionTarget.empty()) addString(table.values, condition.definitionTarget.value());
        }
        for (const auto& action : rule.actions) {
            if (!action.definitionTarget.empty()) addString(table.values, action.definitionTarget.value());
        }
    }
    for (const auto& encounter : data.encounters) {
        addString(table.values, encounter.id.value());
        if (encounter.rewardGrantId) addString(table.values, encounter.rewardGrantId->value());
    }
    for (const auto& scene : data.scenes) {
        addString(table.values, scene.id.value());
        for (const auto& actor : scene.actors) addString(table.values, actor.slotId);
        for (const auto& track : scene.tracks) {
            addString(table.values, track.actorSlot);
            for (const auto& clip : track.clips) {
                addString(table.values, clip.actorSlot);
                if (!clip.dialogueId.empty()) addString(table.values, clip.dialogueId.value());
                if (!clip.effectId.empty()) addString(table.values, clip.effectId.value());
                if (!clip.worldAction.definitionTarget.empty()) {
                    addString(table.values, clip.worldAction.definitionTarget.value());
                }
            }
        }
        for (const auto& marker : scene.markers) addString(table.values, marker.name);
    }
    std::sort(table.values.begin(), table.values.end());
    table.values.erase(std::unique(table.values.begin(), table.values.end()), table.values.end());
    for (std::size_t i = 0; i < table.values.size(); ++i) {
        table.indices.emplace(table.values[i], static_cast<std::uint32_t>(i));
    }
    return table;
}

void appendChunk(ByteWriter& file, const std::array<char, 4>& tag, ByteWriter payload) {
    for (const char value : tag) { file.writeU8(static_cast<std::uint8_t>(value)); }
    file.writeU64(payload.bytes().size());
    file.writeBytes(payload.bytes());
}

std::uint8_t facingValue(gameplay::FacingDirection facing) { return static_cast<std::uint8_t>(facing); }
bool readFacing(ByteReader& reader, gameplay::FacingDirection& facing) {
    std::uint8_t value{}; if (!reader.readU8(value) || value > 3) { return false; }
    facing = static_cast<gameplay::FacingDirection>(value); return true;
}
std::uint8_t persistenceValue(ObjectPersistencePolicy policy) {
    return static_cast<std::uint8_t>(policy);
}
bool readPersistence(ByteReader& reader, ObjectPersistencePolicy& policy) {
    std::uint8_t value{};
    if (!reader.readU8(value) || value > static_cast<std::uint8_t>(
            ObjectPersistencePolicy::resetOnMapEnter)) {
        return false;
    }
    policy = static_cast<ObjectPersistencePolicy>(value);
    return true;
}
void writePoint(ByteWriter& out, core::WorldPointI point) { out.writeI32(point.x); out.writeI32(point.y); }
bool readPoint(ByteReader& in, core::WorldPointI& point) { return in.readI32(point.x) && in.readI32(point.y); }
void writeArea(ByteWriter& out, world::AabbI area) {
    out.writeI32(area.x); out.writeI32(area.y); out.writeI32(area.width); out.writeI32(area.height);
}
bool readArea(ByteReader& in, world::AabbI& area) {
    return in.readI32(area.x) && in.readI32(area.y) && in.readI32(area.width) && in.readI32(area.height);
}

bool readCount(ByteReader& reader, std::uint32_t limit, std::uint32_t& count) {
    return reader.readU32(count) && count <= limit;
}

bool readIndex(ByteReader& reader, const std::vector<std::string>& strings, std::string& value) {
    std::uint32_t index{};
    if (!reader.readU32(index) || index >= strings.size()) { return false; }
    value = strings[index]; return true;
}

template<class Id>
bool readId(ByteReader& reader, const std::vector<std::string>& strings, Id& id) {
    std::string value; if (!readIndex(reader, strings, value) || value.empty()) { return false; }
    id = Id{std::move(value)}; return true;
}

DmapLoadResult fail(std::string error) { return {false, {}, std::move(error)}; }

} // namespace

std::vector<std::uint8_t> serializeDmap(const MapData& data) {
    const auto validation = validateMapData(data);
    if (!validation) { throw std::invalid_argument("cannot serialize invalid map: " + validation.error); }
    const StringTable strings = collectStrings(data);
    if (strings.values.size() > MapLimits::maximumStrings) { throw std::length_error("too many strings"); }
    ByteWriter chunks;
    ByteWriter meta; meta.writeU32(strings.index(data.id.value())); meta.writeU32(data.width);
    meta.writeU32(data.height); meta.writeU16(data.tileSize); appendChunk(chunks, {'M','E','T','A'}, std::move(meta));
    ByteWriter strs; strs.writeU32(static_cast<std::uint32_t>(strings.values.size()));
    for (const auto& value : strings.values) { strs.writeString(value); }
    appendChunk(chunks, {'S','T','R','S'}, std::move(strs));
    ByteWriter tref; tref.writeU32(static_cast<std::uint32_t>(data.tileReferences.size()));
    for (const auto& tile : data.tileReferences) {
        tref.writeU32(strings.index(tile.tilesetId.value())); tref.writeU32(tile.sourceIndex);
        tref.writeU8(static_cast<std::uint8_t>(tile.flags));
    }
    appendChunk(chunks, {'T','R','E','F'}, std::move(tref));
    ByteWriter layr; layr.writeU32(static_cast<std::uint32_t>(data.layers.size()));
    for (const auto& layer : data.layers) {
        layr.writeU32(strings.index(layer.name)); layr.writeU8(layer.visible ? 1 : 0);
        layr.writeU32(static_cast<std::uint32_t>(layer.cells.size()));
        for (const auto& cell : layer.cells) { layr.writeU32(cell ? *cell : emptyCell); }
    }
    appendChunk(chunks, {'L','A','Y','R'}, std::move(layr));
    ByteWriter coll; coll.writeU32(static_cast<std::uint32_t>(data.collision.size()));
    coll.writeBytes(data.collision); appendChunk(chunks, {'C','O','L','L'}, std::move(coll));
    ByteWriter spwn; spwn.writeU32(static_cast<std::uint32_t>(data.playerSpawns.size()));
    for (const auto& spawn : data.playerSpawns) {
        spwn.writeU32(strings.index(spawn.id.value())); writePoint(spwn, spawn.position);
        spwn.writeU8(facingValue(spawn.facing));
    }
    appendChunk(chunks, {'S','P','W','N'}, std::move(spwn));
    ByteWriter ents; ents.writeU32(static_cast<std::uint32_t>(data.enemies.size()));
    for (const auto& enemy : data.enemies) {
        ents.writeU64(enemy.id.value); ents.writeU32(strings.index(enemy.definitionId.value()));
        writePoint(ents, enemy.position); ents.writeU8(facingValue(enemy.facing));
    }
    ents.writeU32(static_cast<std::uint32_t>(data.objects.size()));
    for (const auto& object : data.objects) {
        ents.writeU64(object.id.value); ents.writeU32(strings.index(object.definitionId.value()));
        writePoint(ents, object.position);
        ents.writeU8(persistenceValue(object.persistence));
        ents.writeU32(static_cast<std::uint32_t>(object.initialContents.size()));
        for (const auto& stack : object.initialContents) {
            ents.writeU32(strings.index(stack.itemId.value())); ents.writeU32(stack.quantity);
        }
    }
    ents.writeU32(static_cast<std::uint32_t>(data.pickups.size()));
    for (const auto& pickup : data.pickups) {
        ents.writeU64(pickup.id.value); ents.writeU32(strings.index(pickup.definitionId.value()));
        ents.writeU32(strings.index(pickup.visualId.value())); writePoint(ents, pickup.position);
        writeArea(ents, pickup.collectionBounds);
        if (const auto* health = std::get_if<gameplay::HealthPickup>(&pickup.payload)) {
            ents.writeU8(0); ents.writeI32(health->amount);
        } else if (const auto* currency = std::get_if<gameplay::CurrencyPickup>(&pickup.payload)) {
            ents.writeU8(1); ents.writeU64(currency->amount);
        } else {
            const auto& item = std::get<gameplay::ItemPickup>(pickup.payload);
            ents.writeU8(2); ents.writeU32(strings.index(item.itemId.value())); ents.writeU32(item.quantity);
        }
    }
    appendChunk(chunks, {'E','N','T','S'}, std::move(ents));
    ByteWriter npcs; npcs.writeU32(static_cast<std::uint32_t>(data.npcs.size()));
    for (const auto& npc : data.npcs) {
        npcs.writeU64(npc.id.value); npcs.writeU32(strings.index(npc.definitionId.value()));
        writePoint(npcs, npc.position); npcs.writeU8(facingValue(npc.facing));
    }
    appendChunk(chunks, {'N','P','C','S'}, std::move(npcs));
    ByteWriter link; link.writeU32(static_cast<std::uint32_t>(data.links.size()));
    for (const auto& value : data.links) {
        link.writeU32(strings.index(value.id)); writeArea(link, value.trigger);
        link.writeU32(strings.index(value.targetMapId.value()));
        link.writeU32(strings.index(value.targetSpawnId.value()));
    }
    appendChunk(chunks, {'L','I','N','K'}, std::move(link));
    ByteWriter regions; regions.writeU32(static_cast<std::uint32_t>(data.regions.size()));
    for (const auto& region : data.regions) {
        regions.writeU32(strings.index(region.id.value()));
        writeArea(regions, region.bounds);
        regions.writeU8(region.environmentEffectId ? 1 : 0);
        if (region.environmentEffectId) regions.writeU32(strings.index(region.environmentEffectId->value()));
    }
    appendChunk(chunks, {'R','E','G','N'}, std::move(regions));
    ByteWriter worldRules; worldRules.writeU32(static_cast<std::uint32_t>(data.worldRules.size()));
    const auto writeTarget = [&](ByteWriter& output, const auto& value) {
        const auto& definition = value.definitionTarget;
        if (value.instanceTarget) {
            output.writeU8(2);
            output.writeU64(value.instanceTarget.value);
        } else if (!definition.empty()) {
            output.writeU8(1);
            output.writeU32(strings.index(definition.value()));
        } else {
            output.writeU8(0);
        }
    };
    for (const auto& rule : data.worldRules) {
        worldRules.writeU32(strings.index(rule.id.value()));
        worldRules.writeU8(static_cast<std::uint8_t>(rule.trigger.kind));
        writeTarget(worldRules, rule.trigger);
        worldRules.writeU8(rule.once ? 1 : 0);
        worldRules.writeU32(static_cast<std::uint32_t>(rule.conditions.size()));
        for (const auto& condition : rule.conditions) {
            worldRules.writeU8(static_cast<std::uint8_t>(condition.kind));
            writeTarget(worldRules, condition);
            worldRules.writeU8(static_cast<std::uint8_t>(condition.doorState));
        }
        worldRules.writeU32(static_cast<std::uint32_t>(rule.actions.size()));
        for (const auto& action : rule.actions) {
            worldRules.writeU8(static_cast<std::uint8_t>(action.kind));
            writeTarget(worldRules, action);
            worldRules.writeU8(static_cast<std::uint8_t>(action.doorState));
        }
    }
    appendChunk(chunks, {'W','R','L','D'}, std::move(worldRules));
    ByteWriter encounters; encounters.writeU32(static_cast<std::uint32_t>(data.encounters.size()));
    for (const auto& encounter : data.encounters) {
        encounters.writeU32(strings.index(encounter.id.value()));
        encounters.writeU32(static_cast<std::uint32_t>(encounter.participants.size()));
        for (const auto participant : encounter.participants) encounters.writeU64(participant.value);
        encounters.writeU8(encounter.rewardGrantId ? 1 : 0);
        if (encounter.rewardGrantId) encounters.writeU32(strings.index(encounter.rewardGrantId->value()));
    }
    appendChunk(chunks, {'E','N','C','T'}, std::move(encounters));
    ByteWriter scenes; scenes.writeU32(static_cast<std::uint32_t>(data.scenes.size()));
    const auto writeOptionalString = [&](ByteWriter& output, std::string_view value) {
        output.writeU8(value.empty() ? 0 : 1);
        if (!value.empty()) output.writeU32(strings.index(value));
    };
    const auto writeSceneAction = [&](ByteWriter& output, const WorldAction& action) {
        output.writeU8(static_cast<std::uint8_t>(action.kind));
        if (action.instanceTarget) {
            output.writeU8(2); output.writeU64(action.instanceTarget.value);
        } else if (!action.definitionTarget.empty()) {
            output.writeU8(1); output.writeU32(strings.index(action.definitionTarget.value()));
        } else {
            output.writeU8(0);
        }
        output.writeU8(static_cast<std::uint8_t>(action.doorState));
    };
    for (const auto& scene : data.scenes) {
        scenes.writeU32(strings.index(scene.id.value()));
        scenes.writeU32(scene.durationTicks);
        scenes.writeU32(static_cast<std::uint32_t>(scene.actors.size()));
        for (const auto& actor : scene.actors) {
            scenes.writeU32(strings.index(actor.slotId));
            scenes.writeU8(static_cast<std::uint8_t>(actor.kind));
            scenes.writeU8(actor.instanceId ? 1 : 0);
            if (actor.instanceId) scenes.writeU64(actor.instanceId.value);
        }
        scenes.writeU32(static_cast<std::uint32_t>(scene.tracks.size()));
        for (const auto& track : scene.tracks) {
            scenes.writeU8(static_cast<std::uint8_t>(track.kind));
            writeOptionalString(scenes, track.actorSlot);
            scenes.writeU32(static_cast<std::uint32_t>(track.clips.size()));
            for (const auto& clip : track.clips) {
                scenes.writeU8(static_cast<std::uint8_t>(clip.kind));
                writeOptionalString(scenes, clip.actorSlot);
                scenes.writeU32(clip.startTick); scenes.writeU32(clip.durationTicks);
                writePoint(scenes, clip.targetPosition);
                scenes.writeU8(static_cast<std::uint8_t>(clip.facing));
                scenes.writeU8(static_cast<std::uint8_t>(clip.emote));
                scenes.writeI32(clip.heightPixels);
                writeOptionalString(scenes, clip.dialogueId.value());
                scenes.writeU8(clip.waitForCompletion ? 1 : 0);
                writeOptionalString(scenes, clip.effectId.value());
                writeSceneAction(scenes, clip.worldAction);
            }
        }
        scenes.writeU32(static_cast<std::uint32_t>(scene.markers.size()));
        for (const auto& marker : scene.markers) {
            scenes.writeU32(strings.index(marker.name)); scenes.writeU32(marker.tick);
        }
    }
    appendChunk(chunks, {'S','C','N','E'}, std::move(scenes));

    ByteWriter result;
    for (char value : std::array<char,4>{'D','M','A','P'}) { result.writeU8(static_cast<std::uint8_t>(value)); }
    result.writeU16(dmapMajorVersion); result.writeU16(dmapMinorVersion);
    result.writeU16(0); result.writeU16(headerSize);
    result.writeU64(headerSize + chunks.bytes().size()); result.writeBytes(chunks.bytes());
    return std::move(result).take();
}

DmapLoadResult deserializeDmap(std::span<const std::uint8_t> bytes,
                               const MapValidationCatalogs* catalogs) {
    if (bytes.size() > MapLimits::maximumFileBytes) { return fail("DMAP exceeds file size limit"); }
    ByteReader header(bytes); std::span<const std::uint8_t> magic;
    std::uint16_t major{}, minor{}, flags{}, size{}; std::uint64_t declared{};
    if (!header.readBytes(4, magic) || magic[0] != 'D' || magic[1] != 'M' || magic[2] != 'A' || magic[3] != 'P') return fail("wrong DMAP magic");
    if (!header.readU16(major) || !header.readU16(minor) || !header.readU16(flags) ||
        !header.readU16(size) || !header.readU64(declared)) return fail("truncated DMAP header");
    if (major != dmapMajorVersion) return fail("unsupported DMAP major version");
    if (minor > dmapMinorVersion) return fail("unsupported DMAP minor version");
    if (flags != 0 || size < headerSize || declared != bytes.size() || !header.skip(size - headerSize)) return fail("invalid DMAP header");
    std::unordered_map<std::string, std::span<const std::uint8_t>> chunks;
    while (header.remaining() != 0) {
        std::span<const std::uint8_t> tagBytes, payload; std::uint64_t payloadSize{};
        if (!header.readBytes(4, tagBytes) || !header.readU64(payloadSize) ||
            payloadSize > MapLimits::maximumChunkBytes || payloadSize > header.remaining() ||
            !header.readBytes(static_cast<std::size_t>(payloadSize), payload)) return fail("truncated or oversized DMAP chunk");
        const std::string tag(reinterpret_cast<const char*>(tagBytes.data()), 4);
        const bool known = tag == "META" || tag == "STRS" || tag == "TREF" || tag == "LAYR" ||
                           tag == "COLL" || tag == "SPWN" || tag == "ENTS" || tag == "NPCS" ||
                           tag == "LINK" || tag == "REGN" || tag == "WRLD" || tag == "ENCT" ||
                           tag == "SCNE";
        if (known && !chunks.emplace(tag, payload).second) return fail("duplicate singleton DMAP chunk");
    }
    if (minor == 0 && chunks.contains("NPCS")) {
        return fail("NPCS chunk requires DMAP minor version 1");
    }
    for (const char* required : {"META","STRS","TREF","LAYR","COLL","SPWN","ENTS","LINK"}) {
        if (!chunks.contains(required)) return fail(std::string("missing required DMAP chunk ") + required);
    }
    std::vector<std::string> strings;
    {
        ByteReader in(chunks["STRS"]); std::uint32_t count{};
        if (!readCount(in, MapLimits::maximumStrings, count)) return fail("invalid STRS count");
        strings.reserve(count);
        for (std::uint32_t i=0;i<count;++i) { std::string value; if (!in.readString(value, MapLimits::maximumStringBytes) || value.empty()) return fail("invalid STRS entry"); strings.push_back(std::move(value)); }
        if (in.remaining()!=0) return fail("trailing STRS data");
    }
    MapData data;
    {
        ByteReader in(chunks["META"]); if (!readId(in, strings, data.id) || !in.readU32(data.width) ||
            !in.readU32(data.height) || !in.readU16(data.tileSize) || in.remaining()!=0) return fail("invalid META chunk");
    }
    {
        ByteReader in(chunks["TREF"]); std::uint32_t count{}; if (!readCount(in,MapLimits::maximumTileReferences,count)) return fail("invalid TREF count");
        data.tileReferences.reserve(count);
        for(std::uint32_t i=0;i<count;++i){ simulation::DefinitionId id; std::uint32_t source{}; std::uint8_t flagsValue{};
            if(!readId(in,strings,id)||!in.readU32(source)||!in.readU8(flagsValue)) return fail("truncated TREF record");
            data.tileReferences.push_back({std::move(id),source,static_cast<world::TileFlags>(flagsValue)}); }
        if(in.remaining()!=0)return fail("trailing TREF data");
    }
    const std::uint64_t cellCount64=static_cast<std::uint64_t>(data.width)*data.height;
    if(cellCount64>std::numeric_limits<std::uint32_t>::max())return fail("map cell count exceeds v1 encoding");
    const std::uint32_t cellCount=static_cast<std::uint32_t>(cellCount64);
    {
        ByteReader in(chunks["LAYR"]); std::uint32_t count{}; if(!readCount(in,MapLimits::maximumLayers,count))return fail("invalid LAYR count");
        data.layers.reserve(count);
        for(std::uint32_t i=0;i<count;++i){std::string name;std::uint8_t visible{};std::uint32_t cells{};
            if(!readIndex(in,strings,name)||!in.readU8(visible)||visible>1||!in.readU32(cells)||cells!=cellCount)return fail("invalid LAYR record");
            MapTileLayer layer{std::move(name),visible!=0,{}};layer.cells.reserve(cells);
            for(std::uint32_t c=0;c<cells;++c){std::uint32_t value{};if(!in.readU32(value))return fail("truncated LAYR cells");layer.cells.push_back(value==emptyCell?std::nullopt:std::optional<std::uint32_t>{value});}
            data.layers.push_back(std::move(layer));}
        if(in.remaining()!=0)return fail("trailing LAYR data");
    }
    {
        ByteReader in(chunks["COLL"]);std::uint32_t count{};std::span<const std::uint8_t> cells;
        if(!in.readU32(count)||count!=cellCount||!in.readBytes(count,cells)||in.remaining()!=0)return fail("invalid COLL chunk");
        data.collision.assign(cells.begin(),cells.end());
    }
    {
        ByteReader in(chunks["SPWN"]);std::uint32_t count{};if(!readCount(in,MapLimits::maximumPlacements,count))return fail("invalid SPWN count");
        data.playerSpawns.reserve(count);
        for(std::uint32_t i=0;i<count;++i){simulation::SpawnId id;core::WorldPointI point;gameplay::FacingDirection facing;
            if(!readId(in,strings,id)||!readPoint(in,point)||!readFacing(in,facing)) return fail("invalid SPWN record");
            data.playerSpawns.push_back({std::move(id),point,facing});}
        if(in.remaining()!=0)return fail("trailing SPWN data");
    }
    {
        ByteReader in(chunks["ENTS"]);std::uint32_t count{};
        if(!readCount(in,MapLimits::maximumPlacements,count)) return fail("invalid enemy count");
        data.enemies.reserve(count);
        for(std::uint32_t i=0;i<count;++i){std::uint64_t id{};simulation::DefinitionId def;core::WorldPointI point;gameplay::FacingDirection facing;
            if(!in.readU64(id)||!readId(in,strings,def)||!readPoint(in,point)||!readFacing(in,facing)) return fail("invalid enemy record");
            data.enemies.push_back({{id},std::move(def),point,facing});}
        if(!readCount(in,MapLimits::maximumPlacements,count)) return fail("invalid object count");
        data.objects.reserve(count);
        for(std::uint32_t i=0;i<count;++i){std::uint64_t id{};simulation::DefinitionId def;core::WorldPointI point;ObjectPersistencePolicy persistence{};std::uint32_t stackCount{};
            if(!in.readU64(id)||!readId(in,strings,def)||!readPoint(in,point)||
               (minor >= 5 ? !readPersistence(in,persistence) : false)||
               !readCount(in,MapLimits::maximumPlacements,stackCount))return fail("invalid object record");
            ObjectPlacement object{{id},std::move(def),point,{},persistence};object.initialContents.reserve(stackCount);
            for(std::uint32_t s=0;s<stackCount;++s){simulation::DefinitionId item;std::uint32_t quantity{};if(!readId(in,strings,item)||!in.readU32(quantity)) return fail("invalid object contents");object.initialContents.push_back({std::move(item),quantity});}
            data.objects.push_back(std::move(object));}
        if(!readCount(in,MapLimits::maximumPlacements,count)) return fail("invalid pickup count");
        data.pickups.reserve(count);
        for(std::uint32_t i=0;i<count;++i){std::uint64_t id{};simulation::DefinitionId def,visual;core::WorldPointI point;world::AabbI area;std::uint8_t kind{};
            if(!in.readU64(id)||!readId(in,strings,def)||!readId(in,strings,visual)||!readPoint(in,point)||!readArea(in,area)||!in.readU8(kind))return fail("invalid pickup record");
            gameplay::PickupPayload payload=gameplay::HealthPickup{1};
            if(kind==0){std::int32_t amount{};if(!in.readI32(amount))return fail("invalid health pickup");payload=gameplay::HealthPickup{amount};}
            else if(kind==1){std::uint64_t amount{};if(!in.readU64(amount))return fail("invalid currency pickup");payload=gameplay::CurrencyPickup{amount};}
            else if(kind==2){simulation::DefinitionId item;std::uint32_t quantity{};if(!readId(in,strings,item)||!in.readU32(quantity))return fail("invalid item pickup");payload=gameplay::ItemPickup{std::move(item),quantity};}
            else return fail("unknown pickup payload kind");
            data.pickups.push_back({{id},std::move(def),std::move(visual),point,area,std::move(payload)});}
        if(in.remaining()!=0)return fail("trailing ENTS data");
    }
    if (const auto found = chunks.find("NPCS"); found != chunks.end()) {
        ByteReader in(found->second); std::uint32_t count{};
        if (!readCount(in, MapLimits::maximumPlacements, count)) return fail("invalid NPC count");
        data.npcs.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint64_t id{}; simulation::DefinitionId definition; core::WorldPointI point;
            gameplay::FacingDirection facing{};
            if (!in.readU64(id) || !readId(in, strings, definition) || !readPoint(in, point) ||
                !readFacing(in, facing)) return fail("invalid NPC record");
            data.npcs.push_back({{id}, std::move(definition), point, facing});
        }
        if (in.remaining() != 0) return fail("trailing NPCS data");
    }
    {
        ByteReader in(chunks["LINK"]);std::uint32_t count{};if(!readCount(in,MapLimits::maximumPlacements,count)) return fail("invalid LINK count");data.links.reserve(count);
        for(std::uint32_t i=0;i<count;++i){std::string id;world::AabbI area;simulation::MapId target;simulation::SpawnId spawn;
            if(!readIndex(in,strings,id)||!readArea(in,area)||!readId(in,strings,target)||!readId(in,strings,spawn)) return fail("invalid LINK record");
            data.links.push_back({std::move(id),area,std::move(target),std::move(spawn)});}
        if(in.remaining()!=0)return fail("trailing LINK data");
    }
    if (const auto found = chunks.find("REGN"); found != chunks.end()) {
        if (minor < 2) return fail("REGN chunk requires DMAP minor version 2");
        ByteReader in(found->second); std::uint32_t count{};
        if (!readCount(in, MapLimits::maximumPlacements, count)) return fail("invalid REGN count");
        data.regions.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            simulation::DefinitionId id; world::AabbI bounds{};
            if (!readId(in, strings, id) || !readArea(in, bounds)) return fail("invalid REGN record");
            std::optional<simulation::DefinitionId> environment;
            if (minor >= 3) {
                std::uint8_t hasEffect{};
                if (!in.readU8(hasEffect) || hasEffect > 1) return fail("invalid REGN environment effect");
                if (hasEffect) {
                    simulation::DefinitionId effect;
                    if (!readId(in, strings, effect)) return fail("invalid REGN environment effect id");
                    environment = std::move(effect);
                }
            }
            data.regions.push_back({std::move(id), bounds, std::move(environment)});
        }
        if (in.remaining() != 0) return fail("trailing REGN data");
    }
    if (const auto found = chunks.find("WRLD"); found != chunks.end()) {
        if (minor < 2) return fail("WRLD chunk requires DMAP minor version 2");
        ByteReader in(found->second); std::uint32_t count{};
        if (!readCount(in, MapLimits::maximumPlacements, count)) return fail("invalid WRLD count");
        data.worldRules.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            WorldRuleDefinition rule; std::uint8_t kind{}, once{}; std::uint32_t conditionCount{}, actionCount{};
            std::uint8_t instanceTarget{}; std::uint64_t instanceValue{};
            if (!readId(in, strings, rule.id) || !in.readU8(kind) || kind > 7 ||
                (kind >= 6 && minor < 4) ||
                !in.readU8(instanceTarget) || instanceTarget > 2) return fail("invalid WRLD rule");
            bool targetValid = true;
            if (instanceTarget == 2) targetValid = in.readU64(instanceValue) && instanceValue != 0;
            else if (instanceTarget == 1) targetValid = readId(in, strings, rule.trigger.definitionTarget);
            if (!targetValid || !in.readU8(once) || once > 1 ||
                !readCount(in, MapLimits::maximumPlacements, conditionCount)) return fail("invalid WRLD rule");
            if (instanceTarget == 2) rule.trigger.instanceTarget = {instanceValue};
            rule.trigger.kind = static_cast<WorldTriggerKind>(kind); rule.once = once != 0;
            for (std::uint32_t j = 0; j < conditionCount; ++j) {
                WorldCondition condition; std::uint8_t state{};
                if (!in.readU8(kind) || kind > 6 || (kind >= 5 && minor < 4) ||
                    !in.readU8(instanceTarget) || instanceTarget > 2) {
                    return fail("invalid WRLD condition");
                }
                targetValid = true;
                if (instanceTarget == 2) targetValid = in.readU64(instanceValue) && instanceValue != 0;
                else if (instanceTarget == 1) targetValid = readId(in, strings, condition.definitionTarget);
                if (!targetValid || !in.readU8(state) || state > 2) return fail("invalid WRLD condition");
                if (instanceTarget == 2) condition.instanceTarget = {instanceValue};
                condition.kind = static_cast<WorldConditionKind>(kind);
                condition.doorState = static_cast<DoorState>(state); rule.conditions.push_back(std::move(condition));
            }
            if (!readCount(in, MapLimits::maximumPlacements, actionCount)) return fail("invalid WRLD action count");
            for (std::uint32_t j = 0; j < actionCount; ++j) {
                WorldAction action; std::uint8_t state{};
                if (!in.readU8(kind) || kind > 5 || (kind == 4 && minor < 3) ||
                    (kind == 5 && minor < 5) ||
                    !in.readU8(instanceTarget) || instanceTarget > 2) {
                    return fail("invalid WRLD action");
                }
                targetValid = true;
                if (instanceTarget == 2) targetValid = in.readU64(instanceValue) && instanceValue != 0;
                else if (instanceTarget == 1) targetValid = readId(in, strings, action.definitionTarget);
                if (!targetValid || !in.readU8(state) || state > 2) return fail("invalid WRLD action");
                if (instanceTarget == 2) action.instanceTarget = {instanceValue};
                action.kind = static_cast<WorldActionKind>(kind);
                action.doorState = static_cast<DoorState>(state); rule.actions.push_back(std::move(action));
            }
            data.worldRules.push_back(std::move(rule));
        }
        if (in.remaining() != 0) return fail("trailing WRLD data");
    }
    if (const auto found = chunks.find("ENCT"); found != chunks.end()) {
        if (minor < 2) return fail("ENCT chunk requires DMAP minor version 2");
        ByteReader in(found->second); std::uint32_t count{};
        if (!readCount(in, MapLimits::maximumPlacements, count)) return fail("invalid ENCT count");
        data.encounters.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            EncounterDefinition encounter; std::uint32_t participants{}; std::uint8_t hasReward{};
            if (!readId(in, strings, encounter.id) || !readCount(in, MapLimits::maximumPlacements, participants)) return fail("invalid ENCT record");
            encounter.participants.reserve(participants);
            for (std::uint32_t j = 0; j < participants; ++j) { std::uint64_t id{}; if (!in.readU64(id) || id == 0) return fail("invalid ENCT participant"); encounter.participants.push_back({id}); }
            if (!in.readU8(hasReward) || hasReward > 1) return fail("invalid ENCT reward flag");
            if (hasReward) { simulation::DefinitionId reward; if (!readId(in, strings, reward)) return fail("invalid ENCT reward"); encounter.rewardGrantId = std::move(reward); }
            data.encounters.push_back(std::move(encounter));
        }
        if (in.remaining() != 0) return fail("trailing ENCT data");
    }
    if (const auto found = chunks.find("SCNE"); found != chunks.end()) {
        if (minor < 5) return fail("SCNE chunk requires DMAP minor version 5");
        ByteReader in(found->second); std::uint32_t count{};
        if (!readCount(in, MapLimits::maximumPlacements, count)) return fail("invalid SCNE count");
        const auto readOptionalString = [&](ByteReader& input, std::string& value) {
            std::uint8_t present{};
            if (!input.readU8(present) || present > 1) return false;
            if (present == 0) { value.clear(); return true; }
            return readIndex(input, strings, value);
        };
        const auto readSceneAction = [&](ByteReader& input, WorldAction& action) {
            std::uint8_t kind{}, targetKind{}, state{}; std::uint64_t instance{};
            if (!input.readU8(kind) || kind > 5 || !input.readU8(targetKind) || targetKind > 2) {
                return false;
            }
            if (targetKind == 1 && !readId(input, strings, action.definitionTarget)) return false;
            if (targetKind == 2 && (!input.readU64(instance) || instance == 0)) return false;
            if (targetKind == 2) action.instanceTarget = {instance};
            if (!input.readU8(state) || state > 2) return false;
            action.kind = static_cast<WorldActionKind>(kind);
            action.doorState = static_cast<DoorState>(state);
            return true;
        };
        data.scenes.reserve(count);
        for (std::uint32_t sceneIndex = 0; sceneIndex < count; ++sceneIndex) {
            gameplay::scenes::SceneDefinition scene;
            std::uint32_t idIndex{}, actorCount{}, trackCount{}, markerCount{};
            if (!in.readU32(idIndex) || idIndex >= strings.size() ||
                !in.readU32(scene.durationTicks) ||
                !readCount(in, MapLimits::maximumPlacements, actorCount)) {
                return fail("invalid SCNE scene");
            }
            scene.id = simulation::DefinitionId{strings[idIndex]};
            scene.actors.reserve(actorCount);
            for (std::uint32_t i = 0; i < actorCount; ++i) {
                gameplay::scenes::SceneActorBinding actor;
                std::uint32_t slotIndex{}; std::uint8_t kind{}, present{}; std::uint64_t instance{};
                if (!in.readU32(slotIndex) || slotIndex >= strings.size() ||
                    !in.readU8(kind) || kind > 2 || !in.readU8(present) || present > 1) {
                    return fail("invalid SCNE actor");
                }
                actor.slotId = strings[slotIndex];
                actor.kind = static_cast<gameplay::scenes::SceneActorKind>(kind);
                if (present && (!in.readU64(instance) || instance == 0)) {
                    return fail("invalid SCNE actor instance");
                }
                if (present) actor.instanceId = {instance};
                scene.actors.push_back(std::move(actor));
            }
            if (!readCount(in, MapLimits::maximumPlacements, trackCount)) return fail("invalid SCNE tracks");
            scene.tracks.reserve(trackCount);
            for (std::uint32_t i = 0; i < trackCount; ++i) {
                gameplay::scenes::SceneTrack track; std::uint8_t kind{}; std::string actorSlot;
                std::uint32_t clipCount{};
                if (!in.readU8(kind) || kind > 3 || !readOptionalString(in, actorSlot) ||
                    !readCount(in, MapLimits::maximumPlacements, clipCount)) return fail("invalid SCNE track");
                track.kind = static_cast<gameplay::scenes::SceneTrackKind>(kind);
                track.actorSlot = std::move(actorSlot);
                track.clips.reserve(clipCount);
                for (std::uint32_t j = 0; j < clipCount; ++j) {
                    gameplay::scenes::SceneClip clip;
                    std::uint8_t clipKind{}, facing{}, emote{}, wait{}; std::string text;
                    if (!in.readU8(clipKind) || clipKind > 6 ||
                        !readOptionalString(in, clip.actorSlot) ||
                        !in.readU32(clip.startTick) || !in.readU32(clip.durationTicks) ||
                        !readPoint(in, clip.targetPosition) || !in.readU8(facing) || facing > 3 ||
                        !in.readU8(emote) || emote > 2 || !in.readI32(clip.heightPixels) ||
                        !readOptionalString(in, text)) return fail("invalid SCNE clip");
                    if (!text.empty()) clip.dialogueId = simulation::DefinitionId{text};
                    if (!in.readU8(wait) || wait > 1 || !readOptionalString(in, text)) {
                        return fail("invalid SCNE dialogue clip");
                    }
                    clip.waitForCompletion = wait != 0;
                    if (!text.empty()) clip.effectId = simulation::DefinitionId{text};
                    if (!readSceneAction(in, clip.worldAction)) return fail("invalid SCNE world action");
                    clip.kind = static_cast<gameplay::scenes::SceneClipKind>(clipKind);
                    clip.facing = static_cast<gameplay::FacingDirection>(facing);
                    clip.emote = static_cast<gameplay::scenes::SceneEmoteKind>(emote);
                    track.clips.push_back(std::move(clip));
                }
                scene.tracks.push_back(std::move(track));
            }
            if (!readCount(in, MapLimits::maximumPlacements, markerCount)) return fail("invalid SCNE markers");
            for (std::uint32_t i = 0; i < markerCount; ++i) {
                gameplay::scenes::SceneMarker marker;
                if (!readIndex(in, strings, marker.name) || !in.readU32(marker.tick)) return fail("invalid SCNE marker");
                scene.markers.push_back(std::move(marker));
            }
            data.scenes.push_back(std::move(scene));
        }
        if (in.remaining() != 0) return fail("trailing SCNE data");
    }
    const auto validation=validateMapData(data,catalogs);if(!validation)return fail(validation.error);
    return {true,std::move(data),{}};
}

bool writeDmap(const std::filesystem::path& path,const MapData& data,std::string& error){
    try{const auto bytes=serializeDmap(data);std::ofstream file(path,std::ios::binary|std::ios::trunc);if(!file){error="could not open DMAP for writing";return false;}file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!file){error="could not write DMAP";return false;}return true;}catch(const std::exception& ex){error=ex.what();return false;}}
DmapLoadResult readDmap(const std::filesystem::path& path,const MapValidationCatalogs* catalogs){
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return fail("could not open DMAP");const auto end=file.tellg();if(end<0||static_cast<std::uint64_t>(end)>MapLimits::maximumFileBytes)return fail("invalid DMAP file size");std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!file&& !bytes.empty())return fail("could not read DMAP");return deserializeDmap(bytes,catalogs);
}

} // namespace underworld::game::maps
