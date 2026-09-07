#include "editor/editor_app.h"

#include "engine/core/color_rgba8.h"
#include "engine/platform/image_decoder.h"
#include "engine/render/bitmap_font.h"
#include "engine/render/image.h"
#include "engine/render/renderer_2d.h"
#include "engine/render/sprite.h"
#include "game/content/builtin_content.h"
#include "game/content/content_source.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <sstream>
#include <utility>

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
constexpr std::size_t noContentIndex = std::numeric_limits<std::size_t>::max();

enum ContentEditField : int {
    fieldVisualImagePath,
    fieldStaticImageId,
    fieldStaticSourceXY,
    fieldStaticSourceWH,
    fieldStaticAnchorXY,
    fieldAnimationImageId,
    fieldAnimationFrameSourceXY,
    fieldAnimationFrameSourceWH,
    fieldAnimationFrameAnchorXY,
    fieldAnimationFrameOffsetXY,
    fieldAnimationFrameDuration,
    fieldAnimationMarker,
    fieldAnimationNewMarker,
    fieldEnemyIdleDefault,
    fieldEnemyIdleDown,
    fieldEnemyIdleUp,
    fieldEnemyIdleSide,
    fieldEnemyOptionalDefault,
    fieldEnemyOptionalDown,
    fieldEnemyOptionalUp,
    fieldEnemyOptionalSide,
    fieldEnemyActionDefault,
    fieldEnemyActionDown,
    fieldEnemyActionUp,
    fieldEnemyActionSide,
    fieldEnemyActionId,
    fieldObjectIdle,
    fieldObjectOpened,
    fieldObjectDestroying,
    fieldObjectActivationInactive,
    fieldObjectActivationActive,
    fieldObjectDoorLocked,
    fieldObjectDoorClosed,
    fieldObjectDoorOpen,
    fieldNpcMarker,
    fieldNpcIdleDefault,
    fieldNpcIdleDown,
    fieldNpcIdleUp,
    fieldNpcIdleSide,
    fieldPreviewGridOrigin,
    fieldPreviewGridCell
};

int parsePositive(const std::string& value) {
    try { const int parsed=std::stoi(value); return parsed>0?parsed:0; } catch (...) { return 0; }
}

template<std::size_t Count>
std::optional<std::array<int, Count>> parseIntegerList(std::string_view text) {
    std::istringstream stream{std::string(text)};
    std::array<int, Count> values{};
    for (std::size_t index = 0; index < Count; ++index) {
        if (!(stream >> values[index])) return std::nullopt;
        if (index + 1 < Count) {
            char separator{};
            if (!(stream >> separator) || separator != ',') return std::nullopt;
        }
    }
    stream >> std::ws;
    if (!stream.eof()) return std::nullopt;
    return values;
}

std::optional<std::uint32_t> parseUnsigned(std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::uint32_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

std::string pointText(core::PointI point) {
    return std::to_string(point.x) + "," + std::to_string(point.y);
}

std::string rectPositionText(core::RectI rect) {
    return std::to_string(rect.x) + "," + std::to_string(rect.y);
}

std::string rectSizeText(core::RectI rect) {
    return std::to_string(rect.width) + "," + std::to_string(rect.height);
}

core::ColorRGBA8 categoryColor(SelectionKind kind) noexcept {
    if(kind==SelectionKind::enemy)return {220,75,75,255};
    if(kind==SelectionKind::npc)return {75,180,235,255};
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

EditorDocument initialDocument(const game::GameContentRegistry& content) {
    if (content.authoringSemantics().findTile(
            simulation::DefinitionId{"tile.dungeon.masonry.39"}) != nullptr) {
        return EditorDocument::newAuthoredMap(
            simulation::MapId{"map.untitled"}, 32, 24, 16, content, true);
    }
    // A semantically invalid external workspace still needs an editable shell so
    // its diagnostics can be repaired. This is an empty document, not builtin
    // content fallback; map authoring remains unavailable until the workspace is valid.
    return EditorDocument::newMap(simulation::MapId{"map.untitled"}, 32, 24, 16, true);
}
}

EditorApp::EditorApp(platform::ImageDecoder& decoder, const std::filesystem::path& assetRoot,
                     game::GameContentRegistry content,
                     std::optional<ContentWorkspaceDocument> contentWorkspace)
    : content_(std::move(content)),
      document_(initialDocument(content_)),
      contentWorkspace_(std::move(contentWorkspace)),
      decoder_(&decoder),
      assetRoot_(assetRoot),
      visualPreview_(decoder, assetRoot),
      framebuffer_(std::make_unique<render::Framebuffer>(1000,700)) {
    if (!contentWorkspace_) contentWorkspace_ = ContentWorkspaceDocument::fromBuiltin(
        game::content::makeBuiltinAuthoredContent());
    if (!contentWorkspace_->valid()) status_ = "Content workspace invalid; map compile/playtest unavailable";
    refreshContentRegistry();
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

    if (contentMode_) {
        drawContentShell(ui, input, left, {leftPanelWidth, 0, viewportWidth, viewportHeight}, right, status);
        return;
    }

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
    if(ui.button({97,y,85,18},"TILE",document_.activeTool()==EditorTool::tilePencil)) { document_.activeTool()=EditorTool::tilePencil; }
    y+=20;
    if(ui.button({8,y,85,18},"ERASE",document_.activeTool()==EditorTool::tileErase))document_.activeTool()=EditorTool::tileErase;
    if(ui.button({97,y,85,18},"RECT",document_.activeTool()==EditorTool::tileRectangle)) { document_.activeTool()=EditorTool::tileRectangle; }
    y+=20;
    if(ui.button({8,y,85,18},"FILL",document_.activeTool()==EditorTool::tileFill))document_.activeTool()=EditorTool::tileFill;
    if(ui.button({97,y,85,18},tileFlipX_?"FLIP X ON":"FLIP X",tileFlipX_)) { tileFlipX_=!tileFlipX_; }
    y+=20;
    if(ui.button({8,y,85,18},"PICK",document_.activeTool()==EditorTool::tileEyedropper)) { document_.activeTool()=EditorTool::tileEyedropper; }
    y+=20;
    if(ui.button({8,y,85,18},"COLL +",document_.activeTool()==EditorTool::collisionPaint))document_.activeTool()=EditorTool::collisionPaint;
    if(ui.button({97,y,85,18},"COLL -",document_.activeTool()==EditorTool::collisionErase)) { document_.activeTool()=EditorTool::collisionErase; }
    y+=24;
    if(ui.button({8,y,85,18},"COLL R+",document_.activeTool()==EditorTool::collisionRectangle))document_.activeTool()=EditorTool::collisionRectangle;
    if(ui.button({97,y,85,18},"COLL R-",document_.activeTool()==EditorTool::collisionRectangleErase)) { document_.activeTool()=EditorTool::collisionRectangleErase; }
    y+=20;
    if(ui.button({8,y,85,18},"COLL F+",document_.activeTool()==EditorTool::collisionFill))document_.activeTool()=EditorTool::collisionFill;
    if(ui.button({97,y,85,18},"COLL F-",document_.activeTool()==EditorTool::collisionFillErase)) { document_.activeTool()=EditorTool::collisionFillErase; }
    y+=24;

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
    if(ui.button({8,y,174,18},"Region",document_.activeTool()==EditorTool::regionCreate))document_.activeTool()=EditorTool::regionCreate;
    y += 20;
    if (ui.button({8, y, 174, 18}, playtest_.active() ? "STOP PLAYTEST" : "PLAYTEST",
                  playtest_.active())) {
        togglePlaytest();
    }

    const auto& semantics = content_.authoringSemantics();
    static const std::array<std::string, 6> semanticFamilies{"ALL", "masonry", "ledge", "architectural_detail", "detail", "RAW"};
    ui.label("DATASET TILES",8,y+8); y+=20;
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
            if(index==selectedTile_) { outline(renderer,cell,selectedColor); }
            if(ui.pointerInside(cell)&&input.pointer.leftPressed){selectedTile_=index;document_.activeTool()=EditorTool::tilePencil;}}
    } else if (selectedDefinition) { ui.label("Tileset image unavailable",8,y+4); }

    render::Renderer2D renderer(*framebuffer_);drawViewport(renderer,viewportBounds_,input);drawInspector(ui,right);
    ui.label(status_,6,viewportHeight+6);
    if(newMapDialog_)drawNewMapDialog(ui,input);
}

void EditorApp::drawContentShell(EditorUiContext& ui, const EditorInputState& input,
                                  core::RectI left, core::RectI center, core::RectI right,
                                  core::RectI status) {
    if (input.escapePressed) {
        resetContentEditState();
        previewRectangleDragging_ = false;
        previewPanning_ = false;
    }
    if (visualValidationAttempted_ && contentWorkspace_ &&
        visualValidationRevision_ != contentWorkspace_->revision()) {
        visualValidationAttempted_ = false;
        visualDiagnostics_.clear();
    }
    if (ui.button({left.x + 8, 8, 78, 20}, "MAP")) {
        resetContentEditState();
        resetContentPreviewState();
        contentMode_ = false;
        status_ = "Map mode";
        return;
    }
    (void)ui.button({left.x + 92, 8, 82, 20}, "CONTENT", true);
    ui.label("CATEGORIES", left.x + 8, 38);
    const auto& order = ContentWorkspaceDocument::categoryOrder();
    const int listTop = 54;
    const int rowHeight = 18;
    const int listHeight = std::max(1, left.height - listTop - 8);
    const int maxScroll = std::max(0, static_cast<int>(order.size() * rowHeight) - listHeight);
    if (ui.pointerInside({left.x, left.y + listTop, left.width, listHeight}) && input.pointer.wheelDelta != 0) {
        contentCategoryScroll_ = std::clamp(contentCategoryScroll_ - (input.pointer.wheelDelta / 120) * rowHeight,
                                             0, maxScroll);
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        const int y = listTop + static_cast<int>(i) * rowHeight - contentCategoryScroll_;
        if (y + rowHeight <= listTop || y >= left.height) continue;
        const auto kind = order[i];
        if (ui.button({left.x + 8, y, left.width - 16, 16},
                      ContentWorkspaceDocument::categoryName(kind), selectedContentCategory_ == kind)) {
            selectedContentCategory_ = kind;
            selectedContentDefinition_.reset();
            resetContentEditState();
            resetContentPreviewState();
        }
    }

    ui.label("DEFINITIONS", center.x + 8, 38);
    const auto allDefinitions = contentWorkspace_->index();
    int definitionY = 54;
    const int definitionBottom = center.y + std::min(center.height - 24, 126);
    for (const auto& key : allDefinitions) {
        if (key.kind != selectedContentCategory_) continue;
        if (definitionY + 18 >= definitionBottom) break;
        if (ui.button({center.x + 8, definitionY, center.width - 16, 16},
                      key.id.value(), selectedContentDefinition_ == key)) {
            selectedContentDefinition_ = key;
            resetContentEditState();
            resetContentPreviewState();
        }
        definitionY += 18;
    }
    if (definitionY == 54) ui.label("No definitions", center.x + 8, definitionY);
    const int previewTop = std::max(definitionBottom + 8, definitionY + 8);
    drawContentPreview(ui, input,
                       {center.x + 8, previewTop, center.width - 16,
                        std::max(1, center.height - previewTop - 8)});

    ui.label("INSPECTOR", right.x + 8, 10);
    if (!selectedContentDefinition_) {
        ui.label("Select a definition", right.x + 8, 30);
    } else {
        const auto& key = *selectedContentDefinition_;
        ui.label(std::string(key.id.value()), right.x + 8, 30);
        if (const auto* source = contentWorkspace_->sourceFor(key)) {
            ui.label("Source:", right.x + 8, 52);
            ui.label(source->sourcePath.filename().string(), right.x + 8, 66);
            if (!source->jsonPath.empty()) ui.label(source->jsonPath, right.x + 8, 80);
        }
        ui.label(contentWorkspace_->writable() ? "Typed document API: editable" : "Builtin content - read only",
                 right.x + 8, 104);
        int inspectorY = 124;
        if (key.kind == ContentDefinitionKind::visualImage) {
            const auto* pack = contentWorkspace_->mergedAuthored() ? &*contentWorkspace_->mergedAuthored() : nullptr;
            if (pack) {
                const auto authored = std::find_if(pack->visualImages.begin(), pack->visualImages.end(),
                                                   [&](const auto& value) { return value.id == key.id; });
                if (authored != pack->visualImages.end()) {
                    if (contentEditKey_ != key) {
                        resetContentEditState();
                        contentEditKey_ = key;
                        contentEditValues_[fieldVisualImagePath] = authored->relativePath;
                    }
                    ui.label("relativePath", right.x + 8, inspectorY);
                    if (ui.textField({right.x + 8, inspectorY + 14, right.width - 16, 20},
                                     contentEditValues_[fieldVisualImagePath],
                                     contentFocusedField_ == fieldVisualImagePath)) {
                        contentFocusedField_ = fieldVisualImagePath;
                    }
                    if (ui.button({right.x + 8, inspectorY + 40, 112, 18},
                                  authored->root == game::presentation::VisualAssetRoot::gameAssets
                                      ? "gameAssets" : "contentWorkspace",
                                  false) && contentWorkspace_->writable()) {
                        auto updated = *authored;
                        updated.root = authored->root == game::presentation::VisualAssetRoot::gameAssets
                            ? game::presentation::VisualAssetRoot::contentWorkspace
                            : game::presentation::VisualAssetRoot::gameAssets;
                        std::string editError;
                        if (!contentWorkspace_->updateVisualImage(key.id, updated, editError)) status_ = editError;
                        else { refreshContentRegistry(); return; }
                    }
                    if (input.enterPressed && contentFocusedField_ == fieldVisualImagePath &&
                        contentWorkspace_->writable()) {
                        auto updated = *authored;
                        updated.relativePath = contentEditValues_[fieldVisualImagePath];
                        std::string editError;
                        if (contentWorkspace_->updateVisualImage(key.id, updated, editError)) {
                            refreshContentRegistry();
                            status_ = "Visual image updated";
                            return;
                        }
                        status_ = editError;
                    }
                }
            }
        } else if (key.kind == ContentDefinitionKind::staticSprite) {
            const auto* pack = contentWorkspace_->mergedAuthored() ? &*contentWorkspace_->mergedAuthored() : nullptr;
            if (pack) {
                const auto value = std::find_if(pack->staticSprites.begin(), pack->staticSprites.end(),
                                                [&](const auto& item) { return item.id == key.id; });
                if (value != pack->staticSprites.end()) {
                    if (contentEditKey_ != key) {
                        resetContentEditState();
                        contentEditKey_ = key;
                        contentEditValues_[fieldStaticImageId] = std::string(value->imageId.value());
                        contentStaticSourceEnabled_ = value->source.has_value();
                        const auto source = value->source.value_or(core::RectI{});
                        contentEditValues_[fieldStaticSourceXY] = rectPositionText(source);
                        contentEditValues_[fieldStaticSourceWH] = rectSizeText(source);
                        contentEditValues_[fieldStaticAnchorXY] = pointText(value->anchor);
                    }
                    ui.label("imageId", right.x + 8, inspectorY);
                    if (ui.textField({right.x + 8, inspectorY + 14, right.width - 16, 20},
                                     contentEditValues_[fieldStaticImageId],
                                     contentFocusedField_ == fieldStaticImageId)) {
                        contentFocusedField_ = fieldStaticImageId;
                    }
                    ui.label("source rectangle", right.x + 8, inspectorY + 40);
                    if (ui.button({right.x + 8, inspectorY + 54, 112, 18},
                                  contentStaticSourceEnabled_ ? "SOURCE ON" : "SOURCE OFF",
                                  contentStaticSourceEnabled_) && contentWorkspace_->writable()) {
                        auto updated = *value;
                        if (updated.source) updated.source.reset();
                        else updated.source = core::RectI{0, 0, 1, 1};
                        std::string editError;
                        if (!contentWorkspace_->updateStaticSprite(key.id, updated, editError)) status_ = editError;
                        else { refreshContentRegistry(); return; }
                    }
                    if (contentStaticSourceEnabled_) {
                        ui.label("source x,y", right.x + 8, inspectorY + 78);
                        if (ui.textField({right.x + 8, inspectorY + 92, right.width - 16, 20},
                                         contentEditValues_[fieldStaticSourceXY],
                                         contentFocusedField_ == fieldStaticSourceXY)) {
                            contentFocusedField_ = fieldStaticSourceXY;
                        }
                        ui.label("source w,h", right.x + 8, inspectorY + 116);
                        if (ui.textField({right.x + 8, inspectorY + 130, right.width - 16, 20},
                                         contentEditValues_[fieldStaticSourceWH],
                                         contentFocusedField_ == fieldStaticSourceWH)) {
                            contentFocusedField_ = fieldStaticSourceWH;
                        }
                    } else {
                        ui.label("disabled: full decoded image", right.x + 8, inspectorY + 80);
                    }
                    const int anchorY = inspectorY + (contentStaticSourceEnabled_ ? 154 : 104);
                    ui.label("anchor x,y", right.x + 8, anchorY);
                    if (ui.textField({right.x + 8, anchorY + 14, right.width - 16, 20},
                                     contentEditValues_[fieldStaticAnchorXY],
                                     contentFocusedField_ == fieldStaticAnchorXY)) {
                        contentFocusedField_ = fieldStaticAnchorXY;
                    }
                    if (input.enterPressed && contentFocusedField_ >= fieldStaticImageId &&
                        contentFocusedField_ <= fieldStaticAnchorXY && contentWorkspace_->writable()) {
                        auto updated = *value;
                        std::string editError;
                        bool accepted = false;
                        if (contentFocusedField_ == fieldStaticImageId) {
                            updated.imageId = simulation::DefinitionId{contentEditValues_[fieldStaticImageId]};
                            accepted = contentWorkspace_->updateStaticSprite(key.id, updated, editError);
                        } else if (!contentStaticSourceEnabled_) {
                            editError = "source is disabled; enable it before editing source fields";
                        } else if (contentFocusedField_ == fieldStaticSourceXY) {
                            const auto parsed = parseIntegerList<2>(contentEditValues_[fieldStaticSourceXY]);
                            if (parsed) {
                                if (!updated.source) updated.source = core::RectI{0, 0, 1, 1};
                                updated.source->x = (*parsed)[0];
                                updated.source->y = (*parsed)[1];
                                accepted = contentWorkspace_->updateStaticSprite(key.id, updated, editError);
                            } else editError = "source x,y must contain two comma-separated integers";
                        } else if (contentFocusedField_ == fieldStaticSourceWH) {
                            const auto parsed = parseIntegerList<2>(contentEditValues_[fieldStaticSourceWH]);
                            if (parsed) {
                                if (!updated.source) updated.source = core::RectI{0, 0, 1, 1};
                                updated.source->width = (*parsed)[0];
                                updated.source->height = (*parsed)[1];
                                accepted = contentWorkspace_->updateStaticSprite(key.id, updated, editError);
                            } else editError = "source w,h must contain two comma-separated integers";
                        } else if (const auto parsed = parseIntegerList<2>(contentEditValues_[fieldStaticAnchorXY])) {
                            updated.anchor = {(*parsed)[0], (*parsed)[1]};
                            accepted = contentWorkspace_->updateStaticSprite(key.id, updated, editError);
                        } else editError = "anchor must contain two comma-separated integers";
                        if (accepted) {
                            refreshContentRegistry();
                            status_ = "Static sprite updated";
                            return;
                        } else if (!editError.empty()) status_ = editError;
                    }
                }
            }
        } else if (key.kind == ContentDefinitionKind::animation) {
            const auto* pack = contentWorkspace_->mergedAuthored() ? &*contentWorkspace_->mergedAuthored() : nullptr;
            if (pack) {
                const auto value = std::find_if(pack->animations.begin(), pack->animations.end(),
                                                [&](const auto& item) { return item.id == key.id; });
                if (value != pack->animations.end()) {
                    if (contentEditKey_ != key) {
                        resetContentEditState();
                        contentEditKey_ = key;
                        selectedAnimationFrameIndex_ = 0;
                        selectedAnimationMarkerIndex_ = 0;
                        contentFrameScroll_ = 0;
                        contentMarkerScroll_ = 0;
                        contentEditValues_[fieldAnimationImageId] = std::string(value->imageId.value());
                    }
                    if (!value->frames.empty()) {
                        selectedAnimationFrameIndex_ = std::min(selectedAnimationFrameIndex_, value->frames.size() - 1);
                    } else {
                        selectedAnimationFrameIndex_ = 0;
                    }
                    const auto frameIndex = value->frames.empty() ? noContentIndex : selectedAnimationFrameIndex_;
                    if (frameIndex != noContentIndex && contentEditFrame_ != frameIndex) {
                        contentFocusedField_ = -1;
                        contentEditFrame_ = frameIndex;
                        const auto& frame = value->frames[frameIndex];
                        contentEditValues_[fieldAnimationFrameSourceXY] = rectPositionText(frame.source);
                        contentEditValues_[fieldAnimationFrameSourceWH] = rectSizeText(frame.source);
                        contentEditValues_[fieldAnimationFrameAnchorXY] = pointText(frame.anchor);
                        contentEditValues_[fieldAnimationFrameOffsetXY] = pointText(frame.drawOffset);
                        contentEditValues_[fieldAnimationFrameDuration] = std::to_string(frame.durationTicks);
                        selectedAnimationMarkerIndex_ = frame.markers.empty() ? 0 :
                            std::min(selectedAnimationMarkerIndex_, frame.markers.size() - 1);
                        contentEditMarker_ = noContentIndex;
                    }
                    ui.label("imageId", right.x + 8, inspectorY);
                    if (ui.textField({right.x + 8, inspectorY + 14, right.width - 16, 20},
                                     contentEditValues_[fieldAnimationImageId],
                                     contentFocusedField_ == fieldAnimationImageId)) {
                        contentFocusedField_ = fieldAnimationImageId;
                    }
                    if (ui.button({right.x + 8, inspectorY + 40, 92, 18},
                                  value->loop ? "LOOP ON" : "LOOP OFF", value->loop) &&
                        contentWorkspace_->writable()) {
                        auto updated = *value;
                        updated.loop = !updated.loop;
                        std::string editError;
                        if (!contentWorkspace_->updateAnimation(key.id, updated, editError)) status_ = editError;
                        else { refreshContentRegistry(); status_ = "Animation loop updated"; return; }
                    }
                    if (input.enterPressed && contentFocusedField_ == fieldAnimationImageId &&
                        contentWorkspace_->writable()) {
                        auto updated = *value;
                        updated.imageId = simulation::DefinitionId{contentEditValues_[fieldAnimationImageId]};
                        std::string editError;
                        if (contentWorkspace_->updateAnimation(key.id, updated, editError)) {
                            refreshContentRegistry();
                            status_ = "Animation updated";
                            return;
                        } else status_ = editError;
                    }
                    const int frameListTop = inspectorY + 66;
                    ui.label("FRAMES (authored order)", right.x + 8, frameListTop);
                    const int frameRows = 5;
                    const int frameRowTop = frameListTop + 14;
                    const int maxFrameScroll = std::max(0, static_cast<int>(value->frames.size()) - frameRows);
                    if (ui.pointerInside({right.x, frameRowTop, right.width, frameRows * 18}) &&
                        input.pointer.wheelDelta != 0) {
                        contentFrameScroll_ = std::clamp(contentFrameScroll_ - input.pointer.wheelDelta / 120,
                                                         0, maxFrameScroll);
                    }
                    for (int row = 0; row < frameRows; ++row) {
                        const auto index = contentFrameScroll_ + static_cast<std::size_t>(row);
                        if (index >= value->frames.size()) break;
                        if (ui.button({right.x + 8, frameRowTop + row * 18, right.width - 16, 16},
                                      "Frame " + std::to_string(index), index == frameIndex)) {
                            selectedAnimationFrameIndex_ = index;
                            selectedAnimationMarkerIndex_ = 0;
                            resetContentEditState();
                        }
                    }
                    const int frameButtonsY = frameRowTop + frameRows * 18 + 2;
                    if (ui.button({right.x + 8, frameButtonsY, 82, 18}, "ADD FRAME") &&
                        contentWorkspace_->writable()) {
                        const auto newIndex = value->frames.size();
                        std::string editError;
                        if (!contentWorkspace_->addAnimationFrame(key.id,
                                game::content::AuthoredAnimationFrame{{0, 0, 1, 1}, {0, 0}, {0, 0}, 1, {}},
                                editError)) status_ = editError;
                        else { selectedAnimationFrameIndex_ = newIndex; refreshContentRegistry(); status_ = "Animation frame added"; return; }
                    }
                    if (ui.button({right.x + 96, frameButtonsY, 82, 18}, "REMOVE FRAME") &&
                        contentWorkspace_->writable() && !value->frames.empty()) {
                        const auto removed = std::min(selectedAnimationFrameIndex_, value->frames.size() - 1);
                        const auto next = removed == 0 ? 0 : removed - 1;
                        std::string editError;
                        if (!contentWorkspace_->removeAnimationFrame(key.id, removed, editError)) status_ = editError;
                        else { selectedAnimationFrameIndex_ = next; refreshContentRegistry(); status_ = "Animation frame removed"; return; }
                    }
                    if (ui.button({right.x + 184, frameButtonsY, 32, 18}, "UP") &&
                        frameIndex != noContentIndex && frameIndex > 0 && contentWorkspace_->writable()) {
                        std::string editError;
                        if (!contentWorkspace_->moveAnimationFrame(key.id, frameIndex, frameIndex - 1, editError)) status_ = editError;
                        else { --selectedAnimationFrameIndex_; refreshContentRegistry(); status_ = "Animation frame moved up"; return; }
                    }
                    if (ui.button({right.x + 184, frameButtonsY + 22, 32, 18}, "DOWN") &&
                        frameIndex != noContentIndex && frameIndex + 1 < value->frames.size() && contentWorkspace_->writable()) {
                        std::string editError;
                        if (!contentWorkspace_->moveAnimationFrame(key.id, frameIndex, frameIndex + 1, editError)) status_ = editError;
                        else { ++selectedAnimationFrameIndex_; refreshContentRegistry(); status_ = "Animation frame moved down"; return; }
                    }
                    if (frameIndex != noContentIndex) {
                        const auto& frame = value->frames[frameIndex];
                        const int frameY = frameButtonsY + 44;
                        ui.label("source x,y", right.x + 8, frameY);
                        if (ui.textField({right.x + 8, frameY + 12, right.width - 16, 20},
                                         contentEditValues_[fieldAnimationFrameSourceXY],
                                         contentFocusedField_ == fieldAnimationFrameSourceXY)) {
                            contentFocusedField_ = fieldAnimationFrameSourceXY;
                        }
                        ui.label("source w,h", right.x + 8, frameY + 36);
                        if (ui.textField({right.x + 8, frameY + 48, right.width - 16, 20},
                                         contentEditValues_[fieldAnimationFrameSourceWH],
                                         contentFocusedField_ == fieldAnimationFrameSourceWH)) {
                            contentFocusedField_ = fieldAnimationFrameSourceWH;
                        }
                        ui.label("anchor x,y", right.x + 8, frameY + 72);
                        if (ui.textField({right.x + 8, frameY + 84, right.width - 16, 20},
                                         contentEditValues_[fieldAnimationFrameAnchorXY],
                                         contentFocusedField_ == fieldAnimationFrameAnchorXY)) {
                            contentFocusedField_ = fieldAnimationFrameAnchorXY;
                        }
                        ui.label("drawOffset x,y", right.x + 8, frameY + 108);
                        if (ui.textField({right.x + 8, frameY + 120, right.width - 16, 20},
                                         contentEditValues_[fieldAnimationFrameOffsetXY],
                                         contentFocusedField_ == fieldAnimationFrameOffsetXY)) {
                            contentFocusedField_ = fieldAnimationFrameOffsetXY;
                        }
                        ui.label("durationTicks", right.x + 8, frameY + 144);
                        if (ui.textField({right.x + 8, frameY + 156, right.width - 16, 20},
                                         contentEditValues_[fieldAnimationFrameDuration],
                                         contentFocusedField_ == fieldAnimationFrameDuration)) {
                            contentFocusedField_ = fieldAnimationFrameDuration;
                        }
                        if (input.enterPressed && contentFocusedField_ >= fieldAnimationFrameSourceXY &&
                            contentFocusedField_ <= fieldAnimationFrameDuration && contentWorkspace_->writable()) {
                            auto updated = frame;
                            std::string editError;
                            bool accepted = false;
                            if (contentFocusedField_ == fieldAnimationFrameSourceXY) {
                                if (const auto parsed = parseIntegerList<2>(contentEditValues_[fieldAnimationFrameSourceXY])) {
                                    updated.source.x = (*parsed)[0]; updated.source.y = (*parsed)[1]; accepted = true;
                                } else editError = "frame source x,y must contain two comma-separated integers";
                            } else if (contentFocusedField_ == fieldAnimationFrameSourceWH) {
                                if (const auto parsed = parseIntegerList<2>(contentEditValues_[fieldAnimationFrameSourceWH])) {
                                    updated.source.width = (*parsed)[0]; updated.source.height = (*parsed)[1]; accepted = true;
                                } else editError = "frame source w,h must contain two comma-separated integers";
                            } else if (contentFocusedField_ == fieldAnimationFrameAnchorXY) {
                                if (const auto parsed = parseIntegerList<2>(contentEditValues_[fieldAnimationFrameAnchorXY])) {
                                    updated.anchor = {(*parsed)[0], (*parsed)[1]}; accepted = true;
                                } else editError = "frame anchor x,y must contain two comma-separated integers";
                            } else if (contentFocusedField_ == fieldAnimationFrameOffsetXY) {
                                if (const auto parsed = parseIntegerList<2>(contentEditValues_[fieldAnimationFrameOffsetXY])) {
                                    updated.drawOffset = {(*parsed)[0], (*parsed)[1]}; accepted = true;
                                } else editError = "frame drawOffset x,y must contain two comma-separated integers";
                            } else if (const auto parsed = parseUnsigned(contentEditValues_[fieldAnimationFrameDuration])) {
                                updated.durationTicks = *parsed; accepted = true;
                            } else editError = "frame durationTicks must be an unsigned 32-bit integer";
                            if (accepted && !contentWorkspace_->updateAnimationFrame(key.id, frameIndex, updated, editError)) accepted = false;
                            if (accepted) { refreshContentRegistry(); status_ = "Animation frame updated"; return; }
                            if (!editError.empty()) status_ = editError;
                        }
                        const int markerY = frameY + 184;
                        ui.label("MARKERS", right.x + 8, markerY);
                        const int markerRows = 3;
                        const int markerTop = markerY + 14;
                        const int maxMarkerScroll = std::max(0, static_cast<int>(frame.markers.size()) - markerRows);
                        if (ui.pointerInside({right.x, markerTop, right.width, markerRows * 18}) &&
                            input.pointer.wheelDelta != 0) {
                            contentMarkerScroll_ = std::clamp(contentMarkerScroll_ - input.pointer.wheelDelta / 120,
                                                              0, maxMarkerScroll);
                        }
                        for (int row = 0; row < markerRows; ++row) {
                            const auto markerIndex = contentMarkerScroll_ + static_cast<std::size_t>(row);
                            if (markerIndex >= frame.markers.size()) break;
                            if (ui.button({right.x + 8, markerTop + row * 18, right.width - 16, 16},
                                          "Marker " + std::to_string(markerIndex),
                                          markerIndex == selectedAnimationMarkerIndex_)) {
                                selectedAnimationMarkerIndex_ = markerIndex;
                                resetContentEditState();
                            }
                        }
                        const auto markerIndex = frame.markers.empty() ? noContentIndex :
                            std::min(selectedAnimationMarkerIndex_, frame.markers.size() - 1);
                        if (markerIndex != noContentIndex && contentEditMarker_ != markerIndex) {
                            contentFocusedField_ = -1;
                            contentEditMarker_ = markerIndex;
                            contentEditValues_[fieldAnimationMarker] = frame.markers[markerIndex];
                        }
                        const int markerEditY = markerTop + markerRows * 18 + 2;
                        ui.label("selected marker", right.x + 8, markerEditY);
                        if (ui.textField({right.x + 8, markerEditY + 12, right.width - 16, 20},
                                         contentEditValues_[fieldAnimationMarker],
                                         contentFocusedField_ == fieldAnimationMarker)) {
                            contentFocusedField_ = fieldAnimationMarker;
                        }
                        if (ui.button({right.x + 8, markerEditY + 36, 92, 18}, "REMOVE MARKER") &&
                            markerIndex != noContentIndex && contentWorkspace_->writable()) {
                            std::string editError;
                            if (!contentWorkspace_->removeAnimationMarker(key.id, frameIndex, markerIndex, editError)) status_ = editError;
                            else {
                                selectedAnimationMarkerIndex_ = markerIndex == 0 ? 0 : markerIndex - 1;
                                refreshContentRegistry(); status_ = "Animation marker removed"; return;
                            }
                        }
                        if (input.enterPressed && contentFocusedField_ == fieldAnimationMarker &&
                            markerIndex != noContentIndex && contentWorkspace_->writable()) {
                            std::string editError;
                            if (!contentWorkspace_->updateAnimationMarker(key.id, frameIndex, markerIndex,
                                    contentEditValues_[fieldAnimationMarker], editError)) status_ = editError;
                            else { refreshContentRegistry(); status_ = "Animation marker updated"; return; }
                        }
                        ui.label("new marker", right.x + 8, markerEditY + 58);
                        if (ui.textField({right.x + 8, markerEditY + 70, right.width - 16, 20},
                                         contentEditValues_[fieldAnimationNewMarker],
                                         contentFocusedField_ == fieldAnimationNewMarker)) {
                            contentFocusedField_ = fieldAnimationNewMarker;
                        }
                        if (ui.button({right.x + 8, markerEditY + 94, 92, 18}, "ADD MARKER") &&
                            contentWorkspace_->writable()) {
                            const auto newMarkerIndex = frame.markers.size();
                            std::string editError;
                            if (!contentWorkspace_->addAnimationMarker(key.id, frameIndex,
                                    contentEditValues_[fieldAnimationNewMarker], editError)) status_ = editError;
                            else {
                                selectedAnimationMarkerIndex_ = newMarkerIndex;
                                refreshContentRegistry(); status_ = "Animation marker added"; return;
                            }
                        }
                    }
                }
            }
        } else if (key.kind == ContentDefinitionKind::enemyVisual) {
            const auto* value = contentWorkspace_->enemyVisual(key.id);
            if (value) {
                if (contentEditKey_ != key) {
                    resetContentEditState();
                    contentEditKey_ = key;
                    contentEditValues_[fieldEnemyActionId] = "action.custom";
                }
                const auto bindingText = [&](const std::optional<simulation::DefinitionId>& id) {
                    return id ? std::string(id->value()) : std::string{};
                };
                const auto loadBindingFields = [&](const game::presentation::DirectionalAnimationRef& binding,
                                                   int base) {
                    contentEditValues_[base] = bindingText(binding.defaultAnimation);
                    contentEditValues_[base + 1] = bindingText(binding.down);
                    contentEditValues_[base + 2] = bindingText(binding.up);
                    contentEditValues_[base + 3] = bindingText(binding.side);
                };
                const auto drawBinding = [&](const char* label,
                                             const game::presentation::DirectionalAnimationRef& binding,
                                             int base, int y) {
                    ui.label(label, right.x + 8, y);
                    const char* names[] = {"default", "down", "up", "side"};
                    for (int index = 0; index < 4; ++index) {
                        const int rowY = y + 14 + index * 30;
                        ui.label(names[index], right.x + 8, rowY);
                        if (ui.textField({right.x + 62, rowY - 3, right.width - 70, 20},
                                         contentEditValues_[base + index],
                                         contentFocusedField_ == base + index)) {
                            contentFocusedField_ = base + index;
                        }
                    }
                    static_cast<void>(binding);
                };
                bool optionalStateChanged = false;
                const auto stateButton = [&](PreviewClipState state, const char* label, int x,
                                             bool enabled) {
                    if (ui.button({x, inspectorY + 140, 54, 18}, label,
                                  previewClipState_ == state)) {
                        if (enabled) {
                            resetContentEditState();
                            previewClipState_ = state;
                            previewActionId_ = {};
                        } else if (contentWorkspace_->writable()) {
                            auto updated = *value;
                            if (state == PreviewClipState::move) updated.move = {};
                            if (state == PreviewClipState::hurt) updated.hurt = {};
                            if (state == PreviewClipState::death) updated.death = {};
                            if (state == PreviewClipState::dead) updated.dead = {};
                            std::string editError;
                            if (contentWorkspace_->updateEnemyVisual(key.id, updated, editError)) {
                                previewClipState_ = state;
                                previewActionId_ = {};
                                refreshContentRegistry();
                                status_ = "Optional enemy visual state enabled";
                                optionalStateChanged = true;
                                return;
                            }
                            status_ = editError;
                        }
                    }
                };
                ui.label("IDLE (required fallback)", right.x + 8, inspectorY);
                if (contentBindingState_ != PreviewClipState::idle || !contentBindingAction_.empty()) {
                    contentBindingState_ = PreviewClipState::idle;
                    contentBindingAction_ = {};
                    loadBindingFields(value->idle, fieldEnemyIdleDefault);
                } else if (contentEditValues_[fieldEnemyIdleDefault].empty() &&
                           contentEditValues_[fieldEnemyIdleDown].empty() &&
                           contentEditValues_[fieldEnemyIdleUp].empty() &&
                           contentEditValues_[fieldEnemyIdleSide].empty()) {
                    loadBindingFields(value->idle, fieldEnemyIdleDefault);
                }
                drawBinding("", value->idle, fieldEnemyIdleDefault, inspectorY);
                const int stateY = inspectorY + 140;
                stateButton(PreviewClipState::move, "MOVE", right.x + 8, value->move.has_value());
                if (!optionalStateChanged) stateButton(PreviewClipState::hurt, "HURT", right.x + 66, value->hurt.has_value());
                if (!optionalStateChanged) stateButton(PreviewClipState::death, "DEATH", right.x + 124, value->death.has_value());
                if (!optionalStateChanged) stateButton(PreviewClipState::dead, "DEAD", right.x + 182, value->dead.has_value());
                if (optionalStateChanged) return;
                ui.label("optional states can be enabled in preview", right.x + 8, stateY + 22);
                const auto optionalBinding = [&]() -> const game::presentation::DirectionalAnimationRef* {
                    if (previewClipState_ == PreviewClipState::move && value->move) return &*value->move;
                    if (previewClipState_ == PreviewClipState::hurt && value->hurt) return &*value->hurt;
                    if (previewClipState_ == PreviewClipState::death && value->death) return &*value->death;
                    if (previewClipState_ == PreviewClipState::dead && value->dead) return &*value->dead;
                    return nullptr;
                }();
                if (optionalBinding) {
                    const int optionalBase = fieldEnemyOptionalDefault;
                    if (contentBindingState_ != previewClipState_ || !contentBindingAction_.empty()) {
                        contentBindingState_ = previewClipState_;
                        contentBindingAction_ = {};
                        loadBindingFields(*optionalBinding, optionalBase);
                    }
                    drawBinding("SELECTED OPTIONAL STATE", *optionalBinding, optionalBase, stateY + 38);
                }
                if (previewClipState_ == PreviewClipState::action && !previewActionId_.empty()) {
                    const auto action = std::find_if(value->attacks.begin(), value->attacks.end(),
                        [&](const auto& item) { return item.visualActionId == previewActionId_; });
                    if (action != value->attacks.end()) {
                        if (contentBindingState_ != PreviewClipState::action ||
                            contentBindingAction_ != previewActionId_) {
                            contentBindingState_ = PreviewClipState::action;
                            contentBindingAction_ = previewActionId_;
                            loadBindingFields(action->clips, fieldEnemyActionDefault);
                        }
                        drawBinding("SELECTED ACTION", action->clips,
                                    fieldEnemyActionDefault, stateY + 38);
                    }
                }
                if (input.enterPressed && contentWorkspace_->writable()) {
                    auto updated = *value;
                    game::presentation::DirectionalAnimationRef* target = nullptr;
                    int base = -1;
                    if (contentFocusedField_ >= fieldEnemyIdleDefault && contentFocusedField_ <= fieldEnemyIdleSide) {
                        target = &updated.idle; base = fieldEnemyIdleDefault;
                    } else if (contentFocusedField_ >= fieldEnemyOptionalDefault &&
                               contentFocusedField_ <= fieldEnemyOptionalSide) {
                        if (previewClipState_ == PreviewClipState::move) { if (!updated.move) updated.move = {}; target = &*updated.move; }
                        if (previewClipState_ == PreviewClipState::hurt) { if (!updated.hurt) updated.hurt = {}; target = &*updated.hurt; }
                        if (previewClipState_ == PreviewClipState::death) { if (!updated.death) updated.death = {}; target = &*updated.death; }
                        if (previewClipState_ == PreviewClipState::dead) { if (!updated.dead) updated.dead = {}; target = &*updated.dead; }
                        base = fieldEnemyOptionalDefault;
                    } else if (contentFocusedField_ >= fieldEnemyActionDefault &&
                               contentFocusedField_ <= fieldEnemyActionSide) {
                        const auto actionId = previewActionId_;
                        const auto it = std::find_if(updated.attacks.begin(), updated.attacks.end(),
                            [&](const auto& action) { return action.visualActionId == actionId; });
                        if (it != updated.attacks.end()) { target = &it->clips; base = fieldEnemyActionDefault; }
                    }
                    if (target && base >= 0) {
                        auto& slot = contentEditValues_[contentFocusedField_];
                        std::optional<simulation::DefinitionId>* destinations[] = {
                            &target->defaultAnimation, &target->down, &target->up, &target->side};
                        auto& destination = destinations[contentFocusedField_ - base];
                        if (slot.empty()) destination->reset();
                        else *destination = simulation::DefinitionId{slot};
                        std::string editError;
                        if (contentWorkspace_->updateEnemyVisual(key.id, updated, editError)) {
                            refreshContentRegistry();
                            status_ = "Enemy visual binding updated";
                            return;
                        }
                        status_ = editError;
                    }
                }
                ui.label("actions (arbitrary VisualActionId)", right.x + 8, stateY + 180);
                ui.label("new action id", right.x + 8, stateY + 196);
                if (ui.textField({right.x + 8, stateY + 210, right.width - 16, 20},
                                 contentEditValues_[fieldEnemyActionId],
                                 contentFocusedField_ == fieldEnemyActionId)) {
                    contentFocusedField_ = fieldEnemyActionId;
                }
                if (ui.button({right.x + 8, stateY + 234, right.width - 16, 18}, "ADD ACTION (IDLE)") &&
                    contentWorkspace_->writable()) {
                    game::content::AuthoredEnemyAttackVisual action;
                    action.visualActionId = simulation::DefinitionId{contentEditValues_[fieldEnemyActionId]};
                    action.clips = value->idle;
                    std::string editError;
                    if (!contentWorkspace_->addEnemyVisualAction(key.id, action, editError)) status_ = editError;
                    else { refreshContentRegistry(); status_ = "Enemy visual action added"; return; }
                }
                int actionY = stateY + 258;
                for (const auto& action : value->attacks) {
                    if (actionY + 18 >= right.height - 80) break;
                    if (ui.button({right.x + 8, actionY, right.width - 80, 18},
                                  std::string(action.visualActionId.value()).substr(0, 24),
                                  previewClipState_ == PreviewClipState::action && previewActionId_ == action.visualActionId)) {
                        resetContentEditState();
                        previewClipState_ = PreviewClipState::action;
                        previewActionId_ = action.visualActionId;
                    }
                    if (ui.button({right.x + right.width - 66, actionY, 58, 18}, "REMOVE")) {
                        std::string editError;
                        if (!contentWorkspace_->removeEnemyVisualAction(key.id, action.visualActionId, editError)) status_ = editError;
                        else { refreshContentRegistry(); status_ = "Enemy visual action removed"; return; }
                    }
                    actionY += 20;
                }
            }
        } else if (key.kind == ContentDefinitionKind::objectVisual) {
            const auto* value = contentWorkspace_->objectVisual(key.id);
            if (value) {
                if (contentEditKey_ != key) {
                    resetContentEditState();
                    contentEditKey_ = key;
                    const auto text = [](const auto& id) { return id ? std::string(id->value()) : std::string{}; };
                    contentEditValues_[fieldObjectIdle] = std::string(value->idleAnimationId.value());
                    contentEditValues_[fieldObjectOpened] = text(value->openedAnimationId);
                    contentEditValues_[fieldObjectDestroying] = text(value->destroyingAnimationId);
                    contentEditValues_[fieldObjectActivationInactive] = text(value->activationInactiveAnimationId);
                    contentEditValues_[fieldObjectActivationActive] = text(value->activationActiveAnimationId);
                    contentEditValues_[fieldObjectDoorLocked] = text(value->doorLockedAnimationId);
                    contentEditValues_[fieldObjectDoorClosed] = text(value->doorClosedAnimationId);
                    contentEditValues_[fieldObjectDoorOpen] = text(value->doorOpenAnimationId);
                }
                const std::array<std::pair<const char*, int>, 8> fields{{
                    {"idle", fieldObjectIdle}, {"opened", fieldObjectOpened},
                    {"destroying", fieldObjectDestroying}, {"activation inactive", fieldObjectActivationInactive},
                    {"activation active", fieldObjectActivationActive}, {"door locked", fieldObjectDoorLocked},
                    {"door closed", fieldObjectDoorClosed}, {"door open", fieldObjectDoorOpen}}};
                int fieldY = inspectorY;
                for (const auto& [label, field] : fields) {
                    ui.label(label, right.x + 8, fieldY);
                    if (ui.textField({right.x + 8, fieldY + 14, right.width - 16, 20},
                                     contentEditValues_[field], contentFocusedField_ == field)) {
                        contentFocusedField_ = field;
                    }
                    fieldY += 38;
                }
                if (input.enterPressed && contentFocusedField_ >= fieldObjectIdle &&
                    contentFocusedField_ <= fieldObjectDoorOpen && contentWorkspace_->writable()) {
                    auto updated = *value;
                    const auto idOrNone = [&](int field) -> std::optional<simulation::DefinitionId> {
                        if (contentEditValues_[field].empty()) return std::nullopt;
                        return simulation::DefinitionId{contentEditValues_[field]};
                    };
                    if (contentFocusedField_ == fieldObjectIdle) {
                        updated.idleAnimationId = simulation::DefinitionId{contentEditValues_[fieldObjectIdle]};
                    } else if (contentFocusedField_ == fieldObjectOpened) updated.openedAnimationId = idOrNone(fieldObjectOpened);
                    else if (contentFocusedField_ == fieldObjectDestroying) updated.destroyingAnimationId = idOrNone(fieldObjectDestroying);
                    else if (contentFocusedField_ == fieldObjectActivationInactive) updated.activationInactiveAnimationId = idOrNone(fieldObjectActivationInactive);
                    else if (contentFocusedField_ == fieldObjectActivationActive) updated.activationActiveAnimationId = idOrNone(fieldObjectActivationActive);
                    else if (contentFocusedField_ == fieldObjectDoorLocked) updated.doorLockedAnimationId = idOrNone(fieldObjectDoorLocked);
                    else if (contentFocusedField_ == fieldObjectDoorClosed) updated.doorClosedAnimationId = idOrNone(fieldObjectDoorClosed);
                    else updated.doorOpenAnimationId = idOrNone(fieldObjectDoorOpen);
                    std::string editError;
                    if (contentWorkspace_->updateObjectVisual(key.id, updated, editError)) {
                        refreshContentRegistry();
                        status_ = "Object visual updated";
                        return;
                    }
                    status_ = editError;
                }
            }
        } else if (key.kind == ContentDefinitionKind::npcVisual) {
            const auto* value = contentWorkspace_->npcVisual(key.id);
            if (value) {
                if (contentEditKey_ != key) {
                    resetContentEditState();
                    contentEditKey_ = key;
                    contentEditValues_[fieldNpcMarker] = std::to_string(value->markerColor.r) + "," +
                        std::to_string(value->markerColor.g) + "," + std::to_string(value->markerColor.b) + "," +
                        std::to_string(value->markerColor.a);
                    if (value->idle) {
                        const auto text = [](const auto& id) { return id ? std::string(id->value()) : std::string{}; };
                        contentEditValues_[fieldNpcIdleDefault] = text(value->idle->defaultAnimation);
                        contentEditValues_[fieldNpcIdleDown] = text(value->idle->down);
                        contentEditValues_[fieldNpcIdleUp] = text(value->idle->up);
                        contentEditValues_[fieldNpcIdleSide] = text(value->idle->side);
                    }
                }
                ui.label("marker r,g,b,a", right.x + 8, inspectorY);
                if (ui.textField({right.x + 8, inspectorY + 14, right.width - 16, 20},
                                 contentEditValues_[fieldNpcMarker], contentFocusedField_ == fieldNpcMarker)) {
                    contentFocusedField_ = fieldNpcMarker;
                }
                ui.label("idle directional (optional)", right.x + 8, inspectorY + 42);
                const std::array<std::pair<const char*, int>, 4> fields{{
                    {"default", fieldNpcIdleDefault}, {"down", fieldNpcIdleDown},
                    {"up", fieldNpcIdleUp}, {"side", fieldNpcIdleSide}}};
                int fieldY = inspectorY + 58;
                for (const auto& [label, field] : fields) {
                    ui.label(label, right.x + 8, fieldY);
                    if (ui.textField({right.x + 62, fieldY - 3, right.width - 70, 20},
                                     contentEditValues_[field], contentFocusedField_ == field)) {
                        contentFocusedField_ = field;
                    }
                    fieldY += 28;
                }
                if (input.enterPressed && contentWorkspace_->writable() &&
                    ((contentFocusedField_ >= fieldNpcMarker && contentFocusedField_ <= fieldNpcIdleSide))) {
                    auto updated = *value;
                    bool accepted = true;
                    std::string editError;
                    if (contentFocusedField_ == fieldNpcMarker) {
                        const auto color = parseIntegerList<4>(contentEditValues_[fieldNpcMarker]);
                        if (!color || std::any_of(color->begin(), color->end(), [](int channel) { return channel < 0 || channel > 255; })) {
                            accepted = false;
                            editError = "NPC marker color must contain four integers in 0..255";
                        } else {
                            updated.markerColor = {static_cast<std::uint8_t>((*color)[0]),
                                                   static_cast<std::uint8_t>((*color)[1]),
                                                   static_cast<std::uint8_t>((*color)[2]),
                                                   static_cast<std::uint8_t>((*color)[3])};
                        }
                    } else {
                        if (!updated.idle) updated.idle = {};
                        auto& destination = *updated.idle;
                        std::optional<simulation::DefinitionId>* destinations[] = {
                            &destination.defaultAnimation, &destination.down,
                            &destination.up, &destination.side};
                        auto& text = contentEditValues_[contentFocusedField_];
                        if (text.empty()) destinations[contentFocusedField_ - fieldNpcIdleDefault]->reset();
                        else *destinations[contentFocusedField_ - fieldNpcIdleDefault] = simulation::DefinitionId{text};
                    }
                    if (accepted && contentWorkspace_->updateNpcVisual(key.id, updated, editError)) {
                        refreshContentRegistry();
                        status_ = "NPC visual updated";
                        return;
                    }
                    if (!accepted || !editError.empty()) status_ = editError;
                }
            }
        } else ui.label("Read-only category in 18A", right.x + 8, inspectorY);
    }
    if (ui.button({right.x + 8, right.height - 64, right.width - 16, 20}, "VALIDATE WORKSPACE")) {
        (void)validateWorkspace();
    }
    ui.label(contentWorkspace_->dirty() ? "Workspace dirty *" : "Workspace clean", right.x + 8, right.height - 38);
    const auto& diagnostics = contentWorkspace_->diagnostics();
    ui.label("VALIDATION", status.x + 8, status.y + 4);
    if (diagnostics.empty()) {
        ui.label(contentWorkspace_->valid() ? "CONTENT OK" : "CONTENT INVALID", status.x + 88, status.y + 4);
    } else {
        ui.label("CONTENT: " + std::to_string(diagnostics.size()) + " diagnostic(s)",
                 status.x + 88, status.y + 4);
    }
    if (!visualValidationAttempted_) {
        ui.label("ASSET: not validated", status.x + 250, status.y + 4);
    } else if (!contentWorkspace_->compiledRegistry()) {
        ui.label("ASSET: skipped (content invalid)", status.x + 250, status.y + 4);
    } else if (visualDiagnostics_.empty()) {
        ui.label("ASSET: OK", status.x + 250, status.y + 4);
    } else {
        ui.label("ASSET: " + std::to_string(visualDiagnostics_.size()) + " diagnostic(s)",
                 status.x + 250, status.y + 4);
    }

    const int diagnosticsTop = right.height - 222;
    ui.label("CONTENT VALIDATION", right.x + 8, diagnosticsTop);
    int diagnosticY = diagnosticsTop + 14;
    for (std::size_t index = 0; index < diagnostics.size() && index < 3; ++index) {
        const auto line = game::content::formatContentWorkspaceDiagnostic(diagnostics[index]);
        ui.label(line.substr(0, 38), right.x + 8, diagnosticY);
        diagnosticY += 12;
    }
    if (diagnostics.size() > 3) {
        ui.label("... +" + std::to_string(diagnostics.size() - 3) + " more", right.x + 8, diagnosticY);
    }
    const int assetDiagnosticsTop = right.height - 152;
    ui.label("ASSET VALIDATION", right.x + 8, assetDiagnosticsTop);
    if (!visualValidationAttempted_) {
        ui.label("Not run - press Validate", right.x + 8, assetDiagnosticsTop + 14);
    } else if (!contentWorkspace_->compiledRegistry()) {
        ui.label("Skipped - content invalid", right.x + 8, assetDiagnosticsTop + 14);
    } else if (visualDiagnostics_.empty()) {
        ui.label("OK", right.x + 8, assetDiagnosticsTop + 14);
    } else {
        int assetY = assetDiagnosticsTop + 14;
        for (std::size_t index = 0; index < visualDiagnostics_.size() && index < 3; ++index) {
            const auto line = game::presentation::formatVisualContentDiagnostic(visualDiagnostics_[index]);
            ui.label(line.substr(0, 38), right.x + 8, assetY);
            assetY += 12;
        }
        if (visualDiagnostics_.size() > 3) {
            ui.label("... +" + std::to_string(visualDiagnostics_.size() - 3) + " more",
                     right.x + 8, assetY);
        }
    }
}

void EditorApp::drawContentPreview(EditorUiContext& ui, const EditorInputState& input,
                                    core::RectI canvas) {
    const auto isVisual = selectedContentDefinition_ &&
        (selectedContentDefinition_->kind == ContentDefinitionKind::visualImage ||
         selectedContentDefinition_->kind == ContentDefinitionKind::staticSprite ||
         selectedContentDefinition_->kind == ContentDefinitionKind::animation ||
         selectedContentDefinition_->kind == ContentDefinitionKind::enemyVisual ||
         selectedContentDefinition_->kind == ContentDefinitionKind::objectVisual ||
         selectedContentDefinition_->kind == ContentDefinitionKind::npcVisual);
    if (!isVisual || canvas.width < 32 || canvas.height < 64) {
        ui.label("VISUAL PREVIEW", canvas.x + 4, canvas.y + 4);
        ui.label("Select a visual definition", canvas.x + 4, canvas.y + 22);
        return;
    }

    VisualPreviewRequest request;
    request.key = *selectedContentDefinition_;
    request.frameIndex = selectedAnimationFrameIndex_;
    request.state = previewClipState_;
    request.actionId = previewActionId_;
    request.facing = previewFacing_;
    if (request.key.kind == ContentDefinitionKind::enemyVisual &&
        request.state == PreviewClipState::action && request.actionId.empty()) {
        if (const auto* visual = contentWorkspace_->enemyVisual(request.key.id);
            visual && !visual->attacks.empty()) request.actionId = visual->attacks.front().visualActionId;
    }
    visualPreview_.prepare(*contentWorkspace_, request);
    visualPreview_.advanceTicks(input.previewTicks);

    ui.label("VISUAL PREVIEW", canvas.x + 4, canvas.y + 4);
    if (request.key.kind == ContentDefinitionKind::visualImage) {
        if (const auto* definition = contentWorkspace_->visualImage(request.key.id)) {
            const auto root = definition->root == game::presentation::VisualAssetRoot::gameAssets
                ? "gameAssets" : "contentWorkspace";
            ui.label(std::string(root) + ":" + definition->relativePath,
                     canvas.x + 132, canvas.y + 4);
        }
    }
    const int toolbarY = canvas.y + 18;
    if (ui.button({canvas.x + 4, toolbarY, 38, 18}, "FIT", previewViewport_.zoomStep < 0)) {
        previewViewport_.zoomStep = -1;
        previewViewport_.pan = {};
    }
    const int zoomLabels[] = {1, 2, 4, 8};
    for (int index = 0; index < 4; ++index) {
        if (ui.button({canvas.x + 46 + index * 34, toolbarY, 30, 18},
                      std::to_string(zoomLabels[index]) + "x", previewViewport_.zoomStep == index)) {
            previewViewport_.zoomStep = index;
            previewViewport_.pan = {};
        }
    }
    if (ui.button({canvas.x + 184, toolbarY, 54, 18},
                  previewViewport_.gridEnabled ? "GRID ON" : "GRID OFF", previewViewport_.gridEnabled)) {
        previewViewport_.gridEnabled = !previewViewport_.gridEnabled;
    }
    if (request.key.kind == ContentDefinitionKind::staticSprite &&
        ui.button({canvas.x + 390, toolbarY, 76, 18}, "FULL IMAGE")) {
        if (const auto* value = contentWorkspace_->staticSprite(request.key.id);
            value && contentWorkspace_->writable()) {
            auto updated = *value;
            updated.source.reset();
            std::string error;
            if (contentWorkspace_->updateStaticSprite(updated.id, updated, error)) {
                refreshContentRegistry();
                status_ = "Static sprite uses full image";
                return;
            }
            status_ = error;
        }
    }
    if (request.key.kind == ContentDefinitionKind::animation && previewSelectionRect_) {
        const auto addFrames = [&](const std::vector<core::RectI>& rectangles) {
            if (!contentWorkspace_->writable() || rectangles.empty()) return false;
            std::string error;
            std::size_t index = contentWorkspace_->animation(request.key.id)
                ? contentWorkspace_->animation(request.key.id)->frames.size() : 0;
            for (const auto& rectangle : rectangles) {
                if (!contentWorkspace_->addAnimationFrame(
                        request.key.id,
                        game::content::AuthoredAnimationFrame{rectangle, {0, 0}, {0, 0}, 1, {}},
                        error)) {
                    status_ = error;
                    return false;
                }
                selectedAnimationFrameIndex_ = index++;
            }
            refreshContentRegistry();
            status_ = rectangles.size() == 1 ? "Animation frame added from selection"
                                             : "Animation cells added as frames";
            return true;
        };
        if (ui.button({canvas.x + 390, toolbarY, 70, 18}, "ADD CELL") &&
            addFrames({*previewSelectionRect_})) return;
        if (ui.button({canvas.x + 464, toolbarY, 70, 18}, "ADD CELLS") &&
            addFrames(previewGridSelections(*previewSelectionRect_, previewViewport_.gridOrigin,
                                             previewViewport_.gridCell))) return;
    }
    const bool hasClip = visualPreview_.hasClip();
    if (hasClip) {
        if (ui.button({canvas.x + 244, toolbarY, 48, 18},
                      visualPreview_.playing() ? "PAUSE" : "PLAY", visualPreview_.playing())) {
            visualPreview_.setPlaying(!visualPreview_.playing());
        }
        if (ui.button({canvas.x + 296, toolbarY, 36, 18}, "RESTART")) visualPreview_.restart();
        if (ui.button({canvas.x + 336, toolbarY, 22, 18}, "<")) visualPreview_.stepFrame(-1);
        if (ui.button({canvas.x + 362, toolbarY, 22, 18}, ">")) visualPreview_.stepFrame(1);
    }

    const int gridY = toolbarY + 22;
    ui.label("GRID ORIGIN", canvas.x + 4, gridY + 4);
    if (contentEditValues_[fieldPreviewGridOrigin].empty()) {
        contentEditValues_[fieldPreviewGridOrigin] = pointText(previewViewport_.gridOrigin);
    }
    if (ui.textField({canvas.x + 72, gridY, 72, 20}, contentEditValues_[fieldPreviewGridOrigin],
                     contentFocusedField_ == fieldPreviewGridOrigin)) {
        contentFocusedField_ = fieldPreviewGridOrigin;
    }
    ui.label("CELL", canvas.x + 150, gridY + 4);
    if (contentEditValues_[fieldPreviewGridCell].empty()) {
        contentEditValues_[fieldPreviewGridCell] = pointText(previewViewport_.gridCell);
    }
    if (ui.textField({canvas.x + 184, gridY, 72, 20}, contentEditValues_[fieldPreviewGridCell],
                     contentFocusedField_ == fieldPreviewGridCell)) {
        contentFocusedField_ = fieldPreviewGridCell;
    }
    if (input.enterPressed && contentFocusedField_ == fieldPreviewGridOrigin) {
        if (const auto parsed = parseIntegerList<2>(contentEditValues_[fieldPreviewGridOrigin])) {
            previewViewport_.gridOrigin = {(*parsed)[0], (*parsed)[1]};
        } else status_ = "grid origin must contain two comma-separated integers";
    } else if (input.enterPressed && contentFocusedField_ == fieldPreviewGridCell) {
        if (const auto parsed = parseIntegerList<2>(contentEditValues_[fieldPreviewGridCell]);
            parsed && (*parsed)[0] > 0 && (*parsed)[1] > 0) {
            previewViewport_.gridCell = {(*parsed)[0], (*parsed)[1]};
        } else status_ = "grid cell must contain two positive integers";
    }

    const int stateY = gridY + 24;
    if (request.key.kind == ContentDefinitionKind::enemyVisual) {
        const auto stateButton = [&](PreviewClipState state, const char* text, int x, bool available) {
            if (available && ui.button({x, stateY, 54, 18}, text, previewClipState_ == state)) {
                resetContentEditState();
                previewClipState_ = state;
                previewActionId_ = {};
            }
        };
        const auto* visual = contentWorkspace_->enemyVisual(request.key.id);
        stateButton(PreviewClipState::idle, "IDLE", canvas.x + 4, visual != nullptr);
        stateButton(PreviewClipState::move, "MOVE", canvas.x + 62, visual && visual->move.has_value());
        stateButton(PreviewClipState::hurt, "HURT", canvas.x + 120, visual && visual->hurt.has_value());
        stateButton(PreviewClipState::death, "DEATH", canvas.x + 178, visual && visual->death.has_value());
        stateButton(PreviewClipState::dead, "DEAD", canvas.x + 236, visual && visual->dead.has_value());
        int actionX = canvas.x + 294;
        if (visual) for (const auto& action : visual->attacks) {
            if (actionX + 56 > canvas.x + canvas.width) break;
            if (ui.button({actionX, stateY, 54, 18}, std::string(action.visualActionId.value()).substr(0, 8),
                          previewClipState_ == PreviewClipState::action && previewActionId_ == action.visualActionId)) {
                previewClipState_ = PreviewClipState::action;
                previewActionId_ = action.visualActionId;
            }
            actionX += 58;
        }
    } else if (request.key.kind == ContentDefinitionKind::objectVisual) {
        const auto* visual = contentWorkspace_->objectVisual(request.key.id);
        const auto stateButton = [&](PreviewClipState state, const char* text, int x,
                                     bool available) {
            if (available && ui.button({x, stateY, 64, 18}, text,
                                       previewClipState_ == state)) {
                resetContentEditState();
                previewClipState_ = state;
            }
        };
        stateButton(PreviewClipState::idle, "IDLE", canvas.x + 4, visual != nullptr);
        stateButton(PreviewClipState::opened, "OPENED", canvas.x + 72, visual && visual->openedAnimationId.has_value());
        stateButton(PreviewClipState::destroying, "DESTROY", canvas.x + 140, visual && visual->destroyingAnimationId.has_value());
        stateButton(PreviewClipState::activationInactive, "ACT OFF", canvas.x + 208, visual && visual->activationInactiveAnimationId.has_value());
        stateButton(PreviewClipState::activationActive, "ACT ON", canvas.x + 276, visual && visual->activationActiveAnimationId.has_value());
        stateButton(PreviewClipState::doorLocked, "LOCKED", canvas.x + 344, visual && visual->doorLockedAnimationId.has_value());
        stateButton(PreviewClipState::doorClosed, "CLOSED", canvas.x + 412, visual && visual->doorClosedAnimationId.has_value());
        stateButton(PreviewClipState::doorOpen, "OPEN", canvas.x + 480, visual && visual->doorOpenAnimationId.has_value());
    } else if (request.key.kind == ContentDefinitionKind::npcVisual || visualPreview_.hasClip()) {
        const char* labels[] = {"DOWN", "UP", "LEFT", "RIGHT"};
        for (int index = 0; index < 4; ++index) {
            if (ui.button({canvas.x + 4 + index * 58, stateY, 54, 18}, labels[index],
                          static_cast<int>(previewFacing_) == index)) {
                resetContentEditState();
                previewFacing_ = static_cast<game::gameplay::FacingDirection>(index);
            }
        }
    }

    const int imageTop = stateY + 22;
    const core::RectI imageCanvas{canvas.x, imageTop, canvas.width,
                                  std::max(1, canvas.height - imageTop - 4)};
    handleContentPreview(ui, input, imageCanvas);
    render::Renderer2D renderer(*framebuffer_);
    renderer.fillRect(imageCanvas, {12, 15, 21, 255});
    const auto* image = visualPreview_.image();
    if (image) {
        const auto transform = makeVisualPreviewTransform(previewViewport_, imageCanvas,
                                                           {image->width(), image->height()});
        const core::RectI imageRect{0, 0, image->width(), image->height()};
        const core::RectI imageDestination{
            transform.imageOrigin.x, transform.imageOrigin.y,
            std::max(1, static_cast<int>(std::lround(image->width() * transform.scale))),
            std::max(1, static_cast<int>(std::lround(image->height() * transform.scale)))};
        renderer.drawImageRegionNearest(*image, imageRect, imageDestination);

        if (previewViewport_.gridEnabled) {
            for (const auto& cell : previewGridSelections(imageRect, previewViewport_.gridOrigin,
                                                          previewViewport_.gridCell)) {
                const auto screen = previewImageRectToScreen(transform, cell);
                renderer.fillRect({screen.x, screen.y, 1, std::max(1, screen.height)},
                                  {90, 120, 150, 120});
                renderer.fillRect({screen.x, screen.y, std::max(1, screen.width), 1},
                                  {90, 120, 150, 120});
            }
        }

        auto drawFrameOutline = [&](core::RectI source, core::ColorRGBA8 color, int thickness) {
            const auto screen = previewImageRectToScreen(transform, source);
            outline(renderer, screen, color);
            for (int offset = 1; offset < thickness; ++offset) {
                outline(renderer, {screen.x + offset, screen.y + offset,
                                   std::max(1, screen.width - offset * 2),
                                   std::max(1, screen.height - offset * 2)}, color);
            }
        };
        if (selectedContentDefinition_->kind == ContentDefinitionKind::staticSprite) {
            if (const auto* value = contentWorkspace_->staticSprite(selectedContentDefinition_->id)) {
                drawFrameOutline(value->source.value_or(imageRect), selectedColor, 2);
            }
        } else if (selectedContentDefinition_->kind == ContentDefinitionKind::animation) {
            if (const auto* value = contentWorkspace_->animation(selectedContentDefinition_->id)) {
                for (std::size_t index = 0; index < value->frames.size(); ++index) {
                    drawFrameOutline(value->frames[index].source,
                                     index == selectedAnimationFrameIndex_ ? selectedColor : core::ColorRGBA8{80, 200, 240, 220},
                                     index == selectedAnimationFrameIndex_ ? 2 : 1);
                }
                if (selectedAnimationFrameIndex_ < value->frames.size()) {
                    const auto& frame = value->frames[selectedAnimationFrameIndex_];
                    const auto anchor = previewImageToScreen(transform,
                        {frame.source.x + frame.anchor.x, frame.source.y + frame.anchor.y});
                    renderer.fillRect({anchor.x - 3, anchor.y, 7, 1}, selectedColor);
                    renderer.fillRect({anchor.x, anchor.y - 3, 1, 7}, selectedColor);
                }
            }
        } else if (selectedContentDefinition_->kind == ContentDefinitionKind::visualImage) {
            drawFrameOutline(imageRect, {110, 220, 180, 220}, 1);
        }

        if (previewRectangleDragging_) {
            if (const auto selection = previewDragRectangle(transform, previewRectangleStart_,
                                                            previewRectangleCurrent_,
                                                            {image->width(), image->height()},
                                                            previewViewport_.gridEnabled,
                                                            previewViewport_.gridOrigin,
                                                            previewViewport_.gridCell)) {
                drawFrameOutline(*selection, {255, 150, 70, 255}, 2);
            }
        }

        const auto sourceFitsImage = [&](core::RectI source) {
            const auto right = static_cast<std::int64_t>(source.x) + source.width;
            const auto bottom = static_cast<std::int64_t>(source.y) + source.height;
            return !source.empty() && source.x >= 0 && source.y >= 0 &&
                   right <= image->width() && bottom <= image->height();
        };
        const auto drawPlayback = [&]() {
            const bool staticSprite = selectedContentDefinition_->kind ==
                                      ContentDefinitionKind::staticSprite;
            if (!visualPreview_.hasClip() && !staticSprite) return;
            const core::RectI playback{imageCanvas.x + imageCanvas.width - 112,
                                       imageCanvas.y + imageCanvas.height - 112, 108, 108};
            renderer.fillRect(playback, {24, 28, 36, 255});
            const core::PointI origin{playback.x + playback.width / 2,
                                     playback.y + playback.height / 2};
            renderer.fillRect({origin.x - 1, playback.y + 4, 2, playback.height - 8},
                              {100, 105, 115, 220});
            renderer.fillRect({playback.x + 4, origin.y - 1, playback.width - 8, 2},
                              {100, 105, 115, 220});
            const int scale = 2;
            if (staticSprite) {
                const auto* value = contentWorkspace_->staticSprite(selectedContentDefinition_->id);
                if (!value || (value->source && !sourceFitsImage(*value->source))) return;
                const auto source = value->source.value_or(core::RectI{0, 0, image->width(), image->height()});
                const auto anchor = value->anchor;
                const int drawX = origin.x - anchor.x * scale;
                const int drawY = origin.y - anchor.y * scale;
                renderer.drawImageRegionNearest(*image, source,
                                                {drawX, drawY, source.width * scale,
                                                 source.height * scale});
                renderer.fillRect({origin.x - 3, origin.y, 7, 1}, {255, 220, 80, 255});
                renderer.fillRect({origin.x, origin.y - 3, 1, 7}, {255, 220, 80, 255});
                return;
            }
            const auto& frame = visualPreview_.animator().currentFrame().sprite;
            const int drawX = origin.x + (frame.drawOffset.x - frame.anchor.x) * scale;
            const int drawY = origin.y + (frame.drawOffset.y - frame.anchor.y) * scale;
            renderer.drawImageRegionNearest(*image, frame.source,
                                            {drawX, drawY, frame.source.width * scale,
                                             frame.source.height * scale}, visualPreview_.flipX());
        };
        drawPlayback();
    }

    if (!visualPreview_.diagnostics().empty()) {
        const auto& diagnostic = visualPreview_.diagnostics().front();
        ui.label(game::presentation::formatVisualContentDiagnostic(diagnostic).substr(0, 54),
                 canvas.x + 4, canvas.y + canvas.height - 14);
    } else if (visualPreview_.hasClip()) {
        const auto& animator = visualPreview_.animator();
        ui.label("Frame " + std::to_string(animator.frameIndex() + 1) + "/" +
                     std::to_string(animator.clip().frames().size()) + " tick " +
                     std::to_string(animator.elapsedFrameTicks()) + "/" +
                     std::to_string(animator.currentFrame().durationTicks),
                 canvas.x + 4, canvas.y + canvas.height - 14);
        if (!visualPreview_.markerEvents().empty()) {
            ui.label("marker: " + std::string(visualPreview_.markerEvents().back().marker),
                     canvas.x + 160, canvas.y + canvas.height - 14);
        }
    } else if (image) {
        ui.label(std::to_string(image->width()) + "x" + std::to_string(image->height()),
                 canvas.x + 4, canvas.y + canvas.height - 14);
    }
}

void EditorApp::handleContentPreview(EditorUiContext& ui, const EditorInputState& input,
                                     core::RectI canvas) {
    if (!selectedContentDefinition_ || !visualPreview_.image()) return;
    const auto* image = visualPreview_.image();
    const auto transform = makeVisualPreviewTransform(previewViewport_, canvas,
                                                       {image->width(), image->height()});
    const core::PointI pointer{input.pointer.x, input.pointer.y};
    const bool inside = ui.pointerInside(canvas);
    if (inside && input.pointer.wheelDelta != 0) {
        previewViewport_.zoomStep = std::clamp(previewViewport_.zoomStep - input.pointer.wheelDelta / 120,
                                               -1, 3);
    }
    if (inside && (input.pointer.middlePressed || (input.space && input.pointer.leftPressed))) {
        previewPanning_ = true;
        previewPanPointerStart_ = pointer;
        previewPanStart_ = previewViewport_.pan;
    }
    if (previewPanning_) {
        if (input.pointer.middleDown || (input.space && input.pointer.leftDown)) {
            previewViewport_.pan = {previewPanStart_.x + pointer.x - previewPanPointerStart_.x,
                                    previewPanStart_.y + pointer.y - previewPanPointerStart_.y};
        } else previewPanning_ = false;
    }
    if (inside && input.pointer.leftPressed && !input.space) {
        previewRectangleDragging_ = true;
        previewRectangleStart_ = pointer;
        previewRectangleCurrent_ = pointer;
        if (selectedContentDefinition_->kind == ContentDefinitionKind::animation) {
            if (const auto* animation = contentWorkspace_->animation(selectedContentDefinition_->id)) {
                for (std::size_t index = 0; index < animation->frames.size(); ++index) {
                    const auto frame = previewImageRectToScreen(transform, animation->frames[index].source);
                    if (pointer.x >= frame.x && pointer.y >= frame.y &&
                        pointer.x < frame.x + frame.width && pointer.y < frame.y + frame.height) {
                        selectedAnimationFrameIndex_ = index;
                        contentEditFrame_ = noContentIndex;
                        break;
                    }
                }
            }
        }
    }
    if (previewRectangleDragging_ && input.pointer.leftDown) previewRectangleCurrent_ = pointer;
    if (previewRectangleDragging_ && input.pointer.leftReleased) {
        previewRectangleDragging_ = false;
        const auto selection = previewDragRectangle(transform, previewRectangleStart_,
                                                    previewRectangleCurrent_,
                                                    {image->width(), image->height()},
                                                    previewViewport_.gridEnabled,
                                                    previewViewport_.gridOrigin,
                                                    previewViewport_.gridCell);
        if (!selection) return;
        previewSelectionRect_ = *selection;
        std::string error;
        if (selectedContentDefinition_->kind == ContentDefinitionKind::staticSprite) {
            if (auto value = contentWorkspace_->staticSprite(selectedContentDefinition_->id);
                value && contentWorkspace_->writable()) {
                auto updated = *value;
                updated.source = *selection;
                if (contentWorkspace_->updateStaticSprite(updated.id, updated, error)) {
                    refreshContentRegistry();
                    previewSelectionRect_ = *selection;
                    status_ = "Static sprite source selected";
                } else status_ = error;
            }
        } else if (selectedContentDefinition_->kind == ContentDefinitionKind::animation) {
            if (auto value = contentWorkspace_->animation(selectedContentDefinition_->id);
                value && selectedAnimationFrameIndex_ < value->frames.size() &&
                contentWorkspace_->writable()) {
                auto updated = *value;
                updated.frames[selectedAnimationFrameIndex_].source = *selection;
                if (contentWorkspace_->updateAnimation(updated.id, updated, error)) {
                    refreshContentRegistry();
                    previewSelectionRect_ = *selection;
                    status_ = "Animation frame source selected";
                } else status_ = error;
            }
        }
    }
}

void EditorApp::drawViewport(render::Renderer2D& renderer,core::RectI viewport,const EditorInputState& input){
    renderer.fillRect(viewport,viewportBackground);drawMap(renderer,viewport);drawEntities(renderer,viewport);
    if (!playtest_.active()) { handleViewport(viewport,input); }
}

void EditorApp::drawMap(render::Renderer2D& renderer,core::RectI viewport) const{
    const auto& data=document_.data();const int scaled=std::max(1,static_cast<int>(std::lround(data.tileSize*zoom())));
    const auto visible = visibleTileRange(data, viewport, document_.viewport().worldX,
                                          document_.viewport().worldY, zoom());
    for(std::size_t layerIndex=0;layerIndex<data.layers.size();++layerIndex){if(layerIndex>=document_.layerStates().size()||!document_.layerStates()[layerIndex].visible)continue;const auto& layer=data.layers[layerIndex];
        for(int y=visible.firstY;y<=visible.lastY;++y)for(int x=visible.firstX;x<=visible.lastX;++x){const auto cell=layer.cells[static_cast<std::size_t>(y)*data.width+static_cast<std::size_t>(x)];if(!cell||*cell>=data.tileReferences.size())continue;const auto& reference=data.tileReferences[*cell];
            const auto* definition=content_.tilesets().find(reference.tilesetId);if(!definition||reference.sourceIndex>=definition->tileCount())continue;
            const auto* tileset=tilesetVisuals_.find(runtimeTilesets_.requireRuntimeId(reference.tilesetId));if(!tileset)continue;
            const auto screen=worldToScreen({x*static_cast<int>(data.tileSize),y*static_cast<int>(data.tileSize)},viewport);
            renderer.drawImageRegionNearest(*tileset->image,tileset->atlas.sourceRect(reference.sourceIndex),{screen.x,screen.y,scaled,scaled},world::hasFlag(reference.flags,world::TileFlags::flipX));}}
    if(showCollision_){for(int y=visible.firstY;y<=visible.lastY;++y)for(int x=visible.firstX;x<=visible.lastX;++x){if(data.collision[static_cast<std::size_t>(y)*data.width+static_cast<std::size_t>(x)]==0)continue;const auto screen=worldToScreen({x*static_cast<int>(data.tileSize),y*static_cast<int>(data.tileSize)},viewport);renderer.fillRect({screen.x,screen.y,scaled,scaled},collisionColor);}}
    if(document_.viewport().showGrid&&scaled>=4&&!visible.empty()){for(int x=visible.firstX;x<=visible.lastX+1;++x){const auto p=worldToScreen({x*static_cast<int>(data.tileSize),0},viewport);renderer.fillRect({p.x,viewport.y,1,viewport.height},gridColor);}for(int y=visible.firstY;y<=visible.lastY+1;++y){const auto p=worldToScreen({0,y*static_cast<int>(data.tileSize)},viewport);renderer.fillRect({viewport.x,p.y,viewport.width,1},gridColor);}}
}

void EditorApp::drawEntities(render::Renderer2D& renderer,core::RectI viewport) const{
    const auto drawPoint=[&](SelectionKind kind,simulation::PersistentInstanceId id,core::WorldPointI point){if(drag_.kind==DragState::Kind::move&&document_.selection().kind==kind&&document_.selection().instanceId==id)point=drag_.worldCurrent;const auto p=worldToScreen(point,viewport);const int radius=std::max(3,static_cast<int>(4*zoom()));renderer.fillRect({p.x-radius,p.y-radius,radius*2+1,radius*2+1},categoryColor(kind));if(document_.selection().kind==kind&&document_.selection().instanceId==id)outline(renderer,{p.x-radius-2,p.y-radius-2,radius*2+5,radius*2+5},selectedColor);};
    for(const auto& value:document_.data().enemies)drawPoint(SelectionKind::enemy,value.id,value.position);
    for(const auto& value:document_.data().npcs)drawPoint(SelectionKind::npc,value.id,value.position);
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
        else if(tool==EditorTool::select){auto hit=hitTest(worldPoint);if(hit){document_.selection()=*hit;if(hit->kind==SelectionKind::region){const auto it=std::find_if(document_.regions().begin(),document_.regions().end(),[&](const auto& r){return r.id==hit->instanceId;});if(it!=document_.regions().end()&&std::abs(worldPoint.x-(it->bounds.x+it->bounds.width))<8&&std::abs(worldPoint.y-(it->bounds.y+it->bounds.height))<8){drag_.kind=DragState::Kind::regionResize;drag_.regionStart=it->bounds;drag_.worldCurrent=worldPoint;return;}}if(hit->kind!=SelectionKind::none){drag_.kind=DragState::Kind::move;drag_.worldStart=worldPoint;drag_.worldCurrent=snapped;if(hit->kind==SelectionKind::region){auto it=std::find_if(document_.regions().begin(),document_.regions().end(),[&](const auto& r){return r.id==hit->instanceId;});drag_.entityStart={it->bounds.x,it->bounds.y};}else if(hit->kind==SelectionKind::playerSpawn){auto it=std::find_if(document_.data().playerSpawns.begin(),document_.data().playerSpawns.end(),[&](const auto& v){return v.id.value()==hit->authoredId;});drag_.entityStart=it->position;}else if(hit->kind==SelectionKind::mapLink){auto it=std::find_if(document_.data().links.begin(),document_.data().links.end(),[&](const auto& v){return v.id==hit->authoredId;});drag_.entityStart={it->trigger.x,it->trigger.y};}else{auto point=[&](){if(hit->kind==SelectionKind::enemy)return std::find_if(document_.data().enemies.begin(),document_.data().enemies.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;if(hit->kind==SelectionKind::npc)return std::find_if(document_.data().npcs.begin(),document_.data().npcs.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;if(hit->kind==SelectionKind::object)return std::find_if(document_.data().objects.begin(),document_.data().objects.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;return std::find_if(document_.data().pickups.begin(),document_.data().pickups.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;}();drag_.entityStart=point;}}}else document_.selection().clear();}
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
    if(input.undoPressed) { document_.undo(); }
    if(input.redoPressed){std::string error;if(!document_.redo(error))status_=error;}
}

std::optional<EditorSelection> EditorApp::hitTest(core::WorldPointI point) const{
    const auto near=[&](core::WorldPointI p){return std::abs(point.x-p.x)<=8&&std::abs(point.y-p.y)<=8;};
    for(auto it=document_.data().pickups.rbegin();it!=document_.data().pickups.rend();++it)if(near(it->position))return EditorSelection{SelectionKind::pickup,it->id,{}};
    for(auto it=document_.data().objects.rbegin();it!=document_.data().objects.rend();++it)if(near(it->position))return EditorSelection{SelectionKind::object,it->id,{}};
    for(auto it=document_.data().npcs.rbegin();it!=document_.data().npcs.rend();++it)if(near(it->position))return EditorSelection{SelectionKind::npc,it->id,{}};
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
    else if(selectedCategory_==game::AuthoringCategory::npc)execute(std::make_unique<PlaceEntityCommand>(maps::NpcPlacement{id,selectedDefinition_,point,game::gameplay::FacingDirection::down}));
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
    y=panel.height-150;ui.label("VALIDATION",panel.x+8,y);y+=16;validationCache_.refreshIfNeeded(document_,content_);const auto& report=validationCache_.structural();const auto& semanticReport=validationCache_.semantic();ui.label("Structural: "+std::to_string(report.errorCount())+" errors",panel.x+8,y);y+=14;ui.label("Semantic: "+std::to_string(semanticReport.warningCount())+" warnings",panel.x+8,y);y+=16;for(const auto& issue:report.issues){if(y>panel.height-12)break;ui.label(issue.message.substr(0,32),panel.x+8,y);y+=12;}for(const auto& issue:semanticReport.issues){if(y>panel.height-12)break;ui.label(issue.message.substr(0,32),panel.x+8,y);y+=12;}
}

void EditorApp::drawNewMapDialog(EditorUiContext& ui,const EditorInputState& input){const int width=360,height=220,x=(framebuffer_->width()-width)/2,y=(framebuffer_->height()-height)/2;ui.panel({x,y,width,height});ui.label("NEW MAP",x+12,y+12);std::array<std::string*,4> fields{&newMapId_,&newMapWidth_,&newMapHeight_,&newMapTileSize_};const std::array<const char*,4> names{"MapId","Width","Height","Tile Size"};
    for(int i=0;i<4;++i){const int fy=y+38+i*34;ui.label(names[static_cast<std::size_t>(i)],x+12,fy);if(ui.button({x+110,fy-4,230,24},*fields[static_cast<std::size_t>(i)],newMapField_==i))newMapField_=i;}
    if(!input.textInput.empty()){auto& field=*fields[static_cast<std::size_t>(newMapField_)];for(char c:input.textInput)if(c>=32&&c<127)field.push_back(c);}if(input.backspacePressed){auto& field=*fields[static_cast<std::size_t>(newMapField_)];if(!field.empty())field.pop_back();}
    const bool create=ui.button({x+110,y+184,100,24},"CREATE")||input.enterPressed;const bool cancel=ui.button({x+220,y+184,100,24},"CANCEL")||input.escapePressed;
    if(create){const int w=parsePositive(newMapWidth_),h=parsePositive(newMapHeight_),tile=parsePositive(newMapTileSize_);if(!newMapId_.empty()&&w>0&&h>0&&tile>0){try{document_=EditorDocument::newAuthoredMap(simulation::MapId{newMapId_},static_cast<std::uint32_t>(w),static_cast<std::uint32_t>(h),static_cast<std::uint16_t>(tile),content_);validationCache_.invalidate();newMapDialog_=false;frameMap(viewportBounds_);status_="Created authored canvas: choose a semantic tile and paint";}catch(const std::exception& e){status_=e.what();}}else status_="New Map fields are invalid";}if(cancel)newMapDialog_=false;
}

void EditorApp::frameMap(core::RectI viewport) noexcept{const double mapWidth=static_cast<double>(document_.data().width)*document_.data().tileSize,mapHeight=static_cast<double>(document_.data().height)*document_.data().tileSize;std::size_t best=0;for(std::size_t i=0;i<zoomSteps.size();++i)if(mapWidth*zoomSteps[i]<=viewport.width&&mapHeight*zoomSteps[i]<=viewport.height)best=i;document_.viewport().zoomStep=best;document_.viewport().worldX=(mapWidth-viewport.width/zoom())/2.0;document_.viewport().worldY=(mapHeight-viewport.height/zoom())/2.0;}

void EditorApp::execute(std::unique_ptr<EditorCommand> command){std::string error;if(!document_.execute(std::move(command),error))status_=error;}
void EditorApp::cancelActiveGesture() noexcept{
    drag_={};
    previewPanning_=false;
    previewRectangleDragging_=false;
}
void EditorApp::shellCommand(EditorShellCommand command){
    if(command==EditorShellCommand::newMap){playtest_.stop();newMapDialog_=true;}
    else if(command==EditorShellCommand::undo)document_.undo();
    else if(command==EditorShellCommand::redo){std::string error;if(!document_.redo(error))status_=error;}
    else if(command==EditorShellCommand::toggleGrid)document_.viewport().showGrid=!document_.viewport().showGrid;
    else if(command==EditorShellCommand::playtest)togglePlaytest();
    else if(command==EditorShellCommand::mapMode){resetContentEditState();resetContentPreviewState();contentMode_=false;status_="Map mode";}
    else if(command==EditorShellCommand::contentMode){resetContentEditState();resetContentPreviewState();contentMode_=true;status_="Content mode";}
    else if(command==EditorShellCommand::saveAll){std::string error;if(!saveAll(error))status_=error;}
    else if(command==EditorShellCommand::validateWorkspace){status_=validateWorkspace()?"Workspace valid":"Workspace invalid; see diagnostics";}
    else frameMap(viewportBounds_);
}
bool EditorApp::open(const std::filesystem::path& path,std::string& error){auto loaded=EditorDocument::open(path,content_,error);if(!loaded)return false;playtest_.stop();document_=std::move(*loaded);contentMode_=false;validationCache_.invalidate();frameMap(viewportBounds_);return true;}
bool EditorApp::save(std::string& error){if(contentMode_)return saveAll(error);return document_.save(content_,error);}bool EditorApp::saveAs(const std::filesystem::path& path,std::string& error){if(contentMode_){error="Content workspace Save As is not supported; save the selected workspace files";return false;}return document_.saveAs(path,content_,error);}
bool EditorApp::autosave(std::string& error){error.clear();if(!document_.dirty())return true;const auto path=document_.autosavePath();if(!path)return true;const bool saved=document_.saveBackup(*path,content_,error);if(saved)status_="Autosave backup written: "+path->string();return saved;}
bool EditorApp::saveAll(std::string& error){if(contentWorkspace_&&contentWorkspace_->dirty()){if(!contentWorkspace_->saveAll(error))return false;status_="Content workspace saved";}refreshContentRegistry();if(!contentMode_&&document_.dirty())return document_.save(content_,error);error.clear();return true;}
bool EditorApp::validateWorkspace(){const bool contentValid=contentWorkspace_&&contentWorkspace_->validateWorkspace();refreshContentRegistry();const bool visualValid=runVisualValidation();if(!contentValid){status_="Workspace invalid; asset validation skipped";}else if(!visualValid){status_="Content valid; visual asset diagnostics reported";}else{status_="Workspace and visual assets valid";}return contentValid&&visualValid;}
void EditorApp::refreshContentRegistry(){
    if (!contentWorkspace_) return;
    if (contentWorkspace_->compiledRegistry()) content_ = *contentWorkspace_->compiledRegistry();
    else content_ = game::GameContentRegistry{};
    runtimeTilesets_ = game::RuntimeTilesetCatalog(content_.tilesets());
    visualValidationAttempted_ = false;
    visualValidationRevision_ = noContentIndex;
    visualDiagnostics_.clear();
    resetContentEditState();
    visualPreview_.invalidate();
    previewSelectionRect_.reset();
}
void EditorApp::resetContentEditState() noexcept {
    contentEditKey_.reset();
    contentEditValues_.fill({});
    contentEditFrame_ = noContentIndex;
    contentEditMarker_ = noContentIndex;
    contentFocusedField_ = -1;
    contentStaticSourceEnabled_ = false;
    contentBindingState_ = PreviewClipState::idle;
    contentBindingAction_ = {};
}
void EditorApp::resetContentPreviewState() noexcept {
    previewClipState_ = PreviewClipState::idle;
    previewActionId_ = {};
    previewFacing_ = game::gameplay::FacingDirection::down;
    previewPanning_ = false;
    previewRectangleDragging_ = false;
    previewSelectionRect_.reset();
}
bool EditorApp::runVisualValidation(){
    visualDiagnostics_.clear();
    visualValidationAttempted_ = true;
    visualValidationRevision_ = contentWorkspace_ ? contentWorkspace_->revision() : noContentIndex;
    if (!contentWorkspace_ || !contentWorkspace_->compiledRegistry() || !decoder_) return false;
    const std::optional<std::filesystem::path> workspaceRoot = contentWorkspace_->writable()
        ? std::optional<std::filesystem::path>{contentWorkspace_->root()} : std::nullopt;
    game::presentation::VisualContentLoader loader(*decoder_);
    const auto loaded = loader.load(*contentWorkspace_->compiledRegistry(),
                                    {assetRoot_, workspaceRoot});
    visualDiagnostics_ = loaded.diagnostics;
    return static_cast<bool>(loaded);
}
void EditorApp::togglePlaytest(){if(playtest_.active()){playtest_.stop();status_="Playtest stopped; editor document unchanged";return;}std::string error;if(!playtest_.start(document_.data(),content_,error)){status_=error;return;}status_="Playtest active: runtime world built from document snapshot";}
std::string EditorApp::windowTitle() const{std::string title=contentMode_?"Dungeon Underworld - Content Studio - ":"Dungeon Underworld - Map Maker - ";if(contentMode_)title.append(contentWorkspace_->builtinReadOnly()?"Builtin content":"Content workspace");else title.append(document_.data().id.value());if(hasUnsavedChanges())title+=" *";return title;}
void EditorApp::updateStatus(core::RectI viewport,const EditorInputState& input){if(contentMode_)return;if(input.pointer.x>=viewport.x&&input.pointer.y>=viewport.y&&input.pointer.x<viewport.x+viewport.width&&input.pointer.y<viewport.y+viewport.height){const auto world=screenToWorld({input.pointer.x,input.pointer.y},viewport);std::ostringstream out;out<<"World "<<world.x<<','<<world.y<<"  Tile "<<world.x/document_.data().tileSize<<','<<world.y/document_.data().tileSize<<"  Zoom "<<static_cast<int>(zoom()*100)<<'%';status_=out.str();}}

} // namespace underworld::editor
