#include "game/maps/map_catalog.h"

#include "engine/world/collision.h"
#include "game/gameplay/combat_system.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game::maps {

void MapCatalog::add(simulation::MapId id,std::filesystem::path path){if(path.empty())throw std::invalid_argument("map resource path cannot be empty");if(!paths_.emplace(std::move(id),std::move(path)).second)throw std::logic_error("duplicate map id");}
const std::filesystem::path* MapCatalog::find(const simulation::MapId& id)const noexcept{const auto found=paths_.find(id);return found==paths_.end()?nullptr:&found->second;}
DmapLoadResult MapCatalog::load(const simulation::MapId& id,const MapValidationCatalogs* catalogs)const{const auto* path=find(id);if(!path)return {false,{},"map id is not registered"};auto result=readDmap(*path,catalogs);if(result&&!(result.data.id==id))return {false,{},"map resource id does not match catalog id"};return result;}
std::string MapCatalog::validateLinks(const MapValidationCatalogs* catalogs)const{std::vector<MapData> maps;maps.reserve(paths_.size());for(const auto& [id,path]:paths_){static_cast<void>(path);auto loaded=load(id,catalogs);if(!loaded)return loaded.error;maps.push_back(std::move(loaded.data));}for(const auto& map:maps)for(const auto& link:map.links){const auto target=std::find_if(maps.begin(),maps.end(),[&](const MapData& value){return value.id==link.targetMapId;});if(target==maps.end())return "map link target is not registered";if(std::none_of(target->playerSpawns.begin(),target->playerSpawns.end(),[&](const PlayerSpawn& spawn){return spawn.id==link.targetSpawnId;}))return "map link target spawn does not exist";}return {};}

TransitionResult MapSession::activate(const simulation::MapId& mapId,const simulation::SpawnId& spawnId){return prepareAndSwap(mapId,spawnId);}
bool MapSession::requestTransition(world::AabbI playerArea){if(!data_||pending_||transitionLatch_>0)return false;const MapLink* selected=nullptr;for(const auto& link:data_->links){if(gameplay::overlaps(playerArea,link.trigger)&&(!selected||link.id<selected->id))selected=&link;}if(!selected)return false;pending_=PendingMapTransition{selected->targetMapId,selected->targetSpawnId};return true;}
TransitionResult MapSession::commitPending(){if(!pending_)return {};const auto request=*pending_;pending_.reset();return prepareAndSwap(request.targetMapId,request.targetSpawnId);}
TransitionResult MapSession::restore(const simulation::MapId& mapId,const save::SessionWorldState& restoredState){auto loaded=maps_.load(mapId,&catalogs_);if(!loaded)return {false,{},loaded.error};if(loaded.data.playerSpawns.empty())return {false,{},"saved map has no player spawn"};auto built=builder_.build(loaded.data,loaded.data.playerSpawns.front().id);if(!built)return {false,{},built.error};std::string applyError;if(!save::applyWorldState(restoredState,*built.world,handles_,*catalogs_.items,applyError)){destroyRuntimeHandles(*built.world);return {false,{},applyError};}const PlayerSpawn spawn=built.world->spawn();if(world_)destroyRuntimeHandles(*world_);state_=restoredState;data_=std::move(loaded.data);world_=std::move(built.world);pending_.reset();transitionLatch_=1;return {true,spawn,{}};}
TransitionResult MapSession::prepareAndSwap(const simulation::MapId& mapId,const simulation::SpawnId& spawnId){if(world_&&data_)save::captureWorldState(*data_,*world_,state_);auto loaded=maps_.load(mapId,&catalogs_);if(!loaded)return {false,{},loaded.error};auto built=builder_.build(loaded.data,spawnId);if(!built)return {false,{},built.error};std::string applyError;if(!save::applyWorldState(state_,*built.world,handles_,*catalogs_.items,applyError)){destroyRuntimeHandles(*built.world);return {false,{},applyError};}const PlayerSpawn spawn=built.world->spawn();if(world_)destroyRuntimeHandles(*world_);data_=std::move(loaded.data);world_=std::move(built.world);transitionLatch_=1;return {true,spawn,{}};}
void MapSession::destroyRuntimeHandles(RuntimeWorld& value)noexcept{for(auto& enemy:value.enemies())static_cast<void>(handles_.destroy(enemy.instance.handle()));for(auto& object:value.objects())static_cast<void>(handles_.destroy(object.instance.handle()));for(auto& pickup:value.pickups())static_cast<void>(handles_.destroy(pickup.instance.handle()));}

} // namespace underworld::game::maps
