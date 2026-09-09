#include "game/save/save_data.h"

#include "engine/serialization/byte_io.h"
#include "game/gameplay/player.h"
#include "game/gameplay/rpg/player_progression.h"
#include "game/gameplay/rpg/equipment.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace underworld::game::save {
namespace {
using serialization::ByteReader; using serialization::ByteWriter;
constexpr std::uint16_t headerSize=20;

template<class Delta>
void upsert(std::vector<Delta>& values, Delta delta) {
    const auto found=std::find_if(values.begin(),values.end(),[&](const Delta& value){return value.key==delta.key;});
    if(found==values.end())values.push_back(std::move(delta));else *found=std::move(delta);
}
const maps::MapData* findMap(const SaveValidationCatalogs& catalogs,const simulation::MapId& id){
    const auto found=std::find_if(catalogs.maps.begin(),catalogs.maps.end(),[&](const maps::MapData* map){return map&&map->id==id;});
    return found==catalogs.maps.end()?nullptr:*found;
}
bool hasInstance(const maps::MapData& map,simulation::PersistentInstanceId id,bool object){
    if(object)return std::any_of(map.objects.begin(),map.objects.end(),[&](const auto& value){return value.id==id;});
    return std::any_of(map.pickups.begin(),map.pickups.end(),[&](const auto& value){return value.id==id;});
}
bool resetOnMapEnter(const SaveValidationCatalogs& catalogs,
                     const simulation::PersistentEntityKey& key) {
    const auto* map = findMap(catalogs, key.mapId);
    if (!map) return false;
    const auto found = std::find_if(map->objects.begin(), map->objects.end(),
                                    [&](const auto& value) { return value.id == key.instanceId; });
    return found != map->objects.end() &&
           found->persistence == maps::ObjectPersistencePolicy::resetOnMapEnter;
}
bool validStack(const gameplay::ItemStack& stack,const gameplay::ItemCatalog& items){
    const auto* definition=items.find(stack.itemId);return definition&&stack.quantity>0&&stack.quantity<=definition->stackLimit;
}
bool validDoorState(gameplay::DoorState state) noexcept {
    return state == gameplay::DoorState::locked || state == gameplay::DoorState::closed ||
           state == gameplay::DoorState::open;
}
bool validEncounterState(gameplay::EncounterState state) noexcept {
    return state == gameplay::EncounterState::inactive || state == gameplay::EncounterState::active ||
           state == gameplay::EncounterState::completed;
}
std::vector<std::string> stringsFor(const SaveData& data){
    std::vector<std::string> out;const auto add=[&](std::string_view value){if(!value.empty())out.emplace_back(value);};
    add(data.player.currentMapId.value());
    add(data.progression.definitionId.empty() ? gameplay::rpg::defaultPlayerProgressionId().value() : data.progression.definitionId.value());
    if (data.equipment.armor) add(data.equipment.armor->value());
    if (data.equipment.accessory) add(data.equipment.accessory->value());
    for (const auto& slot : data.bank.items) if (slot) add(slot->itemId.value());
    for(const auto& slot:data.player.inventory)if(slot)add(slot->itemId.value());
    for(const auto& slot:data.player.quickSlots)if(slot)add(slot->value());
    for(const auto& delta:data.world.objects){add(delta.key.mapId.value());for(const auto& stack:delta.remainingContents)add(stack.itemId.value());}
    for(const auto& delta:data.world.pickups)add(delta.key.mapId.value());
    for(const auto& flag:data.dialogueFlags.values())add(flag.value());
    for(const auto& quest:data.quests.snapshot()){add(quest.questId.value());for(const auto& objective:quest.objectives)add(objective.objectiveId.value());}
    for (const auto& rule : data.world.worldRules) {
        add(rule.mapId.value());
        add(rule.ruleId.value());
    }
    for (const auto& encounter : data.world.encounters) {
        add(encounter.mapId.value());
        add(encounter.encounterId.value());
    }
    std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;
}
std::uint32_t stringIndex(const std::vector<std::string>& strings,std::string_view value){
    const auto found=std::lower_bound(strings.begin(),strings.end(),value);if(found==strings.end()||*found!=value)throw std::logic_error("save string missing");return static_cast<std::uint32_t>(found-strings.begin());
}
void chunk(ByteWriter& file,const char(&tag)[5],ByteWriter payload){for(int i=0;i<4;++i)file.writeU8(static_cast<std::uint8_t>(tag[i]));file.writeU64(payload.bytes().size());file.writeBytes(payload.bytes());}
void point(ByteWriter& out,core::WorldPointI value){out.writeI32(value.x);out.writeI32(value.y);}
bool point(ByteReader& in,core::WorldPointI& value){return in.readI32(value.x)&&in.readI32(value.y);}
bool readIndex(ByteReader& in,const std::vector<std::string>& strings,std::string& value){std::uint32_t index{};if(!in.readU32(index)||index>=strings.size())return false;value=strings[index];return true;}
template<class Id>bool readId(ByteReader& in,const std::vector<std::string>& strings,Id& id){std::string value;if(!readIndex(in,strings,value)||value.empty())return false;id=Id{std::move(value)};return true;}
SaveResult fail(std::string error){return {false,{},std::move(error)};}
bool keyBefore(const simulation::PersistentEntityKey& a,const simulation::PersistentEntityKey& b){return a.mapId.value()==b.mapId.value()?a.instanceId.value<b.instanceId.value:a.mapId.value()<b.mapId.value();}
}

void SessionWorldState::set(ObjectDelta delta){upsert(objects,std::move(delta));}
void SessionWorldState::set(PickupDelta delta){upsert(pickups,std::move(delta));}
void SessionWorldState::eraseObject(const simulation::PersistentEntityKey& key) noexcept {
    objects.erase(std::remove_if(objects.begin(), objects.end(),
                                 [&](const ObjectDelta& value) { return value.key == key; }),
                  objects.end());
}
const ObjectDelta* SessionWorldState::findObject(const simulation::PersistentEntityKey& key)const noexcept{const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& value){return value.key==key;});return found==objects.end()?nullptr:&*found;}
const PickupDelta* SessionWorldState::findPickup(const simulation::PersistentEntityKey& key)const noexcept{const auto found=std::find_if(pickups.begin(),pickups.end(),[&](const auto& value){return value.key==key;});return found==pickups.end()?nullptr:&*found;}

std::string validateSaveData(const SaveData& data,const SaveValidationCatalogs& catalogs){
    if(!catalogs.items) return "save validation requires item catalog";
    const auto* current=findMap(catalogs,data.player.currentMapId);
    if(!current) return "save references unknown current map";
    if (!catalogs.progressions) return "save validation requires progression catalog";
    const auto progressionId = data.progression.definitionId.empty()
        ? gameplay::rpg::defaultPlayerProgressionId() : data.progression.definitionId;
    const auto* progression = catalogs.progressions->find(progressionId);
    if (!progression) return "save references unknown player progression";
    gameplay::rpg::PlayerEquipment equipment;
    equipment.restore(data.equipment.armor, data.equipment.accessory);
    const std::array<std::pair<const std::optional<simulation::DefinitionId>*, gameplay::rpg::EquipmentSlot>, 2> equippedSlots{{
        {&data.equipment.armor, gameplay::rpg::EquipmentSlot::armor},
        {&data.equipment.accessory, gameplay::rpg::EquipmentSlot::accessory}}};
    for (const auto& [equipped, expectedSlot] : equippedSlots) {
        if (!*equipped) continue;
        const auto* item = catalogs.items->find(**equipped);
        if (!item || item->category != gameplay::ItemCategory::equipment || !item->equipment ||
            item->stackLimit != 1 || item->equipment->slot != expectedSlot)
            return "saved equipment item is invalid";
    }
    const auto derived = gameplay::rpg::derivePlayerStats(progression->baseStats, equipment, *catalogs.items);
    if(data.player.health<0||data.player.health>derived.maximumHealth)return "saved player health is invalid";
    for(const auto& slot:data.player.inventory)if(slot&&!validStack(*slot,*catalogs.items))return "saved inventory stack is invalid";
    for(const auto& slot:data.bank.items)if(slot&&!validStack(*slot,*catalogs.items))return "saved bank stack is invalid";
    for(const auto& slot:data.player.quickSlots)if(slot&&!catalogs.items->find(*slot))return "saved quick slot item is unknown";
    std::unordered_set<simulation::PersistentEntityKey,simulation::PersistentEntityKeyHash> keys;
    for(const auto& delta:data.world.objects){if(delta.key.mapId.empty()||!delta.key.instanceId||!keys.emplace(delta.key).second)return "duplicate or invalid object delta key";if(delta.doorState&&!validDoorState(*delta.doorState))return "object delta has an invalid door state";const auto* map=findMap(catalogs,delta.key.mapId);if(!map||!hasInstance(*map,delta.key.instanceId,true))return "object delta references unknown map instance";const auto placement=std::find_if(map->objects.begin(),map->objects.end(),[&](const auto& value){return value.id==delta.key.instanceId;});const auto* definition=catalogs.objects?catalogs.objects->find(placement->definitionId):nullptr;if(delta.doorState&&catalogs.objects&&(!definition||!definition->door))return "door delta references a non-door object";if(delta.activationState&&(!catalogs.objects||!definition||!definition->activation||definition->activation->mode!=gameplay::ObjectActivationMode::interactToggle))return "activation delta references a non-persistent activation object";for(const auto& stack:delta.remainingContents)if(!validStack(stack,*catalogs.items))return "object delta stack is invalid";}
    keys.clear();for(const auto& delta:data.world.pickups){if(delta.key.mapId.empty()||!delta.key.instanceId||!keys.emplace(delta.key).second)return "duplicate or invalid pickup delta key";const auto* map=findMap(catalogs,delta.key.mapId);if(!map||!hasInstance(*map,delta.key.instanceId,false))return "pickup delta references unknown map instance";if(!delta.collected&&delta.remainingQuantity&&*delta.remainingQuantity==0)return "partial pickup remainder must be positive";}
    if(data.dialogueFlags.values().size()>maps::MapLimits::maximumStrings)return "too many dialogue flags";
    for(std::size_t index=0;index<data.dialogueFlags.values().size();++index){const auto& flag=data.dialogueFlags.values()[index];if(flag.empty()||(index!=0&&data.dialogueFlags.values()[index-1].value()>=flag.value()))return "dialogue flags are not unique and sorted";}
    if(data.quests.size()!=0&&!catalogs.quests)return "save validation requires quest catalog";
    if(catalogs.quests){const auto records=data.quests.snapshot();gameplay::quests::QuestStateStore restored;if(!restored.restore(records,*catalogs.quests))return "quest progress is invalid";}
    std::unordered_set<std::string> ruleKeys;
    for (const auto& state : data.world.worldRules) {
        if (state.mapId.empty() || state.ruleId.empty() ||
            !ruleKeys.emplace(std::string(state.mapId.value()) + "\n" +
                              std::string(state.ruleId.value())).second) {
            return "duplicate or invalid world rule state";
        }
        const auto* map = findMap(catalogs, state.mapId);
        if (!map || std::none_of(map->worldRules.begin(), map->worldRules.end(),
                                 [&](const auto& rule) { return rule.id == state.ruleId; })) {
            return "world rule state references unknown rule";
        }
    }
    std::unordered_set<std::string> encounterKeys;
    for (const auto& state : data.world.encounters) {
        if (state.mapId.empty() || state.encounterId.empty() ||
            !encounterKeys.emplace(std::string(state.mapId.value()) + "\n" +
                                   std::string(state.encounterId.value())).second) {
            return "duplicate or invalid encounter state";
        }
        if (!validEncounterState(state.state)) return "encounter state has an invalid state";
        const auto* map = findMap(catalogs, state.mapId);
        if (!map || std::none_of(map->encounters.begin(), map->encounters.end(),
                                 [&](const auto& encounter) { return encounter.id == state.encounterId; })) {
            return "encounter state references unknown encounter";
        }
    }
    return {};
}

std::vector<std::uint8_t> serializeSave(const SaveData& data){
    const auto strings=stringsFor(data);ByteWriter chunks;ByteWriter strs;strs.writeU32(static_cast<std::uint32_t>(strings.size()));for(const auto& value:strings)strs.writeString(value);chunk(chunks,"STRS",std::move(strs));
    ByteWriter player;player.writeU32(stringIndex(strings,data.player.currentMapId.value()));point(player,data.player.position);player.writeU8(static_cast<std::uint8_t>(data.player.facing));player.writeI32(data.player.health);player.writeU64(data.player.gold);
    player.writeU32(static_cast<std::uint32_t>(data.player.inventory.size()));for(const auto& slot:data.player.inventory){player.writeU8(slot?1:0);if(slot){player.writeU32(stringIndex(strings,slot->itemId.value()));player.writeU32(slot->quantity);}}
    player.writeU32(static_cast<std::uint32_t>(data.player.quickSlots.size()));for(const auto& slot:data.player.quickSlots){player.writeU8(slot?1:0);if(slot)player.writeU32(stringIndex(strings,slot->value()));}chunk(chunks,"PLYR",std::move(player));
    ByteWriter deltas;auto objects=data.world.objects;auto pickups=data.world.pickups;std::sort(objects.begin(),objects.end(),[](const auto&a,const auto&b){return keyBefore(a.key,b.key);});std::sort(pickups.begin(),pickups.end(),[](const auto&a,const auto&b){return keyBefore(a.key,b.key);});
    deltas.writeU32(static_cast<std::uint32_t>(objects.size()));for(const auto& value:objects){deltas.writeU32(stringIndex(strings,value.key.mapId.value()));deltas.writeU64(value.key.instanceId.value);deltas.writeU8(value.opened?1:0);deltas.writeU8(value.destroyed?1:0);deltas.writeU8(value.doorState?1:0);if(value.doorState)deltas.writeU8(static_cast<std::uint8_t>(*value.doorState));deltas.writeU8(value.activationState?1:0);if(value.activationState)deltas.writeU8(*value.activationState?1:0);deltas.writeU32(static_cast<std::uint32_t>(value.remainingContents.size()));for(const auto& stack:value.remainingContents){deltas.writeU32(stringIndex(strings,stack.itemId.value()));deltas.writeU32(stack.quantity);}}
    deltas.writeU32(static_cast<std::uint32_t>(pickups.size()));for(const auto& value:pickups){deltas.writeU32(stringIndex(strings,value.key.mapId.value()));deltas.writeU64(value.key.instanceId.value);deltas.writeU8(value.collected?1:0);deltas.writeU8(value.remainingQuantity?1:0);if(value.remainingQuantity)deltas.writeU64(*value.remainingQuantity);}chunk(chunks,"DELT",std::move(deltas));
    if(!data.dialogueFlags.values().empty()){ByteWriter flags;flags.writeU32(static_cast<std::uint32_t>(data.dialogueFlags.values().size()));for(const auto& flag:data.dialogueFlags.values())flags.writeU32(stringIndex(strings,flag.value()));chunk(chunks,"FLGS",std::move(flags));}
    const auto quests=data.quests.snapshot();if(!quests.empty()){ByteWriter state;state.writeU32(static_cast<std::uint32_t>(quests.size()));for(const auto& quest:quests){state.writeU32(stringIndex(strings,quest.questId.value()));state.writeU8(static_cast<std::uint8_t>(quest.status));state.writeU8(quest.rewardClaimed ? 1 : 0);state.writeU32(static_cast<std::uint32_t>(quest.objectives.size()));for(const auto& objective:quest.objectives){state.writeU32(stringIndex(strings,objective.objectiveId.value()));state.writeU32(objective.currentCount);}}chunk(chunks,"QSTS",std::move(state));}
    ByteWriter progression;progression.writeU32(stringIndex(strings, data.progression.definitionId.empty() ? gameplay::rpg::defaultPlayerProgressionId().value() : data.progression.definitionId.value()));progression.writeU64(data.progression.totalExperience);chunk(chunks,"PROG",std::move(progression));
    ByteWriter equipment;for (const auto& item : {data.equipment.armor, data.equipment.accessory}) { equipment.writeU8(item ? 1 : 0); if (item) equipment.writeU32(stringIndex(strings, item->value())); } chunk(chunks, "EQIP", std::move(equipment));
    ByteWriter bank; bank.writeU64(data.bank.gold); bank.writeU32(static_cast<std::uint32_t>(data.bank.items.size())); for (const auto& slot : data.bank.items) { bank.writeU8(slot ? 1 : 0); if (slot) { bank.writeU32(stringIndex(strings, slot->itemId.value())); bank.writeU32(slot->quantity); } } chunk(chunks, "BANK", std::move(bank));
    auto ruleStates = data.world.worldRules;
    std::sort(ruleStates.begin(), ruleStates.end(), [](const auto& left, const auto& right) {
        return left.mapId == right.mapId ? left.ruleId.value() < right.ruleId.value()
                                         : left.mapId.value() < right.mapId.value();
    });
    ByteWriter worldRules;
    worldRules.writeU32(static_cast<std::uint32_t>(ruleStates.size()));
    for (const auto& state : ruleStates) {
        worldRules.writeU32(stringIndex(strings, state.mapId.value()));
        worldRules.writeU32(stringIndex(strings, state.ruleId.value()));
        worldRules.writeU8(state.fired ? 1 : 0);
    }
    chunk(chunks, "WRLD", std::move(worldRules));
    auto encounterStates = data.world.encounters;
    std::sort(encounterStates.begin(), encounterStates.end(), [](const auto& left, const auto& right) {
        return left.mapId == right.mapId ? left.encounterId.value() < right.encounterId.value()
                                         : left.mapId.value() < right.mapId.value();
    });
    ByteWriter encounters;
    encounters.writeU32(static_cast<std::uint32_t>(encounterStates.size()));
    for (const auto& state : encounterStates) {
        encounters.writeU32(stringIndex(strings, state.mapId.value()));
        encounters.writeU32(stringIndex(strings, state.encounterId.value()));
        encounters.writeU8(static_cast<std::uint8_t>(state.state));
        encounters.writeU8(state.rewardClaimed ? 1 : 0);
    }
    chunk(chunks, "ENCT", std::move(encounters));
    ByteWriter out;for(char c:std::array<char,4>{'D','S','A','V'})out.writeU8(static_cast<std::uint8_t>(c));out.writeU16(saveMajorVersion);out.writeU16(saveMinorVersion);out.writeU16(0);out.writeU16(headerSize);out.writeU64(headerSize+chunks.bytes().size());out.writeBytes(chunks.bytes());return std::move(out).take();
}

SaveResult deserializeSave(std::span<const std::uint8_t> bytes,const SaveValidationCatalogs& catalogs){
    if(bytes.size()>maps::MapLimits::maximumFileBytes) return fail("save exceeds file size limit");
    ByteReader in(bytes);std::span<const std::uint8_t> magic;std::uint16_t major{},minor{},flags{},size{};std::uint64_t declared{};
    if(!in.readBytes(4,magic)||magic[0]!='D'||magic[1]!='S'||magic[2]!='A'||magic[3]!='V') return fail("wrong DSAV magic");
    if(!in.readU16(major)||!in.readU16(minor)||!in.readU16(flags)||!in.readU16(size)||!in.readU64(declared)) return fail("truncated DSAV header");
    if(major!=saveMajorVersion||minor>saveMinorVersion) return fail("unsupported DSAV version");
    if(flags||size<headerSize||declared!=bytes.size()||!in.skip(size-headerSize)) return fail("invalid DSAV header");
    std::unordered_map<std::string,std::span<const std::uint8_t>> chunks;while(in.remaining()){std::span<const std::uint8_t>tag,payload;std::uint64_t amount{};if(!in.readBytes(4,tag)||!in.readU64(amount)||amount>maps::MapLimits::maximumChunkBytes||amount>in.remaining()||!in.readBytes(static_cast<std::size_t>(amount),payload))return fail("truncated DSAV chunk");std::string name(reinterpret_cast<const char*>(tag.data()),4);if((name=="STRS"||name=="PLYR"||name=="DELT"||name=="FLGS"||name=="QSTS"||name=="PROG"||name=="EQIP"||name=="BANK"||name=="WRLD"||name=="ENCT")&&!chunks.emplace(name,payload).second)return fail("duplicate DSAV chunk");}for(const char* required:{"STRS","PLYR","DELT"})if(!chunks.contains(required))return fail("missing required DSAV chunk");if(minor==0&&chunks.contains("FLGS"))return fail("FLGS chunk requires DSAV minor version 1");if(minor<2&&chunks.contains("QSTS"))return fail("QSTS chunk requires DSAV minor version 2");if(minor<3&&chunks.contains("PROG"))return fail("PROG chunk requires DSAV minor version 3");if(minor>=3&&!chunks.contains("PROG"))return fail("DSAV minor version 3 requires PROG chunk");if(minor<4&&chunks.contains("EQIP"))return fail("EQIP chunk requires DSAV minor version 4");if(minor>=4&&!chunks.contains("EQIP"))return fail("DSAV minor version 4 requires EQIP chunk");if(minor<5&&chunks.contains("BANK"))return fail("BANK chunk requires DSAV minor version 5");if(minor>=5&&!chunks.contains("BANK"))return fail("DSAV minor version 5 requires BANK chunk");if(minor<7&&(chunks.contains("WRLD")||chunks.contains("ENCT")))return fail("world state chunks require DSAV minor version 7");if(minor>=7&&(!chunks.contains("WRLD")||!chunks.contains("ENCT")))return fail("DSAV minor version 7 requires world state chunks");if(chunks.contains("QSTS")&&!catalogs.quests)return fail("save validation requires quest catalog");
    std::vector<std::string> strings;{ByteReader r(chunks["STRS"]);std::uint32_t count{};if(!r.readU32(count)||count>maps::MapLimits::maximumStrings)return fail("invalid save string count");for(std::uint32_t i=0;i<count;++i){std::string value;if(!r.readString(value,maps::MapLimits::maximumStringBytes)||value.empty())return fail("invalid save string");strings.push_back(std::move(value));}if(r.remaining())return fail("trailing save string data");}
    SaveData data;{ByteReader r(chunks["PLYR"]);std::uint8_t facing{};std::uint32_t count{};if(!readId(r,strings,data.player.currentMapId)||!point(r,data.player.position)||!r.readU8(facing)||facing>3||!r.readI32(data.player.health)||!r.readU64(data.player.gold)||!r.readU32(count)||count!=data.player.inventory.size())return fail("invalid saved player");data.player.facing=static_cast<gameplay::FacingDirection>(facing);for(auto& slot:data.player.inventory){std::uint8_t present{};if(!r.readU8(present)||present>1)return fail("invalid inventory slot");if(present){simulation::DefinitionId id;std::uint32_t quantity{};if(!readId(r,strings,id)||!r.readU32(quantity))return fail("truncated inventory slot");slot=gameplay::ItemStack{std::move(id),quantity};}}if(!r.readU32(count)||count!=data.player.quickSlots.size())return fail("invalid quick slot count");for(auto& slot:data.player.quickSlots){std::uint8_t present{};if(!r.readU8(present)||present>1)return fail("invalid quick slot");if(present){simulation::DefinitionId id;if(!readId(r,strings,id))return fail("truncated quick slot");slot=std::move(id);}}if(r.remaining())return fail("trailing player data");}
    {ByteReader r(chunks["DELT"]);std::uint32_t count{};if(!r.readU32(count)||count>maps::MapLimits::maximumPlacements)return fail("invalid object delta count");for(std::uint32_t i=0;i<count;++i){simulation::MapId map;std::uint64_t id{};std::uint8_t opened{},destroyed{},hasDoor{},hasActivation{};std::uint32_t stacks{};if(!readId(r,strings,map)||!r.readU64(id)||!r.readU8(opened)||opened>1||!r.readU8(destroyed)||destroyed>1)return fail("invalid object delta");ObjectDelta delta{{std::move(map),{id}},opened!=0,destroyed!=0,{},std::nullopt,std::nullopt};if(minor>=7){if(!r.readU8(hasDoor)||hasDoor>1)return fail("invalid door delta");if(hasDoor){std::uint8_t door{};if(!r.readU8(door)||door>2)return fail("invalid door state");delta.doorState=static_cast<gameplay::DoorState>(door);}}if(minor>=8){if(!r.readU8(hasActivation)||hasActivation>1)return fail("invalid activation delta");if(hasActivation){std::uint8_t active{};if(!r.readU8(active)||active>1)return fail("invalid activation state");delta.activationState=active!=0;}}if(!r.readU32(stacks)||stacks>maps::MapLimits::maximumPlacements)return fail("invalid object delta");for(std::uint32_t s=0;s<stacks;++s){simulation::DefinitionId item;std::uint32_t quantity{};if(!readId(r,strings,item)||!r.readU32(quantity))return fail("invalid object delta contents");delta.remainingContents.push_back({std::move(item),quantity});}if(!resetOnMapEnter(catalogs,delta.key))data.world.objects.push_back(std::move(delta));}if(!r.readU32(count)||count>maps::MapLimits::maximumPlacements)return fail("invalid pickup delta count");for(std::uint32_t i=0;i<count;++i){simulation::MapId map;std::uint64_t id{},remaining{};std::uint8_t collected{},hasRemaining{};if(!readId(r,strings,map)||!r.readU64(id)||!r.readU8(collected)||collected>1||!r.readU8(hasRemaining)||hasRemaining>1)return fail("invalid pickup delta");std::optional<std::uint64_t> quantity;if(hasRemaining){if(!r.readU64(remaining))return fail("truncated pickup remainder");quantity=remaining;}data.world.pickups.push_back({{std::move(map),{id}},collected!=0,quantity});}if(r.remaining())return fail("trailing delta data");}
    if(const auto found=chunks.find("FLGS");found!=chunks.end()){ByteReader r(found->second);std::uint32_t count{};if(!r.readU32(count)||count>maps::MapLimits::maximumStrings)return fail("invalid dialogue flag count");std::vector<simulation::DefinitionId> dialogueFlags;dialogueFlags.reserve(count);for(std::uint32_t i=0;i<count;++i){simulation::DefinitionId flag;if(!readId(r,strings,flag))return fail("invalid dialogue flag");dialogueFlags.push_back(std::move(flag));}if(r.remaining()||!data.dialogueFlags.restore(dialogueFlags))return fail("invalid dialogue flags");}
    if(const auto found=chunks.find("QSTS");found!=chunks.end()){ByteReader r(found->second);std::uint32_t count{};if(!r.readU32(count)||count>maps::MapLimits::maximumPlacements)return fail("invalid quest state count");std::vector<gameplay::quests::QuestProgress> progress;progress.reserve(count);for(std::uint32_t i=0;i<count;++i){simulation::DefinitionId questId;if(!readId(r,strings,questId))return fail("invalid quest state ID");std::uint8_t status{},claimed{};std::uint32_t objectiveCount{};if(!r.readU8(status)||status<static_cast<std::uint8_t>(gameplay::quests::QuestStatus::active)||status>static_cast<std::uint8_t>(gameplay::quests::QuestStatus::completed))return fail("invalid quest status");if(minor>=6&&(!r.readU8(claimed)||claimed>1))return fail("invalid quest reward claim");if(!r.readU32(objectiveCount)||objectiveCount>maps::MapLimits::maximumPlacements)return fail("invalid quest objective count");gameplay::quests::QuestProgress value{std::move(questId),static_cast<gameplay::quests::QuestStatus>(status),{},minor<6 ? status==static_cast<std::uint8_t>(gameplay::quests::QuestStatus::completed) : claimed!=0};value.objectives.reserve(objectiveCount);for(std::uint32_t objectiveIndex=0;objectiveIndex<objectiveCount;++objectiveIndex){simulation::DefinitionId objectiveId;std::uint32_t currentCount{};if(!readId(r,strings,objectiveId)||!r.readU32(currentCount))return fail("invalid quest objective progress");value.objectives.push_back({std::move(objectiveId),currentCount});}progress.push_back(std::move(value));}if(r.remaining()||!data.quests.restore(progress,*catalogs.quests))return fail("invalid quest state");}
    if(const auto found=chunks.find("PROG");found!=chunks.end()){ByteReader r(found->second);if(!readId(r,strings,data.progression.definitionId)||!r.readU64(data.progression.totalExperience)||r.remaining())return fail("invalid progression data");}else{data.progression={gameplay::rpg::defaultPlayerProgressionId(),0};}
    if(const auto found=chunks.find("EQIP");found!=chunks.end()){ByteReader r(found->second);for(auto* slot : {&data.equipment.armor, &data.equipment.accessory}){std::uint8_t present{};if(!r.readU8(present)||present>1)return fail("invalid equipment slot");if(present){simulation::DefinitionId id;if(!readId(r,strings,id))return fail("invalid equipment item");*slot=std::move(id);}}if(r.remaining())return fail("trailing equipment data");}
    if(const auto found=chunks.find("BANK");found!=chunks.end()){ByteReader r(found->second);std::uint32_t count{};if(!r.readU64(data.bank.gold)||!r.readU32(count)||count!=data.bank.items.size())return fail("invalid bank slot count");for(auto& slot:data.bank.items){std::uint8_t present{};if(!r.readU8(present)||present>1)return fail("invalid bank slot");if(present){simulation::DefinitionId id;std::uint32_t quantity{};if(!readId(r,strings,id)||!r.readU32(quantity))return fail("invalid bank item");slot=gameplay::ItemStack{std::move(id),quantity};}}if(r.remaining())return fail("trailing bank data");}
    if(const auto found=chunks.find("WRLD");found!=chunks.end()){ByteReader r(found->second);std::uint32_t count{};if(!r.readU32(count)||count>maps::MapLimits::maximumPlacements)return fail("invalid world rule state count");for(std::uint32_t i=0;i<count;++i){simulation::MapId map;simulation::DefinitionId rule;std::uint8_t fired{};if(!readId(r,strings,map)||!readId(r,strings,rule)||!r.readU8(fired)||fired>1)return fail("invalid world rule state");data.world.worldRules.push_back({std::move(map),std::move(rule),fired!=0});}if(r.remaining())return fail("trailing world rule state");}
    if(const auto found=chunks.find("ENCT");found!=chunks.end()){ByteReader r(found->second);std::uint32_t count{};if(!r.readU32(count)||count>maps::MapLimits::maximumPlacements)return fail("invalid encounter state count");for(std::uint32_t i=0;i<count;++i){simulation::MapId map;simulation::DefinitionId encounter;std::uint8_t state{},claimed{};if(!readId(r,strings,map)||!readId(r,strings,encounter)||!r.readU8(state)||state>static_cast<std::uint8_t>(gameplay::EncounterState::completed)||!r.readU8(claimed)||claimed>1)return fail("invalid encounter state");data.world.encounters.push_back({std::move(map),std::move(encounter),static_cast<gameplay::EncounterState>(state),claimed!=0});}if(r.remaining())return fail("trailing encounter state");}
    const auto error=validateSaveData(data,catalogs);if(!error.empty())return fail(error);return {true,std::move(data),{}};
}

bool writeSaveAtomic(const std::filesystem::path& path,const SaveData& data,std::string& error){
    try{const auto bytes=serializeSave(data);auto temporary=path;temporary+=L".tmp";auto backup=path;backup+=L".bak";{std::ofstream file(temporary,std::ios::binary|std::ios::trunc);if(!file){error="could not open temporary save";return false;}file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));file.flush();if(!file){error="could not write temporary save";return false;}}
        std::error_code ec;std::filesystem::remove(backup,ec);ec.clear();if(std::filesystem::exists(path,ec)){ec.clear();std::filesystem::rename(path,backup,ec);if(ec){std::filesystem::remove(temporary);error="could not back up previous save";return false;}}ec.clear();std::filesystem::rename(temporary,path,ec);if(ec){std::error_code restore;if(std::filesystem::exists(backup,restore))std::filesystem::rename(backup,path,restore);std::filesystem::remove(temporary);error="could not replace save";return false;}return true;
    }catch(const std::exception& ex){error=ex.what();return false;}}
SaveResult readSave(const std::filesystem::path& path,const SaveValidationCatalogs& catalogs){std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return fail("could not open save");const auto end=file.tellg();if(end<0||static_cast<std::uint64_t>(end)>maps::MapLimits::maximumFileBytes)return fail("invalid save size");std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));if(!file&&!bytes.empty())return fail("could not read save");return deserializeSave(bytes,catalogs);}

bool applyWorldState(const SessionWorldState& state, maps::RuntimeWorld& world,
                     simulation::EntityHandlePool& handles,
                     const gameplay::ItemCatalog& items, std::string& error) {
    auto& objects = world.objects();
    for (auto it = objects.begin(); it != objects.end();) {
        if (it->persistence == maps::ObjectPersistencePolicy::resetOnMapEnter) {
            ++it;
            continue;
        }
        const auto* delta = state.findObject({world.id(), it->persistentId});
        if (!delta) { ++it; continue; }
        if (delta->destroyed) {
            world.addDestroyedObjectResidue(it->persistentId,
                                            it->instance.definition().visualSetId,
                                            it->instance.position());
            static_cast<void>(handles.destroy(it->instance.handle()));
            it = objects.erase(it);
            continue;
        }
        if (delta->doorState) {
            static_cast<void>(world.setDoorState(it->persistentId, *delta->doorState));
        }
        if (delta->activationState && it->instance.definition().activation &&
            it->instance.definition().activation->mode ==
                gameplay::ObjectActivationMode::interactToggle) {
            static_cast<void>(world.setObjectActivation(it->persistentId, *delta->activationState));
        }
        if (delta->opened) { static_cast<void>(it->instance.open()); }
        if (auto* contents = it->instance.contents()) {
            for (std::size_t index = 0; index < contents->capacity(); ++index) {
                if (const auto slot = contents->slot(index)) {
                    static_cast<void>(contents->remove(slot->itemId, slot->quantity));
                }
            }
            for (const auto& stack : delta->remainingContents) {
                const auto added = contents->add(stack.itemId, stack.quantity);
                if (added.remainder) {
                    error = "saved object contents do not fit";
                    return false;
                }
            }
        }
        ++it;
    }
    auto& pickups = world.pickups();
    for (auto it = pickups.begin(); it != pickups.end();) {
        if (it->transient) { ++it; continue; }
        const auto* delta = state.findPickup({world.id(), it->persistentId});
        if (!delta) { ++it; continue; }
        if (delta->collected) {
            static_cast<void>(handles.destroy(it->instance.handle()));
            it = pickups.erase(it);
            continue;
        }
        if (delta->remainingQuantity) {
            if (auto* currency = std::get_if<gameplay::CurrencyPickup>(&it->instance.payload())) {
                currency->amount = *delta->remainingQuantity;
            } else if (auto* item = std::get_if<gameplay::ItemPickup>(&it->instance.payload())) {
                if (*delta->remainingQuantity > std::numeric_limits<std::uint32_t>::max()) {
                    error = "saved item pickup remainder overflows";
                    return false;
                }
                item->quantity = static_cast<std::uint32_t>(*delta->remainingQuantity);
            } else {
                error = "health pickup cannot have partial remainder";
                return false;
            }
        }
        ++it;
    }
    static_cast<void>(items);
    return true;
}

void captureWorldState(const maps::MapData& original, const maps::RuntimeWorld& world,
                       SessionWorldState& state) {
    for (const auto& placement : original.objects) {
        if (placement.persistence == maps::ObjectPersistencePolicy::resetOnMapEnter) {
            state.eraseObject({original.id, placement.id});
            continue;
        }
        const auto runtime = std::find_if(
            world.objects().begin(), world.objects().end(), [&](const auto& candidate) {
                return candidate.persistentId == placement.id;
            });
        if (runtime == world.objects().end()) {
            state.set(ObjectDelta{{original.id, placement.id}, false, true, {}, std::nullopt, std::nullopt});
            continue;
        }

        std::vector<gameplay::ItemStack> contents;
        if (const auto* container = runtime->instance.contents()) {
            for (std::size_t index = 0; index < container->capacity(); ++index) {
                if (const auto& slot = container->slot(index)) { contents.push_back(*slot); }
            }
        }
        const bool opened = runtime->instance.state() == gameplay::WorldObjectState::opened;
        std::optional<gameplay::DoorState> doorState;
        if (runtime->instance.isDoor() &&
            runtime->instance.doorState() != runtime->instance.definition().door->initialState) {
            doorState = runtime->instance.doorState();
        }
        std::optional<bool> activationState;
        if (runtime->instance.definition().activation &&
            runtime->instance.definition().activation->mode == gameplay::ObjectActivationMode::interactToggle &&
            runtime->instance.activationActive() != runtime->instance.definition().activation->initialActive) {
            activationState = runtime->instance.activationActive();
        }
        const auto sameContents = [&] {
            if (contents.size() != placement.initialContents.size()) { return false; }
            for (std::size_t index = 0; index < contents.size(); ++index) {
                if (!(contents[index].itemId == placement.initialContents[index].itemId) ||
                    contents[index].quantity != placement.initialContents[index].quantity) {
                    return false;
                }
            }
            return true;
        }();
        if (opened || !sameContents) {
            state.set(ObjectDelta{{original.id, placement.id}, opened, false,
                                  std::move(contents), doorState, activationState});
        } else if (doorState || activationState) {
            state.set(ObjectDelta{{original.id, placement.id}, false, false, {}, doorState,
                                  activationState});
        } else {
            state.eraseObject({original.id, placement.id});
        }
    }

    for (const auto& placement : original.pickups) {
        const auto runtime = std::find_if(
            world.pickups().begin(), world.pickups().end(), [&](const auto& candidate) {
                return !candidate.transient && candidate.persistentId == placement.id;
            });
        if (runtime == world.pickups().end()) {
            state.set(PickupDelta{{original.id, placement.id}, true, std::nullopt});
            continue;
        }
        std::optional<std::uint64_t> originalQuantity;
        std::optional<std::uint64_t> runtimeQuantity;
        if (const auto* currency = std::get_if<gameplay::CurrencyPickup>(&placement.payload)) {
            originalQuantity = currency->amount;
            runtimeQuantity = std::get<gameplay::CurrencyPickup>(runtime->instance.payload()).amount;
        } else if (const auto* item = std::get_if<gameplay::ItemPickup>(&placement.payload)) {
            originalQuantity = item->quantity;
            runtimeQuantity = std::get<gameplay::ItemPickup>(runtime->instance.payload()).quantity;
        }
        if (originalQuantity && runtimeQuantity && *originalQuantity != *runtimeQuantity) {
            state.set(PickupDelta{{original.id, placement.id}, false, runtimeQuantity});
        }
    }
}

SavedPlayer capturePlayer(const gameplay::Player& player,const gameplay::PlayerItems& items,const simulation::MapId& currentMapId){SavedPlayer saved;saved.currentMapId=currentMapId;saved.position=player.feetPosition();saved.facing=player.facing();saved.health=player.health().current;saved.gold=items.wallet().gold();for(std::size_t index=0;index<saved.inventory.size();++index)saved.inventory[index]=items.inventory().items().slot(index);for(std::size_t index=0;index<saved.quickSlots.size();++index)saved.quickSlots[index]=items.quickSlots().binding(index);return saved;}
bool applyPlayer(const SavedPlayer& saved,gameplay::Player& player,gameplay::PlayerItems& items,const gameplay::ItemCatalog& catalog,std::string& error){try{if(saved.health<0||saved.health>player.health().maximum){error="saved player health is invalid";return false;}for(const auto& binding:saved.quickSlots)if(binding&&!catalog.find(*binding)){error="saved quick slot item is unknown";return false;}items.inventory().items().restoreSlots(saved.inventory);items.wallet().restoreGold(saved.gold);for(std::size_t index=0;index<saved.quickSlots.size();++index){if(saved.quickSlots[index])items.quickSlots().bind(index,*saved.quickSlots[index]);else items.quickSlots().clear(index);}player.health().current=saved.health;player.relocate(saved.position,saved.facing);return true;}catch(const std::exception& exception){error=exception.what();return false;}}

} // namespace underworld::game::save
