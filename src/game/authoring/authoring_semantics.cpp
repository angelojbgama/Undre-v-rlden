#include "game/authoring/authoring_semantics.h"

#include <algorithm>
#include <stdexcept>

namespace underworld::game::authoring {
namespace {

std::string key(const simulation::DefinitionId& tileset, std::uint32_t index) {
    return std::string(tileset.value()) + "#" + std::to_string(index);
}

struct Seed { int x; int y; const char* name; TileRole role; TileTopology topology; const char* family; };

// Every non-transparent Dungeon atlas cell is deliberately represented.  Names stay visual
// where the asset audit cannot prove a stronger gameplay meaning.
constexpr Seed dungeonTiles[] = {
    {4,0,"top_cap.left",TileRole::wall,TileTopology::cap,"masonry"},{5,0,"top_cap.center",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{6,0,"top_cap.right",TileRole::wall,TileTopology::cap,"masonry"},
    {14,1,"detail.01",TileRole::detail,TileTopology::architecturalDetail,"detail"},
    {0,2,"masonry.00",TileRole::wall,TileTopology::outerCorner,"masonry"},{1,2,"masonry.01",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{2,2,"frame.nw",TileRole::corner,TileTopology::outerCorner,"masonry"},{3,2,"frame.n",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{4,2,"frame.ne",TileRole::corner,TileTopology::outerCorner,"masonry"},{5,2,"masonry.05",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{6,2,"masonry.06",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{7,2,"masonry.07",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{8,2,"masonry.08",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{10,2,"masonry.10",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{11,2,"masonry.11",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{12,2,"masonry.12",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{13,2,"masonry.13",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{14,2,"masonry.14",TileRole::wall,TileTopology::outerCorner,"masonry"},
    {0,3,"masonry.15",TileRole::wall,TileTopology::straightVertical,"masonry"},{1,3,"masonry.16",TileRole::wall,TileTopology::interior,"masonry"},{2,3,"frame.w",TileRole::wall,TileTopology::straightVertical,"masonry"},{4,3,"frame.e",TileRole::wall,TileTopology::straightVertical,"masonry"},{5,3,"masonry.20",TileRole::wall,TileTopology::interior,"masonry"},{6,3,"masonry.21",TileRole::wall,TileTopology::interior,"masonry"},{7,3,"masonry.22",TileRole::wall,TileTopology::interior,"masonry"},{8,3,"masonry.23",TileRole::wall,TileTopology::interior,"masonry"},{9,3,"inset.nw",TileRole::opening,TileTopology::outerCorner,"architectural_detail"},{10,3,"inset.ne",TileRole::opening,TileTopology::outerCorner,"architectural_detail"},{12,3,"masonry.26",TileRole::wall,TileTopology::interior,"masonry"},{13,3,"masonry.27",TileRole::wall,TileTopology::interior,"masonry"},{14,3,"masonry.28",TileRole::wall,TileTopology::straightVertical,"masonry"},
    {0,4,"masonry.29",TileRole::wall,TileTopology::outerCorner,"masonry"},{1,4,"masonry.30",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{2,4,"frame.sw",TileRole::corner,TileTopology::outerCorner,"masonry"},{3,4,"frame.s",TileRole::wall,TileTopology::straightHorizontal,"masonry"},{4,4,"frame.se",TileRole::corner,TileTopology::outerCorner,"masonry"},{7,4,"masonry.35",TileRole::wall,TileTopology::interior,"masonry"},{8,4,"masonry.36",TileRole::wall,TileTopology::interior,"masonry"},{9,4,"inset.sw",TileRole::opening,TileTopology::outerCorner,"architectural_detail"},{10,4,"inset.se",TileRole::opening,TileTopology::outerCorner,"architectural_detail"},{11,4,"masonry.39",TileRole::wall,TileTopology::interior,"masonry"},{12,4,"masonry.40",TileRole::wall,TileTopology::interior,"masonry"},{13,4,"masonry.41",TileRole::wall,TileTopology::interior,"masonry"},{14,4,"masonry.42",TileRole::wall,TileTopology::interior,"masonry"},{15,4,"masonry.43",TileRole::wall,TileTopology::interior,"masonry"},{16,4,"masonry.44",TileRole::wall,TileTopology::interior,"masonry"},{17,4,"masonry.45",TileRole::wall,TileTopology::interior,"masonry"},{18,4,"masonry.46",TileRole::wall,TileTopology::interior,"masonry"},
    {7,5,"strip_right.top",TileRole::wall,TileTopology::cap,"architectural_detail"},{11,5,"masonry.48",TileRole::wall,TileTopology::interior,"masonry"},{12,5,"masonry.49",TileRole::wall,TileTopology::interior,"masonry"},{13,5,"masonry.50",TileRole::wall,TileTopology::interior,"masonry"},{14,5,"masonry.51",TileRole::wall,TileTopology::interior,"masonry"},
    {1,6,"strip_left.top",TileRole::wall,TileTopology::cap,"architectural_detail"},{4,6,"small_frame.nw",TileRole::corner,TileTopology::outerCorner,"masonry"},{5,6,"small_frame.ne",TileRole::corner,TileTopology::outerCorner,"masonry"},{7,6,"strip_right.mid_a",TileRole::wall,TileTopology::straightVertical,"architectural_detail"},{14,6,"detail.02",TileRole::detail,TileTopology::architecturalDetail,"detail"},
    {1,7,"strip_left.mid",TileRole::wall,TileTopology::straightVertical,"architectural_detail"},{2,7,"detail.03",TileRole::detail,TileTopology::architecturalDetail,"detail"},{4,7,"small_frame.sw",TileRole::corner,TileTopology::outerCorner,"masonry"},{5,7,"small_frame.se",TileRole::corner,TileTopology::outerCorner,"masonry"},{6,7,"detail.04",TileRole::detail,TileTopology::architecturalDetail,"detail"},{7,7,"strip_right.mid_b",TileRole::wall,TileTopology::straightVertical,"architectural_detail"},
    {1,8,"strip_left.bottom",TileRole::wall,TileTopology::cap,"architectural_detail"},{7,8,"strip_right.bottom",TileRole::wall,TileTopology::cap,"architectural_detail"},
    {3,9,"ledge.left",TileRole::ledge,TileTopology::straightHorizontal,"ledge"},{4,9,"ledge.center",TileRole::ledge,TileTopology::straightHorizontal,"ledge"},{5,9,"ledge.right",TileRole::ledge,TileTopology::straightHorizontal,"ledge"},
    {3,11,"toothed.left",TileRole::ledge,TileTopology::straightHorizontal,"ledge"},{4,11,"toothed.center",TileRole::ledge,TileTopology::straightHorizontal,"ledge"},{5,11,"toothed.right",TileRole::ledge,TileTopology::straightHorizontal,"ledge"},
};

simulation::DefinitionId tileId(const char* name) { return simulation::DefinitionId{"tile.dungeon." + std::string(name)}; }

} // namespace

AuthoringSemanticRegistry::AuthoringSemanticRegistry() {
    for (const auto& seed : dungeonTiles) {
        TileSemanticDefinition definition;
        definition.id = tileId(seed.name);
        definition.tilesetId = simulation::DefinitionId{"tileset.dungeon"};
        definition.sourceIndex = static_cast<std::uint32_t>(seed.y * 19 + seed.x);
        definition.family = seed.family;
        definition.role = seed.role;
        definition.topology = seed.topology;
        definition.preferredLayer = seed.role == TileRole::floor ? "ground" : "walls";
        definition.semanticConfidence = (seed.role == TileRole::detail || seed.role == TileRole::opening)
            ? SemanticConfidence::unverified : SemanticConfidence::probable;
        definition.north = definition.south = definition.east = definition.west =
            seed.family == std::string("masonry") ? EdgeProfile::masonry : EdgeProfile::unknown;
        addTile(std::move(definition));
    }
    const auto stamp = [&](const char* id, const char* name, std::uint32_t width, std::uint32_t height,
                           bool atomic, std::initializer_list<std::pair<int,int>> cells) {
        StampDefinition value{simulation::DefinitionId{id}, name, width, height, {}, {0,0}, false, atomic,
                              SemanticConfidence::confirmed};
        for (const auto& [x,y] : cells) {
            const auto* tile = findTile(simulation::DefinitionId{"tileset.dungeon"}, static_cast<std::uint32_t>(y * 19 + x));
            if (tile) { value.cells.push_back({x - cells.begin()->first, y - cells.begin()->second, tile->id}); }
        }
        addStamp(std::move(value));
    };
    stamp("stamp.dungeon.masonry_frame_3x3", "Masonry Frame 3x3", 3, 3, true, {{2,2},{3,2},{4,2},{2,3},{4,3},{2,4},{3,4},{4,4}});
    stamp("stamp.dungeon.inset_2x2", "Inset 2x2", 2, 2, true, {{9,3},{10,3},{9,4},{10,4}});
    stamp("stamp.dungeon.vertical_strip_left_1x3", "Vertical Strip Left", 1, 3, true, {{1,6},{1,7},{1,8}});
    stamp("stamp.dungeon.vertical_strip_right_1x4", "Vertical Strip Right", 1, 4, true, {{7,5},{7,6},{7,7},{7,8}});
    stamp("stamp.dungeon.small_masonry_2x2", "Small Masonry 2x2", 2, 2, true, {{4,6},{5,6},{4,7},{5,7}});
    stamp("stamp.dungeon.horizontal_ledge_3x1", "Horizontal Ledge", 3, 1, true, {{3,9},{4,9},{5,9}});
    stamp("stamp.dungeon.horizontal_toothed_3x1", "Horizontal Toothed", 3, 1, true, {{3,11},{4,11},{5,11}});
    stamp("stamp.dungeon.top_cap_3x1", "Top Cap", 3, 1, true, {{4,0},{5,0},{6,0}});
}

void AuthoringSemanticRegistry::addTile(TileSemanticDefinition definition) {
    if (definition.id.empty() || definition.tilesetId.empty() || tileById_.contains(std::string(definition.id.value())) ||
        tileByReference_.contains(key(definition.tilesetId, definition.sourceIndex))) throw std::invalid_argument("duplicate semantic tile");
    const auto index = tiles_.size(); tileByReference_.emplace(key(definition.tilesetId, definition.sourceIndex), index);
    tileById_.emplace(std::string(definition.id.value()), index); tiles_.push_back(std::move(definition));
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
        const auto cell=map.layers[layerIndex].cells[index]; if(!cell || *cell>=map.tileReferences.size())continue; const auto& reference=map.tileReferences[*cell]; const auto* tile=semantics.findTile(reference.tilesetId,reference.sourceIndex);
        const core::WorldPointI point{static_cast<int>((index%map.width)*map.tileSize),static_cast<int>((index/map.width)*map.tileSize)};
        if(!tile){report.issues.push_back({SemanticIssueSeverity::info,"unclassified_tile","unclassified tile",point,layerIndex,{}});continue;}
        if(world::hasFlag(reference.flags,world::TileFlags::flipX)&&!tile->flipXAllowed)report.issues.push_back({SemanticIssueSeverity::warning,"forbidden_flip_x","tile does not approve FlipX",point,layerIndex,tile->id});
        if(index%map.width+1<map.width){const auto right=map.layers[layerIndex].cells[index+1];if(right&&*right<map.tileReferences.size()){const auto* other=semantics.findTile(map.tileReferences[*right].tilesetId,map.tileReferences[*right].sourceIndex);if(other&&!semantics.edgesCompatible(tile->east,other->west))report.issues.push_back({SemanticIssueSeverity::warning,"incompatible_edge","known edge profiles are incompatible",point,layerIndex,tile->id});}}
    }
    const auto onSolid=[&](core::WorldPointI point){if(point.x<0||point.y<0)return false;const auto x=static_cast<std::uint32_t>(point.x/map.tileSize),y=static_cast<std::uint32_t>(point.y/map.tileSize);return x<map.width&&y<map.height&&map.collision[static_cast<std::size_t>(y)*map.width+x]!=0;};
    for(const auto& spawn:map.playerSpawns)if(onSolid(spawn.position))report.issues.push_back({SemanticIssueSeverity::warning,"spawn_solid","player spawn is inside authored collision",spawn.position,{},{}});
    for(const auto& object:map.objects)if(object.definitionId.value()=="object.chest"){bool approach=false;const int d=map.tileSize;for(const auto& offset:std::vector<core::WorldPointI>{{d,0},{-d,0},{0,d},{0,-d}})if(!onSolid({object.position.x+offset.x,object.position.y+offset.y})){approach=true;break;}if(!approach)report.issues.push_back({SemanticIssueSeverity::warning,"chest_no_approach","chest has no adjacent non-solid approach cell",object.position,{}, {}});}
    // An atomic-stamp warning needs nearly complete visual evidence. Tiles are still reusable
    // individually, so a partial pair or a stamp clipped by the map boundary is not enough
    // evidence to claim that an author accidentally broke a stamp.
    for (const auto& stamp : semantics.stamps()) if (stamp.atomic) {
        for (std::size_t layerIndex=0; layerIndex<map.layers.size(); ++layerIndex) {
            for (std::uint32_t y=0; y<map.height; ++y) for (std::uint32_t x=0; x<map.width; ++x) {
                if (x + stamp.width > map.width || y + stamp.height > map.height) continue;
                unsigned matches{};
                for (const auto& member : stamp.cells) {
                    const int px=static_cast<int>(x)+member.x, py=static_cast<int>(y)+member.y;
                    const auto cell=map.layers[layerIndex].cells[static_cast<std::size_t>(py)*map.width+static_cast<std::uint32_t>(px)];
                    if(cell&&*cell<map.tileReferences.size()){const auto* actual=semantics.findTile(map.tileReferences[*cell].tilesetId,map.tileReferences[*cell].sourceIndex);if(actual&&actual->id==member.tileId)++matches;}
                }
                if(matches + 1U >= stamp.cells.size() && matches < stamp.cells.size()) report.issues.push_back({SemanticIssueSeverity::warning,"broken_atomic_stamp","atomic stamp appears incomplete",core::WorldPointI{static_cast<int>(x*map.tileSize),static_cast<int>(y*map.tileSize)},layerIndex,stamp.id});
            }
        }
    }
    return report;
}

const char* toString(TileRole value) noexcept { switch(value){case TileRole::floor:return "floor";case TileRole::wall:return "wall";case TileRole::corner:return "corner";case TileRole::ledge:return "ledge";case TileRole::opening:return "opening";case TileRole::detail:return "detail";default:return "unknown";} }
const char* toString(TileTopology value) noexcept { switch(value){case TileTopology::interior:return "interior";case TileTopology::straightHorizontal:return "straight_horizontal";case TileTopology::straightVertical:return "straight_vertical";case TileTopology::outerCorner:return "outer_corner";case TileTopology::innerCorner:return "inner_corner";case TileTopology::cap:return "cap";case TileTopology::junction:return "junction";case TileTopology::architecturalDetail:return "architectural_detail";default:return "unknown";} }
const char* toString(SemanticConfidence value) noexcept { return value==SemanticConfidence::confirmed?"CONFIRMED":value==SemanticConfidence::probable?"PROBABLE":"UNVERIFIED"; }

} // namespace underworld::game::authoring
