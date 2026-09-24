#include "game/maps/map_catalog.h"

#include "engine/world/collision.h"
#include "game/gameplay/combat_system.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game::maps {

void MapCatalog::add(simulation::MapId id,std::filesystem::path path){if(path.empty())throw std::invalid_argument("map resource path cannot be empty");if(paths_.contains(id)||data_.contains(id))throw std::logic_error("duplicate map id");paths_.emplace(std::move(id),std::move(path));}
void MapCatalog::addData(MapData data){if(data.id.empty())throw std::invalid_argument("map data id cannot be empty");if(paths_.contains(data.id)||data_.contains(data.id))throw std::logic_error("duplicate map id");data_.emplace(data.id,std::move(data));}
const std::filesystem::path* MapCatalog::find(const simulation::MapId& id)const noexcept{const auto found=paths_.find(id);return found==paths_.end()?nullptr:&found->second;}
DmapLoadResult MapCatalog::load(const simulation::MapId& id,const MapValidationCatalogs* catalogs)const{if(const auto found=data_.find(id);found!=data_.end())return {true,found->second,{}};const auto* path=find(id);if(!path)return {false,{},"map id is not registered"};auto result=readDmap(*path,catalogs);if(result&&!(result.data.id==id))return {false,{},"map resource id does not match catalog id"};return result;}
std::string MapCatalog::validateLinks(const MapValidationCatalogs* catalogs)const{std::vector<simulation::MapId> ids;ids.reserve(paths_.size()+data_.size());for(const auto& [id,path]:paths_){static_cast<void>(path);ids.push_back(id);}for(const auto& [id,data]:data_){static_cast<void>(data);ids.push_back(id);}std::sort(ids.begin(),ids.end(),[](const auto& left,const auto& right){return left.value()<right.value();});std::vector<MapData> maps;maps.reserve(ids.size());for(const auto& id:ids){auto loaded=load(id,catalogs);if(!loaded)return loaded.error;maps.push_back(std::move(loaded.data));}for(const auto& map:maps)for(const auto& link:map.links){const auto target=std::find_if(maps.begin(),maps.end(),[&](const MapData& value){return value.id==link.targetMapId;});if(target==maps.end())return "map '"+std::string(map.id.value())+"' link '"+link.id+"' targets missing map '"+std::string(link.targetMapId.value())+"' spawn '"+std::string(link.targetSpawnId.value())+"'";if(std::none_of(target->playerSpawns.begin(),target->playerSpawns.end(),[&](const PlayerSpawn& spawn){return spawn.id==link.targetSpawnId;}))return "map '"+std::string(map.id.value())+"' link '"+link.id+"' targets map '"+std::string(link.targetMapId.value())+"' missing spawn '"+std::string(link.targetSpawnId.value())+"'";}for(const auto& map:maps)for(const auto& object:map.objects){if(!object.transition)continue;const auto target=std::find_if(maps.begin(),maps.end(),[&](const MapData& value){return value.id==object.transition->targetMapId;});if(target==maps.end())return "map '"+std::string(map.id.value())+"' object '"+std::string(object.definitionId.value())+"' transitions to missing map '"+std::string(object.transition->targetMapId.value())+"'";if(std::none_of(target->playerSpawns.begin(),target->playerSpawns.end(),[&](const PlayerSpawn& spawn){return spawn.id==object.transition->targetSpawnId;}))return "map '"+std::string(map.id.value())+"' object '"+std::string(object.definitionId.value())+"' transitions to map '"+std::string(object.transition->targetMapId.value())+"' missing spawn '"+std::string(object.transition->targetSpawnId.value())+"'";}return {};}

namespace {
void suppressCompletedEncounterParticipants(
    const MapData& data, RuntimeWorld& world, simulation::EntityHandlePool& handles,
    const save::SessionWorldState& state) noexcept {
    std::vector<simulation::PersistentInstanceId> suppressed;
    for (const auto& encounter : data.encounters) {
        const auto found = std::find_if(state.encounters.begin(), state.encounters.end(),
            [&](const auto& value) {
                return value.mapId == data.id && value.encounterId == encounter.id &&
                       value.state == gameplay::EncounterState::completed;
            });
        if (found == state.encounters.end()) continue;
        suppressed.insert(suppressed.end(), encounter.participants.begin(),
                          encounter.participants.end());
    }
    auto& enemies = world.enemies();
    for (std::size_t index = 0; index < enemies.size();) {
        if (std::find(suppressed.begin(), suppressed.end(), enemies[index].persistentId) ==
            suppressed.end()) {
            ++index;
            continue;
        }
        static_cast<void>(handles.destroy(enemies[index].instance.handle()));
        enemies.erase(enemies.begin() + static_cast<std::ptrdiff_t>(index));
    }
}
} // namespace

TransitionResult MapSession::activate(const simulation::MapId& mapId,const simulation::SpawnId& spawnId){return prepareAndSwap(mapId,spawnId);}
MapSession::~MapSession(){if(world_)destroyRuntimeHandles(*world_);}
bool MapSession::requestTransition(PendingMapTransition transition){
    if(!data_||pending_||transitionLatch_>0)return false;
    if(transition.targetMapId.empty()||transition.targetSpawnId.empty())return false;
    pending_=std::move(transition);
    return true;
}
bool MapSession::requestTransition(world::AabbI playerArea){
    if(!data_||pending_||transitionLatch_>0)return false;
    const MapLink* selected=nullptr;
    for(const auto& link:data_->links){
        if(gameplay::overlaps(playerArea,link.trigger)&&(!selected||link.id<selected->id)){
            selected=&link;
        }
    }
    if(!selected)return false;
    return requestTransition(PendingMapTransition{
        selected->targetMapId,
        selected->targetSpawnId
    });
}
namespace {
constexpr unsigned kTransitionRetryDelayTicks = 30;
const std::vector<gameplay::PickupDefinition>& noPickups() {
    static const std::vector<gameplay::PickupDefinition> empty;
    return empty;
}
}
TransitionResult MapSession::commitPending(){
    if(!pending_)return {};
    // A failed swap keeps the pending request and retries after a delay
    // instead of dropping it (the player would re-request it every tick
    // anyway) or re-reading the DMAP every single tick.
    if(transitionRetryTicks_>0){--transitionRetryTicks_;return {};}
    const auto request=*pending_;
    const auto result=prepareAndSwap(request.targetMapId,request.targetSpawnId);
    if(result.changed){pending_.reset();transitionRetryTicks_=0;transitionError_.clear();}
    else{transitionRetryTicks_=kTransitionRetryDelayTicks;transitionError_=result.error;}
    return result;
}
TransitionResult MapSession::restore(const simulation::MapId& mapId,const save::SessionWorldState& restoredState){auto loaded=maps_.load(mapId,&catalogs_);if(!loaded)return {false,{},loaded.error};if(loaded.data.playerSpawns.empty())return {false,{},"saved map has no player spawn"};const simulation::SpawnId buildSpawn{"entry.start"};const auto canonical=std::find_if(loaded.data.playerSpawns.begin(),loaded.data.playerSpawns.end(),[&](const PlayerSpawn& spawn){return spawn.id==buildSpawn;});const simulation::SpawnId& spawnId=canonical!=loaded.data.playerSpawns.end()?canonical->id:loaded.data.playerSpawns.front().id;auto built=builder_.build(loaded.data,handles_,spawnId);if(!built)return {false,{},built.error};std::string applyError;if(!save::applyWorldState(restoredState,*built.world,handles_,*catalogs_.items,catalogs_.pickups?*catalogs_.pickups:noPickups(),applyError)){destroyRuntimeHandles(*built.world);return {false,{},applyError};}suppressCompletedEncounterParticipants(loaded.data,*built.world,handles_,restoredState);const PlayerSpawn spawn=built.world->spawn();if(world_)destroyRuntimeHandles(*world_);state_=restoredState;data_=std::move(loaded.data);world_=std::move(built.world);pending_.reset();transitionLatch_=1;return {true,spawn,{}};}
TransitionResult MapSession::prepareAndSwap(const simulation::MapId& mapId,const simulation::SpawnId& spawnId){if(world_&&data_)save::captureWorldState(*data_,*world_,state_);auto loaded=maps_.load(mapId,&catalogs_);if(!loaded)return {false,{},loaded.error};auto built=builder_.build(loaded.data,handles_,spawnId);if(!built)return {false,{},built.error};std::string applyError;if(!save::applyWorldState(state_,*built.world,handles_,*catalogs_.items,catalogs_.pickups?*catalogs_.pickups:noPickups(),applyError)){destroyRuntimeHandles(*built.world);return {false,{},applyError};}suppressCompletedEncounterParticipants(loaded.data,*built.world,handles_,state_);const PlayerSpawn spawn=built.world->spawn();if(world_)destroyRuntimeHandles(*world_);data_=std::move(loaded.data);world_=std::move(built.world);transitionLatch_=1;return {true,spawn,{}};}
void MapSession::destroyRuntimeHandles(RuntimeWorld& value)noexcept{for(auto& enemy:value.enemies())static_cast<void>(handles_.destroy(enemy.instance.handle()));for(auto& npc:value.npcs())static_cast<void>(handles_.destroy(npc.instance.handle()));for(auto& object:value.objects())static_cast<void>(handles_.destroy(object.instance.handle()));for(auto& pickup:value.pickups())static_cast<void>(handles_.destroy(pickup.instance.handle()));}

} // namespace underworld::game::maps
