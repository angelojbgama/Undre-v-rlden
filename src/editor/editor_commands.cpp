#include "editor/editor_commands.h"

#include <algorithm>
#include <deque>
#include <type_traits>
#include <unordered_set>

namespace underworld::editor {
namespace {

std::optional<std::size_t> cellIndex(const maps::MapData& data, TileCoordinate cell) noexcept {
    if (cell.x >= data.width || cell.y >= data.height) { return std::nullopt; }
    return static_cast<std::size_t>(cell.y) * data.width + cell.x;
}

template<class Placement>
auto findPersistent(std::vector<Placement>& values, simulation::PersistentInstanceId id) {
    return std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.id == id; });
}

template<class Placement>
auto findPersistent(const std::vector<Placement>& values, simulation::PersistentInstanceId id) {
    return std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.id == id; });
}

bool persistentIdExists(const EditorDocument& document, simulation::PersistentInstanceId id) {
    if (!id) { return true; }
    return findPersistent(document.data().enemies, id) != document.data().enemies.end() ||
           findPersistent(document.data().objects, id) != document.data().objects.end() ||
           findPersistent(document.data().pickups, id) != document.data().pickups.end() ||
           findPersistent(document.regions(), id) != document.regions().end();
}

template<class Placement>
void insertAt(std::vector<Placement>& values, std::size_t index, Placement value) {
    values.insert(values.begin() + static_cast<std::ptrdiff_t>(std::min(index, values.size())),
                  std::move(value));
}

} // namespace

PaintTilesCommand::PaintTilesCommand(std::size_t layer, std::vector<TileCoordinate> cells,
                                     std::optional<maps::MapTileReference> value)
    : layer_(layer), cells_(std::move(cells)), desired_(std::move(value)) {}

bool PaintTilesCommand::apply(EditorDocument& document, std::string& error) {
    auto& data = document.commandData();
    if (layer_ >= data.layers.size()) { error = "tile layer does not exist"; return false; }
    if (layer_ < document.layerStates().size() && document.layerStates()[layer_].locked) {
        error = "active tile layer is locked"; return false;
    }
    if (previous_.empty()) {
        std::unordered_set<std::size_t> seen;
        for (const auto cell : cells_) {
            const auto index = cellIndex(data, cell);
            if (index && seen.insert(*index).second) {
                previous_.push_back({*index, data.layers[layer_].cells[*index]});
            }
        }
        if (previous_.empty()) { error = "tile edit is outside map bounds"; return false; }
    }
    if (!referenceIndex_ && desired_) {
        const auto found = std::find(data.tileReferences.begin(), data.tileReferences.end(), *desired_);
        if (found == data.tileReferences.end()) {
            if (data.tileReferences.size() >= maps::MapLimits::maximumTileReferences) {
                error = "tile reference limit reached"; return false;
            }
            data.tileReferences.push_back(*desired_);
            referenceIndex_ = static_cast<std::uint32_t>(data.tileReferences.size() - 1);
            ownsReference_ = true;
        } else {
            referenceIndex_ = static_cast<std::uint32_t>(found - data.tileReferences.begin());
        }
    } else if (desired_ && ownsReference_ && referenceIndex_ &&
               *referenceIndex_ == data.tileReferences.size()) {
        data.tileReferences.push_back(*desired_);
    }
    for (const auto& previous : previous_) {
        data.layers[layer_].cells[previous.index] = desired_ ? referenceIndex_ : std::nullopt;
    }
    return true;
}

void PaintTilesCommand::revert(EditorDocument& document) noexcept {
    auto& data = document.commandData();
    if (layer_ >= data.layers.size()) { return; }
    for (const auto& previous : previous_) { data.layers[layer_].cells[previous.index] = previous.value; }
    if (ownsReference_ && referenceIndex_ && *referenceIndex_ + 1U == data.tileReferences.size()) {
        const bool used = std::any_of(data.layers.begin(), data.layers.end(), [&](const auto& layer) {
            return std::find(layer.cells.begin(), layer.cells.end(), referenceIndex_) != layer.cells.end();
        });
        if (!used) { data.tileReferences.pop_back(); }
    }
}

SetCollisionCommand::SetCollisionCommand(std::vector<TileCoordinate> cells, bool solid)
    : cells_(std::move(cells)), solid_(solid) {}

bool SetCollisionCommand::apply(EditorDocument& document, std::string& error) {
    auto& data = document.commandData();
    if (previous_.empty()) {
        std::unordered_set<std::size_t> seen;
        for (const auto cell : cells_) {
            const auto index = cellIndex(data, cell);
            if (index && seen.insert(*index).second) {
                previous_.push_back({*index, data.collision[*index]});
            }
        }
        if (previous_.empty()) { error = "collision edit is outside map bounds"; return false; }
    }
    for (const auto& previous : previous_) { data.collision[previous.index] = solid_ ? 1U : 0U; }
    return true;
}

void SetCollisionCommand::revert(EditorDocument& document) noexcept {
    auto& data = document.commandData();
    for (const auto& previous : previous_) { data.collision[previous.index] = previous.value; }
}

bool PlaceEntityCommand::apply(EditorDocument& document, std::string& error) {
    const bool placed = std::visit([&](auto& placement) -> bool {
        using Type = std::decay_t<decltype(placement)>;
        if constexpr (std::is_same_v<Type, maps::EnemyPlacement> ||
                      std::is_same_v<Type, maps::ObjectPlacement> ||
                      std::is_same_v<Type, maps::PickupPlacement> ||
                      std::is_same_v<Type, RegionPlacement>) {
            if (persistentIdExists(document, placement.id)) {
                error = "persistent instance id is zero or already used"; return false;
            }
        }
        if constexpr (std::is_same_v<Type, maps::EnemyPlacement>) document.commandData().enemies.push_back(placement);
        else if constexpr (std::is_same_v<Type, maps::ObjectPlacement>) document.commandData().objects.push_back(placement);
        else if constexpr (std::is_same_v<Type, maps::PickupPlacement>) document.commandData().pickups.push_back(placement);
        else if constexpr (std::is_same_v<Type, maps::PlayerSpawn>) {
            if (std::any_of(document.data().playerSpawns.begin(), document.data().playerSpawns.end(),
                [&](const auto& value) { return value.id == placement.id; })) {
                error = "player spawn id is already used"; return false;
            }
            document.commandData().playerSpawns.push_back(placement);
        } else if constexpr (std::is_same_v<Type, maps::MapLink>) {
            if (std::any_of(document.data().links.begin(), document.data().links.end(),
                [&](const auto& value) { return value.id == placement.id; })) {
                error = "map link id is already used"; return false;
            }
            document.commandData().links.push_back(placement);
        } else document.commandRegions().push_back(placement);
        return true;
    }, placement_);
    if (!placed) return false;
    std::visit([&](const auto& placement) {
        using Type = std::decay_t<decltype(placement)>;
        if constexpr (std::is_same_v<Type, maps::EnemyPlacement> ||
                      std::is_same_v<Type, maps::ObjectPlacement> ||
                      std::is_same_v<Type, maps::PickupPlacement> ||
                      std::is_same_v<Type, RegionPlacement>) {
            if (overrides_) document.commandPropertyOverrides()[placement.id.value] = *overrides_;
        }
    }, placement_);
    return true;
}

void PlaceEntityCommand::revert(EditorDocument& document) noexcept {
    std::visit([&](const auto& placement) {
        using Type = std::decay_t<decltype(placement)>;
        if constexpr (std::is_same_v<Type, maps::EnemyPlacement>) {
            auto& values=document.commandData().enemies; values.erase(findPersistent(values,placement.id));
        } else if constexpr (std::is_same_v<Type, maps::ObjectPlacement>) {
            auto& values=document.commandData().objects; values.erase(findPersistent(values,placement.id));
        } else if constexpr (std::is_same_v<Type, maps::PickupPlacement>) {
            auto& values=document.commandData().pickups; values.erase(findPersistent(values,placement.id));
        } else if constexpr (std::is_same_v<Type, maps::PlayerSpawn>) {
            auto& values=document.commandData().playerSpawns;
            values.erase(std::find_if(values.begin(),values.end(),[&](const auto& v){return v.id==placement.id;}));
        } else if constexpr (std::is_same_v<Type, maps::MapLink>) {
            auto& values=document.commandData().links;
            values.erase(std::find_if(values.begin(),values.end(),[&](const auto& v){return v.id==placement.id;}));
        } else {
            auto& values=document.commandRegions(); values.erase(findPersistent(values,placement.id));
        }
        if constexpr (std::is_same_v<Type, maps::EnemyPlacement> ||
                      std::is_same_v<Type, maps::ObjectPlacement> ||
                      std::is_same_v<Type, maps::PickupPlacement> ||
                      std::is_same_v<Type, RegionPlacement>) {
            document.commandPropertyOverrides().erase(placement.id.value);
        }
    }, placement_);
}

MoveEntityCommand::MoveEntityCommand(SelectionKind kind, simulation::PersistentInstanceId id,
                                     core::WorldPointI before, core::WorldPointI after,
                                     std::string authoredId)
    : kind_(kind), id_(id), authoredId_(std::move(authoredId)), before_(before), after_(after) {}

bool MoveEntityCommand::set(EditorDocument& document, core::WorldPointI value) noexcept {
    if (kind_ == SelectionKind::enemy) { auto it=findPersistent(document.commandData().enemies,id_); if(it!=document.commandData().enemies.end()){it->position=value;return true;} }
    if (kind_ == SelectionKind::object) { auto it=findPersistent(document.commandData().objects,id_); if(it!=document.commandData().objects.end()){it->position=value;return true;} }
    if (kind_ == SelectionKind::pickup) { auto it=findPersistent(document.commandData().pickups,id_); if(it!=document.commandData().pickups.end()){it->position=value;return true;} }
    if (kind_ == SelectionKind::region) { auto it=findPersistent(document.commandRegions(),id_); if(it!=document.commandRegions().end()){it->bounds.x=value.x;it->bounds.y=value.y;return true;} }
    if (kind_ == SelectionKind::playerSpawn) { auto it=std::find_if(document.commandData().playerSpawns.begin(),document.commandData().playerSpawns.end(),[&](const auto& v){return v.id.value()==authoredId_;}); if(it!=document.commandData().playerSpawns.end()){it->position=value;return true;} }
    if (kind_ == SelectionKind::mapLink) { auto it=std::find_if(document.commandData().links.begin(),document.commandData().links.end(),[&](const auto& v){return v.id==authoredId_;}); if(it!=document.commandData().links.end()){it->trigger.x=value.x;it->trigger.y=value.y;return true;} }
    return false;
}
bool MoveEntityCommand::apply(EditorDocument& document, std::string& error) { if(set(document,after_))return true;error="entity to move does not exist";return false; }
void MoveEntityCommand::revert(EditorDocument& document) noexcept { static_cast<void>(set(document,before_)); }

DeleteEntityCommand::DeleteEntityCommand(SelectionKind kind, simulation::PersistentInstanceId id,
                                         std::string authoredId)
    : kind_(kind), id_(id), authoredId_(std::move(authoredId)) {}

bool DeleteEntityCommand::apply(EditorDocument& document, std::string& error) {
    const auto removePersistent = [&](auto& values) -> bool {
        const auto found=findPersistent(values,id_); if(found==values.end())return false;
        index_=static_cast<std::size_t>(found-values.begin()); removed_=std::move(*found); values.erase(found); return true;
    };
    bool removed=false;
    if(kind_==SelectionKind::enemy)removed=removePersistent(document.commandData().enemies);
    else if(kind_==SelectionKind::object)removed=removePersistent(document.commandData().objects);
    else if(kind_==SelectionKind::pickup)removed=removePersistent(document.commandData().pickups);
    else if(kind_==SelectionKind::region)removed=removePersistent(document.commandRegions());
    else if(kind_==SelectionKind::playerSpawn){auto& v=document.commandData().playerSpawns;auto it=std::find_if(v.begin(),v.end(),[&](const auto& x){return x.id.value()==authoredId_;});if(it!=v.end()){index_=it-v.begin();removed_=std::move(*it);v.erase(it);removed=true;}}
    else if(kind_==SelectionKind::mapLink){auto& v=document.commandData().links;auto it=std::find_if(v.begin(),v.end(),[&](const auto& x){return x.id==authoredId_;});if(it!=v.end()){index_=it-v.begin();removed_=std::move(*it);v.erase(it);removed=true;}}
    if(!removed){error="entity to delete does not exist";return false;}
    if (id_) { auto it=document.commandPropertyOverrides().find(id_.value); if(it!=document.commandPropertyOverrides().end()){removedOverrides_=it->second;document.commandPropertyOverrides().erase(it);} }
    document.selection().clear(); return true;
}

void DeleteEntityCommand::revert(EditorDocument& document) noexcept {
    if(!removed_)return;
    std::visit([&](auto value){using Type=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<Type,maps::EnemyPlacement>)insertAt(document.commandData().enemies,index_,std::move(value));
        else if constexpr(std::is_same_v<Type,maps::ObjectPlacement>)insertAt(document.commandData().objects,index_,std::move(value));
        else if constexpr(std::is_same_v<Type,maps::PickupPlacement>)insertAt(document.commandData().pickups,index_,std::move(value));
        else if constexpr(std::is_same_v<Type,maps::PlayerSpawn>)insertAt(document.commandData().playerSpawns,index_,std::move(value));
        else if constexpr(std::is_same_v<Type,maps::MapLink>)insertAt(document.commandData().links,index_,std::move(value));
        else insertAt(document.commandRegions(),index_,std::move(value));
    },*removed_);
    if (id_ && removedOverrides_) document.commandPropertyOverrides()[id_.value]=*removedOverrides_;
}

SetPropertyCommand::SetPropertyCommand(simulation::PersistentInstanceId source,
    PropertySchema schema, std::optional<PropertyValue> value,
    const game::GameContentRegistry& content)
    : source_(source), schema_(std::move(schema)), value_(std::move(value)), content_(&content) {}

bool SetPropertyCommand::apply(EditorDocument& document, std::string& error) {
    if(!source_){error="property source id is invalid";return false;}
    if(value_ && !validatePropertyValue(schema_,*value_,*content_,document,error))return false;
    auto& all=document.commandPropertyOverrides(); auto outer=all.find(source_ .value);
    if(!previous_ && outer!=all.end()){auto found=outer->second.find(schema_.id);if(found!=outer->second.end())previous_=found->second;}
    if(value_)all[source_.value][schema_.id]=*value_;
    else if(outer!=all.end()){outer->second.erase(schema_.id);if(outer->second.empty())all.erase(outer);}
    return true;
}
void SetPropertyCommand::revert(EditorDocument& document) noexcept {
    auto& all=document.commandPropertyOverrides();
    if(previous_)all[source_.value][schema_.id]=*previous_;
    else {auto outer=all.find(source_.value);if(outer!=all.end()){outer->second.erase(schema_.id);if(outer->second.empty())all.erase(outer);}}
}

bool ResizeRegionCommand::set(EditorDocument& document, world::AabbI bounds) noexcept {auto it=findPersistent(document.commandRegions(),id_);if(it==document.commandRegions().end())return false;it->bounds=bounds;return true;}
bool ResizeRegionCommand::apply(EditorDocument& document,std::string& error){if(after_.width<=0||after_.height<=0){error="region dimensions must be positive";return false;}if(set(document,after_))return true;error="region does not exist";return false;}
void ResizeRegionCommand::revert(EditorDocument& document) noexcept{static_cast<void>(set(document,before_));}

void CompoundEditorCommand::add(std::unique_ptr<EditorCommand> command){if(command)commands_.push_back(std::move(command));}
bool CompoundEditorCommand::apply(EditorDocument& document,std::string& error){std::size_t applied=0;for(auto& command:commands_){if(!command->apply(document,error)){while(applied>0){commands_[--applied]->revert(document);}return false;}++applied;}return true;}
void CompoundEditorCommand::revert(EditorDocument& document) noexcept{for(auto it=commands_.rbegin();it!=commands_.rend();++it)(*it)->revert(document);}

std::vector<TileCoordinate> rectangleCells(int x0,int y0,int x1,int y1,const maps::MapData& data){
    const int left=std::max(0,std::min(x0,x1));const int top=std::max(0,std::min(y0,y1));
    const int right=std::min(static_cast<int>(data.width)-1,std::max(x0,x1));
    const int bottom=std::min(static_cast<int>(data.height)-1,std::max(y0,y1));
    std::vector<TileCoordinate> result;if(left>right||top>bottom)return result;
    result.reserve(static_cast<std::size_t>(right-left+1)*static_cast<std::size_t>(bottom-top+1));
    for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x)result.push_back({static_cast<std::uint32_t>(x),static_cast<std::uint32_t>(y)});return result;
}

std::vector<TileCoordinate> tileFloodCells(const maps::MapData& data,std::size_t layer,std::uint32_t startX,std::uint32_t startY){
    if(layer>=data.layers.size()||startX>=data.width||startY>=data.height)return{};
    const std::size_t start=static_cast<std::size_t>(startY)*data.width+startX;const auto target=data.layers[layer].cells[start];
    std::vector<TileCoordinate> result;std::vector<std::uint8_t> visited(data.layers[layer].cells.size());std::deque<TileCoordinate> pending{{startX,startY}};
    while(!pending.empty()){const auto cell=pending.front();pending.pop_front();const std::size_t index=static_cast<std::size_t>(cell.y)*data.width+cell.x;if(visited[index]||data.layers[layer].cells[index]!=target)continue;visited[index]=1;result.push_back(cell);
        if(cell.x>0)pending.push_back({cell.x-1,cell.y});if(cell.x+1<data.width)pending.push_back({cell.x+1,cell.y});if(cell.y>0)pending.push_back({cell.x,cell.y-1});if(cell.y+1<data.height)pending.push_back({cell.x,cell.y+1});}
    return result;
}

std::vector<TileCoordinate> collisionFloodCells(const maps::MapData& data,std::uint32_t startX,std::uint32_t startY){
    if(startX>=data.width||startY>=data.height)return{};const std::size_t start=static_cast<std::size_t>(startY)*data.width+startX;const auto target=data.collision[start];
    std::vector<TileCoordinate> result;std::vector<std::uint8_t> visited(data.collision.size());std::deque<TileCoordinate> pending{{startX,startY}};
    while(!pending.empty()){const auto cell=pending.front();pending.pop_front();const std::size_t index=static_cast<std::size_t>(cell.y)*data.width+cell.x;if(visited[index]||data.collision[index]!=target)continue;visited[index]=1;result.push_back(cell);
        if(cell.x>0)pending.push_back({cell.x-1,cell.y});if(cell.x+1<data.width)pending.push_back({cell.x+1,cell.y});if(cell.y>0)pending.push_back({cell.x,cell.y-1});if(cell.y+1<data.height)pending.push_back({cell.x,cell.y+1});}
    return result;
}

std::optional<AuthoredPlacement> duplicatePlacement(const EditorDocument& document,SelectionKind kind,simulation::PersistentInstanceId id,simulation::PersistentInstanceId newId,int offset){
    if(!newId)return std::nullopt;
    if(kind==SelectionKind::enemy){auto it=findPersistent(document.data().enemies,id);if(it!=document.data().enemies.end()){auto v=*it;v.id=newId;v.position.x+=offset;v.position.y+=offset;return v;}}
    if(kind==SelectionKind::object){auto it=findPersistent(document.data().objects,id);if(it!=document.data().objects.end()){auto v=*it;v.id=newId;v.position.x+=offset;v.position.y+=offset;return v;}}
    if(kind==SelectionKind::pickup){auto it=findPersistent(document.data().pickups,id);if(it!=document.data().pickups.end()){auto v=*it;v.id=newId;v.position.x+=offset;v.position.y+=offset;return v;}}
    if(kind==SelectionKind::region){auto it=findPersistent(document.regions(),id);if(it!=document.regions().end()){auto v=*it;v.id=newId;v.regionId+="_copy";v.bounds.x+=offset;v.bounds.y+=offset;return v;}}
    return std::nullopt;
}

std::optional<AuthoredPlacement> duplicateAuthoredPlacement(const EditorDocument& document,
    SelectionKind kind, std::string_view authoredId, int offset) {
    if (kind == SelectionKind::playerSpawn) {
        for (const auto& value : document.data().playerSpawns) {
            if (value.id.value() != authoredId) continue;
            auto copy = value;
            std::string candidate = std::string(authoredId) + "_copy";
            for (unsigned suffix = 2; std::any_of(document.data().playerSpawns.begin(), document.data().playerSpawns.end(), [&](const auto& spawn) { return spawn.id.value() == candidate; }); ++suffix) candidate = std::string(authoredId) + "_copy_" + std::to_string(suffix);
            copy.id = simulation::SpawnId{candidate}; copy.position.x += offset; copy.position.y += offset;
            return copy;
        }
    }
    if (kind == SelectionKind::mapLink) {
        for (const auto& value : document.data().links) {
            if (value.id != authoredId) continue;
            auto copy = value;
            std::string candidate = std::string(authoredId) + "_copy";
            for (unsigned suffix = 2; std::any_of(document.data().links.begin(), document.data().links.end(), [&](const auto& link) { return link.id == candidate; }); ++suffix) candidate = std::string(authoredId) + "_copy_" + std::to_string(suffix);
            copy.id = std::move(candidate); copy.trigger.x += offset; copy.trigger.y += offset;
            return copy;
        }
    }
    return std::nullopt;
}

std::optional<TileCoordinate> worldPointToTile(const maps::MapData& data,
                                                core::WorldPointI point) noexcept {
    if (data.tileSize == 0 || point.x < 0 || point.y < 0) return std::nullopt;
    const auto x = static_cast<std::uint32_t>(point.x / data.tileSize);
    const auto y = static_cast<std::uint32_t>(point.y / data.tileSize);
    if (x >= data.width || y >= data.height) return std::nullopt;
    return TileCoordinate{x, y};
}

} // namespace underworld::editor
