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
        writePoint(ents, object.position); ents.writeU32(static_cast<std::uint32_t>(object.initialContents.size()));
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
    ByteWriter link; link.writeU32(static_cast<std::uint32_t>(data.links.size()));
    for (const auto& value : data.links) {
        link.writeU32(strings.index(value.id)); writeArea(link, value.trigger);
        link.writeU32(strings.index(value.targetMapId.value()));
        link.writeU32(strings.index(value.targetSpawnId.value()));
    }
    appendChunk(chunks, {'L','I','N','K'}, std::move(link));

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
                           tag == "COLL" || tag == "SPWN" || tag == "ENTS" || tag == "LINK";
        if (known && !chunks.emplace(tag, payload).second) return fail("duplicate singleton DMAP chunk");
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
        data.playerSpawns.reserve(count);for(std::uint32_t i=0;i<count;++i){simulation::SpawnId id;core::WorldPointI point;gameplay::FacingDirection facing;
            if(!readId(in,strings,id)||!readPoint(in,point)||!readFacing(in,facing))return fail("invalid SPWN record");data.playerSpawns.push_back({std::move(id),point,facing});}
        if(in.remaining()!=0)return fail("trailing SPWN data");
    }
    {
        ByteReader in(chunks["ENTS"]);std::uint32_t count{};
        if(!readCount(in,MapLimits::maximumPlacements,count))return fail("invalid enemy count");data.enemies.reserve(count);
        for(std::uint32_t i=0;i<count;++i){std::uint64_t id{};simulation::DefinitionId def;core::WorldPointI point;gameplay::FacingDirection facing;
            if(!in.readU64(id)||!readId(in,strings,def)||!readPoint(in,point)||!readFacing(in,facing))return fail("invalid enemy record");data.enemies.push_back({{id},std::move(def),point,facing});}
        if(!readCount(in,MapLimits::maximumPlacements,count))return fail("invalid object count");data.objects.reserve(count);
        for(std::uint32_t i=0;i<count;++i){std::uint64_t id{};simulation::DefinitionId def;core::WorldPointI point;std::uint32_t stackCount{};
            if(!in.readU64(id)||!readId(in,strings,def)||!readPoint(in,point)||!readCount(in,MapLimits::maximumPlacements,stackCount))return fail("invalid object record");
            ObjectPlacement object{{id},std::move(def),point,{}};object.initialContents.reserve(stackCount);
            for(std::uint32_t s=0;s<stackCount;++s){simulation::DefinitionId item;std::uint32_t quantity{};if(!readId(in,strings,item)||!in.readU32(quantity))return fail("invalid object contents");object.initialContents.push_back({std::move(item),quantity});}data.objects.push_back(std::move(object));}
        if(!readCount(in,MapLimits::maximumPlacements,count))return fail("invalid pickup count");data.pickups.reserve(count);
        for(std::uint32_t i=0;i<count;++i){std::uint64_t id{};simulation::DefinitionId def,visual;core::WorldPointI point;world::AabbI area;std::uint8_t kind{};
            if(!in.readU64(id)||!readId(in,strings,def)||!readId(in,strings,visual)||!readPoint(in,point)||!readArea(in,area)||!in.readU8(kind))return fail("invalid pickup record");
            gameplay::PickupPayload payload=gameplay::HealthPickup{1};
            if(kind==0){std::int32_t amount{};if(!in.readI32(amount))return fail("invalid health pickup");payload=gameplay::HealthPickup{amount};}
            else if(kind==1){std::uint64_t amount{};if(!in.readU64(amount))return fail("invalid currency pickup");payload=gameplay::CurrencyPickup{amount};}
            else if(kind==2){simulation::DefinitionId item;std::uint32_t quantity{};if(!readId(in,strings,item)||!in.readU32(quantity))return fail("invalid item pickup");payload=gameplay::ItemPickup{std::move(item),quantity};}
            else return fail("unknown pickup payload kind");data.pickups.push_back({{id},std::move(def),std::move(visual),point,area,std::move(payload)});}
        if(in.remaining()!=0)return fail("trailing ENTS data");
    }
    {
        ByteReader in(chunks["LINK"]);std::uint32_t count{};if(!readCount(in,MapLimits::maximumPlacements,count))return fail("invalid LINK count");data.links.reserve(count);
        for(std::uint32_t i=0;i<count;++i){std::string id;world::AabbI area;simulation::MapId target;simulation::SpawnId spawn;
            if(!readIndex(in,strings,id)||!readArea(in,area)||!readId(in,strings,target)||!readId(in,strings,spawn))return fail("invalid LINK record");data.links.push_back({std::move(id),area,std::move(target),std::move(spawn)});}
        if(in.remaining()!=0)return fail("trailing LINK data");
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
