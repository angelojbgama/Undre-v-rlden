#include "game/authoring/authoring_semantics.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game::authoring {
namespace {
std::string key(const simulation::DefinitionId& tileset, std::uint32_t index) { return std::string(tileset.value()) + "#" + std::to_string(index); }
}

AuthoringSemanticRegistry::AuthoringSemanticRegistry() = default;

void AuthoringSemanticRegistry::addTile(TileSemanticDefinition definition) {
    if (definition.id.empty() || definition.tilesetId.empty() || tileById_.contains(std::string(definition.id.value())) || tileByReference_.contains(key(definition.tilesetId, definition.sourceIndex))) throw std::invalid_argument("duplicate semantic tile");
    const auto index = tiles_.size(); tileByReference_.emplace(key(definition.tilesetId, definition.sourceIndex), index); tileById_.emplace(std::string(definition.id.value()), index); tiles_.push_back(std::move(definition));
}
void AuthoringSemanticRegistry::addStamp(StampDefinition definition) {
    if (definition.id.empty() || definition.cells.empty() || stampById_.contains(std::string(definition.id.value()))) throw std::invalid_argument("invalid semantic stamp");
    stampById_.emplace(std::string(definition.id.value()), stamps_.size()); stamps_.push_back(std::move(definition));
}
const TileSemanticDefinition* AuthoringSemanticRegistry::findTile(const simulation::DefinitionId& id) const noexcept { const auto it=tileById_.find(std::string(id.value())); return it==tileById_.end()?nullptr:&tiles_[it->second]; }
const TileSemanticDefinition* AuthoringSemanticRegistry::findTile(const simulation::DefinitionId& tileset, std::uint32_t sourceIndex) const noexcept { const auto it=tileByReference_.find(key(tileset,sourceIndex)); return it==tileByReference_.end()?nullptr:&tiles_[it->second]; }
const StampDefinition* AuthoringSemanticRegistry::findStamp(const simulation::DefinitionId& id) const noexcept { const auto it=stampById_.find(std::string(id.value())); return it==stampById_.end()?nullptr:&stamps_[it->second]; }
std::vector<const TileSemanticDefinition*> AuthoringSemanticRegistry::tilesByFamily(const std::string& family) const { std::vector<const TileSemanticDefinition*> result; for(const auto& t:tiles_)if(t.family==family)result.push_back(&t);return result; }
bool AuthoringSemanticRegistry::edgesCompatible(EdgeProfile a, EdgeProfile b) const noexcept { return a==EdgeProfile::unknown||b==EdgeProfile::unknown||a==b; }
maps::MapTileReference tileReferenceFor(const TileSemanticDefinition& definition, world::TileFlags flags) { return {definition.tilesetId,definition.sourceIndex,flags}; }
std::size_t SemanticValidationReport::warningCount() const noexcept { return static_cast<std::size_t>(std::count_if(issues.begin(),issues.end(),[](const auto& i){return i.severity==SemanticIssueSeverity::warning;})); }
std::size_t SemanticValidationReport::infoCount() const noexcept { return static_cast<std::size_t>(std::count_if(issues.begin(),issues.end(),[](const auto& i){return i.severity==SemanticIssueSeverity::info;})); }

SemanticValidationReport MapSemanticValidator::validate(const maps::MapData& map, const AuthoringSemanticRegistry& semantics) const {
    SemanticValidationReport report;
    for(std::size_t layerIndex=0;layerIndex<map.layers.size();++layerIndex) for(std::size_t index=0;index<map.layers[layerIndex].cells.size();++index) {
        const auto cell=map.layers[layerIndex].cells[index]; if(!cell||*cell>=map.tileReferences.size())continue; const auto& ref=map.tileReferences[*cell]; const auto* tile=semantics.findTile(ref.tilesetId,ref.sourceIndex); const core::WorldPointI point{static_cast<int>((index%map.width)*map.tileSize),static_cast<int>((index/map.width)*map.tileSize)};
        if(!tile){report.issues.push_back({SemanticIssueSeverity::info,"unclassified_tile","unclassified tile",point,layerIndex,{}});continue;} if(world::hasFlag(ref.flags,world::TileFlags::flipX)&&!tile->flipXAllowed)report.issues.push_back({SemanticIssueSeverity::warning,"forbidden_flip_x","tile does not approve FlipX",point,layerIndex,tile->id});
        if(index%map.width+1<map.width){const auto right=map.layers[layerIndex].cells[index+1];if(right&&*right<map.tileReferences.size()){const auto* other=semantics.findTile(map.tileReferences[*right].tilesetId,map.tileReferences[*right].sourceIndex);if(other&&!semantics.edgesCompatible(tile->east,other->west))report.issues.push_back({SemanticIssueSeverity::warning,"incompatible_edge","known edge profiles are incompatible",point,layerIndex,tile->id});}}
    }
    const auto onSolid=[&](core::WorldPointI point){if(point.x<0||point.y<0)return false;const auto x=static_cast<std::uint32_t>(point.x/map.tileSize),y=static_cast<std::uint32_t>(point.y/map.tileSize);return x<map.width&&y<map.height&&map.collision[static_cast<std::size_t>(y)*map.width+x]!=0;};
    for(const auto& spawn:map.playerSpawns)if(onSolid(spawn.position))report.issues.push_back({SemanticIssueSeverity::warning,"spawn_solid","player spawn is inside authored collision",spawn.position,{}, {}});
    for(const auto& object:map.objects)if(object.definitionId.value()=="object.chest"){bool approach=false;const int d=map.tileSize;for(const auto& offset:std::vector<core::WorldPointI>{{d,0},{-d,0},{0,d},{0,-d}})if(!onSolid({object.position.x+offset.x,object.position.y+offset.y})){approach=true;break;}if(!approach)report.issues.push_back({SemanticIssueSeverity::warning,"chest_no_approach","chest has no adjacent non-solid approach cell",object.position,{}, {}});}
    for(const auto& stamp:semantics.stamps())if(stamp.atomic)for(std::size_t layerIndex=0;layerIndex<map.layers.size();++layerIndex)for(std::uint32_t y=0;y<map.height;++y)for(std::uint32_t x=0;x<map.width;++x){if(x+stamp.width>map.width||y+stamp.height>map.height)continue;unsigned matches{};for(const auto& member:stamp.cells){const int px=static_cast<int>(x)+member.x,py=static_cast<int>(y)+member.y;const auto cell=map.layers[layerIndex].cells[static_cast<std::size_t>(py)*map.width+static_cast<std::uint32_t>(px)];if(cell&&*cell<map.tileReferences.size()){const auto* actual=semantics.findTile(map.tileReferences[*cell].tilesetId,map.tileReferences[*cell].sourceIndex);if(actual&&actual->id==member.tileId)++matches;}}if(matches+1U>=stamp.cells.size()&&matches<stamp.cells.size())report.issues.push_back({SemanticIssueSeverity::warning,"broken_atomic_stamp","atomic stamp appears incomplete",core::WorldPointI{static_cast<int>(x*map.tileSize),static_cast<int>(y*map.tileSize)},layerIndex,stamp.id});}
    return report;
}

const char* toString(TileRole value) noexcept { switch(value){case TileRole::floor:return "floor";case TileRole::wall:return "wall";case TileRole::corner:return "corner";case TileRole::ledge:return "ledge";case TileRole::opening:return "opening";case TileRole::detail:return "detail";default:return "unknown";} }
const char* toString(TileTopology value) noexcept { switch(value){case TileTopology::interior:return "interior";case TileTopology::straightHorizontal:return "straight_horizontal";case TileTopology::straightVertical:return "straight_vertical";case TileTopology::outerCorner:return "outer_corner";case TileTopology::innerCorner:return "inner_corner";case TileTopology::cap:return "cap";case TileTopology::junction:return "junction";case TileTopology::architecturalDetail:return "architectural_detail";default:return "unknown";} }
const char* toString(SemanticConfidence value) noexcept { return value==SemanticConfidence::confirmed?"CONFIRMED":value==SemanticConfidence::probable?"PROBABLE":"UNVERIFIED"; }
} // namespace underworld::game::authoring
