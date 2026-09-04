#include "editor/editor_app.h"

#include "engine/core/color_rgba8.h"
#include "engine/platform/image_decoder.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <exception>

namespace underworld::editor {
namespace {
constexpr int leftPanelWidth = 190;
constexpr int rightPanelWidth = 250;
constexpr int statusHeight = 22;
constexpr std::array<double, 6> zoomSteps{0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
constexpr core::ColorRGBA8 background{20, 23, 29, 255};
constexpr core::ColorRGBA8 viewportBackground{12, 14, 18, 255};
constexpr core::ColorRGBA8 gridColor{54, 61, 72, 150};
constexpr core::ColorRGBA8 collisionColor{220, 60, 70, 105};
constexpr core::ColorRGBA8 selectedColor{255, 220, 70, 255};

int parsePositive(const std::string& value) {
    try { const int parsed=std::stoi(value); return parsed>0?parsed:0; } catch (...) { return 0; }
}

core::ColorRGBA8 categoryColor(SelectionKind kind) noexcept {
    if(kind==SelectionKind::enemy)return {220,75,75,255};
    if(kind==SelectionKind::object)return {195,135,60,255};
    if(kind==SelectionKind::pickup)return {75,210,120,255};
    if(kind==SelectionKind::playerSpawn)return {70,150,240,255};
    if(kind==SelectionKind::mapLink)return {185,85,220,180};
    return {70,210,210,160};
}

void outline(render::Renderer2D& renderer, core::RectI bounds, core::ColorRGBA8 color) {
    renderer.fillRect({bounds.x,bounds.y,bounds.width,1},color);
    renderer.fillRect({bounds.x,bounds.y+bounds.height-1,bounds.width,1},color);
    renderer.fillRect({bounds.x,bounds.y,1,bounds.height},color);
    renderer.fillRect({bounds.x+bounds.width-1,bounds.y,1,bounds.height},color);
}
}

EditorApp::EditorApp(platform::ImageDecoder& decoder, const std::filesystem::path& assetRoot)
    : document_(EditorDocument::newMap(simulation::MapId{"map.untitled"},32,24,16,true)),
      framebuffer_(std::make_unique<render::Framebuffer>(1000,700)) {
    for (const auto& definition : content_.tilesets().definitions()) {
        std::string error;
        if (!tilesetVisuals_.load(definition, runtimeTilesets_.requireRuntimeId(definition.id),
                                 assets_, decoder, assetRoot, error)) {
            status_ += definition.displayName + " unavailable: " + error + "  ";
        }
    }
    try {
        const auto fontImage=assets_.loadImage("editor.font",assetRoot/"fonts_index.png",decoder);
        font_=std::make_unique<render::BitmapFont>(fontImage);
    } catch (const std::exception& exception) {
        status_ += std::string("Font unavailable; authoring shell remains active: ")+exception.what();
    }
}

EditorApp::~EditorApp()=default;

void EditorApp::resize(int width,int height){if(width>0&&height>0)framebuffer_=std::make_unique<render::Framebuffer>(width,height);}

double EditorApp::zoom() const noexcept{return zoomSteps[std::min(document_.viewport().zoomStep,zoomSteps.size()-1)];}

core::WorldPointI EditorApp::screenToWorld(core::PointI screen,core::RectI viewport) const noexcept{
    return {static_cast<int>(std::floor(document_.viewport().worldX+(screen.x-viewport.x)/zoom())),
            static_cast<int>(std::floor(document_.viewport().worldY+(screen.y-viewport.y)/zoom()))};
}
core::PointI EditorApp::worldToScreen(core::WorldPointI world,core::RectI viewport) const noexcept{
    return {viewport.x+static_cast<int>(std::lround((world.x-document_.viewport().worldX)*zoom())),
            viewport.y+static_cast<int>(std::lround((world.y-document_.viewport().worldY)*zoom()))};
}

void EditorApp::updateAndRender(const EditorInputState& input){
    if(input.focusLost)cancelActiveGesture();
    render::Renderer2D renderer(*framebuffer_);framebuffer_->clear(background);
    EditorUiContext ui(renderer,font_.get(),input);drawShell(ui,input);
    updateStatus(viewportBounds_,input);
}

void EditorApp::drawShell(EditorUiContext& ui,const EditorInputState& input){
    const int width=framebuffer_->width(),height=framebuffer_->height();
    const int viewportWidth=std::max(1,width-leftPanelWidth-rightPanelWidth);
    const int viewportHeight=std::max(1,height-statusHeight);
    viewportBounds_={leftPanelWidth,0,viewportWidth,viewportHeight};
    const core::RectI left{0,0,leftPanelWidth,viewportHeight};
    const core::RectI right{leftPanelWidth+viewportWidth,0,rightPanelWidth,viewportHeight};
    const core::RectI status{0,viewportHeight,width,statusHeight};
    ui.panel(left);ui.panel(right);ui.panel(status);

    ui.label("LAYERS",8,8);int y=22;
    for(std::size_t i=0;i<document_.data().layers.size();++i){
        const auto& layer=document_.data().layers[i];
        if(ui.button({8,y,110,18},layer.name,i==document_.activeLayer()))document_.activeLayer()=i;
        auto& state=document_.layerStates()[i];
        if(ui.toggle({120,y,30,18},state.visible?"ON":"OFF",state.visible))state.visible=!state.visible;
        if(ui.toggle({152,y,30,18},state.locked?"L":"U",state.locked))state.locked=!state.locked;
        y+=20;
    }
    if(ui.button({8,y,85,18},"SELECT",document_.activeTool()==EditorTool::select))document_.activeTool()=EditorTool::select;
    if(ui.button({97,y,85,18},"TILE",document_.activeTool()==EditorTool::tilePencil))document_.activeTool()=EditorTool::tilePencil;y+=20;
    if(ui.button({8,y,85,18},"ERASE",document_.activeTool()==EditorTool::tileErase))document_.activeTool()=EditorTool::tileErase;
    if(ui.button({97,y,85,18},"RECT",document_.activeTool()==EditorTool::tileRectangle))document_.activeTool()=EditorTool::tileRectangle;y+=20;
    if(ui.button({8,y,85,18},"FILL",document_.activeTool()==EditorTool::tileFill))document_.activeTool()=EditorTool::tileFill;
    if(ui.button({97,y,85,18},tileFlipX_?"FLIP X ON":"FLIP X",tileFlipX_))tileFlipX_=!tileFlipX_;y+=20;
    if(ui.button({8,y,85,18},"PICK",document_.activeTool()==EditorTool::tileEyedropper))document_.activeTool()=EditorTool::tileEyedropper;y+=20;
    if(ui.button({8,y,85,18},"COLL +",document_.activeTool()==EditorTool::collisionPaint))document_.activeTool()=EditorTool::collisionPaint;
    if(ui.button({97,y,85,18},"COLL -",document_.activeTool()==EditorTool::collisionErase))document_.activeTool()=EditorTool::collisionErase;y+=24;
    if(ui.button({8,y,85,18},"COLL R+",document_.activeTool()==EditorTool::collisionRectangle))document_.activeTool()=EditorTool::collisionRectangle;
    if(ui.button({97,y,85,18},"COLL R-",document_.activeTool()==EditorTool::collisionRectangleErase))document_.activeTool()=EditorTool::collisionRectangleErase;y+=20;
    if(ui.button({8,y,85,18},"COLL F+",document_.activeTool()==EditorTool::collisionFill))document_.activeTool()=EditorTool::collisionFill;
    if(ui.button({97,y,85,18},"COLL F-",document_.activeTool()==EditorTool::collisionFillErase))document_.activeTool()=EditorTool::collisionFillErase;y+=24;

    ui.label("CONTENT",8,y);y+=14;
    for(const auto& descriptor:content_.authoringDescriptors()){
        if(y+18>viewportHeight-210)break;
        if(ui.button({8,y,174,18},descriptor.displayName,selectedDefinition_==descriptor.definitionId&&document_.activeTool()==EditorTool::entityPlace)){
            selectedDefinition_=descriptor.definitionId;selectedCategory_=descriptor.category;document_.activeTool()=EditorTool::entityPlace;
        }y+=20;
    }
    if(ui.button({8,y,174,18},"Player Spawn",document_.activeTool()==EditorTool::entityPlace&&selectedDefinition_.value()=="world.player_spawn")){
        selectedDefinition_=simulation::DefinitionId{"world.player_spawn"};document_.activeTool()=EditorTool::entityPlace;
    }y+=20;
    if(ui.button({8,y,174,18},"Map Link",document_.activeTool()==EditorTool::entityPlace&&selectedDefinition_.value()=="world.map_link")){
        selectedDefinition_=simulation::DefinitionId{"world.map_link"};document_.activeTool()=EditorTool::entityPlace;
    }y+=20;
    if(ui.button({8,y,174,18},"Region (experimental)",document_.activeTool()==EditorTool::regionCreate))document_.activeTool()=EditorTool::regionCreate;

    const auto& semantics = content_.authoringSemantics();
    static const std::array<std::string, 6> semanticFamilies{"ALL", "masonry", "ledge", "architectural_detail", "detail", "RAW"};
    ui.label("SEMANTIC",8,y+8); y+=20;
    if(ui.button({8,y,28,18},"<")) semanticFamilyIndex_=(semanticFamilyIndex_+semanticFamilies.size()-1)%semanticFamilies.size();
    if(ui.button({38,y,116,18},semanticFamilies[semanticFamilyIndex_],true)){}
    if(ui.button({156,y,26,18},">")) semanticFamilyIndex_=(semanticFamilyIndex_+1)%semanticFamilies.size();
    rawPalette_=semanticFamilies[semanticFamilyIndex_]=="RAW"; y+=22;
    if(!semantics.stamps().empty()) {
        ui.label("STAMPS",8,y+8); y+=20;
        const auto& stamp=semantics.stamps()[std::min(selectedStamp_,semantics.stamps().size()-1)];
        if(ui.button({8,y,28,18},"<")) selectedStamp_=(selectedStamp_+semantics.stamps().size()-1)%semantics.stamps().size();
        if(ui.button({38,y,116,18},stamp.displayName,document_.activeTool()==EditorTool::stampPlace)) document_.activeTool()=EditorTool::stampPlace;
        if(ui.button({156,y,26,18},">")) selectedStamp_=(selectedStamp_+1)%semantics.stamps().size();
        y+=22;
    }
    const auto* selectedDefinition=content_.tilesets().find(selectedTileset_);
    if(selectedDefinition){
        ui.label("TILESET",8,y+8); y+=20;
        if(ui.button({8,y,28,18},"<"))selectTileset(-1);
        if(ui.button({38,y,116,18},selectedDefinition->displayName,true)){}
        if(ui.button({156,y,26,18},">"))selectTileset(1);
        y+=22;
    }
    if(const auto* tileset=selectedTilesetVisual()){const int paletteTop=std::max(y+24,viewportHeight-204);ui.label(rawPalette_?"RAW TILES":"SEMANTIC TILES",8,paletteTop-12);
        const int paletteHeight=std::max(18,viewportHeight-paletteTop);
        const int paletteColumns=std::max(1,std::min(10,static_cast<int>(selectedDefinition->columns)));
        std::vector<std::uint32_t> paletteTiles;
        for(std::uint32_t index=0;index<selectedDefinition->tileCount();++index){ const auto* semantic=semantics.findTile(selectedTileset_,index); if(rawPalette_ || (semantic && (semanticFamilies[semanticFamilyIndex_]=="ALL" || semantic->family==semanticFamilies[semanticFamilyIndex_]))) paletteTiles.push_back(index); }
        const int contentHeight=((static_cast<int>(paletteTiles.size())+paletteColumns-1)/paletteColumns)*17;
        const int maxScroll=std::max(0,contentHeight-paletteHeight);
        if(ui.pointerInside({0,paletteTop,leftPanelWidth,paletteHeight})&&input.pointer.wheelDelta)tilePaletteScroll_=std::clamp(tilePaletteScroll_-(input.pointer.wheelDelta/120)*17,0,maxScroll);
        for(std::size_t paletteIndex=0;paletteIndex<paletteTiles.size();++paletteIndex){const auto index=paletteTiles[paletteIndex];const int column=static_cast<int>(paletteIndex%static_cast<std::size_t>(paletteColumns)),row=static_cast<int>(paletteIndex/static_cast<std::size_t>(paletteColumns));const core::RectI cell{8+column*17,paletteTop+row*17-tilePaletteScroll_,16,16};if(cell.y+16<=paletteTop||cell.y>=viewportHeight)continue;
            render::Renderer2D renderer(*framebuffer_);const core::RectI source=tileset->atlas.sourceRect(index);renderer.drawImageRegion(*tileset->image,source,cell.x,cell.y);
            if(index==selectedTile_)outline(renderer,cell,selectedColor);if(ui.pointerInside(cell)&&input.pointer.leftPressed){selectedTile_=index;document_.activeTool()=EditorTool::tilePencil;}}
    } else if (selectedDefinition) { ui.label("Tileset image unavailable",8,y+4); }

    render::Renderer2D renderer(*framebuffer_);drawViewport(renderer,viewportBounds_,input);drawInspector(ui,right);
    ui.label(status_,6,viewportHeight+6);
    if(newMapDialog_)drawNewMapDialog(ui,input);
}

void EditorApp::drawViewport(render::Renderer2D& renderer,core::RectI viewport,const EditorInputState& input){
    renderer.fillRect(viewport,viewportBackground);drawMap(renderer,viewport);drawEntities(renderer,viewport);handleViewport(viewport,input);
}

void EditorApp::drawMap(render::Renderer2D& renderer,core::RectI viewport) const{
    const auto& data=document_.data();const int scaled=std::max(1,static_cast<int>(std::lround(data.tileSize*zoom())));
    for(std::size_t layerIndex=0;layerIndex<data.layers.size();++layerIndex){if(layerIndex>=document_.layerStates().size()||!document_.layerStates()[layerIndex].visible)continue;const auto& layer=data.layers[layerIndex];
        for(std::uint32_t y=0;y<data.height;++y)for(std::uint32_t x=0;x<data.width;++x){const auto cell=layer.cells[static_cast<std::size_t>(y)*data.width+x];if(!cell||*cell>=data.tileReferences.size())continue;const auto& reference=data.tileReferences[*cell];
            const auto* definition=content_.tilesets().find(reference.tilesetId);if(!definition||reference.sourceIndex>=definition->tileCount())continue;
            const auto* tileset=tilesetVisuals_.find(runtimeTilesets_.requireRuntimeId(reference.tilesetId));if(!tileset)continue;
            const auto screen=worldToScreen({static_cast<int>(x*data.tileSize),static_cast<int>(y*data.tileSize)},viewport);if(screen.x+scaled<viewport.x||screen.y+scaled<viewport.y||screen.x>=viewport.x+viewport.width||screen.y>=viewport.y+viewport.height)continue;
            renderer.drawImageRegionNearest(*tileset->image,tileset->atlas.sourceRect(reference.sourceIndex),{screen.x,screen.y,scaled,scaled},world::hasFlag(reference.flags,world::TileFlags::flipX));}}
    if(showCollision_){for(std::uint32_t y=0;y<data.height;++y)for(std::uint32_t x=0;x<data.width;++x){if(data.collision[static_cast<std::size_t>(y)*data.width+x]==0)continue;const auto screen=worldToScreen({static_cast<int>(x*data.tileSize),static_cast<int>(y*data.tileSize)},viewport);renderer.fillRect({screen.x,screen.y,scaled,scaled},collisionColor);}}
    if(document_.viewport().showGrid&&scaled>=4){for(std::uint32_t x=0;x<=data.width;++x){const auto p=worldToScreen({static_cast<int>(x*data.tileSize),0},viewport);renderer.fillRect({p.x,viewport.y,1,viewport.height},gridColor);}for(std::uint32_t y=0;y<=data.height;++y){const auto p=worldToScreen({0,static_cast<int>(y*data.tileSize)},viewport);renderer.fillRect({viewport.x,p.y,viewport.width,1},gridColor);}}
}

void EditorApp::drawEntities(render::Renderer2D& renderer,core::RectI viewport) const{
    const auto drawPoint=[&](SelectionKind kind,simulation::PersistentInstanceId id,core::WorldPointI point){if(drag_.kind==DragState::Kind::move&&document_.selection().kind==kind&&document_.selection().instanceId==id)point=drag_.worldCurrent;const auto p=worldToScreen(point,viewport);const int radius=std::max(3,static_cast<int>(4*zoom()));renderer.fillRect({p.x-radius,p.y-radius,radius*2+1,radius*2+1},categoryColor(kind));if(document_.selection().kind==kind&&document_.selection().instanceId==id)outline(renderer,{p.x-radius-2,p.y-radius-2,radius*2+5,radius*2+5},selectedColor);};
    for(const auto& value:document_.data().enemies)drawPoint(SelectionKind::enemy,value.id,value.position);
    for(const auto& value:document_.data().objects)drawPoint(SelectionKind::object,value.id,value.position);
    for(const auto& value:document_.data().pickups)drawPoint(SelectionKind::pickup,value.id,value.position);
    for(const auto& value:document_.data().playerSpawns){auto point=value.position;if(drag_.kind==DragState::Kind::move&&document_.selection().kind==SelectionKind::playerSpawn&&document_.selection().authoredId==value.id.value())point=drag_.worldCurrent;const auto p=worldToScreen(point,viewport);renderer.fillRect({p.x-5,p.y-5,11,11},categoryColor(SelectionKind::playerSpawn));}
    for(const auto& value:document_.data().links){core::WorldPointI point{value.trigger.x,value.trigger.y};if(drag_.kind==DragState::Kind::move&&document_.selection().kind==SelectionKind::mapLink&&document_.selection().authoredId==value.id)point=drag_.worldCurrent;const auto p=worldToScreen(point,viewport);const int w=std::max(2,static_cast<int>(value.trigger.width*zoom())),h=std::max(2,static_cast<int>(value.trigger.height*zoom()));outline(renderer,{p.x,p.y,w,h},categoryColor(SelectionKind::mapLink));}
    for(const auto& region:document_.regions()){world::AabbI bounds=region.bounds;if(drag_.kind==DragState::Kind::regionResize&&document_.selection().instanceId==region.id){bounds.width=std::max(1,drag_.worldCurrent.x-bounds.x);bounds.height=std::max(1,drag_.worldCurrent.y-bounds.y);}const auto p=worldToScreen({bounds.x,bounds.y},viewport);core::RectI screen{p.x,p.y,std::max(2,static_cast<int>(bounds.width*zoom())),std::max(2,static_cast<int>(bounds.height*zoom()))};outline(renderer,screen,document_.selection().kind==SelectionKind::region&&document_.selection().instanceId==region.id?selectedColor:categoryColor(SelectionKind::region));}
    if(drag_.kind==DragState::Kind::regionCreate){world::AabbI b{std::min(drag_.worldStart.x,drag_.worldCurrent.x),std::min(drag_.worldStart.y,drag_.worldCurrent.y),std::abs(drag_.worldCurrent.x-drag_.worldStart.x),std::abs(drag_.worldCurrent.y-drag_.worldStart.y)};const auto p=worldToScreen({b.x,b.y},viewport);outline(renderer,{p.x,p.y,std::max(1,static_cast<int>(b.width*zoom())),std::max(1,static_cast<int>(b.height*zoom()))},selectedColor);}
}

void EditorApp::handleViewport(core::RectI viewport,const EditorInputState& input){
    const bool inside=input.pointer.x>=viewport.x&&input.pointer.y>=viewport.y&&input.pointer.x<viewport.x+viewport.width&&input.pointer.y<viewport.y+viewport.height;
    const core::PointI pointer{input.pointer.x,input.pointer.y};const auto worldPoint=screenToWorld(pointer,viewport);const int tileSize=document_.data().tileSize;
    auto snapped=worldPoint;if(!input.alt){snapped.x=(snapped.x/tileSize)*tileSize;snapped.y=(snapped.y/tileSize)*tileSize;}
    if(input.homePressed)frameMap(viewport);
    if(inside&&input.pointer.wheelDelta!=0){const auto anchor=worldPoint;auto& step=document_.viewport().zoomStep;if(input.pointer.wheelDelta>0&&step+1<zoomSteps.size())++step;else if(input.pointer.wheelDelta<0&&step>0)--step;document_.viewport().worldX=anchor.x-(pointer.x-viewport.x)/zoom();document_.viewport().worldY=anchor.y-(pointer.y-viewport.y)/zoom();}
    if(inside&&(input.pointer.middlePressed||(input.space&&input.pointer.leftPressed))){drag_.kind=DragState::Kind::pan;drag_.pointerStart=pointer;drag_.worldStart={static_cast<int>(document_.viewport().worldX),static_cast<int>(document_.viewport().worldY)};}
    if(drag_.kind==DragState::Kind::pan){if(input.pointer.middleDown||(input.space&&input.pointer.leftDown)){document_.viewport().worldX=drag_.worldStart.x-(pointer.x-drag_.pointerStart.x)/zoom();document_.viewport().worldY=drag_.worldStart.y-(pointer.y-drag_.pointerStart.y)/zoom();}else drag_={};return;}
    if(!inside&&drag_.kind==DragState::Kind::none)return;
    const auto tile=worldPointToTile(document_.data(),worldPoint);
    if (!tile && drag_.kind==DragState::Kind::none) return;
    const auto tool=document_.activeTool();
    if(input.pointer.leftPressed&&inside){
        if((tool==EditorTool::tilePencil||tool==EditorTool::tileErase||tool==EditorTool::collisionPaint||tool==EditorTool::collisionErase) && tile){drag_.kind=DragState::Kind::brush;drag_.stroke={*tile};}
        else if((tool==EditorTool::tileRectangle||tool==EditorTool::collisionRectangle||tool==EditorTool::collisionRectangleErase) && tile){drag_.kind=DragState::Kind::rectangle;drag_.worldStart={static_cast<int>(tile->x),static_cast<int>(tile->y)};drag_.worldCurrent=drag_.worldStart;}
        else if((tool==EditorTool::tileFill && input.alt)||tool==EditorTool::tileEyedropper){if(tile){const auto index=static_cast<std::size_t>(tile->y)*document_.data().width+tile->x;const auto cell=document_.data().layers[document_.activeLayer()].cells[index];if(cell){const auto& ref=document_.data().tileReferences[*cell];selectedTileset_=ref.tilesetId;selectedTile_=ref.sourceIndex;tileFlipX_=world::hasFlag(ref.flags,world::TileFlags::flipX);tilePaletteScroll_=0;}if(tool==EditorTool::tileEyedropper)document_.activeTool()=EditorTool::tilePencil;}}
        else if(tool==EditorTool::tileFill && tile)execute(std::make_unique<PaintTilesCommand>(document_.activeLayer(),tileFloodCells(document_.data(),document_.activeLayer(),tile->x,tile->y),selectedTileReference()));
        else if((tool==EditorTool::collisionFill||tool==EditorTool::collisionFillErase) && tile)execute(std::make_unique<SetCollisionCommand>(collisionFloodCells(document_.data(),tile->x,tile->y),tool==EditorTool::collisionFill));
        else if(tool==EditorTool::stampPlace && tile && selectedStamp_<content_.authoringSemantics().stamps().size()) execute(std::make_unique<PlaceStampCommand>(document_.activeLayer(),content_.authoringSemantics().stamps()[selectedStamp_],*tile,content_.authoringSemantics()));
        else if(tool==EditorTool::entityPlace)placeSelected(snapped);
        else if(tool==EditorTool::regionCreate){drag_.kind=DragState::Kind::regionCreate;drag_.worldStart=snapped;drag_.worldCurrent=snapped;}
        else if(tool==EditorTool::select){auto hit=hitTest(worldPoint);if(hit){document_.selection()=*hit;if(hit->kind==SelectionKind::region){const auto it=std::find_if(document_.regions().begin(),document_.regions().end(),[&](const auto& r){return r.id==hit->instanceId;});if(it!=document_.regions().end()&&std::abs(worldPoint.x-(it->bounds.x+it->bounds.width))<8&&std::abs(worldPoint.y-(it->bounds.y+it->bounds.height))<8){drag_.kind=DragState::Kind::regionResize;drag_.regionStart=it->bounds;drag_.worldCurrent=worldPoint;return;}}if(hit->kind!=SelectionKind::none){drag_.kind=DragState::Kind::move;drag_.worldStart=worldPoint;drag_.worldCurrent=snapped;if(hit->kind==SelectionKind::region){auto it=std::find_if(document_.regions().begin(),document_.regions().end(),[&](const auto& r){return r.id==hit->instanceId;});drag_.entityStart={it->bounds.x,it->bounds.y};}else if(hit->kind==SelectionKind::playerSpawn){auto it=std::find_if(document_.data().playerSpawns.begin(),document_.data().playerSpawns.end(),[&](const auto& v){return v.id.value()==hit->authoredId;});drag_.entityStart=it->position;}else if(hit->kind==SelectionKind::mapLink){auto it=std::find_if(document_.data().links.begin(),document_.data().links.end(),[&](const auto& v){return v.id==hit->authoredId;});drag_.entityStart={it->trigger.x,it->trigger.y};}else{auto point=[&](){if(hit->kind==SelectionKind::enemy)return std::find_if(document_.data().enemies.begin(),document_.data().enemies.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;if(hit->kind==SelectionKind::object)return std::find_if(document_.data().objects.begin(),document_.data().objects.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;return std::find_if(document_.data().pickups.begin(),document_.data().pickups.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;}();drag_.entityStart=point;}}}else document_.selection().clear();}
    }
    if(drag_.kind==DragState::Kind::brush&&input.pointer.leftDown&&tile){if(std::none_of(drag_.stroke.begin(),drag_.stroke.end(),[&](auto v){return v.x==tile->x&&v.y==tile->y;}))drag_.stroke.push_back(*tile);}
    if(drag_.kind==DragState::Kind::rectangle||drag_.kind==DragState::Kind::regionCreate||drag_.kind==DragState::Kind::regionResize||drag_.kind==DragState::Kind::move){if(drag_.kind!=DragState::Kind::rectangle||tile)drag_.worldCurrent=(drag_.kind==DragState::Kind::rectangle)?core::WorldPointI{static_cast<int>(tile->x),static_cast<int>(tile->y)}:snapped;}
    if(input.pointer.leftReleased){
        if(drag_.kind==DragState::Kind::brush){if(tool==EditorTool::collisionPaint||tool==EditorTool::collisionErase)execute(std::make_unique<SetCollisionCommand>(drag_.stroke,tool==EditorTool::collisionPaint));else execute(std::make_unique<PaintTilesCommand>(document_.activeLayer(),drag_.stroke,tool==EditorTool::tileErase?std::nullopt:std::optional<maps::MapTileReference>{selectedTileReference()}));}
        else if(drag_.kind==DragState::Kind::rectangle){const auto cells=rectangleCells(drag_.worldStart.x,drag_.worldStart.y,drag_.worldCurrent.x,drag_.worldCurrent.y,document_.data());if(tool==EditorTool::collisionRectangle||tool==EditorTool::collisionRectangleErase)execute(std::make_unique<SetCollisionCommand>(cells,tool==EditorTool::collisionRectangle));else execute(std::make_unique<PaintTilesCommand>(document_.activeLayer(),cells,selectedTileReference()));}
        else if(drag_.kind==DragState::Kind::move&&drag_.worldCurrent!=drag_.entityStart)execute(std::make_unique<MoveEntityCommand>(document_.selection().kind,document_.selection().instanceId,drag_.entityStart,drag_.worldCurrent,document_.selection().authoredId));
        else if(drag_.kind==DragState::Kind::regionCreate){world::AabbI bounds{std::min(drag_.worldStart.x,drag_.worldCurrent.x),std::min(drag_.worldStart.y,drag_.worldCurrent.y),std::abs(drag_.worldCurrent.x-drag_.worldStart.x),std::abs(drag_.worldCurrent.y-drag_.worldStart.y)};if(bounds.width>0&&bounds.height>0){const auto id=document_.allocatePersistentId();execute(std::make_unique<PlaceEntityCommand>(RegionPlacement{id,"region."+std::to_string(id.value),bounds}));}}
        else if(drag_.kind==DragState::Kind::regionResize){world::AabbI after=drag_.regionStart;after.width=std::max(1,drag_.worldCurrent.x-after.x);after.height=std::max(1,drag_.worldCurrent.y-after.y);execute(std::make_unique<ResizeRegionCommand>(document_.selection().instanceId,drag_.regionStart,after));}
        drag_={};
    }
    if(input.deletePressed&&document_.selection().kind!=SelectionKind::none)execute(std::make_unique<DeleteEntityCommand>(document_.selection().kind,document_.selection().instanceId,document_.selection().authoredId));
    if(input.duplicatePressed){const auto& selection=document_.selection();if(selection.instanceId){const auto id=document_.allocatePersistentId();const auto copy=duplicatePlacement(document_,selection.kind,selection.instanceId,id,tileSize);if(copy){std::optional<PropertyOverrideSet> overrides;const auto found=document_.propertyOverrides().find(selection.instanceId.value);if(found!=document_.propertyOverrides().end())overrides=found->second;execute(std::make_unique<PlaceEntityCommand>(*copy,std::move(overrides)));}}else {const auto copy=duplicateAuthoredPlacement(document_,selection.kind,selection.authoredId,tileSize);if(copy)execute(std::make_unique<PlaceEntityCommand>(*copy));}}
    if(input.undoPressed)document_.undo();if(input.redoPressed){std::string error;if(!document_.redo(error))status_=error;}
}

std::optional<EditorSelection> EditorApp::hitTest(core::WorldPointI point) const{
    const auto near=[&](core::WorldPointI p){return std::abs(point.x-p.x)<=8&&std::abs(point.y-p.y)<=8;};
    for(auto it=document_.data().pickups.rbegin();it!=document_.data().pickups.rend();++it)if(near(it->position))return EditorSelection{SelectionKind::pickup,it->id,{}};
    for(auto it=document_.data().objects.rbegin();it!=document_.data().objects.rend();++it)if(near(it->position))return EditorSelection{SelectionKind::object,it->id,{}};
    for(auto it=document_.data().enemies.rbegin();it!=document_.data().enemies.rend();++it)if(near(it->position))return EditorSelection{SelectionKind::enemy,it->id,{}};
    for(auto it=document_.regions().rbegin();it!=document_.regions().rend();++it)if(point.x>=it->bounds.x&&point.y>=it->bounds.y&&point.x<it->bounds.x+it->bounds.width&&point.y<it->bounds.y+it->bounds.height)return EditorSelection{SelectionKind::region,it->id,it->regionId};
    for(auto it=document_.data().playerSpawns.rbegin();it!=document_.data().playerSpawns.rend();++it)if(near(it->position))return EditorSelection{SelectionKind::playerSpawn,{},std::string(it->id.value())};
    for(auto it=document_.data().links.rbegin();it!=document_.data().links.rend();++it)if(point.x>=it->trigger.x&&point.y>=it->trigger.y&&point.x<it->trigger.x+it->trigger.width&&point.y<it->trigger.y+it->trigger.height)return EditorSelection{SelectionKind::mapLink,{},it->id};
    return std::nullopt;
}

maps::MapTileReference EditorApp::selectedTileReference() const {
    return {selectedTileset_, selectedTile_,
            tileFlipX_ ? world::TileFlags::flipX : world::TileFlags::none};
}

const game::LoadedTilesetVisual* EditorApp::selectedTilesetVisual() const noexcept {
    const auto* definition = content_.tilesets().find(selectedTileset_);
    if (!definition) { return nullptr; }
    return tilesetVisuals_.find(runtimeTilesets_.requireRuntimeId(definition->id));
}

void EditorApp::selectTileset(int direction) {
    const auto& definitions = content_.tilesets().definitions();
    if (definitions.empty() || direction == 0) { return; }
    auto current = std::find_if(definitions.begin(), definitions.end(), [&](const auto& value) {
        return value.id == selectedTileset_;
    });
    std::size_t index = current == definitions.end() ? 0U :
        static_cast<std::size_t>(current - definitions.begin());
    const std::size_t count = definitions.size();
    index = direction > 0 ? (index + 1U) % count : (index + count - 1U) % count;
    selectedTileset_ = definitions[index].id;
    selectedTile_ = 0;
    tilePaletteScroll_ = 0;
    if (!selectedTilesetVisual()) { status_ = definitions[index].displayName + " image is unavailable"; }
}

void EditorApp::placeSelected(core::WorldPointI point){const auto id=document_.allocatePersistentId();
    if(selectedDefinition_.value()=="world.player_spawn"){const std::string name="spawn."+std::to_string(document_.data().playerSpawns.size()+1);execute(std::make_unique<PlaceEntityCommand>(maps::PlayerSpawn{simulation::SpawnId{name},point,game::gameplay::FacingDirection::down}));return;}
    if(selectedDefinition_.value()=="world.map_link"){const std::string targetSpawn=document_.data().playerSpawns.empty()?"missing":std::string(document_.data().playerSpawns.front().id.value());execute(std::make_unique<PlaceEntityCommand>(maps::MapLink{"link."+std::to_string(document_.data().links.size()+1),{point.x,point.y,document_.data().tileSize,document_.data().tileSize},document_.data().id,simulation::SpawnId{targetSpawn}}));return;}
    if(selectedCategory_==game::AuthoringCategory::enemy)execute(std::make_unique<PlaceEntityCommand>(maps::EnemyPlacement{id,selectedDefinition_,point,game::gameplay::FacingDirection::down}));
    else if(selectedCategory_==game::AuthoringCategory::object)execute(std::make_unique<PlaceEntityCommand>(maps::ObjectPlacement{id,selectedDefinition_,point,{}}));
    else if(const auto* pickup=content_.pickup(selectedDefinition_))execute(std::make_unique<PlaceEntityCommand>(maps::PickupPlacement{id,pickup->id,pickup->visualId,point,pickup->collectionBounds,pickup->payload}));
}

void EditorApp::drawInspector(EditorUiContext& ui,core::RectI panel){int y=10;ui.label("PROPERTIES",panel.x+8,y);y+=18;ui.label(std::string(document_.data().id.value()),panel.x+8,y);y+=18;
    if(const auto* semantic=content_.authoringSemantics().findTile(selectedTileset_,selectedTile_)) { ui.label("TILE: "+std::string(semantic->id.value()),panel.x+8,y); y+=14; ui.label(std::string(toString(semantic->role))+" / "+toString(semantic->topology),panel.x+8,y); y+=14; ui.label("FlipX "+std::string(semantic->flipXAllowed?"allowed":"not approved"),panel.x+8,y); y+=18; }
    else { ui.label("TILE: Unclassified (RAW)",panel.x+8,y); y+=18; }
    const auto& selection=document_.selection();if(selection.kind==SelectionKind::none){ui.label("No selection",panel.x+8,y);}else{ui.label("Selection",panel.x+8,y);y+=16;if(selection.instanceId)ui.label("ID "+std::to_string(selection.instanceId.value),panel.x+8,y);else ui.label(selection.authoredId,panel.x+8,y);y+=20;
        simulation::DefinitionId definition;for(const auto& enemy:document_.data().enemies)if(enemy.id==selection.instanceId)definition=enemy.definitionId;
    const auto schemas=propertySchemasFor(content_,definition);for(const auto& schema:schemas){ui.label(schema.displayName,panel.x+8,y);y+=14;std::int64_t value=std::get<std::int64_t>(schema.defaultValue);const auto outer=document_.propertyOverrides().find(selection.instanceId.value);bool overridden=false;if(outer!=document_.propertyOverrides().end()){const auto found=outer->second.find(schema.id);if(found!=outer->second.end()){value=std::get<std::int64_t>(found->second);overridden=true;}}
            ui.label(std::to_string(value)+(overridden?" *":""),panel.x+8,y+4);if(ui.button({panel.x+90,y,32,18},"-"))execute(std::make_unique<SetPropertyCommand>(selection.instanceId,schema,PropertyValue{value-1},content_));if(ui.button({panel.x+126,y,32,18},"+"))execute(std::make_unique<SetPropertyCommand>(selection.instanceId,schema,PropertyValue{value+1},content_));if(ui.button({panel.x+162,y,72,18},"RESET"))execute(std::make_unique<SetPropertyCommand>(selection.instanceId,schema,std::nullopt,content_));y+=24;}}
    y=panel.height-150;ui.label("VALIDATION",panel.x+8,y);y+=16;const auto report=document_.validate(content_); game::authoring::MapSemanticValidator semanticValidator; const auto semanticReport=semanticValidator.validate(document_.data(),content_.authoringSemantics());ui.label("Structural: "+std::to_string(report.errorCount())+" errors",panel.x+8,y);y+=14;ui.label("Semantic: "+std::to_string(semanticReport.warningCount())+" warnings",panel.x+8,y);y+=16;for(const auto& issue:report.issues){if(y>panel.height-12)break;ui.label(issue.message.substr(0,32),panel.x+8,y);y+=12;}for(const auto& issue:semanticReport.issues){if(y>panel.height-12)break;ui.label(issue.message.substr(0,32),panel.x+8,y);y+=12;}
}

void EditorApp::drawNewMapDialog(EditorUiContext& ui,const EditorInputState& input){const int width=360,height=220,x=(framebuffer_->width()-width)/2,y=(framebuffer_->height()-height)/2;ui.panel({x,y,width,height});ui.label("NEW MAP",x+12,y+12);std::array<std::string*,4> fields{&newMapId_,&newMapWidth_,&newMapHeight_,&newMapTileSize_};const std::array<const char*,4> names{"MapId","Width","Height","Tile Size"};
    for(int i=0;i<4;++i){const int fy=y+38+i*34;ui.label(names[static_cast<std::size_t>(i)],x+12,fy);if(ui.button({x+110,fy-4,230,24},*fields[static_cast<std::size_t>(i)],newMapField_==i))newMapField_=i;}
    if(!input.textInput.empty()){auto& field=*fields[static_cast<std::size_t>(newMapField_)];for(char c:input.textInput)if(c>=32&&c<127)field.push_back(c);}if(input.backspacePressed){auto& field=*fields[static_cast<std::size_t>(newMapField_)];if(!field.empty())field.pop_back();}
    const bool create=ui.button({x+110,y+184,100,24},"CREATE")||input.enterPressed;const bool cancel=ui.button({x+220,y+184,100,24},"CANCEL")||input.escapePressed;
    if(create){const int w=parsePositive(newMapWidth_),h=parsePositive(newMapHeight_),tile=parsePositive(newMapTileSize_);if(!newMapId_.empty()&&w>0&&h>0&&tile>0){try{document_=EditorDocument::newMap(simulation::MapId{newMapId_},static_cast<std::uint32_t>(w),static_cast<std::uint32_t>(h),static_cast<std::uint16_t>(tile));newMapDialog_=false;frameMap(viewportBounds_);}catch(const std::exception& e){status_=e.what();}}else status_="New Map fields are invalid";}if(cancel)newMapDialog_=false;
}

void EditorApp::frameMap(core::RectI viewport) noexcept{const double mapWidth=static_cast<double>(document_.data().width)*document_.data().tileSize,mapHeight=static_cast<double>(document_.data().height)*document_.data().tileSize;std::size_t best=0;for(std::size_t i=0;i<zoomSteps.size();++i)if(mapWidth*zoomSteps[i]<=viewport.width&&mapHeight*zoomSteps[i]<=viewport.height)best=i;document_.viewport().zoomStep=best;document_.viewport().worldX=(mapWidth-viewport.width/zoom())/2.0;document_.viewport().worldY=(mapHeight-viewport.height/zoom())/2.0;}

void EditorApp::execute(std::unique_ptr<EditorCommand> command){std::string error;if(!document_.execute(std::move(command),error))status_=error;}
void EditorApp::cancelActiveGesture() noexcept{drag_={};}
void EditorApp::shellCommand(EditorShellCommand command){if(command==EditorShellCommand::newMap)newMapDialog_=true;else if(command==EditorShellCommand::undo)document_.undo();else if(command==EditorShellCommand::redo){std::string error;if(!document_.redo(error))status_=error;}else if(command==EditorShellCommand::toggleGrid)document_.viewport().showGrid=!document_.viewport().showGrid;else frameMap(viewportBounds_);}
bool EditorApp::open(const std::filesystem::path& path,std::string& error){auto loaded=EditorDocument::open(path,content_,error);if(!loaded)return false;document_=std::move(*loaded);frameMap(viewportBounds_);return true;}
bool EditorApp::save(std::string& error){return document_.save(content_,error);}bool EditorApp::saveAs(const std::filesystem::path& path,std::string& error){return document_.saveAs(path,content_,error);}
std::string EditorApp::windowTitle() const{std::string title="Dungeon Underworld - Map Maker - ";title.append(document_.data().id.value());if(document_.dirty())title+=" *";return title;}
void EditorApp::updateStatus(core::RectI viewport,const EditorInputState& input){if(input.pointer.x>=viewport.x&&input.pointer.y>=viewport.y&&input.pointer.x<viewport.x+viewport.width&&input.pointer.y<viewport.y+viewport.height){const auto world=screenToWorld({input.pointer.x,input.pointer.y},viewport);std::ostringstream out;out<<"World "<<world.x<<','<<world.y<<"  Tile "<<world.x/document_.data().tileSize<<','<<world.y/document_.data().tileSize<<"  Zoom "<<static_cast<int>(zoom()*100)<<'%';status_=out.str();}}

} // namespace underworld::editor
