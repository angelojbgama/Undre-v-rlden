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
#include <iterator>
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
    fieldPreviewGridCell,
    fieldNewDefinitionId,
    fieldNewDefinitionFile,
    fieldProjectileVisual, fieldProjectileFacing, fieldProjectileSpeed,
    fieldProjectileLifetime, fieldProjectileHitbox, fieldProjectileOffsetsDown,
    fieldProjectileOffsetsUp, fieldProjectileOffsetsLeft, fieldProjectileOffsetsRight,
    fieldAttackKind, fieldAttackDamage, fieldAttackKnockback, fieldAttackTotal,
    fieldAttackCooldown, fieldAttackMinimum, fieldAttackMaximum, fieldAttackVisual,
    fieldAttackProjectile, fieldAttackMeleeDown, fieldAttackMeleeUp,
    fieldAttackMeleeLeft, fieldAttackMeleeRight, fieldAttackTimelineTick,
    fieldAttackTimelineKind,
    fieldBehaviorDetection, fieldBehaviorDisengage, fieldBehaviorIdle, fieldBehaviorWander,
    fieldEnemyVisual, fieldEnemyBehavior, fieldEnemyFaction, fieldEnemyHealth,
    fieldEnemySpeed, fieldEnemyCollision, fieldEnemyHurtbox, fieldEnemyReward,
    fieldEnemyAttack,
    fieldItemVisual, fieldItemCategory, fieldItemStack, fieldItemUseAmount,
    fieldItemEquipmentSlot, fieldItemEquipmentHealth, fieldItemEquipmentAttack,
    fieldPickupVisual, fieldPickupBounds, fieldPickupPayload, fieldPickupAmount,
    fieldPickupItem, fieldPickupQuantity,
    fieldObjectVisual, fieldObjectInteractBounds, fieldObjectCapacity,
    fieldObjectDestructibleHealth, fieldObjectDestructibleHurtbox,
    fieldObjectDestructibleDuration, fieldObjectDoorState, fieldObjectDoorBounds,
    fieldObjectActivationMode, fieldObjectActivationInitial, fieldObjectActivationBounds,
    fieldNpcVisual, fieldNpcInteractionBounds, fieldNpcDialogue, fieldNpcTag,
    fieldDialogueEntry, fieldDialogueNodeId, fieldDialogueSpeaker, fieldDialoguePage,
    fieldDialogueNext, fieldDialogueChoiceLabel, fieldDialogueChoiceTarget,
    fieldDialogueFlag, fieldDialogueActionTarget, fieldDialogueConditionKind,
    fieldDialogueActionKind,
    fieldQuestTitle, fieldQuestTag, fieldQuestReward, fieldQuestObjectiveId,
    fieldQuestObjectiveKind, fieldQuestObjectiveTarget, fieldQuestObjectiveCount,
    fieldQuestObjectiveDescription,
    fieldRewardProfileExperience, fieldRewardPickup, fieldRewardChance,
    fieldRewardMinimum, fieldRewardMaximum,
    fieldRewardGrantExperience, fieldRewardGrantGold, fieldRewardGrantItem,
    fieldRewardGrantQuantity,
    fieldShopItem, fieldShopBuyPrice, fieldShopSellPrice,
    fieldProgressionHealth, fieldProgressionThreshold,
    fieldPresentationLifetime, fieldPresentationDuration, fieldPresentationPriority,
    fieldPresentationShake, fieldPresentationOverlay, fieldPresentationOverlayMode,
    fieldPresentationOverlayPulse, fieldPresentationOverlayLayer, fieldPresentationVision,
    fieldPresentationFade,
    fieldTilesetDisplay, fieldTilesetPath, fieldTilesetSize, fieldTilesetColumns,
    fieldTilesetRows, fieldDescriptorDisplay, fieldDescriptorCategory,
    fieldDescriptorTag, fieldSemanticTileset, fieldSemanticSource, fieldSemanticFamily,
    fieldSemanticPreferredLayer, fieldSemanticRole, fieldSemanticTopology,
    fieldSemanticEdges, fieldStampDisplay, fieldStampSize, fieldStampAnchor,
    fieldStampCell, fieldStampConfidence,
    contentEditFieldCount
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

    if (ui.button({8, 2, 42, 18}, "TILES", mapPaletteTab_ == MapPaletteTab::tiles)) mapPaletteTab_ = MapPaletteTab::tiles;
    if (ui.button({52, 2, 42, 18}, "SEM", mapPaletteTab_ == MapPaletteTab::semantics)) mapPaletteTab_ = MapPaletteTab::semantics;
    if (ui.button({96, 2, 42, 18}, "STAMPS", mapPaletteTab_ == MapPaletteTab::stamps)) mapPaletteTab_ = MapPaletteTab::stamps;
    if (ui.button({140, 2, 42, 18}, "ENT", mapPaletteTab_ == MapPaletteTab::entities)) mapPaletteTab_ = MapPaletteTab::entities;
    ui.label("LAYERS",8,26);int y=40;
    for(std::size_t i=0;i<document_.data().layers.size();++i){
        const auto& layer=document_.data().layers[i];
        if(ui.button({8,y,110,18},layer.name,i==document_.activeLayer())) {
            document_.activeLayer()=i;
            layerNameEdit_=layer.name;
            layerNameFocused_=false;
        }
        auto& state=document_.layerStates()[i];
        if(ui.toggle({120,y,30,18},state.visible?"ON":"OFF",state.visible))state.visible=!state.visible;
        if(ui.toggle({152,y,30,18},state.locked?"L":"U",state.locked))state.locked=!state.locked;
        y+=20;
    }
    if (ui.button({8, y, 52, 18}, "ADD", false)) {
        layerNameEdit_ = "Layer " + std::to_string(document_.data().layers.size() + 1);
        execute(std::make_unique<AddLayerCommand>(document_.data().layers.size(), layerNameEdit_));
    }
    if (ui.button({64, y, 52, 18}, "DEL", false)) execute(std::make_unique<RemoveLayerCommand>(document_.activeLayer()));
    if (ui.button({120, y, 28, 18}, "<", false) && document_.activeLayer() > 0)
        execute(std::make_unique<MoveLayerCommand>(document_.activeLayer(), document_.activeLayer() - 1));
    if (ui.button({152, y, 28, 18}, ">", false) && document_.activeLayer() + 1 < document_.data().layers.size())
        execute(std::make_unique<MoveLayerCommand>(document_.activeLayer(), document_.activeLayer() + 1));
    y += 20;
    if (document_.activeLayer() < document_.data().layers.size()) {
        if (!layerNameFocused_ && layerNameEdit_.empty()) {
            layerNameEdit_ = document_.data().layers[document_.activeLayer()].name;
        }
        if (ui.textField({8, y, 174, 18}, layerNameEdit_, layerNameFocused_)) {
            layerNameFocused_=true;
        }
        if (input.escapePressed) {
            layerNameEdit_=document_.data().layers[document_.activeLayer()].name;
            layerNameFocused_=false;
        } else if (input.enterPressed && layerNameFocused_) {
            if (!layerNameEdit_.empty()) execute(std::make_unique<RenameLayerCommand>(document_.activeLayer(), layerNameEdit_));
            layerNameFocused_=false;
        }
    }
    y += 22;
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
    if(ui.button({8,y,85,18},"ENTITY",document_.activeTool()==EditorTool::entityPlace))document_.activeTool()=EditorTool::entityPlace;
    if(ui.button({97,y,85,18},"REGION",document_.activeTool()==EditorTool::regionCreate))document_.activeTool()=EditorTool::regionCreate;
    y+=20;
    if(ui.button({8,y,85,18},"STAMP",document_.activeTool()==EditorTool::stampPlace))document_.activeTool()=EditorTool::stampPlace;
    if(ui.button({97,y,85,18},"SELECT TILES",document_.activeTool()==EditorTool::tileSelection))document_.activeTool()=EditorTool::tileSelection;
    y+=24;
    if(ui.button({8,y,85,18},"RULES",mapPaletteTab_==MapPaletteTab::rules))mapPaletteTab_=MapPaletteTab::rules;
    if(ui.button({97,y,85,18},"ENCOUNTERS",mapPaletteTab_==MapPaletteTab::encounters))mapPaletteTab_=MapPaletteTab::encounters;
    y+=24;

    if (mapPaletteTab_ == MapPaletteTab::entities) {
        ui.label("CONTENT",8,y);y+=14;
        struct PaletteEntry final { simulation::DefinitionId id; std::string label; game::AuthoringCategory category; };
        std::vector<PaletteEntry> entries;
        const auto addEntry = [&](simulation::DefinitionId id, game::AuthoringCategory category,
                                  std::string fallback) {
            const auto duplicate = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
                return entry.id == id && entry.category == category;
            });
            if (duplicate != entries.end()) return;
            auto label = std::move(fallback);
            for (const auto& descriptor : content_.authoringDescriptors()) {
                if (descriptor.definitionId == id && descriptor.category == category) {
                    label = descriptor.displayName + " (" + std::string(id.value()) + ")";
                    break;
                }
            }
            entries.push_back({std::move(id), std::move(label), category});
        };
        for (const auto& descriptor:content_.authoringDescriptors()) {
            if (descriptor.category == game::AuthoringCategory::enemy ||
                descriptor.category == game::AuthoringCategory::object ||
                descriptor.category == game::AuthoringCategory::pickup ||
                descriptor.category == game::AuthoringCategory::npc) {
                addEntry(descriptor.definitionId, descriptor.category,
                         std::string(descriptor.definitionId.value()));
            }
        }
        for (const auto& entry : content_.enemies().values())
            addEntry(entry.first, game::AuthoringCategory::enemy, std::string(entry.first.value()));
        for (const auto& entry : content_.objects().values())
            addEntry(entry.first, game::AuthoringCategory::object, std::string(entry.first.value()));
        for (const auto& definition : content_.pickups())
            addEntry(definition.id, game::AuthoringCategory::pickup, std::string(definition.id.value()));
        for (const auto& entry : content_.npcs().values())
            addEntry(entry.first, game::AuthoringCategory::npc, std::string(entry.first.value()));
        std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            if (left.category != right.category) return left.category < right.category;
            return left.id.value() < right.id.value();
        });
        for(const auto& descriptor:entries){
            if(y+18>viewportHeight-210)break;
            if(ui.button({8,y,174,18},descriptor.label,selectedDefinition_==descriptor.id&&document_.activeTool()==EditorTool::entityPlace)){
                selectedDefinition_=descriptor.id;selectedCategory_=descriptor.category;document_.activeTool()=EditorTool::entityPlace;
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
    }
    if (ui.button({8, y, 174, 18}, playtest_.active() ? "STOP PLAYTEST" : "PLAYTEST",
                  playtest_.active())) {
        togglePlaytest();
    }

    const auto& semantics = content_.authoringSemantics();
    static const std::array<std::string, 6> semanticFamilies{"ALL", "masonry", "ledge", "architectural_detail", "detail", "RAW"};
    if (mapPaletteTab_ == MapPaletteTab::semantics) {
        ui.label("SEMANTIC TILES",8,y+8); y+=20;
        if(ui.button({8,y,28,18},"<")) semanticFamilyIndex_=(semanticFamilyIndex_+semanticFamilies.size()-1)%semanticFamilies.size();
        if(ui.button({38,y,116,18},semanticFamilies[semanticFamilyIndex_],true)){}
        if(ui.button({156,y,26,18},">")) semanticFamilyIndex_=(semanticFamilyIndex_+1)%semanticFamilies.size();
        rawPalette_=semanticFamilies[semanticFamilyIndex_]=="RAW"; y+=22;
    } else if (mapPaletteTab_ == MapPaletteTab::tiles) {
        rawPalette_=true;
    }
    if(mapPaletteTab_ == MapPaletteTab::stamps && !semantics.stamps().empty()) {
        ui.label("STAMPS",8,y+8); y+=20;
        const auto& stamp=semantics.stamps()[std::min(selectedStamp_,semantics.stamps().size()-1)];
        if(ui.button({8,y,28,18},"<")) selectedStamp_=(selectedStamp_+semantics.stamps().size()-1)%semantics.stamps().size();
        if(ui.button({38,y,116,18},stamp.displayName,document_.activeTool()==EditorTool::stampPlace)) document_.activeTool()=EditorTool::stampPlace;
        if(ui.button({156,y,26,18},">")) selectedStamp_=(selectedStamp_+1)%semantics.stamps().size();
        y+=22;
    }
    if (mapPaletteTab_ == MapPaletteTab::tiles || mapPaletteTab_ == MapPaletteTab::semantics) {
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
        if(ui.pointerInside(cell)&&input.pointer.leftPressed){selectedTile_=index;tileBrush_={selectedTileset_,1,1,{selectedTileReference()}};paletteDragging_=true;paletteDragStart_=index;paletteDragCurrent_=index;document_.activeTool()=EditorTool::tilePencil;}
            if(paletteDragging_&&ui.pointerInside(cell)&&input.pointer.leftDown)paletteDragCurrent_=index;
            if(paletteDragging_&&ui.pointerInside(cell)&&input.pointer.leftReleased)paletteDragCurrent_=index;
        }
        if (paletteDragging_ && input.pointer.leftReleased) {
            const auto columns = std::max<std::uint32_t>(1, selectedDefinition->columns);
            const auto sx = paletteDragStart_ % columns; const auto sy = paletteDragStart_ / columns;
            const auto ex = paletteDragCurrent_ % columns; const auto ey = paletteDragCurrent_ / columns;
            const auto minX = std::min(sx, ex); const auto minY = std::min(sy, ey);
            const auto maxX = std::max(sx, ex); const auto maxY = std::max(sy, ey);
            tileBrush_.tilesetId = selectedTileset_; tileBrush_.width = maxX - minX + 1; tileBrush_.height = maxY - minY + 1; tileBrush_.cells.clear();
            for (std::uint32_t brushY = minY; brushY <= maxY; ++brushY) for (std::uint32_t brushX = minX; brushX <= maxX; ++brushX)
                tileBrush_.cells.push_back({selectedTileset_, brushY * columns + brushX, tileFlipX_ ? world::TileFlags::flipX : world::TileFlags::none});
            selectedTile_ = paletteDragStart_; paletteDragging_ = false;
        }
        if (paletteDragging_ && input.pointer.leftReleased && !ui.pointerInside({0, paletteTop, leftPanelWidth, paletteHeight})) paletteDragging_ = false;
    } else if (selectedDefinition) { ui.label("Tileset image unavailable",8,y+4); }
    }

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
    if (ui.pointerInside({right.x, right.y + 118, right.width, right.height - 260}) &&
        input.pointer.wheelDelta != 0) {
        contentInspectorScroll_ = std::clamp(
            contentInspectorScroll_ - (input.pointer.wheelDelta / 120) * 36, 0, 2200);
    }
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
            pendingStampFromSelection_.reset();
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

    const auto createDefinition = [&]() {
        if (!contentWorkspace_->writable()) { status_ = "Builtin content is read-only"; return; }
        if (newContentDefinitionId_.empty() || newContentDefinitionFile_.empty()) {
            status_ = "Enter a definition ID and destination .json file"; return;
        }
        const auto file = std::filesystem::path{newContentDefinitionFile_};
        const auto id = simulation::DefinitionId{newContentDefinitionId_};
        std::string error;
        bool created = false;
        switch (selectedContentCategory_) {
        case ContentDefinitionKind::projectile: { game::content::AuthoredProjectile value; value.id=id; created = contentWorkspace_->addProjectile(file, value, error); break; }
        case ContentDefinitionKind::attack: { game::content::AuthoredAttack value; value.id=id; created = contentWorkspace_->addAttack(file, value, error); break; }
        case ContentDefinitionKind::behavior: { game::content::AuthoredBehaviorProfile value; value.id=id; created = contentWorkspace_->addBehavior(file, value, error); break; }
        case ContentDefinitionKind::enemy: { game::content::AuthoredEnemy value; value.id=id; value.maximumHealth=1; value.collisionBody={0,0,16,16}; value.hurtbox={0,0,16,16}; created=contentWorkspace_->addEnemy(file,value,error); break; }
        case ContentDefinitionKind::item: { game::content::AuthoredItem value; value.id=id; value.stackLimit=1; created=contentWorkspace_->addItem(file,value,error); break; }
        case ContentDefinitionKind::object: { game::content::AuthoredWorldObject value; value.id=id; created=contentWorkspace_->addObject(file,value,error); break; }
        case ContentDefinitionKind::pickup: { game::content::AuthoredPickup value; value.id=id; value.collectionBounds={0,0,16,16}; created=contentWorkspace_->addPickup(file,value,error); break; }
        case ContentDefinitionKind::npc: { game::content::AuthoredNpc value; value.id=id; value.interaction.bounds={0,0,16,16}; created=contentWorkspace_->addNpc(file,value,error); break; }
        case ContentDefinitionKind::dialogue: { game::content::AuthoredDialogue value; value.id=id; created=contentWorkspace_->addDialogue(file,value,error); break; }
        case ContentDefinitionKind::quest: { game::content::AuthoredQuest value; value.id=id; created=contentWorkspace_->addQuest(file,value,error); break; }
        case ContentDefinitionKind::playerProgression: { game::content::AuthoredPlayerProgression value; value.id=id; value.baseStats.maximumHealth=1; created=contentWorkspace_->addPlayerProgression(file,value,error); break; }
        case ContentDefinitionKind::rewardProfile: { game::content::AuthoredRewardProfile value; value.id=id; created=contentWorkspace_->addRewardProfile(file,value,error); break; }
        case ContentDefinitionKind::rewardGrant: { game::content::AuthoredRewardGrant value; value.id=id; created=contentWorkspace_->addRewardGrant(file,value,error); break; }
        case ContentDefinitionKind::shop: { game::content::AuthoredShop value; value.id=id; created=contentWorkspace_->addShop(file,value,error); break; }
        case ContentDefinitionKind::presentationEffect: { game::content::AuthoredPresentationEffect value; value.id=id; created=contentWorkspace_->addPresentationEffect(file,value,error); break; }
        case ContentDefinitionKind::visualImage: { game::content::AuthoredVisualImage value; value.id=id; created=contentWorkspace_->addVisualImage(file,value,error); break; }
        case ContentDefinitionKind::staticSprite: { game::content::AuthoredStaticSprite value; value.id=id; created=contentWorkspace_->addStaticSprite(file,value,error); break; }
        case ContentDefinitionKind::animation: { game::content::AuthoredAnimation value; value.id=id; created=contentWorkspace_->addAnimation(file,value,error); break; }
        case ContentDefinitionKind::enemyVisual: { game::content::AuthoredEnemyVisual value; value.id=id; created=contentWorkspace_->addEnemyVisual(file,value,error); break; }
        case ContentDefinitionKind::objectVisual: { game::content::AuthoredWorldObjectVisual value; value.id=id; created=contentWorkspace_->addObjectVisual(file,value,error); break; }
        case ContentDefinitionKind::npcVisual: { game::content::AuthoredNpcVisualSet value; value.id=id; created=contentWorkspace_->addNpcVisual(file,value,error); break; }
        case ContentDefinitionKind::tileset: { game::content::AuthoredTileset value; value.id=id; value.displayName=newContentDefinitionId_; value.relativeAssetPath="assets/tileset.png"; value.tileSize=16; value.columns=1; value.rows=1; created=contentWorkspace_->addTileset(file,value,error); break; }
        case ContentDefinitionKind::authoringDescriptor: { game::content::AuthoringDescriptor value; value.definitionId=id; value.displayName=newContentDefinitionId_; created=contentWorkspace_->addAuthoringDescriptor(file,value,error); break; }
        case ContentDefinitionKind::tileSemantic: { game::content::AuthoredTileSemantic value; value.id=id; value.family="unclassified"; created=contentWorkspace_->addTileSemantic(file,value,error); break; }
        case ContentDefinitionKind::stamp: { game::content::AuthoredStamp value = pendingStampFromSelection_.value_or(game::content::AuthoredStamp{}); value.id=id; if (value.displayName.empty()) value.displayName=newContentDefinitionId_; if (value.width == 0) value.width=1; if (value.height == 0) value.height=1; created=contentWorkspace_->addStamp(file,value,error); if (created) pendingStampFromSelection_.reset(); break; }
        default: error = "This category is reserved for the unified map workflow (18D)"; break;
        }
        if (!created) { status_ = error; return; }
        selectedContentDefinition_ = ContentDefinitionKey{selectedContentCategory_, id};
        refreshContentRegistry();
        selectedContentDefinition_ = ContentDefinitionKey{selectedContentCategory_, id};
        status_ = "Definition created";
    };

    ui.label("INSPECTOR", right.x + 8, 10);
    if (!selectedContentDefinition_) {
        ui.label("Select a definition", right.x + 8, 30);
        if (contentWorkspace_->writable()) {
            if (newContentDefinitionFile_.empty() && !contentWorkspace_->files().empty()) {
                std::error_code relativeError;
                newContentDefinitionFile_ = std::filesystem::relative(
                    contentWorkspace_->files().front().path, contentWorkspace_->root(), relativeError).generic_string();
            }
            ui.label("NEW DEFINITION", right.x + 8, 58);
            if (ui.textField({right.x + 8, 72, right.width - 16, 20}, newContentDefinitionId_,
                             contentFocusedField_ == fieldNewDefinitionId)) contentFocusedField_ = fieldNewDefinitionId;
            if (ui.textField({right.x + 8, 108, right.width - 16, 20}, newContentDefinitionFile_,
                             contentFocusedField_ == fieldNewDefinitionFile)) contentFocusedField_ = fieldNewDefinitionFile;
            ui.label("id / destination .json", right.x + 8, 132);
            if (ui.button({right.x + 8, 150, right.width - 16, 20}, "CREATE DEFINITION")) createDefinition();
            if (ui.button({right.x + 8, 174, right.width - 16, 20}, "CREATE CONTENT FILE") &&
                contentWorkspace_->writable()) {
                std::string fileError;
                if (contentWorkspace_->createContentFile(newContentDefinitionFile_, fileError)) {
                    status_ = "Content JSON v5 file created";
                } else status_ = fileError;
            }
        }
    } else {
        const auto& key = *selectedContentDefinition_;
        ui.label(std::string(key.id.value()), right.x + 8, 30);
        if (contentWorkspace_->writable() &&
            ui.button({right.x + right.width - 86, 30, 78, 20}, "NEW")) {
            selectedContentDefinition_.reset();
            resetContentEditState();
            status_ = "New definition";
            return;
        }
        if (const auto* source = contentWorkspace_->sourceFor(key)) {
            ui.label("Source:", right.x + 8, 52);
            ui.label(source->sourcePath.filename().string(), right.x + 8, 66);
            if (!source->jsonPath.empty()) ui.label(source->jsonPath, right.x + 8, 80);
        }
        ui.label(contentWorkspace_->writable() ? "Typed document API: editable" : "Builtin content - read only",
                 right.x + 8, 104);
        if (contentWorkspace_->writable() && ui.button({right.x + right.width - 86, 102, 78, 20}, "DELETE")) {
            std::string removeError;
            if (contentWorkspace_->removeDefinition(key, removeError)) {
                selectedContentDefinition_.reset();
                resetContentEditState();
                refreshContentRegistry();
                status_ = "Definition deleted";
                return;
            }
            status_ = removeError;
        }
        int inspectorY = 124 - contentInspectorScroll_;
        const bool gameplayCategory = key.kind == ContentDefinitionKind::projectile ||
            key.kind == ContentDefinitionKind::attack || key.kind == ContentDefinitionKind::behavior ||
            key.kind == ContentDefinitionKind::enemy || key.kind == ContentDefinitionKind::item ||
            key.kind == ContentDefinitionKind::pickup || key.kind == ContentDefinitionKind::object ||
            key.kind == ContentDefinitionKind::npc || key.kind == ContentDefinitionKind::dialogue ||
            key.kind == ContentDefinitionKind::quest || key.kind == ContentDefinitionKind::playerProgression ||
            key.kind == ContentDefinitionKind::rewardProfile || key.kind == ContentDefinitionKind::rewardGrant ||
            key.kind == ContentDefinitionKind::shop || key.kind == ContentDefinitionKind::presentationEffect ||
            key.kind == ContentDefinitionKind::tileset || key.kind == ContentDefinitionKind::authoringDescriptor ||
            key.kind == ContentDefinitionKind::tileSemantic || key.kind == ContentDefinitionKind::stamp;
        if (gameplayCategory) {
            drawGameplayContentInspector(ui, input, right, key, inspectorY);
        } else if (key.kind == ContentDefinitionKind::visualImage) {
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

void EditorApp::drawGameplayContentInspector(EditorUiContext& ui, const EditorInputState& input,
                                              core::RectI panel, const ContentDefinitionKey& key,
                                              int inspectorY) {
    if (!contentWorkspace_) return;
    const bool writable = contentWorkspace_->writable();
    auto field = [&](std::string_view label, int id, int y, int width = -1) {
        ui.label(label, panel.x + 8, y);
        if (ui.textField({panel.x + 8, y + 12, width < 0 ? panel.width - 16 : width, 20},
                         contentEditValues_[id], contentFocusedField_ == id)) {
            contentFocusedField_ = id;
        }
    };
    auto reference = [&](std::string_view label, ContentDefinitionKind kind, int id, int y) {
        field(label, id, y, panel.width - 82);
        const auto ids = [&] {
            std::vector<simulation::DefinitionId> result;
            for (const auto& candidate : contentWorkspace_->index()) {
                if (candidate.kind == kind) result.push_back(candidate.id);
            }
            std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
                return left.value() < right.value();
            });
            return result;
        }();
        if (ui.button({panel.x + panel.width - 68, y + 12, 20, 20}, "<") && !ids.empty()) {
            const auto it = std::find(ids.begin(), ids.end(), simulation::DefinitionId{contentEditValues_[id]});
            const auto index = it == ids.begin() || it == ids.end()
                ? ids.size() - 1 : static_cast<std::size_t>(it - ids.begin() - 1);
            contentEditValues_[id] = std::string(ids[index].value());
            contentFocusedField_ = id;
        }
        if (ui.button({panel.x + panel.width - 46, y + 12, 20, 20}, ">") && !ids.empty()) {
            const auto it = std::find(ids.begin(), ids.end(), simulation::DefinitionId{contentEditValues_[id]});
            const auto index = it == ids.end() ? 0 : (static_cast<std::size_t>(it - ids.begin() + 1) % ids.size());
            contentEditValues_[id] = std::string(ids[index].value());
            contentFocusedField_ = id;
        }
        if (ui.button({panel.x + panel.width - 24, y + 12, 16, 20}, "O") && !ids.empty()) {
            const auto it = std::find(ids.begin(), ids.end(), simulation::DefinitionId{contentEditValues_[id]});
            if (it != ids.end()) {
                selectedContentCategory_ = kind;
                selectedContentDefinition_ = ContentDefinitionKey{kind, *it};
                resetContentEditState();
            }
        }
        if (!contentEditValues_[id].empty() &&
            std::find(ids.begin(), ids.end(), simulation::DefinitionId{contentEditValues_[id]}) == ids.end()) {
            ui.label("MISSING", panel.x + panel.width - 68, y + 34);
        }
    };
    auto enumButton = [&](std::string_view label, int id, int y,
                          std::span<const std::string_view> values) {
        ui.label(label, panel.x + 8, y);
        if (ui.button({panel.x + 8, y + 12, panel.width - 16, 20}, contentEditValues_[id], false)) {
            auto it = std::find(values.begin(), values.end(), contentEditValues_[id]);
            const auto next = it == values.end() ? 0 : (static_cast<std::size_t>(it - values.begin() + 1) % values.size());
            contentEditValues_[id] = std::string(values[next]);
            contentFocusedField_ = id;
            return true;
        }
        return false;
    };
    auto rectText = [](const world::AabbI& value) {
        return std::to_string(value.x) + "," + std::to_string(value.y) + "," +
               std::to_string(value.width) + "," + std::to_string(value.height);
    };
    auto parseRect = [](std::string_view value) -> std::optional<world::AabbI> {
        const auto parsed = parseIntegerList<4>(value);
        if (!parsed) return std::nullopt;
        return world::AabbI{(*parsed)[0], (*parsed)[1], (*parsed)[2], (*parsed)[3]};
    };
    auto boxText = [](const game::gameplay::DirectionalBoxDefinition& value) {
        return std::to_string(value.offsetX) + "," + std::to_string(value.offsetY) + "," +
               std::to_string(value.width) + "," + std::to_string(value.height);
    };
    auto parseBox = [](std::string_view value) -> std::optional<game::gameplay::DirectionalBoxDefinition> {
        const auto parsed = parseIntegerList<4>(value);
        if (!parsed) return std::nullopt;
        return game::gameplay::DirectionalBoxDefinition{(*parsed)[0], (*parsed)[1],
                                                        (*parsed)[2], (*parsed)[3]};
    };
    auto parseOffset = [](std::string_view value) -> std::optional<core::WorldPointI> {
        const auto parsed = parseIntegerList<2>(value);
        if (!parsed) return std::nullopt;
        return core::WorldPointI{(*parsed)[0], (*parsed)[1]};
    };
    auto parseU64 = [](std::string_view value) -> std::optional<std::uint64_t> {
        if (value.empty()) return std::nullopt;
        std::uint64_t result{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        return error == std::errc{} && end == value.data() + value.size()
            ? std::optional<std::uint64_t>{result} : std::nullopt;
    };
    auto refresh = [&](std::string_view message) {
        refreshContentRegistry();
        status_ = std::string(message);
    };
    if (key.kind == ContentDefinitionKind::tileset ||
        key.kind == ContentDefinitionKind::tileSemantic ||
        key.kind == ContentDefinitionKind::stamp) {
        if (ui.button({panel.x + 8, inspectorY, panel.width - 16, 20},
                      key.kind == ContentDefinitionKind::tileset ? "SHOW IN MAP PALETTE" :
                      key.kind == ContentDefinitionKind::tileSemantic ? "USE SEMANTIC IN MAP" :
                      "USE STAMP IN MAP")) {
            if (key.kind == ContentDefinitionKind::tileset) {
                selectedTileset_ = key.id;
                mapPaletteTab_ = MapPaletteTab::tiles;
            } else if (key.kind == ContentDefinitionKind::tileSemantic) {
                if (const auto* semantic = contentWorkspace_->tileSemantic(key.id)) {
                    selectedTileset_ = semantic->tilesetId;
                    selectedTile_ = semantic->sourceIndex;
                    tileBrush_ = {selectedTileset_, 1, 1, {selectedTileReference()}};
                    rawPalette_ = false;
                }
                mapPaletteTab_ = MapPaletteTab::semantics;
            } else {
                const auto& stamps = content_.authoringSemantics().stamps();
                const auto it = std::find_if(stamps.begin(), stamps.end(),
                    [&](const auto& stamp) { return stamp.id == key.id; });
                if (it != stamps.end()) selectedStamp_ = static_cast<std::size_t>(it - stamps.begin());
                mapPaletteTab_ = MapPaletteTab::stamps;
            }
            contentMode_ = false;
            status_ = "Map palette selected";
            return;
        }
        inspectorY += 28;
    }
    if (key.kind == ContentDefinitionKind::enemy || key.kind == ContentDefinitionKind::npc ||
        key.kind == ContentDefinitionKind::object || key.kind == ContentDefinitionKind::pickup) {
        const auto category = key.kind == ContentDefinitionKind::enemy ? game::AuthoringCategory::enemy :
            key.kind == ContentDefinitionKind::npc ? game::AuthoringCategory::npc :
            key.kind == ContentDefinitionKind::object ? game::AuthoringCategory::object : game::AuthoringCategory::pickup;
        if (ui.button({panel.x + 8, inspectorY, 108, 20}, "PLACE IN MAP") && writable) {
            selectedDefinition_ = key.id; selectedCategory_ = category; contentMode_ = false;
            document_.activeTool() = EditorTool::entityPlace; status_ = "Select a map position to place this definition"; return;
        }
        if (ui.button({panel.x + 120, inspectorY, 108, 20}, "FIND IN MAP")) {
            std::optional<EditorSelection> found;
            if (key.kind == ContentDefinitionKind::enemy) for (const auto& value : document_.data().enemies) if (value.definitionId == key.id) { found = EditorSelection{SelectionKind::enemy, value.id, {}}; document_.viewport().worldX = value.position.x - viewportBounds_.width / (2.0 * zoom()); document_.viewport().worldY = value.position.y - viewportBounds_.height / (2.0 * zoom()); break; }
            if (key.kind == ContentDefinitionKind::npc) for (const auto& value : document_.data().npcs) if (value.definitionId == key.id) { found = EditorSelection{SelectionKind::npc, value.id, {}}; break; }
            if (key.kind == ContentDefinitionKind::object) for (const auto& value : document_.data().objects) if (value.definitionId == key.id) { found = EditorSelection{SelectionKind::object, value.id, {}}; break; }
            if (key.kind == ContentDefinitionKind::pickup) for (const auto& value : document_.data().pickups) if (value.definitionId == key.id) { found = EditorSelection{SelectionKind::pickup, value.id, {}}; break; }
            if (found) { document_.selection() = *found; contentMode_ = false; status_ = "Found definition usage in current map"; return; }
            status_ = "Definition has no placement in current map";
        }
    }
    const auto facingNames = std::array<std::string_view, 4>{"down", "up", "left", "right"};
    const auto attackKinds = std::array<std::string_view, 2>{"meleeHitbox", "projectile"};
    const auto factions = std::array<std::string_view, 4>{"player", "enemy", "environment", "neutral"};
    const auto itemCategories = std::array<std::string_view, 4>{"consumable", "equipment", "key", "misc"};
    const auto doorStates = std::array<std::string_view, 3>{"locked", "closed", "open"};
    const auto activationModes = std::array<std::string_view, 2>{"interactToggle", "playerPressure"};
    const auto lifetimeNames = std::array<std::string_view, 2>{"transient", "persistent"};
    const auto equipmentSlots = std::array<std::string_view, 2>{"armor", "accessory"};
    const auto overlayModes = std::array<std::string_view, 3>{"constant", "linearFadeOut", "pulse"};
    const auto compositionLayers = std::array<std::string_view, 2>{"world", "final"};

    if (key.kind == ContentDefinitionKind::projectile) {
        const auto* value = contentWorkspace_->projectile(key.id);
        if (!value) return;
        if (contentEditKey_ != key) {
            resetContentEditState(); contentEditKey_ = key;
            contentEditValues_[fieldProjectileVisual] = std::string(value->visualId.value());
            contentEditValues_[fieldProjectileFacing] = std::string(facingNames[static_cast<int>(value->canonicalFacing)]);
            contentEditValues_[fieldProjectileSpeed] = std::to_string(value->speedPixelsPerTick);
            contentEditValues_[fieldProjectileLifetime] = std::to_string(value->lifetimeTicks);
            contentEditValues_[fieldProjectileHitbox] = std::to_string(value->hitboxWidth) + "," + std::to_string(value->hitboxHeight);
            const auto offsetText = [](core::WorldPointI point) { return std::to_string(point.x) + "," + std::to_string(point.y); };
            contentEditValues_[fieldProjectileOffsetsDown] = offsetText(value->spawnOffsets.values[0]);
            contentEditValues_[fieldProjectileOffsetsUp] = offsetText(value->spawnOffsets.values[1]);
            contentEditValues_[fieldProjectileOffsetsLeft] = offsetText(value->spawnOffsets.values[2]);
            contentEditValues_[fieldProjectileOffsetsRight] = offsetText(value->spawnOffsets.values[3]);
        }
        reference("visualId (StaticSprite)", ContentDefinitionKind::staticSprite, fieldProjectileVisual, inspectorY);
        const bool facingChanged = enumButton("canonicalFacing", fieldProjectileFacing, inspectorY + 52, facingNames);
        field("speedPixelsPerTick", fieldProjectileSpeed, inspectorY + 102);
        field("lifetimeTicks", fieldProjectileLifetime, inspectorY + 150);
        field("hitbox width,height", fieldProjectileHitbox, inspectorY + 198);
        field("spawn down x,y", fieldProjectileOffsetsDown, inspectorY + 246);
        field("spawn up x,y", fieldProjectileOffsetsUp, inspectorY + 294);
        field("spawn left x,y", fieldProjectileOffsetsLeft, inspectorY + 342);
        field("spawn right x,y", fieldProjectileOffsetsRight, inspectorY + 390);
        if (writable && (input.enterPressed || facingChanged) &&
            (contentFocusedField_ >= fieldProjectileVisual && contentFocusedField_ <= fieldProjectileOffsetsRight)) {
            auto updated = *value; std::string error; bool accepted = false;
            if (contentFocusedField_ == fieldProjectileVisual) { updated.visualId = simulation::DefinitionId{contentEditValues_[fieldProjectileVisual]}; accepted = true; }
            else if (contentFocusedField_ == fieldProjectileFacing) {
                const auto it = std::find(facingNames.begin(), facingNames.end(), contentEditValues_[fieldProjectileFacing]);
                if (it != facingNames.end()) { updated.canonicalFacing = static_cast<game::gameplay::FacingDirection>(it - facingNames.begin()); accepted = true; }
            } else if (contentFocusedField_ == fieldProjectileSpeed) { if (const auto p = parseIntegerList<1>(contentEditValues_[fieldProjectileSpeed])) { updated.speedPixelsPerTick = (*p)[0]; accepted = true; } }
            else if (contentFocusedField_ == fieldProjectileLifetime) { if (const auto p = parseUnsigned(contentEditValues_[fieldProjectileLifetime])) { updated.lifetimeTicks = *p; accepted = true; } }
            else if (contentFocusedField_ == fieldProjectileHitbox) { if (const auto p = parseIntegerList<2>(contentEditValues_[fieldProjectileHitbox])) { updated.hitboxWidth = (*p)[0]; updated.hitboxHeight = (*p)[1]; accepted = true; } }
            else { const auto p = parseOffset(contentEditValues_[contentFocusedField_]); if (p) { updated.spawnOffsets.values[contentFocusedField_ - fieldProjectileOffsetsDown] = *p; accepted = true; } }
            if (!accepted && error.empty()) error = "projectile field has an invalid value";
            if (accepted && contentWorkspace_->updateProjectile(key.id, updated, error)) refresh("Projectile updated"); else if (!accepted || !error.empty()) status_ = error;
        }
        return;
    }

    if (key.kind == ContentDefinitionKind::behavior) {
        const auto* value = contentWorkspace_->behavior(key.id); if (!value) return;
        if (contentEditKey_ != key) { resetContentEditState(); contentEditKey_ = key;
            contentEditValues_[fieldBehaviorDetection] = std::to_string(value->detectionRangePixels);
            contentEditValues_[fieldBehaviorDisengage] = std::to_string(value->disengageRangePixels);
            contentEditValues_[fieldBehaviorIdle] = std::to_string(value->idleDurationTicks);
            contentEditValues_[fieldBehaviorWander] = std::to_string(value->wanderDurationTicks); }
        field("detectionRangePixels", fieldBehaviorDetection, inspectorY);
        field("disengageRangePixels", fieldBehaviorDisengage, inspectorY + 48);
        field("idleDurationTicks", fieldBehaviorIdle, inspectorY + 96);
        field("wanderDurationTicks", fieldBehaviorWander, inspectorY + 144);
        if (writable && input.enterPressed && contentFocusedField_ >= fieldBehaviorDetection && contentFocusedField_ <= fieldBehaviorWander) {
            auto updated = *value; std::string error; bool ok = false;
            if (contentFocusedField_ == fieldBehaviorDetection) { if (auto p = parseIntegerList<1>(contentEditValues_[contentFocusedField_])) { updated.detectionRangePixels = (*p)[0]; ok = true; } }
            else if (contentFocusedField_ == fieldBehaviorDisengage) { if (auto p = parseIntegerList<1>(contentEditValues_[contentFocusedField_])) { updated.disengageRangePixels = (*p)[0]; ok = true; } }
            else if (contentFocusedField_ == fieldBehaviorIdle) { if (auto p = parseUnsigned(contentEditValues_[contentFocusedField_])) { updated.idleDurationTicks = *p; ok = true; } }
            else if (auto p = parseUnsigned(contentEditValues_[contentFocusedField_])) { updated.wanderDurationTicks = *p; ok = true; }
            if (!ok) error = "behavior field has an invalid value";
            if (ok && contentWorkspace_->updateBehavior(key.id, updated, error)) refresh("Behavior updated"); else status_ = error;
        }
        return;
    }

    if (key.kind == ContentDefinitionKind::item) {
        const auto* value = contentWorkspace_->item(key.id); if (!value) return;
        if (contentEditKey_ != key) { resetContentEditState(); contentEditKey_ = key;
            contentEditValues_[fieldItemVisual] = std::string(value->visualId.value());
            contentEditValues_[fieldItemCategory] = std::array<std::string_view,4>{"consumable","equipment","key","misc"}[static_cast<int>(value->category)];
            contentEditValues_[fieldItemStack] = std::to_string(value->stackLimit);
            contentEditValues_[fieldItemUseAmount] = value->use ? std::to_string(value->use->amount) : "0";
            contentEditValues_[fieldItemEquipmentSlot] = value->equipment ? (value->equipment->slot == game::content::AuthoredEquipmentSlot::armor ? "armor" : "accessory") : "armor";
            contentEditValues_[fieldItemEquipmentHealth] = value->equipment ? std::to_string(value->equipment->modifiers.maximumHealthBonus) : "0";
            contentEditValues_[fieldItemEquipmentAttack] = value->equipment ? std::to_string(value->equipment->modifiers.playerAttackDamageBonus) : "0"; }
        reference("visualId (StaticSprite)", ContentDefinitionKind::staticSprite, fieldItemVisual, inspectorY);
        const bool categoryChanged = enumButton("category", fieldItemCategory, inspectorY + 52, itemCategories);
        field("stackLimit", fieldItemStack, inspectorY + 102);
        const bool useToggle = ui.button({panel.x + 8, inspectorY + 150, panel.width - 16, 20}, value->use ? "USE ENABLED" : "USE DISABLED", value->use.has_value());
        field("use restoreHealth amount", fieldItemUseAmount, inspectorY + 178);
        const bool equipmentToggle = ui.button({panel.x + 8, inspectorY + 226, panel.width - 16, 20}, value->equipment ? "EQUIPMENT ENABLED" : "EQUIPMENT DISABLED", value->equipment.has_value());
        const bool slotChanged = enumButton("equipment slot", fieldItemEquipmentSlot, inspectorY + 254, equipmentSlots);
        field("maximumHealthBonus", fieldItemEquipmentHealth, inspectorY + 304);
        field("playerAttackDamageBonus", fieldItemEquipmentAttack, inspectorY + 352);
        if (writable && (input.enterPressed || categoryChanged || useToggle || equipmentToggle || slotChanged) && contentFocusedField_ >= fieldItemVisual && contentFocusedField_ <= fieldItemEquipmentAttack) {
            auto updated = *value; std::string error; bool ok = true;
            if (contentFocusedField_ == fieldItemVisual) updated.visualId = simulation::DefinitionId{contentEditValues_[fieldItemVisual]};
            else if (contentFocusedField_ == fieldItemCategory) { const auto it = std::find(itemCategories.begin(), itemCategories.end(), contentEditValues_[fieldItemCategory]); if (it == itemCategories.end()) ok=false; else updated.category = static_cast<game::gameplay::ItemCategory>(it-itemCategories.begin()); }
            else if (contentFocusedField_ == fieldItemStack) { if (auto p=parseUnsigned(contentEditValues_[fieldItemStack])) updated.stackLimit=*p; else ok=false; }
            else if (contentFocusedField_ == fieldItemUseAmount || useToggle) { if (useToggle) { if (updated.use) updated.use.reset(); else updated.use = game::gameplay::ItemUseDefinition{}; } else if (auto p=parseIntegerList<1>(contentEditValues_[fieldItemUseAmount])) { if (!updated.use) updated.use = game::gameplay::ItemUseDefinition{}; updated.use->amount=(*p)[0]; } else ok=false; }
            else if ((contentFocusedField_ >= fieldItemEquipmentSlot && contentFocusedField_ <= fieldItemEquipmentAttack) || equipmentToggle) { if (equipmentToggle) { if (updated.equipment) updated.equipment.reset(); else updated.equipment = game::content::AuthoredEquipment{}; } else { if (!updated.equipment) updated.equipment = game::content::AuthoredEquipment{}; if (contentFocusedField_ == fieldItemEquipmentSlot) { const auto it=std::find(equipmentSlots.begin(),equipmentSlots.end(),contentEditValues_[fieldItemEquipmentSlot]); if(it==equipmentSlots.end()) ok=false; else updated.equipment->slot=static_cast<game::content::AuthoredEquipmentSlot>(it-equipmentSlots.begin()); } else if (auto p=parseIntegerList<1>(contentEditValues_[contentFocusedField_])) { if(contentFocusedField_==fieldItemEquipmentHealth) updated.equipment->modifiers.maximumHealthBonus=(*p)[0]; else updated.equipment->modifiers.playerAttackDamageBonus=(*p)[0]; } else ok=false; } }
            if (!ok) error = "item field has an invalid value";
            if (ok && contentWorkspace_->updateItem(key.id, updated, error)) refresh("Item updated"); else if (!ok || !error.empty()) status_ = error;
        }
        return;
    }

    if (key.kind == ContentDefinitionKind::presentationEffect) {
        const auto* value = contentWorkspace_->presentationEffect(key.id); if (!value) return;
        auto colorText = [](const core::ColorRGBA8& c) { return std::to_string(c.r)+","+std::to_string(c.g)+","+std::to_string(c.b)+","+std::to_string(c.a); };
        if (contentEditKey_ != key) { resetContentEditState(); contentEditKey_ = key;
            contentEditValues_[fieldPresentationLifetime] = value->lifetime == game::presentation::PresentationEffectLifetime::transient ? "transient" : "persistent";
            contentEditValues_[fieldPresentationDuration] = std::to_string(value->durationTicks);
            contentEditValues_[fieldPresentationPriority] = std::to_string(value->priority);
            contentEditValues_[fieldPresentationShake] = value->cameraShake ? std::to_string(value->cameraShake->amplitudePixels) : "";
            contentEditValues_[fieldPresentationOverlay] = value->overlay ? colorText(value->overlay->color) : "";
            contentEditValues_[fieldPresentationOverlayMode] = value->overlay ?
                std::string(overlayModes[static_cast<int>(value->overlay->mode)]) : "constant";
            contentEditValues_[fieldPresentationOverlayPulse] = value->overlay ?
                std::to_string(value->overlay->pulsePeriodTicks) : "0";
            contentEditValues_[fieldPresentationOverlayLayer] = value->overlay ?
                std::string(compositionLayers[static_cast<int>(value->overlay->layer)]) : "world";
            contentEditValues_[fieldPresentationVision] = value->visionMask ? std::to_string(value->visionMask->innerRadiusPixels)+","+std::to_string(value->visionMask->outerRadiusPixels)+","+std::to_string(value->visionMask->outsideAlpha)+","+colorText(value->visionMask->color) : "";
            contentEditValues_[fieldPresentationFade] = value->fade ? colorText(value->fade->color)+","+std::to_string(value->fade->startAlpha)+","+std::to_string(value->fade->endAlpha) : ""; }
        const bool lifeChanged = enumButton("lifetime", fieldPresentationLifetime, inspectorY, lifetimeNames);
        field("durationTicks", fieldPresentationDuration, inspectorY + 48);
        field("priority", fieldPresentationPriority, inspectorY + 96);
        const bool shake = ui.button({panel.x + 8, inspectorY + 144, panel.width - 16, 20}, value->cameraShake ? "CAMERA SHAKE ON" : "CAMERA SHAKE OFF", value->cameraShake.has_value());
        field("shake amplitude", fieldPresentationShake, inspectorY + 172);
        const bool overlay = ui.button({panel.x + 8, inspectorY + 220, panel.width - 16, 20}, value->overlay ? "OVERLAY ON" : "OVERLAY OFF", value->overlay.has_value());
        field("overlay color r,g,b,a", fieldPresentationOverlay, inspectorY + 248);
        const bool overlayModeChanged = enumButton("overlay mode", fieldPresentationOverlayMode, inspectorY + 276, overlayModes);
        field("overlay pulsePeriodTicks", fieldPresentationOverlayPulse, inspectorY + 324);
        const bool overlayLayerChanged = enumButton("overlay layer", fieldPresentationOverlayLayer, inspectorY + 372, compositionLayers);
        const bool vision = ui.button({panel.x + 8, inspectorY + 420, panel.width - 16, 20}, value->visionMask ? "VISION MASK ON" : "VISION MASK OFF", value->visionMask.has_value());
        field("vision inner,outer,alpha,r,g,b,a", fieldPresentationVision, inspectorY + 448);
        const bool fade = ui.button({panel.x + 8, inspectorY + 496, panel.width - 16, 20}, value->fade ? "FADE ON" : "FADE OFF", value->fade.has_value());
        field("fade r,g,b,a,start,end", fieldPresentationFade, inspectorY + 524);
        if (writable && (input.enterPressed || lifeChanged || shake || overlay || overlayModeChanged || overlayLayerChanged || vision || fade) && contentFocusedField_ >= fieldPresentationLifetime && contentFocusedField_ <= fieldPresentationFade) {
            auto updated=*value; std::string error; bool ok=true;
            if (contentFocusedField_==fieldPresentationLifetime || lifeChanged) { const auto it=std::find(lifetimeNames.begin(),lifetimeNames.end(),contentEditValues_[fieldPresentationLifetime]); if(it==lifetimeNames.end())ok=false; else updated.lifetime=static_cast<game::presentation::PresentationEffectLifetime>(it-lifetimeNames.begin()); }
            if (contentFocusedField_==fieldPresentationDuration) { if(auto p=parseUnsigned(contentEditValues_[fieldPresentationDuration]))updated.durationTicks=*p;else ok=false; }
            else if(contentFocusedField_==fieldPresentationPriority){if(auto p=parseIntegerList<1>(contentEditValues_[fieldPresentationPriority]))updated.priority=(*p)[0];else ok=false;}
            else if(shake){if(updated.cameraShake)updated.cameraShake.reset();else updated.cameraShake=game::presentation::CameraShakeDefinition{};}
            else if(contentFocusedField_==fieldPresentationShake){if(auto p=parseIntegerList<1>(contentEditValues_[fieldPresentationShake])){if(!updated.cameraShake)updated.cameraShake={};updated.cameraShake->amplitudePixels=(*p)[0];}else ok=false;}
            else if(overlay){if(updated.overlay)updated.overlay.reset();else updated.overlay=game::presentation::ColorOverlayDefinition{};}
            else if(contentFocusedField_==fieldPresentationOverlay){if(auto p=parseIntegerList<4>(contentEditValues_[fieldPresentationOverlay])){if(!updated.overlay)updated.overlay={};updated.overlay->color={static_cast<std::uint8_t>((*p)[0]),static_cast<std::uint8_t>((*p)[1]),static_cast<std::uint8_t>((*p)[2]),static_cast<std::uint8_t>((*p)[3])};}else ok=false;}
            else if(contentFocusedField_==fieldPresentationOverlayMode||overlayModeChanged){const auto it=std::find(overlayModes.begin(),overlayModes.end(),contentEditValues_[fieldPresentationOverlayMode]);if(it==overlayModes.end())ok=false;else{if(!updated.overlay)updated.overlay={};updated.overlay->mode=static_cast<game::presentation::PresentationOverlayMode>(it-overlayModes.begin());}}
            else if(contentFocusedField_==fieldPresentationOverlayPulse){if(auto p=parseUnsigned(contentEditValues_[fieldPresentationOverlayPulse])){if(!updated.overlay)updated.overlay={};updated.overlay->pulsePeriodTicks=*p;}else ok=false;}
            else if(contentFocusedField_==fieldPresentationOverlayLayer||overlayLayerChanged){const auto it=std::find(compositionLayers.begin(),compositionLayers.end(),contentEditValues_[fieldPresentationOverlayLayer]);if(it==compositionLayers.end())ok=false;else{if(!updated.overlay)updated.overlay={};updated.overlay->layer=static_cast<game::presentation::PresentationCompositionLayer>(it-compositionLayers.begin());}}
            else if(vision){if(updated.visionMask)updated.visionMask.reset();else updated.visionMask=game::presentation::VisionMaskDefinition{};}
            else if(contentFocusedField_==fieldPresentationVision){const auto p=parseIntegerList<7>(contentEditValues_[fieldPresentationVision]);if(p){if(!updated.visionMask)updated.visionMask={};updated.visionMask->innerRadiusPixels=(*p)[0];updated.visionMask->outerRadiusPixels=(*p)[1];updated.visionMask->outsideAlpha=static_cast<std::uint8_t>((*p)[2]);updated.visionMask->color={static_cast<std::uint8_t>((*p)[3]),static_cast<std::uint8_t>((*p)[4]),static_cast<std::uint8_t>((*p)[5]),static_cast<std::uint8_t>((*p)[6])};}else ok=false;}
            else if(fade){if(updated.fade)updated.fade.reset();else updated.fade=game::presentation::FadeDefinition{};}
            else if(contentFocusedField_==fieldPresentationFade){const auto p=parseIntegerList<6>(contentEditValues_[fieldPresentationFade]);if(p){if(!updated.fade)updated.fade={};updated.fade->color={static_cast<std::uint8_t>((*p)[0]),static_cast<std::uint8_t>((*p)[1]),static_cast<std::uint8_t>((*p)[2]),static_cast<std::uint8_t>((*p)[3])};updated.fade->startAlpha=static_cast<std::uint8_t>((*p)[4]);updated.fade->endAlpha=static_cast<std::uint8_t>((*p)[5]);}else ok=false;}
            if(!ok)error="presentation effect field has an invalid value";
            if(ok&&contentWorkspace_->updatePresentationEffect(key.id,updated,error))refresh("Presentation effect updated");else if(!ok||!error.empty())status_=error;
        }
        return;
    }

    // The remaining inspectors use the same typed document boundary. They expose
    // the scalar fields and list mutation entry points; detailed nested records
    // remain editable through the full typed DTO update API used by tests/tools.
    auto showReadOnlyFields = [&](std::string_view title, std::initializer_list<std::pair<std::string_view, std::string>> values) {
        ui.label(title, panel.x + 8, inspectorY);
        int y = inspectorY + 24;
        for (const auto& [name, value] : values) { ui.label(name, panel.x + 8, y); ui.label(value, panel.x + 104, y); y += 18; }
    };
    if (key.kind == ContentDefinitionKind::attack) {
        const auto* value = contentWorkspace_->attack(key.id);
        if (!value) return;
        const auto timelineKinds = std::array<std::string_view, 3>{
            "activateHitbox", "deactivateHitbox", "spawnProjectile"};
        if (contentEditKey_ != key) {
            resetContentEditState();
            contentEditKey_ = key;
            selectedAnimationFrameIndex_ = 0;
            contentEditValues_[fieldAttackKind] = value->kind == game::gameplay::AttackKind::meleeHitbox
                ? "meleeHitbox" : "projectile";
            contentEditValues_[fieldAttackDamage] = std::to_string(value->damage.amount);
            contentEditValues_[fieldAttackKnockback] = std::to_string(value->damage.knockbackPixels);
            contentEditValues_[fieldAttackTotal] = std::to_string(value->totalTicks);
            contentEditValues_[fieldAttackCooldown] = std::to_string(value->cooldownTicks);
            contentEditValues_[fieldAttackMinimum] = std::to_string(value->minimumRangePixels);
            contentEditValues_[fieldAttackMaximum] = std::to_string(value->maximumRangePixels);
            contentEditValues_[fieldAttackVisual] = std::string(value->visualActionId.value());
            contentEditValues_[fieldAttackProjectile] = value->projectileDefinitionId
                ? std::string(value->projectileDefinitionId->value()) : std::string{};
            if (value->meleeHitboxes) {
                contentEditValues_[fieldAttackMeleeDown] = boxText(value->meleeHitboxes->values[0]);
                contentEditValues_[fieldAttackMeleeUp] = boxText(value->meleeHitboxes->values[1]);
                contentEditValues_[fieldAttackMeleeLeft] = boxText(value->meleeHitboxes->values[2]);
                contentEditValues_[fieldAttackMeleeRight] = boxText(value->meleeHitboxes->values[3]);
            }
        }
        const bool kindChanged = enumButton("kind", fieldAttackKind, inspectorY, attackKinds);
        field("damage", fieldAttackDamage, inspectorY + 48);
        field("knockbackPixels", fieldAttackKnockback, inspectorY + 96);
        field("totalTicks", fieldAttackTotal, inspectorY + 144);
        field("cooldownTicks", fieldAttackCooldown, inspectorY + 192);
        field("minimumRangePixels", fieldAttackMinimum, inspectorY + 240);
        field("maximumRangePixels", fieldAttackMaximum, inspectorY + 288);
        field("visualActionId", fieldAttackVisual, inspectorY + 336);
        if (value->kind == game::gameplay::AttackKind::projectile) {
            reference("projectileDefinitionId", ContentDefinitionKind::projectile,
                      fieldAttackProjectile, inspectorY + 384);
        } else {
            const bool boxesEnabled = ui.button(
                {panel.x + 8, inspectorY + 384, panel.width - 16, 20},
                value->meleeHitboxes ? "MELEE HITBOXES ON" : "MELEE HITBOXES OFF",
                value->meleeHitboxes.has_value());
            field("down offsetX,offsetY,w,h", fieldAttackMeleeDown, inspectorY + 412);
            field("up offsetX,offsetY,w,h", fieldAttackMeleeUp, inspectorY + 460);
            field("left offsetX,offsetY,w,h", fieldAttackMeleeLeft, inspectorY + 508);
            field("right offsetX,offsetY,w,h", fieldAttackMeleeRight, inspectorY + 556);
            if (boxesEnabled && writable) {
                auto updated = *value;
                if (updated.meleeHitboxes) updated.meleeHitboxes.reset();
                else updated.meleeHitboxes = game::gameplay::DirectionalBoxes{};
                std::string editError;
                if (contentWorkspace_->updateAttack(key.id, updated, editError)) {
                    refresh("Attack melee hitboxes toggled"); return;
                }
                status_ = editError;
            }
        }
        const int timelineY = inspectorY + (value->kind == game::gameplay::AttackKind::projectile ? 432 : 604);
        ui.label("TIMELINE (fixed gameplay ticks)", panel.x + 8, timelineY);
        const std::size_t eventIndex = value->timeline.empty() ? noContentIndex :
            std::min(selectedAnimationFrameIndex_, value->timeline.size() - 1);
        const int rows = 4;
        for (int row = 0; row < rows; ++row) {
            const auto index = static_cast<std::size_t>(row);
            if (index >= value->timeline.size()) break;
            if (ui.button({panel.x + 8, timelineY + 16 + row * 18, panel.width - 16, 16},
                          "event " + std::to_string(index) + " @" +
                              std::to_string(value->timeline[index].tick), index == eventIndex)) {
                selectedAnimationFrameIndex_ = index;
                contentFocusedField_ = -1;
                contentEditKey_ = key;
            }
        }
        const int eventButtonsY = timelineY + 16 + rows * 18 + 2;
        if (ui.button({panel.x + 8, eventButtonsY, 72, 18}, "ADD EVENT") && writable) {
            auto updated = *value;
            updated.timeline.push_back({1, game::gameplay::AttackTimelineEventKind::activateHitbox});
            std::string editError;
            if (contentWorkspace_->updateAttack(key.id, updated, editError)) {
                selectedAnimationFrameIndex_ = updated.timeline.size() - 1;
                refresh("Attack timeline event added"); return;
            }
            status_ = editError;
        }
        if (ui.button({panel.x + 84, eventButtonsY, 72, 18}, "REMOVE") &&
            writable && eventIndex != noContentIndex) {
            auto updated = *value;
            updated.timeline.erase(updated.timeline.begin() + static_cast<std::ptrdiff_t>(eventIndex));
            std::string editError;
            if (contentWorkspace_->updateAttack(key.id, updated, editError)) {
                selectedAnimationFrameIndex_ = eventIndex == 0 ? 0 : eventIndex - 1;
                refresh("Attack timeline event removed"); return;
            }
            status_ = editError;
        }
        if (eventIndex != noContentIndex) {
            if (ui.button({panel.x + 160, eventButtonsY, 32, 18}, "UP") &&
                writable && eventIndex > 0) {
                auto updated = *value;
                std::swap(updated.timeline[eventIndex], updated.timeline[eventIndex - 1]);
                std::string editError;
                if (contentWorkspace_->updateAttack(key.id, updated, editError)) {
                    --selectedAnimationFrameIndex_; refresh("Attack timeline event moved"); return;
                }
                status_ = editError;
            }
            if (ui.button({panel.x + 196, eventButtonsY, 44, 18}, "DOWN") &&
                writable && eventIndex + 1 < value->timeline.size()) {
                auto updated = *value;
                std::swap(updated.timeline[eventIndex], updated.timeline[eventIndex + 1]);
                std::string editError;
                if (contentWorkspace_->updateAttack(key.id, updated, editError)) {
                    ++selectedAnimationFrameIndex_; refresh("Attack timeline event moved"); return;
                }
                status_ = editError;
            }
            const auto& event = value->timeline[eventIndex];
            if (contentEditFrame_ != eventIndex) {
                contentEditFrame_ = eventIndex;
                contentEditValues_[fieldAttackTimelineTick] = std::to_string(event.tick);
                contentEditValues_[fieldAttackTimelineKind] = std::string(timelineKinds[static_cast<int>(event.kind)]);
            }
            field("selected event tick", fieldAttackTimelineTick, eventButtonsY + 24);
            const bool eventKindChanged = enumButton("selected event kind", fieldAttackTimelineKind,
                                                      eventButtonsY + 72, timelineKinds);
            if (writable && (input.enterPressed || eventKindChanged) &&
                (contentFocusedField_ == fieldAttackTimelineTick ||
                 contentFocusedField_ == fieldAttackTimelineKind)) {
                auto updated = *value;
                bool accepted = true;
                if (contentFocusedField_ == fieldAttackTimelineTick) {
                    if (const auto parsed = parseUnsigned(contentEditValues_[fieldAttackTimelineTick])) {
                        updated.timeline[eventIndex].tick = *parsed;
                    } else accepted = false;
                } else {
                    const auto it = std::find(timelineKinds.begin(), timelineKinds.end(),
                                              contentEditValues_[fieldAttackTimelineKind]);
                    if (it == timelineKinds.end()) accepted = false;
                    else updated.timeline[eventIndex].kind =
                        static_cast<game::gameplay::AttackTimelineEventKind>(it - timelineKinds.begin());
                }
                std::string editError;
                if (accepted && contentWorkspace_->updateAttack(key.id, updated, editError)) {
                    refresh("Attack timeline event updated"); return;
                }
                status_ = accepted ? editError : "attack timeline event has an invalid value";
            }
        }
        if (writable && (input.enterPressed || kindChanged) &&
            ((contentFocusedField_ >= fieldAttackKind && contentFocusedField_ <= fieldAttackProjectile) ||
             (contentFocusedField_ >= fieldAttackMeleeDown && contentFocusedField_ <= fieldAttackMeleeRight))) {
            auto updated = *value;
            std::string editError;
            bool accepted = true;
            if (contentFocusedField_ == fieldAttackKind) {
                const auto it = std::find(attackKinds.begin(), attackKinds.end(),
                                          contentEditValues_[fieldAttackKind]);
                if (it == attackKinds.end()) accepted = false;
                else updated.kind = static_cast<game::gameplay::AttackKind>(it - attackKinds.begin());
            } else if (contentFocusedField_ == fieldAttackDamage) {
                if (const auto p = parseIntegerList<1>(contentEditValues_[fieldAttackDamage])) updated.damage.amount = (*p)[0]; else accepted = false;
            } else if (contentFocusedField_ == fieldAttackKnockback) {
                if (const auto p = parseIntegerList<1>(contentEditValues_[fieldAttackKnockback])) updated.damage.knockbackPixels = (*p)[0]; else accepted = false;
            } else if (contentFocusedField_ == fieldAttackTotal) {
                if (const auto p = parseUnsigned(contentEditValues_[fieldAttackTotal])) updated.totalTicks = *p; else accepted = false;
            } else if (contentFocusedField_ == fieldAttackCooldown) {
                if (const auto p = parseUnsigned(contentEditValues_[fieldAttackCooldown])) updated.cooldownTicks = *p; else accepted = false;
            } else if (contentFocusedField_ == fieldAttackMinimum) {
                if (const auto p = parseIntegerList<1>(contentEditValues_[fieldAttackMinimum])) updated.minimumRangePixels = (*p)[0]; else accepted = false;
            } else if (contentFocusedField_ == fieldAttackMaximum) {
                if (const auto p = parseIntegerList<1>(contentEditValues_[fieldAttackMaximum])) updated.maximumRangePixels = (*p)[0]; else accepted = false;
            } else if (contentFocusedField_ == fieldAttackVisual) {
                updated.visualActionId = simulation::DefinitionId{contentEditValues_[fieldAttackVisual]};
            } else if (contentFocusedField_ == fieldAttackProjectile) {
                if (contentEditValues_[fieldAttackProjectile].empty()) updated.projectileDefinitionId.reset();
                else updated.projectileDefinitionId = simulation::DefinitionId{contentEditValues_[fieldAttackProjectile]};
            }
            if (contentFocusedField_ >= fieldAttackMeleeDown && contentFocusedField_ <= fieldAttackMeleeRight) {
                if (!updated.meleeHitboxes) updated.meleeHitboxes = game::gameplay::DirectionalBoxes{};
                if (const auto parsed = parseBox(contentEditValues_[contentFocusedField_])) {
                    updated.meleeHitboxes->values[contentFocusedField_ - fieldAttackMeleeDown] = *parsed;
                } else accepted = false;
            }
            if (!accepted) editError = "attack field has an invalid value";
            if (accepted && contentWorkspace_->updateAttack(key.id, updated, editError)) {
                refresh("Attack updated"); return;
            }
            if (!editError.empty()) status_ = editError;
        }
        return;
    }
    if (key.kind == ContentDefinitionKind::enemy) {
        const auto* value=contentWorkspace_->enemy(key.id);if(!value)return;
        if(contentEditKey_!=key){resetContentEditState();contentEditKey_=key;contentEditValues_[fieldEnemyVisual]=std::string(value->visualSetId.value());contentEditValues_[fieldEnemyBehavior]=std::string(value->behaviorProfileId.value());contentEditValues_[fieldEnemyFaction]=std::array<std::string_view,4>{"player","enemy","environment","neutral"}[static_cast<int>(value->faction)];contentEditValues_[fieldEnemyHealth]=std::to_string(value->maximumHealth);contentEditValues_[fieldEnemySpeed]=std::to_string(value->movementSpeedSubpixelsPerTick);contentEditValues_[fieldEnemyCollision]=std::to_string(value->collisionBody.offsetX)+","+std::to_string(value->collisionBody.offsetY)+","+std::to_string(value->collisionBody.width)+","+std::to_string(value->collisionBody.height);contentEditValues_[fieldEnemyHurtbox]=std::to_string(value->hurtbox.offsetX)+","+std::to_string(value->hurtbox.offsetY)+","+std::to_string(value->hurtbox.width)+","+std::to_string(value->hurtbox.height);contentEditValues_[fieldEnemyReward]=value->rewardProfileId?std::string(value->rewardProfileId->value()):"";}
        reference("visualSetId",ContentDefinitionKind::enemyVisual,fieldEnemyVisual,inspectorY);reference("behaviorProfileId",ContentDefinitionKind::behavior,fieldEnemyBehavior,inspectorY+48);const bool factionChanged=enumButton("faction",fieldEnemyFaction,inspectorY+96,factions);field("maximumHealth",fieldEnemyHealth,inspectorY+144);field("movementSpeedSubpixelsPerTick",fieldEnemySpeed,inspectorY+192);field("collision offsetX,offsetY,w,h",fieldEnemyCollision,inspectorY+240);field("hurtbox offsetX,offsetY,w,h",fieldEnemyHurtbox,inspectorY+288);reference("rewardProfileId (optional)",ContentDefinitionKind::rewardProfile,fieldEnemyReward,inspectorY+336);reference("new attack ID",ContentDefinitionKind::attack,fieldEnemyAttack,inspectorY+384);
        if(ui.button({panel.x+8,inspectorY+432,panel.width/2-12,20},"ADD ATTACK")&&writable&&!contentEditValues_[fieldEnemyAttack].empty()){auto u=*value;u.attackIds.push_back(simulation::DefinitionId{contentEditValues_[fieldEnemyAttack]});std::string e;if(contentWorkspace_->updateEnemy(key.id,u,e))refresh("Enemy attack added");else status_=e;return;}if(ui.button({panel.x+panel.width/2,inspectorY+432,panel.width/2-8,20},"REMOVE")&&writable&&!value->attackIds.empty()){auto u=*value;u.attackIds.pop_back();std::string e;if(contentWorkspace_->updateEnemy(key.id,u,e))refresh("Enemy attack removed");else status_=e;return;}
        ui.label("ATTACKS", panel.x + 8, inspectorY + 456);
        for (std::size_t index = 0; index < value->attackIds.size() && index < 4; ++index) {
            if (ui.button({panel.x + 8, inspectorY + 472 + static_cast<int>(index) * 18,
                           panel.width - 16, 16}, value->attackIds[index].value(),
                          index == selectedAnimationFrameIndex_)) {
                selectedAnimationFrameIndex_ = index; contentEditFrame_ = noContentIndex;
            }
        }
        const auto attackIndex = value->attackIds.empty() ? noContentIndex :
            std::min(selectedAnimationFrameIndex_, value->attackIds.size() - 1);
        const int attackListButtons = inspectorY + 548;
        if (ui.button({panel.x + 8, attackListButtons, 32, 18}, "UP") && writable && attackIndex > 0) {
            auto updated = *value; std::swap(updated.attackIds[attackIndex], updated.attackIds[attackIndex - 1]);
            std::string editError; if (contentWorkspace_->updateEnemy(key.id, updated, editError)) { --selectedAnimationFrameIndex_; refresh("Enemy attack moved"); return; } status_ = editError;
        }
        if (ui.button({panel.x + 44, attackListButtons, 44, 18}, "DOWN") && writable && attackIndex != noContentIndex && attackIndex + 1 < value->attackIds.size()) {
            auto updated = *value; std::swap(updated.attackIds[attackIndex], updated.attackIds[attackIndex + 1]);
            std::string editError; if (contentWorkspace_->updateEnemy(key.id, updated, editError)) { ++selectedAnimationFrameIndex_; refresh("Enemy attack moved"); return; } status_ = editError;
        }
        if(writable&&(input.enterPressed||factionChanged)&&contentFocusedField_>=fieldEnemyVisual&&contentFocusedField_<=fieldEnemyReward){auto u=*value;std::string e;bool ok=true;if(contentFocusedField_==fieldEnemyVisual)u.visualSetId=simulation::DefinitionId{contentEditValues_[fieldEnemyVisual]};else if(contentFocusedField_==fieldEnemyBehavior)u.behaviorProfileId=simulation::DefinitionId{contentEditValues_[fieldEnemyBehavior]};else if(contentFocusedField_==fieldEnemyFaction){auto it=std::find(factions.begin(),factions.end(),contentEditValues_[fieldEnemyFaction]);if(it==factions.end())ok=false;else u.faction=static_cast<game::gameplay::Faction>(it-factions.begin());}else if(contentFocusedField_==fieldEnemyHealth){if(auto p=parseIntegerList<1>(contentEditValues_[fieldEnemyHealth]))u.maximumHealth=(*p)[0];else ok=false;}else if(contentFocusedField_==fieldEnemySpeed){if(auto p=parseIntegerList<1>(contentEditValues_[fieldEnemySpeed]))u.movementSpeedSubpixelsPerTick=(*p)[0];else ok=false;}else if(contentFocusedField_==fieldEnemyCollision){if(auto p=parseIntegerList<4>(contentEditValues_[fieldEnemyCollision])){u.collisionBody={(*p)[0],(*p)[1],(*p)[2],(*p)[3]};}else ok=false;}else if(contentFocusedField_==fieldEnemyHurtbox){if(auto p=parseIntegerList<4>(contentEditValues_[fieldEnemyHurtbox]))u.hurtbox={(*p)[0],(*p)[1],(*p)[2],(*p)[3]};else ok=false;}else if(contentEditValues_[fieldEnemyReward].empty())u.rewardProfileId.reset();else u.rewardProfileId=simulation::DefinitionId{contentEditValues_[fieldEnemyReward]};if(!ok)e="enemy field has an invalid value";if(ok&&contentWorkspace_->updateEnemy(key.id,u,e))refresh("Enemy updated");else if(!ok||!e.empty())status_=e;}
        return;
    }
    if (key.kind == ContentDefinitionKind::quest) {
        const auto* value = contentWorkspace_->quest(key.id);
        if (!value) return;
        const auto objectiveKinds = std::array<std::string_view, 6>{
            "talk", "kill", "pickup", "enter", "open", "deliver"};
        if (contentEditKey_ != key) {
            resetContentEditState(); contentEditKey_ = key; selectedAnimationFrameIndex_ = 0;
            contentEditValues_[fieldQuestTitle] = value->title;
            contentEditValues_[fieldQuestReward] = value->rewardGrantId
                ? std::string(value->rewardGrantId->value()) : std::string{};
            contentEditValues_[fieldQuestTag] = value->tags.empty() ? "tag" : value->tags.front();
            contentEditMarker_ = value->tags.empty() ? noContentIndex : 0;
            contentEditValues_[fieldQuestObjectiveId] = "objective.1";
            contentEditValues_[fieldQuestObjectiveTarget] = {};
            contentEditValues_[fieldQuestObjectiveDescription] = "Objective";
            contentEditValues_[fieldQuestObjectiveCount] = "1";
        }
        field("title", fieldQuestTitle, inspectorY);
        reference("rewardGrantId", ContentDefinitionKind::rewardGrant, fieldQuestReward, inspectorY + 48);
        field("new tag", fieldQuestTag, inspectorY + 96);
        if (ui.button({panel.x + 8, inspectorY + 144, panel.width / 2 - 12, 20}, "ADD TAG") &&
            writable && !contentEditValues_[fieldQuestTag].empty()) {
            auto updated = *value; updated.tags.push_back(contentEditValues_[fieldQuestTag]);
            std::string editError;
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) { refresh("Quest tag added"); return; }
            status_ = editError;
        }
        if (ui.button({panel.x + panel.width / 2, inspectorY + 144, panel.width / 2 - 8, 20}, "REMOVE TAG") &&
            writable && !value->tags.empty()) {
            auto updated = *value; updated.tags.pop_back(); std::string editError;
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) { refresh("Quest tag removed"); return; }
            status_ = editError;
        }
        const auto tagIndex = value->tags.empty() ? noContentIndex :
            std::min(contentEditMarker_, value->tags.size() - 1);
        ui.label("TAGS", panel.x + 8, inspectorY + 168);
        for (int row = 0; row < 3; ++row) {
            const auto index = static_cast<std::size_t>(row);
            if (index >= value->tags.size()) break;
            if (ui.button({panel.x + 8, inspectorY + 184 + row * 18, panel.width - 16, 16},
                          value->tags[index], index == tagIndex)) {
                contentEditMarker_ = index;
                contentEditValues_[fieldQuestTag] = value->tags[index];
                contentFocusedField_ = -1;
            }
        }
        const int tagButtonsY = inspectorY + 240;
        if (ui.button({panel.x + 8, tagButtonsY, 54, 18}, "ADD") && writable &&
            !contentEditValues_[fieldQuestTag].empty()) {
            auto updated = *value; updated.tags.push_back(contentEditValues_[fieldQuestTag]);
            std::string editError;
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) {
                contentEditMarker_ = updated.tags.size() - 1; refresh("Quest tag added"); return;
            }
            status_ = editError;
        }
        if (ui.button({panel.x + 66, tagButtonsY, 54, 18}, "EDIT") && writable && tagIndex != noContentIndex) {
            auto updated = *value; updated.tags[tagIndex] = contentEditValues_[fieldQuestTag];
            std::string editError;
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) { refresh("Quest tag updated"); return; }
            status_ = editError;
        }
        if (ui.button({panel.x + 124, tagButtonsY, 64, 18}, "REMOVE") && writable && tagIndex != noContentIndex) {
            auto updated = *value; updated.tags.erase(updated.tags.begin() + static_cast<std::ptrdiff_t>(tagIndex));
            std::string editError;
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) {
                contentEditMarker_ = tagIndex == 0 ? noContentIndex : tagIndex - 1; refresh("Quest tag removed"); return;
            }
            status_ = editError;
        }
        if (ui.button({panel.x + 192, tagButtonsY, 32, 18}, "UP") && writable && tagIndex > 0) {
            auto updated = *value; std::swap(updated.tags[tagIndex], updated.tags[tagIndex - 1]);
            std::string editError;
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) { --contentEditMarker_; refresh("Quest tag moved"); return; }
            status_ = editError;
        }
        if (ui.button({panel.x + 228, tagButtonsY, 44, 18}, "DOWN") && writable &&
            tagIndex != noContentIndex && tagIndex + 1 < value->tags.size()) {
            auto updated = *value; std::swap(updated.tags[tagIndex], updated.tags[tagIndex + 1]);
            std::string editError;
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) { ++contentEditMarker_; refresh("Quest tag moved"); return; }
            status_ = editError;
        }
        if (writable && input.enterPressed &&
            (contentFocusedField_ == fieldQuestTitle || contentFocusedField_ == fieldQuestReward ||
             contentFocusedField_ == fieldQuestTag)) {
            auto updated = *value; std::string editError;
            if (contentFocusedField_ == fieldQuestTitle) updated.title = contentEditValues_[fieldQuestTitle];
            else if (contentFocusedField_ == fieldQuestReward) {
                if (contentEditValues_[fieldQuestReward].empty()) updated.rewardGrantId.reset();
                else updated.rewardGrantId = simulation::DefinitionId{contentEditValues_[fieldQuestReward]};
            }
            else if (tagIndex != noContentIndex) updated.tags[tagIndex] = contentEditValues_[fieldQuestTag];
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) { refresh("Quest updated"); return; }
            status_ = editError;
        }
        const int listY = inspectorY + 270;
        ui.label("OBJECTIVES", panel.x + 8, listY);
        const auto objectiveIndex = value->objectives.empty() ? noContentIndex :
            std::min(selectedAnimationFrameIndex_, value->objectives.size() - 1);
        for (int row = 0; row < 4; ++row) {
            const auto index = static_cast<std::size_t>(row);
            if (index >= value->objectives.size()) break;
            if (ui.button({panel.x + 8, listY + 16 + row * 18, panel.width - 16, 16},
                          value->objectives[index].id.value(), index == objectiveIndex)) {
                selectedAnimationFrameIndex_ = index; contentEditFrame_ = noContentIndex; contentFocusedField_ = -1;
            }
        }
        const int objectiveButtonsY = listY + 90;
        if (ui.button({panel.x + 8, objectiveButtonsY, 64, 18}, "ADD") && writable) {
            auto updated = *value;
            updated.objectives.push_back({simulation::DefinitionId{contentEditValues_[fieldQuestObjectiveId]},
                game::gameplay::quests::QuestObjectiveKind::kill,
                simulation::DefinitionId{contentEditValues_[fieldQuestObjectiveTarget]},
                parseUnsigned(contentEditValues_[fieldQuestObjectiveCount]).value_or(1),
                contentEditValues_[fieldQuestObjectiveDescription]});
            std::string editError;
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) {
                selectedAnimationFrameIndex_ = updated.objectives.size() - 1; contentEditFrame_ = noContentIndex;
                refresh("Quest objective added"); return;
            }
            status_ = editError;
        }
        if (ui.button({panel.x + 76, objectiveButtonsY, 64, 18}, "REMOVE") && writable && objectiveIndex != noContentIndex) {
            auto updated = *value;
            updated.objectives.erase(updated.objectives.begin() + static_cast<std::ptrdiff_t>(objectiveIndex));
            std::string editError;
            if (contentWorkspace_->updateQuest(key.id, updated, editError)) {
                selectedAnimationFrameIndex_ = objectiveIndex == 0 ? 0 : objectiveIndex - 1; contentEditFrame_ = noContentIndex;
                refresh("Quest objective removed"); return;
            }
            status_ = editError;
        }
        if (objectiveIndex != noContentIndex) {
            if (ui.button({panel.x + 144, objectiveButtonsY, 32, 18}, "UP") && writable && objectiveIndex > 0) {
                auto updated = *value; std::swap(updated.objectives[objectiveIndex], updated.objectives[objectiveIndex - 1]);
                std::string editError;
                if (contentWorkspace_->updateQuest(key.id, updated, editError)) { --selectedAnimationFrameIndex_; contentEditFrame_ = noContentIndex; refresh("Quest objective moved"); return; }
                status_ = editError;
            }
            if (ui.button({panel.x + 180, objectiveButtonsY, 44, 18}, "DOWN") && writable && objectiveIndex + 1 < value->objectives.size()) {
                auto updated = *value; std::swap(updated.objectives[objectiveIndex], updated.objectives[objectiveIndex + 1]);
                std::string editError;
                if (contentWorkspace_->updateQuest(key.id, updated, editError)) { ++selectedAnimationFrameIndex_; contentEditFrame_ = noContentIndex; refresh("Quest objective moved"); return; }
                status_ = editError;
            }
        }
        if (objectiveIndex != noContentIndex) {
            const auto& objective = value->objectives[objectiveIndex];
            if (contentEditFrame_ != objectiveIndex) {
                contentEditFrame_ = objectiveIndex;
                contentEditValues_[fieldQuestObjectiveId] = std::string(objective.id.value());
                contentEditValues_[fieldQuestObjectiveKind] = std::string(objectiveKinds[static_cast<int>(objective.kind)]);
                contentEditValues_[fieldQuestObjectiveTarget] = std::string(objective.targetId.value());
                contentEditValues_[fieldQuestObjectiveCount] = std::to_string(objective.requiredCount);
                contentEditValues_[fieldQuestObjectiveDescription] = objective.description;
            }
            const bool objectiveKindChanged = enumButton("objective kind", fieldQuestObjectiveKind,
                                                          objectiveButtonsY + 24, objectiveKinds);
            const auto targetKind = [&]() -> std::optional<ContentDefinitionKind> {
                switch (objective.kind) {
                case game::gameplay::quests::QuestObjectiveKind::talk: return ContentDefinitionKind::npc;
                case game::gameplay::quests::QuestObjectiveKind::kill: return ContentDefinitionKind::enemy;
                case game::gameplay::quests::QuestObjectiveKind::pickup: return ContentDefinitionKind::pickup;
                case game::gameplay::quests::QuestObjectiveKind::open: return ContentDefinitionKind::object;
                case game::gameplay::quests::QuestObjectiveKind::deliver: return ContentDefinitionKind::item;
                case game::gameplay::quests::QuestObjectiveKind::enter: return std::nullopt;
                }
                return std::nullopt;
            }();
            if (targetKind) reference("targetId", *targetKind, fieldQuestObjectiveTarget, objectiveButtonsY + 72);
            else field("targetId (map scoped)", fieldQuestObjectiveTarget, objectiveButtonsY + 72);
            field("requiredCount", fieldQuestObjectiveCount, objectiveButtonsY + 120);
            field("description", fieldQuestObjectiveDescription, objectiveButtonsY + 168);
            if (writable && (input.enterPressed || objectiveKindChanged) && contentFocusedField_ >= fieldQuestObjectiveKind &&
                contentFocusedField_ <= fieldQuestObjectiveDescription) {
                auto updated = *value; auto& target = updated.objectives[objectiveIndex];
                bool accepted = true;
                if (contentFocusedField_ == fieldQuestObjectiveKind) {
                    const auto it = std::find(objectiveKinds.begin(), objectiveKinds.end(), contentEditValues_[fieldQuestObjectiveKind]);
                    if (it == objectiveKinds.end()) accepted = false;
                    else target.kind = static_cast<game::gameplay::quests::QuestObjectiveKind>(it - objectiveKinds.begin());
                } else if (contentFocusedField_ == fieldQuestObjectiveTarget) target.targetId = simulation::DefinitionId{contentEditValues_[fieldQuestObjectiveTarget]};
                else if (contentFocusedField_ == fieldQuestObjectiveCount) { if (const auto parsed = parseUnsigned(contentEditValues_[fieldQuestObjectiveCount])) target.requiredCount = *parsed; else accepted = false; }
                else if (contentFocusedField_ == fieldQuestObjectiveDescription) target.description = contentEditValues_[fieldQuestObjectiveDescription];
                std::string editError;
                if (accepted && contentWorkspace_->updateQuest(key.id, updated, editError)) { refresh("Quest objective updated"); return; }
                status_ = accepted ? editError : "quest objective has an invalid value";
            }
        }
        return;
    }
    if (key.kind == ContentDefinitionKind::dialogue) {
        const auto* value = contentWorkspace_->dialogue(key.id);
        if (!value) return;
        const auto conditionKinds = std::array<std::string_view, 2>{"flagSet", "flagNotSet"};
        const auto actionKinds = std::array<std::string_view, 4>{"setFlag", "clearFlag", "startQuest", "openShop"};
        if (contentEditKey_ != key) {
            resetContentEditState(); contentEditKey_ = key; selectedAnimationFrameIndex_ = 0;
            contentEditValues_[fieldDialogueEntry] = std::string(value->entryNodeId.value());
            contentEditValues_[fieldDialogueNodeId] = "node.1";
            contentEditValues_[fieldDialogueSpeaker] = "Speaker";
            contentEditValues_[fieldDialoguePage] = "Page";
            contentEditValues_[fieldDialogueNext] = {};
            contentEditValues_[fieldDialogueChoiceLabel] = "Choice";
            contentEditValues_[fieldDialogueChoiceTarget] = {};
            contentEditValues_[fieldDialogueFlag] = "flag.example";
            contentEditValues_[fieldDialogueActionTarget] = {};
            contentEditValues_[fieldDialogueConditionKind] = "flagSet";
            contentEditValues_[fieldDialogueActionKind] = "setFlag";
        }
        const auto localNodeReference = [&](std::string_view label, int id, int y) {
            field(label, id, y, panel.width - 82);
            const auto current = simulation::DefinitionId{contentEditValues_[id]};
            if (ui.button({panel.x + panel.width - 68, y + 12, 20, 20}, "<") && !value->nodes.empty()) {
                const auto it = std::find_if(value->nodes.begin(), value->nodes.end(),
                                             [&](const auto& node) { return node.id == current; });
                const auto index = it == value->nodes.begin() || it == value->nodes.end()
                    ? value->nodes.size() - 1
                    : static_cast<std::size_t>(it - value->nodes.begin() - 1);
                contentEditValues_[id] = std::string(value->nodes[index].id.value());
                contentFocusedField_ = id;
            }
            if (ui.button({panel.x + panel.width - 46, y + 12, 20, 20}, ">") && !value->nodes.empty()) {
                const auto it = std::find_if(value->nodes.begin(), value->nodes.end(),
                                             [&](const auto& node) { return node.id == current; });
                const auto index = it == value->nodes.end() ? 0 :
                    (static_cast<std::size_t>(it - value->nodes.begin() + 1) % value->nodes.size());
                contentEditValues_[id] = std::string(value->nodes[index].id.value());
                contentFocusedField_ = id;
            }
            if (!contentEditValues_[id].empty() &&
                std::none_of(value->nodes.begin(), value->nodes.end(),
                             [&](const auto& node) { return node.id.value() == contentEditValues_[id]; })) {
                ui.label("MISSING", panel.x + panel.width - 68, y + 34);
            }
        };
        localNodeReference("entryNodeId", fieldDialogueEntry, inspectorY);
        if (writable && input.enterPressed && contentFocusedField_ == fieldDialogueEntry) {
            auto updated = *value; updated.entryNodeId = simulation::DefinitionId{contentEditValues_[fieldDialogueEntry]};
            std::string editError;
            if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { refresh("Dialogue entry updated"); return; }
            status_ = editError;
        }
        const int nodeY = inspectorY + 48;
        ui.label("NODES", panel.x + 8, nodeY);
        const auto nodeIndex = value->nodes.empty() ? noContentIndex :
            std::min(selectedAnimationFrameIndex_, value->nodes.size() - 1);
        for (int row = 0; row < 4; ++row) {
            const auto index = static_cast<std::size_t>(row);
            if (index >= value->nodes.size()) break;
            if (ui.button({panel.x + 8, nodeY + 16 + row * 18, panel.width - 16, 16},
                          value->nodes[index].id.value(), index == nodeIndex)) {
                selectedAnimationFrameIndex_ = index; contentEditFrame_ = noContentIndex;
                contentEditMarker_ = noContentIndex;
                contentDialogueConditionIndex_ = noContentIndex;
                contentDialogueActionIndex_ = noContentIndex;
                contentFocusedField_ = -1;
            }
        }
        const int nodeButtonsY = nodeY + 90;
        if (ui.button({panel.x + 8, nodeButtonsY, 64, 18}, "ADD NODE") && writable &&
            !contentEditValues_[fieldDialogueNodeId].empty()) {
            auto updated = *value;
            game::content::AuthoredDialogueNode node;
            node.id = simulation::DefinitionId{contentEditValues_[fieldDialogueNodeId]};
            node.speaker = contentEditValues_[fieldDialogueSpeaker];
            node.pages = {contentEditValues_[fieldDialoguePage]};
            node.nextNodeId = simulation::DefinitionId{contentEditValues_[fieldDialogueNext]};
            updated.nodes.push_back(std::move(node));
            if (updated.entryNodeId.empty()) updated.entryNodeId = updated.nodes.back().id;
            std::string editError;
            if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { selectedAnimationFrameIndex_ = updated.nodes.size() - 1; contentEditFrame_ = noContentIndex; refresh("Dialogue node added"); return; }
            status_ = editError;
        }
        if (ui.button({panel.x + 76, nodeButtonsY, 64, 18}, "REMOVE") && writable && nodeIndex != noContentIndex) {
            auto updated = *value; updated.nodes.erase(updated.nodes.begin() + static_cast<std::ptrdiff_t>(nodeIndex));
            std::string editError;
            if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { selectedAnimationFrameIndex_ = nodeIndex == 0 ? 0 : nodeIndex - 1; contentEditFrame_ = noContentIndex; refresh("Dialogue node removed"); return; }
            status_ = editError;
        }
        if (nodeIndex != noContentIndex) {
            if (ui.button({panel.x + 144, nodeButtonsY, 32, 18}, "UP") && writable && nodeIndex > 0) {
                auto updated = *value; std::swap(updated.nodes[nodeIndex], updated.nodes[nodeIndex - 1]); std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { --selectedAnimationFrameIndex_; contentEditFrame_ = noContentIndex; refresh("Dialogue node moved"); return; } status_ = editError;
            }
            if (ui.button({panel.x + 180, nodeButtonsY, 44, 18}, "DOWN") && writable && nodeIndex + 1 < value->nodes.size()) {
                auto updated = *value; std::swap(updated.nodes[nodeIndex], updated.nodes[nodeIndex + 1]); std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { ++selectedAnimationFrameIndex_; contentEditFrame_ = noContentIndex; refresh("Dialogue node moved"); return; } status_ = editError;
            }
            auto& node = value->nodes[nodeIndex];
            if (contentEditFrame_ != nodeIndex) {
                contentEditFrame_ = nodeIndex;
                contentEditValues_[fieldDialogueSpeaker] = node.speaker;
                contentEditValues_[fieldDialogueNext] = std::string(node.nextNodeId.value());
                selectedAnimationMarkerIndex_ = node.pages.empty() ? 0 : 0;
                contentEditMarker_ = node.choices.empty() ? noContentIndex : 0;
            }
            const int nodeEditY = nodeButtonsY + 24;
            field("speaker", fieldDialogueSpeaker, nodeEditY);
            localNodeReference("nextNodeId (local)", fieldDialogueNext, nodeEditY + 48);
            if (writable && input.enterPressed &&
                (contentFocusedField_ == fieldDialogueSpeaker || contentFocusedField_ == fieldDialogueNext)) {
                auto updated = *value; auto& selected = updated.nodes[nodeIndex];
                if (contentFocusedField_ == fieldDialogueSpeaker) selected.speaker = contentEditValues_[fieldDialogueSpeaker];
                else selected.nextNodeId = simulation::DefinitionId{contentEditValues_[fieldDialogueNext]};
                std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { refresh("Dialogue node updated"); return; }
                status_ = editError;
            }
            const int pageY = nodeEditY + 96;
            ui.label("PAGES", panel.x + 8, pageY);
            const auto pageIndex = node.pages.empty() ? noContentIndex :
                std::min(selectedAnimationMarkerIndex_, node.pages.size() - 1);
            for (int row = 0; row < 4; ++row) {
                const auto index = static_cast<std::size_t>(row);
                if (index >= node.pages.size()) break;
                if (ui.button({panel.x + 8, pageY + 16 + row * 18, panel.width - 16, 16},
                              "Page " + std::to_string(index), index == pageIndex)) {
                    selectedAnimationMarkerIndex_ = index;
                    contentFocusedField_ = -1;
                }
            }
            const int pageButtonsY = pageY + 90;
            if (ui.button({panel.x + 8, pageButtonsY, 58, 18}, "ADD PAGE") && writable) {
                auto updated = *value; updated.nodes[nodeIndex].pages.push_back("Page"); std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) {
                    selectedAnimationMarkerIndex_ = updated.nodes[nodeIndex].pages.size() - 1;
                    refresh("Dialogue page added"); return;
                }
                status_ = editError;
            }
            if (ui.button({panel.x + 70, pageButtonsY, 58, 18}, "REMOVE") && writable && pageIndex != noContentIndex) {
                auto updated = *value; updated.nodes[nodeIndex].pages.erase(updated.nodes[nodeIndex].pages.begin() + static_cast<std::ptrdiff_t>(pageIndex)); std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) {
                    selectedAnimationMarkerIndex_ = pageIndex == 0 ? 0 : pageIndex - 1;
                    refresh("Dialogue page removed"); return;
                }
                status_ = editError;
            }
            if (ui.button({panel.x + 132, pageButtonsY, 32, 18}, "UP") && writable && pageIndex > 0) {
                auto updated = *value; std::swap(updated.nodes[nodeIndex].pages[pageIndex], updated.nodes[nodeIndex].pages[pageIndex - 1]); std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { --selectedAnimationMarkerIndex_; refresh("Dialogue page moved"); return; }
                status_ = editError;
            }
            if (ui.button({panel.x + 168, pageButtonsY, 44, 18}, "DOWN") && writable && pageIndex != noContentIndex && pageIndex + 1 < node.pages.size()) {
                auto updated = *value; std::swap(updated.nodes[nodeIndex].pages[pageIndex], updated.nodes[nodeIndex].pages[pageIndex + 1]); std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { ++selectedAnimationMarkerIndex_; refresh("Dialogue page moved"); return; }
                status_ = editError;
            }
            if (pageIndex != noContentIndex) {
                if (contentFocusedField_ != fieldDialoguePage) {
                    contentEditValues_[fieldDialoguePage] = node.pages[pageIndex];
                }
                field("selected page", fieldDialoguePage, pageButtonsY + 24);
                if (writable && input.enterPressed && contentFocusedField_ == fieldDialoguePage) {
                    auto updated = *value; updated.nodes[nodeIndex].pages[pageIndex] = contentEditValues_[fieldDialoguePage]; std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { refresh("Dialogue page updated"); return; }
                    status_ = editError;
                }
            }
            const int choiceY = pageButtonsY + 72;
            ui.label("CHOICES", panel.x + 8, choiceY);
            const auto choiceIndex = node.choices.empty() ? noContentIndex :
                std::min(contentEditMarker_, node.choices.size() - 1);
            for (int row = 0; row < 4; ++row) {
                const auto index = static_cast<std::size_t>(row);
                if (index >= node.choices.size()) break;
                if (ui.button({panel.x + 8, choiceY + 16 + row * 18, panel.width - 16, 16},
                              "Choice " + std::to_string(index), index == choiceIndex)) {
                    contentEditMarker_ = index;
                    contentDialogueConditionIndex_ = noContentIndex;
                    contentDialogueActionIndex_ = noContentIndex;
                    contentFocusedField_ = -1;
                }
            }
            field("new choice label", fieldDialogueChoiceLabel, choiceY + 90);
            field("new choice target", fieldDialogueChoiceTarget, choiceY + 138);
            const int choiceButtonsY = choiceY + 186;
            if (ui.button({panel.x + 8, choiceButtonsY, 96, 18}, "ADD CHOICE") && writable) {
                auto updated = *value;
                updated.nodes[nodeIndex].choices.push_back({contentEditValues_[fieldDialogueChoiceLabel], simulation::DefinitionId{contentEditValues_[fieldDialogueChoiceTarget]}, {}, {}});
                std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) {
                    contentEditMarker_ = updated.nodes[nodeIndex].choices.size() - 1;
                    contentDialogueConditionIndex_ = noContentIndex;
                    contentDialogueActionIndex_ = noContentIndex;
                    refresh("Dialogue choice added"); return;
                }
                status_ = editError;
            }
            if (ui.button({panel.x + 110, choiceButtonsY, 58, 18}, "REMOVE") && writable && choiceIndex != noContentIndex) {
                auto updated = *value; updated.nodes[nodeIndex].choices.erase(updated.nodes[nodeIndex].choices.begin() + static_cast<std::ptrdiff_t>(choiceIndex)); std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) {
                    contentEditMarker_ = choiceIndex == 0 ? noContentIndex : choiceIndex - 1;
                    contentDialogueConditionIndex_ = noContentIndex;
                    contentDialogueActionIndex_ = noContentIndex;
                    refresh("Dialogue choice removed"); return;
                }
                status_ = editError;
            }
            if (ui.button({panel.x + 172, choiceButtonsY, 32, 18}, "UP") && writable && choiceIndex > 0) {
                auto updated = *value; std::swap(updated.nodes[nodeIndex].choices[choiceIndex], updated.nodes[nodeIndex].choices[choiceIndex - 1]); std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { --contentEditMarker_; refresh("Dialogue choice moved"); return; }
                status_ = editError;
            }
            if (ui.button({panel.x + 208, choiceButtonsY, 44, 18}, "DOWN") && writable && choiceIndex != noContentIndex && choiceIndex + 1 < node.choices.size()) {
                auto updated = *value; std::swap(updated.nodes[nodeIndex].choices[choiceIndex], updated.nodes[nodeIndex].choices[choiceIndex + 1]); std::string editError;
                if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { ++contentEditMarker_; refresh("Dialogue choice moved"); return; }
                status_ = editError;
            }
            if (choiceIndex != noContentIndex) {
                const auto& choice = node.choices[choiceIndex];
                if (contentFocusedField_ != fieldDialogueChoiceLabel &&
                    contentFocusedField_ != fieldDialogueChoiceTarget) {
                    contentEditValues_[fieldDialogueChoiceLabel] = choice.label;
                    contentEditValues_[fieldDialogueChoiceTarget] = std::string(choice.targetNodeId.value());
                }
                field("choice label", fieldDialogueChoiceLabel, choiceButtonsY + 24);
                localNodeReference("choice target", fieldDialogueChoiceTarget, choiceButtonsY + 72);
                if (writable && input.enterPressed && (contentFocusedField_ == fieldDialogueChoiceLabel || contentFocusedField_ == fieldDialogueChoiceTarget)) {
                    auto updated = *value; auto& selected = updated.nodes[nodeIndex].choices[choiceIndex];
                    if (contentFocusedField_ == fieldDialogueChoiceLabel) selected.label = contentEditValues_[fieldDialogueChoiceLabel];
                    else selected.targetNodeId = simulation::DefinitionId{contentEditValues_[fieldDialogueChoiceTarget]};
                    std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { refresh("Dialogue choice updated"); return; } status_ = editError;
                }
                const int conditionY = choiceButtonsY + 120;
                ui.label("CONDITIONS", panel.x + 8, conditionY);
                const auto conditionIndex = choice.conditions.empty() ? noContentIndex :
                    std::min(contentDialogueConditionIndex_, choice.conditions.size() - 1);
                for (int row = 0; row < 3; ++row) {
                    const auto index = static_cast<std::size_t>(row);
                    if (index >= choice.conditions.size()) break;
                    if (ui.button({panel.x + 8, conditionY + 16 + row * 18, panel.width - 16, 16},
                                  "condition " + std::to_string(index), index == conditionIndex)) {
                        contentDialogueConditionIndex_ = index; contentFocusedField_ = -1;
                    }
                }
                const int conditionButtonsY = conditionY + 72;
                if (ui.button({panel.x + 8, conditionButtonsY, 54, 18}, "ADD") && writable) {
                    auto updated = *value;
                    const auto kind = std::find(conditionKinds.begin(), conditionKinds.end(), contentEditValues_[fieldDialogueConditionKind]);
                    updated.nodes[nodeIndex].choices[choiceIndex].conditions.push_back({
                        kind == conditionKinds.end() ? game::gameplay::dialogue::DialogueConditionKind::flagSet :
                            static_cast<game::gameplay::dialogue::DialogueConditionKind>(kind - conditionKinds.begin()),
                        simulation::DefinitionId{contentEditValues_[fieldDialogueFlag]}});
                    std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) {
                        contentDialogueConditionIndex_ = updated.nodes[nodeIndex].choices[choiceIndex].conditions.size() - 1;
                        refresh("Dialogue condition added"); return;
                    }
                    status_ = editError;
                }
                if (ui.button({panel.x + 66, conditionButtonsY, 54, 18}, "EDIT") && writable && conditionIndex != noContentIndex) {
                    auto updated = *value;
                    const auto kind = std::find(conditionKinds.begin(), conditionKinds.end(), contentEditValues_[fieldDialogueConditionKind]);
                    if (kind == conditionKinds.end()) status_ = "invalid dialogue condition kind";
                    else {
                        updated.nodes[nodeIndex].choices[choiceIndex].conditions[conditionIndex] = {
                            static_cast<game::gameplay::dialogue::DialogueConditionKind>(kind - conditionKinds.begin()),
                            simulation::DefinitionId{contentEditValues_[fieldDialogueFlag]}};
                        std::string editError;
                        if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { refresh("Dialogue condition updated"); return; }
                        status_ = editError;
                    }
                }
                if (ui.button({panel.x + 124, conditionButtonsY, 64, 18}, "REMOVE") && writable && conditionIndex != noContentIndex) {
                    auto updated = *value;
                    updated.nodes[nodeIndex].choices[choiceIndex].conditions.erase(updated.nodes[nodeIndex].choices[choiceIndex].conditions.begin() + static_cast<std::ptrdiff_t>(conditionIndex));
                    std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) {
                        contentDialogueConditionIndex_ = conditionIndex == 0 ? noContentIndex : conditionIndex - 1;
                        refresh("Dialogue condition removed"); return;
                    }
                    status_ = editError;
                }
                if (ui.button({panel.x + 192, conditionButtonsY, 32, 18}, "UP") && writable && conditionIndex > 0) {
                    auto updated = *value;
                    std::swap(updated.nodes[nodeIndex].choices[choiceIndex].conditions[conditionIndex], updated.nodes[nodeIndex].choices[choiceIndex].conditions[conditionIndex - 1]);
                    std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { --contentDialogueConditionIndex_; refresh("Dialogue condition moved"); return; }
                    status_ = editError;
                }
                if (ui.button({panel.x + 228, conditionButtonsY, 44, 18}, "DOWN") && writable &&
                    conditionIndex != noContentIndex && conditionIndex + 1 < choice.conditions.size()) {
                    auto updated = *value;
                    std::swap(updated.nodes[nodeIndex].choices[choiceIndex].conditions[conditionIndex], updated.nodes[nodeIndex].choices[choiceIndex].conditions[conditionIndex + 1]);
                    std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { ++contentDialogueConditionIndex_; refresh("Dialogue condition moved"); return; }
                    status_ = editError;
                }
                if (conditionIndex != noContentIndex) {
                    const auto& condition = choice.conditions[conditionIndex];
                    if (contentFocusedField_ != fieldDialogueConditionKind && contentFocusedField_ != fieldDialogueFlag) {
                        contentEditValues_[fieldDialogueConditionKind] = std::string(conditionKinds[static_cast<int>(condition.kind)]);
                        contentEditValues_[fieldDialogueFlag] = std::string(condition.flagId.value());
                    }
                }
                const bool conditionKindChanged = enumButton("condition kind", fieldDialogueConditionKind, conditionButtonsY + 24, conditionKinds);
                field("condition flag", fieldDialogueFlag, conditionButtonsY + 72);

                const int actionY = conditionButtonsY + 120;
                ui.label("ACTIONS", panel.x + 8, actionY);
                const auto actionIndex = choice.actions.empty() ? noContentIndex :
                    std::min(contentDialogueActionIndex_, choice.actions.size() - 1);
                for (int row = 0; row < 3; ++row) {
                    const auto index = static_cast<std::size_t>(row);
                    if (index >= choice.actions.size()) break;
                    if (ui.button({panel.x + 8, actionY + 16 + row * 18, panel.width - 16, 16},
                                  "action " + std::to_string(index), index == actionIndex)) {
                        contentDialogueActionIndex_ = index; contentFocusedField_ = -1;
                    }
                }
                const int actionButtonsY = actionY + 72;
                if (ui.button({panel.x + 8, actionButtonsY, 54, 18}, "ADD") && writable) {
                    auto updated = *value;
                    const auto kind = std::find(actionKinds.begin(), actionKinds.end(), contentEditValues_[fieldDialogueActionKind]);
                    updated.nodes[nodeIndex].choices[choiceIndex].actions.push_back({
                        kind == actionKinds.end() ? game::gameplay::dialogue::DialogueActionKind::setFlag :
                            static_cast<game::gameplay::dialogue::DialogueActionKind>(kind - actionKinds.begin()),
                        simulation::DefinitionId{contentEditValues_[fieldDialogueActionTarget]}});
                    std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) {
                        contentDialogueActionIndex_ = updated.nodes[nodeIndex].choices[choiceIndex].actions.size() - 1;
                        refresh("Dialogue action added"); return;
                    }
                    status_ = editError;
                }
                if (ui.button({panel.x + 66, actionButtonsY, 54, 18}, "EDIT") && writable && actionIndex != noContentIndex) {
                    auto updated = *value;
                    const auto kind = std::find(actionKinds.begin(), actionKinds.end(), contentEditValues_[fieldDialogueActionKind]);
                    if (kind == actionKinds.end()) status_ = "invalid dialogue action kind";
                    else {
                        updated.nodes[nodeIndex].choices[choiceIndex].actions[actionIndex] = {
                            static_cast<game::gameplay::dialogue::DialogueActionKind>(kind - actionKinds.begin()),
                            simulation::DefinitionId{contentEditValues_[fieldDialogueActionTarget]}};
                        std::string editError;
                        if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { refresh("Dialogue action updated"); return; }
                        status_ = editError;
                    }
                }
                if (ui.button({panel.x + 124, actionButtonsY, 64, 18}, "REMOVE") && writable && actionIndex != noContentIndex) {
                    auto updated = *value;
                    updated.nodes[nodeIndex].choices[choiceIndex].actions.erase(updated.nodes[nodeIndex].choices[choiceIndex].actions.begin() + static_cast<std::ptrdiff_t>(actionIndex));
                    std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) {
                        contentDialogueActionIndex_ = actionIndex == 0 ? noContentIndex : actionIndex - 1;
                        refresh("Dialogue action removed"); return;
                    }
                    status_ = editError;
                }
                if (ui.button({panel.x + 192, actionButtonsY, 32, 18}, "UP") && writable && actionIndex > 0) {
                    auto updated = *value;
                    std::swap(updated.nodes[nodeIndex].choices[choiceIndex].actions[actionIndex], updated.nodes[nodeIndex].choices[choiceIndex].actions[actionIndex - 1]);
                    std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { --contentDialogueActionIndex_; refresh("Dialogue action moved"); return; }
                    status_ = editError;
                }
                if (ui.button({panel.x + 228, actionButtonsY, 44, 18}, "DOWN") && writable &&
                    actionIndex != noContentIndex && actionIndex + 1 < choice.actions.size()) {
                    auto updated = *value;
                    std::swap(updated.nodes[nodeIndex].choices[choiceIndex].actions[actionIndex], updated.nodes[nodeIndex].choices[choiceIndex].actions[actionIndex + 1]);
                    std::string editError;
                    if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { ++contentDialogueActionIndex_; refresh("Dialogue action moved"); return; }
                    status_ = editError;
                }
                if (actionIndex != noContentIndex) {
                    const auto& action = choice.actions[actionIndex];
                    if (contentFocusedField_ != fieldDialogueActionKind && contentFocusedField_ != fieldDialogueActionTarget) {
                        contentEditValues_[fieldDialogueActionKind] = std::string(actionKinds[static_cast<int>(action.kind)]);
                        contentEditValues_[fieldDialogueActionTarget] = std::string(action.targetId.value());
                    }
                }
                const bool actionKindChanged = enumButton("action kind", fieldDialogueActionKind, actionButtonsY + 24, actionKinds);
                field("action target", fieldDialogueActionTarget, actionButtonsY + 72);
                if (writable && (input.enterPressed || conditionKindChanged) && conditionIndex != noContentIndex &&
                    (contentFocusedField_ == fieldDialogueConditionKind || contentFocusedField_ == fieldDialogueFlag)) {
                    auto updated = *value; auto& selected = updated.nodes[nodeIndex].choices[choiceIndex].conditions[conditionIndex];
                    const auto kind = std::find(conditionKinds.begin(), conditionKinds.end(), contentEditValues_[fieldDialogueConditionKind]);
                    if (kind == conditionKinds.end()) status_ = "invalid dialogue condition kind";
                    else {
                        selected.kind = static_cast<game::gameplay::dialogue::DialogueConditionKind>(kind - conditionKinds.begin());
                        selected.flagId = simulation::DefinitionId{contentEditValues_[fieldDialogueFlag]};
                        std::string editError;
                        if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { refresh("Dialogue condition updated"); return; }
                        status_ = editError;
                    }
                }
                if (writable && (input.enterPressed || actionKindChanged) && actionIndex != noContentIndex &&
                    (contentFocusedField_ == fieldDialogueActionKind || contentFocusedField_ == fieldDialogueActionTarget)) {
                    auto updated = *value; auto& selected = updated.nodes[nodeIndex].choices[choiceIndex].actions[actionIndex];
                    const auto kind = std::find(actionKinds.begin(), actionKinds.end(), contentEditValues_[fieldDialogueActionKind]);
                    if (kind == actionKinds.end()) status_ = "invalid dialogue action kind";
                    else {
                        selected.kind = static_cast<game::gameplay::dialogue::DialogueActionKind>(kind - actionKinds.begin());
                        selected.targetId = simulation::DefinitionId{contentEditValues_[fieldDialogueActionTarget]};
                        std::string editError;
                        if (contentWorkspace_->updateDialogue(key.id, updated, editError)) { refresh("Dialogue action updated"); return; }
                        status_ = editError;
                    }
                }
            }
        }
        return;
    }
    if (key.kind == ContentDefinitionKind::npc) {
        const auto* value = contentWorkspace_->npc(key.id);
        if (!value) return;
        if (contentEditKey_ != key) {
            resetContentEditState(); contentEditKey_ = key;
            contentEditValues_[fieldNpcVisual] = std::string(value->visualSetId.value());
            contentEditValues_[fieldNpcInteractionBounds] = rectText(value->interaction.bounds);
            contentEditValues_[fieldNpcDialogue] = std::string(value->defaultDialogueId.value());
            contentEditValues_[fieldNpcTag] = value->tags.empty() ? "tag" : value->tags.front();
            selectedAnimationMarkerIndex_ = value->tags.empty() ? noContentIndex : 0;
        }
        reference("visualSetId", ContentDefinitionKind::npcVisual, fieldNpcVisual, inspectorY);
        field("interaction x,y,w,h", fieldNpcInteractionBounds, inspectorY + 48);
        reference("defaultDialogueId", ContentDefinitionKind::dialogue, fieldNpcDialogue, inspectorY + 96);
        const bool interactionChanged = ui.button(
            {panel.x + 8, inspectorY + 144, panel.width - 16, 20},
            value->interaction.enabled ? "INTERACTION ENABLED" : "INTERACTION DISABLED",
            value->interaction.enabled);
        field("tag (selected/new)", fieldNpcTag, inspectorY + 172);
        ui.label("TAGS", panel.x + 8, inspectorY + 210);
        const auto tagIndex = value->tags.empty() ? noContentIndex :
            std::min(selectedAnimationMarkerIndex_, value->tags.size() - 1);
        for (int row = 0; row < 4; ++row) {
            const auto index = static_cast<std::size_t>(row);
            if (index >= value->tags.size()) break;
            if (ui.button({panel.x + 8, inspectorY + 226 + row * 18, panel.width - 16, 16},
                          value->tags[index], index == tagIndex)) {
                selectedAnimationMarkerIndex_ = index;
                contentEditValues_[fieldNpcTag] = value->tags[index];
                contentFocusedField_ = -1;
            }
        }
        const int tagButtonsY = inspectorY + 300;
        if (ui.button({panel.x + 8, tagButtonsY, 54, 18}, "ADD") && writable &&
            !contentEditValues_[fieldNpcTag].empty()) {
            auto updated = *value;
            updated.tags.push_back(contentEditValues_[fieldNpcTag]);
            std::string editError;
            if (contentWorkspace_->updateNpc(key.id, updated, editError)) {
                selectedAnimationMarkerIndex_ = updated.tags.size() - 1;
                refresh("NPC tag added"); return;
            }
            status_ = editError;
        }
        if (ui.button({panel.x + 66, tagButtonsY, 54, 18}, "EDIT") && writable && tagIndex != noContentIndex) {
            auto updated = *value;
            updated.tags[tagIndex] = contentEditValues_[fieldNpcTag];
            std::string editError;
            if (contentWorkspace_->updateNpc(key.id, updated, editError)) { refresh("NPC tag updated"); return; }
            status_ = editError;
        }
        if (ui.button({panel.x + 124, tagButtonsY, 64, 18}, "REMOVE") && writable && tagIndex != noContentIndex) {
            auto updated = *value;
            updated.tags.erase(updated.tags.begin() + static_cast<std::ptrdiff_t>(tagIndex));
            std::string editError;
            if (contentWorkspace_->updateNpc(key.id, updated, editError)) {
                selectedAnimationMarkerIndex_ = tagIndex == 0 ? noContentIndex : tagIndex - 1;
                refresh("NPC tag removed"); return;
            }
            status_ = editError;
        }
        if (ui.button({panel.x + 192, tagButtonsY, 32, 18}, "UP") && writable && tagIndex > 0) {
            auto updated = *value; std::swap(updated.tags[tagIndex], updated.tags[tagIndex - 1]);
            std::string editError;
            if (contentWorkspace_->updateNpc(key.id, updated, editError)) { --selectedAnimationMarkerIndex_; refresh("NPC tag moved"); return; }
            status_ = editError;
        }
        if (ui.button({panel.x + 228, tagButtonsY, 44, 18}, "DOWN") && writable &&
            tagIndex != noContentIndex && tagIndex + 1 < value->tags.size()) {
            auto updated = *value; std::swap(updated.tags[tagIndex], updated.tags[tagIndex + 1]);
            std::string editError;
            if (contentWorkspace_->updateNpc(key.id, updated, editError)) { ++selectedAnimationMarkerIndex_; refresh("NPC tag moved"); return; }
            status_ = editError;
        }
        if (writable && input.enterPressed &&
            (contentFocusedField_ == fieldNpcVisual || contentFocusedField_ == fieldNpcInteractionBounds ||
             contentFocusedField_ == fieldNpcDialogue || contentFocusedField_ == fieldNpcTag)) {
            auto updated = *value; std::string editError; bool ok = true;
            if (contentFocusedField_ == fieldNpcVisual) updated.visualSetId = simulation::DefinitionId{contentEditValues_[fieldNpcVisual]};
            else if (contentFocusedField_ == fieldNpcInteractionBounds) {
                const auto parsed = parseRect(contentEditValues_[fieldNpcInteractionBounds]);
                if (!parsed) ok = false; else updated.interaction.bounds = *parsed;
            } else if (contentFocusedField_ == fieldNpcDialogue) {
                updated.defaultDialogueId = simulation::DefinitionId{contentEditValues_[fieldNpcDialogue]};
            } else if (tagIndex != noContentIndex) {
                updated.tags[tagIndex] = contentEditValues_[fieldNpcTag];
            }
            if (!ok) editError = "NPC field has an invalid rectangle";
            if (ok && contentWorkspace_->updateNpc(key.id, updated, editError)) refresh("NPC updated");
            else if (!ok || !editError.empty()) status_ = editError;
        }
        if (interactionChanged && writable) {
            auto updated = *value; updated.interaction.enabled = !updated.interaction.enabled;
            std::string editError;
            if (contentWorkspace_->updateNpc(key.id, updated, editError)) refresh("NPC interaction toggled");
            else status_ = editError;
        }
        return;
    }
    if (key.kind == ContentDefinitionKind::rewardProfile) { const auto* value=contentWorkspace_->rewardProfile(key.id);if(!value)return;if(contentEditKey_!=key){resetContentEditState();contentEditKey_=key;contentEditValues_[fieldRewardProfileExperience]=std::to_string(value->experience);contentEditValues_[fieldRewardPickup]="pickup";contentEditValues_[fieldRewardChance]="10000";contentEditValues_[fieldRewardMinimum]="1";contentEditValues_[fieldRewardMaximum]="1";}field("experience",fieldRewardProfileExperience,inspectorY);reference("new pickupDefinitionId",ContentDefinitionKind::pickup,fieldRewardPickup,inspectorY+48);field("chanceBasisPoints",fieldRewardChance,inspectorY+96);field("minimumCount",fieldRewardMinimum,inspectorY+144);field("maximumCount",fieldRewardMaximum,inspectorY+192);if(ui.button({panel.x+8,inspectorY+240,panel.width-16,20},"ADD LOOT ENTRY")&&writable){auto u=*value;game::content::AuthoredLootEntry e;e.pickupDefinitionId=simulation::DefinitionId{contentEditValues_[fieldRewardPickup]};e.chanceBasisPoints=parseUnsigned(contentEditValues_[fieldRewardChance]).value_or(0);e.minimumCount=parseUnsigned(contentEditValues_[fieldRewardMinimum]).value_or(0);e.maximumCount=parseUnsigned(contentEditValues_[fieldRewardMaximum]).value_or(0);u.loot.push_back(e);std::string error;if(contentWorkspace_->updateRewardProfile(key.id,u,error))refresh("Reward loot added");else status_=error;return;}ui.label("loot entries: "+std::to_string(value->loot.size()),panel.x+8,inspectorY+268);if(ui.button({panel.x+8,inspectorY+288,92,18},"REMOVE LOOT")&&writable&&!value->loot.empty()){auto u=*value;u.loot.pop_back();std::string error;if(contentWorkspace_->updateRewardProfile(key.id,u,error))refresh("Reward loot removed");else status_=error;return;}if(writable&&input.enterPressed&&contentFocusedField_==fieldRewardProfileExperience){auto u=*value;u.experience=parseU64(contentEditValues_[fieldRewardProfileExperience]).value_or(value->experience);std::string e;if(contentWorkspace_->updateRewardProfile(key.id,u,e))refresh("Reward profile updated");else status_=e;}return; }
    if (key.kind == ContentDefinitionKind::rewardGrant) { const auto* value=contentWorkspace_->rewardGrant(key.id);if(!value)return;if(contentEditKey_!=key){resetContentEditState();contentEditKey_=key;contentEditValues_[fieldRewardGrantExperience]=std::to_string(value->experience);contentEditValues_[fieldRewardGrantGold]=std::to_string(value->gold);contentEditValues_[fieldRewardGrantItem]="item";contentEditValues_[fieldRewardGrantQuantity]="1";}field("experience",fieldRewardGrantExperience,inspectorY);field("gold",fieldRewardGrantGold,inspectorY+48);reference("new itemId",ContentDefinitionKind::item,fieldRewardGrantItem,inspectorY+96);field("quantity",fieldRewardGrantQuantity,inspectorY+144);if(ui.button({panel.x+8,inspectorY+192,panel.width-16,20},"ADD ITEM GRANT")&&writable){auto u=*value;u.items.push_back({simulation::DefinitionId{contentEditValues_[fieldRewardGrantItem]},parseUnsigned(contentEditValues_[fieldRewardGrantQuantity]).value_or(0)});std::string e;if(contentWorkspace_->updateRewardGrant(key.id,u,e))refresh("Reward item grant added");else status_=e;return;}ui.label("items: "+std::to_string(value->items.size()),panel.x+8,inspectorY+216);if(ui.button({panel.x+8,inspectorY+236,100,18},"REMOVE ITEM")&&writable&&!value->items.empty()){auto u=*value;u.items.pop_back();std::string e;if(contentWorkspace_->updateRewardGrant(key.id,u,e))refresh("Reward item grant removed");else status_=e;return;}if(writable&&input.enterPressed&&(contentFocusedField_==fieldRewardGrantExperience||contentFocusedField_==fieldRewardGrantGold)){auto u=*value;std::string e;if(contentFocusedField_==fieldRewardGrantExperience)u.experience=parseU64(contentEditValues_[fieldRewardGrantExperience]).value_or(value->experience);else u.gold=parseU64(contentEditValues_[fieldRewardGrantGold]).value_or(value->gold);if(contentWorkspace_->updateRewardGrant(key.id,u,e))refresh("Reward grant updated");else status_=e;}return; }
    if (key.kind == ContentDefinitionKind::shop) { const auto* value=contentWorkspace_->shop(key.id);if(!value)return;if(contentEditKey_!=key){resetContentEditState();contentEditKey_=key;contentEditValues_[fieldShopItem]="item";contentEditValues_[fieldShopBuyPrice]="";contentEditValues_[fieldShopSellPrice]="";}reference("new itemId",ContentDefinitionKind::item,fieldShopItem,inspectorY);field("playerBuyPrice optional",fieldShopBuyPrice,inspectorY+48);field("playerSellPrice optional",fieldShopSellPrice,inspectorY+96);if(ui.button({panel.x+8,inspectorY+144,panel.width-16,20},"ADD OFFER")&&writable){auto u=*value;game::content::AuthoredShopOffer offer;offer.itemId=simulation::DefinitionId{contentEditValues_[fieldShopItem]};offer.playerBuyPrice=parseU64(contentEditValues_[fieldShopBuyPrice]);offer.playerSellPrice=parseU64(contentEditValues_[fieldShopSellPrice]);u.offers.push_back(offer);std::string e;if(contentWorkspace_->updateShop(key.id,u,e))refresh("Shop offer added");else status_=e;return;}ui.label("offers: "+std::to_string(value->offers.size())+" (no stock system)",panel.x+8,inspectorY+192);if(ui.button({panel.x+8,inspectorY+216,100,18},"REMOVE OFFER")&&writable&&!value->offers.empty()){auto u=*value;u.offers.pop_back();std::string e;if(contentWorkspace_->updateShop(key.id,u,e))refresh("Shop offer removed");else status_=e;return;}return; }
    if (key.kind == ContentDefinitionKind::playerProgression) {const auto* value=contentWorkspace_->playerProgression(key.id);if(!value)return;if(contentEditKey_!=key){resetContentEditState();contentEditKey_=key;contentEditValues_[fieldProgressionHealth]=std::to_string(value->baseStats.maximumHealth);contentEditValues_[fieldProgressionThreshold]="0";}field("baseStats.maximumHealth",fieldProgressionHealth,inspectorY);field("new threshold",fieldProgressionThreshold,inspectorY+48);if(ui.button({panel.x+8,inspectorY+96,panel.width-16,20},"ADD THRESHOLD")&&writable){auto u=*value;u.cumulativeExperienceThresholds.push_back(parseU64(contentEditValues_[fieldProgressionThreshold]).value_or(0));std::string e;if(contentWorkspace_->updatePlayerProgression(key.id,u,e))refresh("Progression threshold added");else status_=e;return;}ui.label("thresholds: "+std::to_string(value->cumulativeExperienceThresholds.size()),panel.x+8,inspectorY+144);if(ui.button({panel.x+8,inspectorY+164,110,18},"REMOVE THRESHOLD")&&writable&&!value->cumulativeExperienceThresholds.empty()){auto u=*value;u.cumulativeExperienceThresholds.pop_back();std::string e;if(contentWorkspace_->updatePlayerProgression(key.id,u,e))refresh("Progression threshold removed");else status_=e;return;}if(writable&&input.enterPressed&&contentFocusedField_==fieldProgressionHealth){auto u=*value;u.baseStats.maximumHealth=parseIntegerList<1>(contentEditValues_[fieldProgressionHealth]).value_or(std::array<int,1>{value->baseStats.maximumHealth})[0];std::string e;if(contentWorkspace_->updatePlayerProgression(key.id,u,e))refresh("Progression updated");else status_=e;}return;}
    if (key.kind == ContentDefinitionKind::pickup) {const auto* value=contentWorkspace_->pickup(key.id);if(!value)return;if(contentEditKey_!=key){resetContentEditState();contentEditKey_=key;contentEditValues_[fieldPickupVisual]=std::string(value->visualId.value());contentEditValues_[fieldPickupBounds]=rectText(value->collectionBounds);contentEditValues_[fieldPickupPayload]=std::holds_alternative<game::content::AuthoredHealthPickup>(value->payload)?"health":std::holds_alternative<game::content::AuthoredCurrencyPickup>(value->payload)?"currency":"item";contentEditValues_[fieldPickupAmount]="0";contentEditValues_[fieldPickupItem]="";contentEditValues_[fieldPickupQuantity]="1";}reference("visualId",ContentDefinitionKind::staticSprite,fieldPickupVisual,inspectorY);field("collection x,y,w,h",fieldPickupBounds,inspectorY+48);const bool payloadChanged=enumButton("payload",fieldPickupPayload,inspectorY+96,std::array<std::string_view,3>{"health","currency","item"});field("health/currency amount",fieldPickupAmount,inspectorY+144);reference("itemId",ContentDefinitionKind::item,fieldPickupItem,inspectorY+192);field("item quantity",fieldPickupQuantity,inspectorY+240);if(writable&&(input.enterPressed||payloadChanged)&&contentFocusedField_>=fieldPickupVisual&&contentFocusedField_<=fieldPickupQuantity){auto u=*value;std::string e;bool ok=true;if(contentFocusedField_==fieldPickupVisual)u.visualId=simulation::DefinitionId{contentEditValues_[fieldPickupVisual]};else if(contentFocusedField_==fieldPickupBounds){auto p=parseRect(contentEditValues_[fieldPickupBounds]);if(!p)ok=false;else u.collectionBounds=*p;}else if(contentFocusedField_==fieldPickupPayload||payloadChanged){if(contentEditValues_[fieldPickupPayload]=="health")u.payload=game::content::AuthoredHealthPickup{};else if(contentEditValues_[fieldPickupPayload]=="currency")u.payload=game::content::AuthoredCurrencyPickup{};else u.payload=game::content::AuthoredItemPickup{};}else if(contentFocusedField_==fieldPickupAmount){auto p=parseIntegerList<1>(contentEditValues_[fieldPickupAmount]);if(!p)ok=false;else if(auto h=std::get_if<game::content::AuthoredHealthPickup>(&u.payload))h->amount=(*p)[0];else if(auto c=std::get_if<game::content::AuthoredCurrencyPickup>(&u.payload))c->amount=static_cast<std::uint64_t>((*p)[0]);}else if(contentFocusedField_==fieldPickupItem){if(auto i=std::get_if<game::content::AuthoredItemPickup>(&u.payload))i->itemId=simulation::DefinitionId{contentEditValues_[fieldPickupItem]};}else if(auto i=std::get_if<game::content::AuthoredItemPickup>(&u.payload))i->quantity=parseUnsigned(contentEditValues_[fieldPickupQuantity]).value_or(i->quantity);if(!ok)e="pickup field has an invalid value";if(ok&&contentWorkspace_->updatePickup(key.id,u,e))refresh("Pickup updated");else if(!ok||!e.empty())status_=e;}return;}
    if (key.kind == ContentDefinitionKind::object) {const auto* value=contentWorkspace_->object(key.id);if(!value)return;if(contentEditKey_!=key){resetContentEditState();contentEditKey_=key;contentEditValues_[fieldObjectVisual]=std::string(value->visualSetId.value());contentEditValues_[fieldObjectInteractBounds]=value->interactable?rectText(value->interactable->bounds):"0,0,16,16";contentEditValues_[fieldObjectCapacity]=value->container?std::to_string(value->container->capacity):"1";contentEditValues_[fieldObjectDestructibleHealth]=value->destructible?std::to_string(value->destructible->maximumHealth):"1";contentEditValues_[fieldObjectDestructibleHurtbox]=value->destructible?rectText(value->destructible->hurtbox):"0,0,16,16";contentEditValues_[fieldObjectDestructibleDuration]=value->destructible?std::to_string(value->destructible->destructionDurationTicks):"1";contentEditValues_[fieldObjectDoorState]=value->door?std::array<std::string_view,3>{"locked","closed","open"}[static_cast<int>(value->door->initialState)]:"closed";contentEditValues_[fieldObjectDoorBounds]=value->door?rectText(value->door->blockingBounds):"0,0,16,16";contentEditValues_[fieldObjectActivationMode]=value->activation?(value->activation->mode==game::gameplay::ObjectActivationMode::interactToggle?"interactToggle":"playerPressure"):"interactToggle";contentEditValues_[fieldObjectActivationBounds]=value->activation&&value->activation->activationBounds?rectText(*value->activation->activationBounds):"0,0,16,16";}
        reference("visualSetId",ContentDefinitionKind::objectVisual,fieldObjectVisual,inspectorY);const bool interact=ui.button({panel.x+8,inspectorY+48,panel.width-16,20},value->interactable?"INTERACTABLE ON":"INTERACTABLE OFF",value->interactable.has_value());field("interactable bounds",fieldObjectInteractBounds,inspectorY+72);const bool container=ui.button({panel.x+8,inspectorY+120,panel.width-16,20},value->container?"CONTAINER ON":"CONTAINER OFF",value->container.has_value());field("container capacity",fieldObjectCapacity,inspectorY+144);const bool destructible=ui.button({panel.x+8,inspectorY+192,panel.width-16,20},value->destructible?"DESTRUCTIBLE ON":"DESTRUCTIBLE OFF",value->destructible.has_value());field("destructible health",fieldObjectDestructibleHealth,inspectorY+216);field("destructible hurtbox",fieldObjectDestructibleHurtbox,inspectorY+264);field("destruction duration",fieldObjectDestructibleDuration,inspectorY+312);const bool bank=ui.button({panel.x+8,inspectorY+360,panel.width-16,20},value->bankAccess?"BANK ACCESS ON":"BANK ACCESS OFF",value->bankAccess.has_value());const bool door=ui.button({panel.x+8,inspectorY+384,panel.width-16,20},value->door?"DOOR ON":"DOOR OFF",value->door.has_value());const bool doorStateChanged=enumButton("door initial state",fieldObjectDoorState,inspectorY+408,doorStates);field("door blocking x,y,w,h",fieldObjectDoorBounds,inspectorY+456);const bool activation=ui.button({panel.x+8,inspectorY+504,panel.width-16,20},value->activation?"ACTIVATION ON":"ACTIVATION OFF",value->activation.has_value());const bool activationInitial=ui.button({panel.x+8,inspectorY+528,panel.width-16,20},value->activation&&value->activation->initialActive?"INITIAL ACTIVE":"INITIAL INACTIVE",value->activation&&value->activation->initialActive);const bool modeChanged=enumButton("activation mode",fieldObjectActivationMode,inspectorY+552,activationModes);field("activation bounds",fieldObjectActivationBounds,inspectorY+600);if(writable&&(input.enterPressed||interact||container||destructible||bank||door||doorStateChanged||activation||activationInitial||modeChanged)){auto u=*value;std::string e;bool ok=true;if(interact){if(u.interactable)u.interactable.reset();else u.interactable=game::gameplay::ObjectInteractionDefinition{};}else if(container){if(u.container)u.container.reset();else u.container=game::gameplay::ObjectContainerDefinition{};}else if(destructible){if(u.destructible)u.destructible.reset();else u.destructible=game::gameplay::ObjectDestructibleDefinition{};}else if(bank){if(u.bankAccess)u.bankAccess.reset();else u.bankAccess=game::content::AuthoredObjectBankAccess{};}else if(door){if(u.door)u.door.reset();else u.door=game::gameplay::ObjectDoorDefinition{};}else if(activation){if(u.activation)u.activation.reset();else u.activation=game::gameplay::ObjectActivationDefinition{};}else if(activationInitial){if(!u.activation)u.activation=game::gameplay::ObjectActivationDefinition{};u.activation->initialActive=!u.activation->initialActive;}else if(contentFocusedField_==fieldObjectVisual)u.visualSetId=simulation::DefinitionId{contentEditValues_[fieldObjectVisual]};else if(contentFocusedField_==fieldObjectInteractBounds){auto p=parseRect(contentEditValues_[fieldObjectInteractBounds]);if(!p)ok=false;else{if(!u.interactable)u.interactable={};u.interactable->bounds=*p;}}else if(contentFocusedField_==fieldObjectCapacity){if(auto p=parseUnsigned(contentEditValues_[fieldObjectCapacity])){if(!u.container)u.container={};u.container->capacity=*p;}else ok=false;}else if(contentFocusedField_==fieldObjectDestructibleHealth){if(auto p=parseIntegerList<1>(contentEditValues_[fieldObjectDestructibleHealth])){if(!u.destructible)u.destructible={};u.destructible->maximumHealth=(*p)[0];}else ok=false;}else if(contentFocusedField_==fieldObjectDestructibleHurtbox){auto p=parseRect(contentEditValues_[fieldObjectDestructibleHurtbox]);if(!p)ok=false;else{if(!u.destructible)u.destructible={};u.destructible->hurtbox=*p;}}else if(contentFocusedField_==fieldObjectDestructibleDuration){if(auto p=parseUnsigned(contentEditValues_[fieldObjectDestructibleDuration])){if(!u.destructible)u.destructible={};u.destructible->destructionDurationTicks=*p;}else ok=false;}else if(contentFocusedField_==fieldObjectDoorState||doorStateChanged){auto it=std::find(doorStates.begin(),doorStates.end(),contentEditValues_[fieldObjectDoorState]);if(it==doorStates.end())ok=false;else{if(!u.door)u.door={};u.door->initialState=static_cast<game::gameplay::DoorState>(it-doorStates.begin());}}else if(contentFocusedField_==fieldObjectDoorBounds){auto p=parseRect(contentEditValues_[fieldObjectDoorBounds]);if(!p)ok=false;else{if(!u.door)u.door={};u.door->blockingBounds=*p;}}else if(contentFocusedField_==fieldObjectActivationMode||modeChanged){auto it=std::find(activationModes.begin(),activationModes.end(),contentEditValues_[fieldObjectActivationMode]);if(it==activationModes.end())ok=false;else{if(!u.activation)u.activation={};u.activation->mode=static_cast<game::gameplay::ObjectActivationMode>(it-activationModes.begin());}}else if(contentFocusedField_==fieldObjectActivationBounds){auto p=parseRect(contentEditValues_[fieldObjectActivationBounds]);if(!p)ok=false;else{if(!u.activation)u.activation={};u.activation->activationBounds=*p;}}if(!ok)e="object field has an invalid value";if(ok&&contentWorkspace_->updateObject(key.id,u,e))refresh("World object updated");else if(!ok||!e.empty())status_=e;}return;}
    if (key.kind == ContentDefinitionKind::tileset) {
        const auto* value = contentWorkspace_->tileset(key.id); if (!value) return;
        if (contentEditKey_ != key) { resetContentEditState(); contentEditKey_ = key;
            contentEditValues_[fieldTilesetDisplay] = value->displayName;
            contentEditValues_[fieldTilesetPath] = value->relativeAssetPath;
            contentEditValues_[fieldTilesetSize] = std::to_string(value->tileSize);
            contentEditValues_[fieldTilesetColumns] = std::to_string(value->columns);
            contentEditValues_[fieldTilesetRows] = std::to_string(value->rows); }
        field("displayName", fieldTilesetDisplay, inspectorY);
        field("relativeAssetPath", fieldTilesetPath, inspectorY + 48);
        field("tileSize", fieldTilesetSize, inspectorY + 96);
        field("columns", fieldTilesetColumns, inspectorY + 144);
        field("rows", fieldTilesetRows, inspectorY + 192);
        if (writable && input.enterPressed && contentFocusedField_ >= fieldTilesetDisplay &&
            contentFocusedField_ <= fieldTilesetRows) {
            auto updated = *value; std::string error; bool ok = true;
            if (contentFocusedField_ == fieldTilesetDisplay) updated.displayName = contentEditValues_[fieldTilesetDisplay];
            else if (contentFocusedField_ == fieldTilesetPath) updated.relativeAssetPath = contentEditValues_[fieldTilesetPath];
            else if (contentFocusedField_ == fieldTilesetSize) { const auto p = parseUnsigned(contentEditValues_[fieldTilesetSize]); if (!p || *p > 65535) ok = false; else updated.tileSize = static_cast<std::uint16_t>(*p); }
            else if (contentFocusedField_ == fieldTilesetColumns) { const auto p = parseUnsigned(contentEditValues_[fieldTilesetColumns]); if (!p) ok = false; else updated.columns = *p; }
            else { const auto p = parseUnsigned(contentEditValues_[fieldTilesetRows]); if (!p) ok = false; else updated.rows = *p; }
            if (ok && contentWorkspace_->updateTileset(key.id, updated, error)) refresh("Tileset updated");
            else status_ = ok ? error : "Tileset numeric field is invalid";
        }
        return;
    }
    if (key.kind == ContentDefinitionKind::authoringDescriptor) {
        const auto* value = contentWorkspace_->authoringDescriptor(key.id); if (!value) return;
        if (contentEditKey_ != key) { resetContentEditState(); contentEditKey_ = key;
            contentEditValues_[fieldDescriptorDisplay] = value->displayName;
            contentEditValues_[fieldDescriptorCategory] = ContentWorkspaceDocument::categoryName(
                value->category == game::content::AuthoringCategory::enemy ? ContentDefinitionKind::enemy :
                value->category == game::content::AuthoringCategory::object ? ContentDefinitionKind::object :
                value->category == game::content::AuthoringCategory::pickup ? ContentDefinitionKind::pickup :
                value->category == game::content::AuthoringCategory::npc ? ContentDefinitionKind::npc :
                value->category == game::content::AuthoringCategory::item ? ContentDefinitionKind::item :
                value->category == game::content::AuthoringCategory::rewardProfile ? ContentDefinitionKind::rewardProfile :
                value->category == game::content::AuthoringCategory::rewardGrant ? ContentDefinitionKind::rewardGrant : ContentDefinitionKind::shop);
            contentEditValues_[fieldDescriptorTag] = value->tags.empty() ? "tag" : value->tags.front(); }
        const auto categories = std::array<std::string_view, 8>{"enemy", "object", "pickup", "npc", "item", "rewardProfile", "rewardGrant", "shop"};
        field("displayName", fieldDescriptorDisplay, inspectorY);
        const bool categoryChanged = enumButton("category", fieldDescriptorCategory, inspectorY + 48, categories);
        field("tag", fieldDescriptorTag, inspectorY + 96);
        const auto tagIndex = value->tags.empty() ? noContentIndex : std::min(selectedAnimationMarkerIndex_, value->tags.size() - 1);
        if (ui.button({panel.x + 8, inspectorY + 144, 54, 18}, "ADD") && writable && !contentEditValues_[fieldDescriptorTag].empty()) {
            auto updated = *value; updated.tags.push_back(contentEditValues_[fieldDescriptorTag]); std::string error;
            if (contentWorkspace_->updateAuthoringDescriptor(key.id, updated, error)) { selectedAnimationMarkerIndex_ = updated.tags.size() - 1; refresh("Descriptor tag added"); } else status_ = error;
            return;
        }
        if (ui.button({panel.x + 66, inspectorY + 144, 54, 18}, "EDIT") && writable && tagIndex != noContentIndex) {
            auto updated = *value; updated.tags[tagIndex] = contentEditValues_[fieldDescriptorTag]; std::string error;
            if (contentWorkspace_->updateAuthoringDescriptor(key.id, updated, error)) refresh("Descriptor tag updated"); else status_ = error; return;
        }
        if (ui.button({panel.x + 124, inspectorY + 144, 64, 18}, "REMOVE") && writable && tagIndex != noContentIndex) {
            auto updated = *value; updated.tags.erase(updated.tags.begin() + static_cast<std::ptrdiff_t>(tagIndex)); std::string error;
            if (contentWorkspace_->updateAuthoringDescriptor(key.id, updated, error)) refresh("Descriptor tag removed"); else status_ = error; return;
        }
        if (writable && (input.enterPressed || categoryChanged) &&
            (contentFocusedField_ == fieldDescriptorDisplay || contentFocusedField_ == fieldDescriptorCategory)) {
            auto updated = *value; updated.displayName = contentEditValues_[fieldDescriptorDisplay];
            const auto it = std::find(categories.begin(), categories.end(), contentEditValues_[fieldDescriptorCategory]);
            if (it != categories.end()) updated.category = static_cast<game::content::AuthoringCategory>(it - categories.begin());
            std::string error; if (contentWorkspace_->updateAuthoringDescriptor(key.id, updated, error)) refresh("Descriptor updated"); else status_ = error;
        }
        return;
    }
    if (key.kind == ContentDefinitionKind::tileSemantic) {
        const auto* value = contentWorkspace_->tileSemantic(key.id); if (!value) return;
        if (contentEditKey_ != key) { resetContentEditState(); contentEditKey_ = key;
            contentEditValues_[fieldSemanticTileset] = std::string(value->tilesetId.value());
            contentEditValues_[fieldSemanticSource] = std::to_string(value->sourceIndex);
            contentEditValues_[fieldSemanticFamily] = value->family;
            contentEditValues_[fieldSemanticPreferredLayer] = value->preferredLayer;
            contentEditValues_[fieldSemanticRole] = game::authoring::toString(value->role);
            contentEditValues_[fieldSemanticTopology] = game::authoring::toString(value->topology);
            contentEditValues_[fieldSemanticEdges] = std::to_string(static_cast<int>(value->north)) + "," + std::to_string(static_cast<int>(value->east)) + "," + std::to_string(static_cast<int>(value->south)) + "," + std::to_string(static_cast<int>(value->west)); }
        const auto roles = std::array<std::string_view, 7>{"floor", "wall", "corner", "ledge", "opening", "detail", "unknown"};
        const auto topologies = std::array<std::string_view, 9>{"unknown", "interior", "straightHorizontal", "straightVertical", "outerCorner", "innerCorner", "cap", "junction", "architecturalDetail"};
        field("tilesetId", fieldSemanticTileset, inspectorY); field("sourceIndex", fieldSemanticSource, inspectorY + 48); field("family", fieldSemanticFamily, inspectorY + 96);
        const bool roleChanged = enumButton("role", fieldSemanticRole, inspectorY + 144, roles);
        const bool topologyChanged = enumButton("topology", fieldSemanticTopology, inspectorY + 192, topologies);
        field("preferredLayer", fieldSemanticPreferredLayer, inspectorY + 240); field("edges N,E,S,W", fieldSemanticEdges, inspectorY + 288);
        const bool flipChanged = ui.button({panel.x + 8, inspectorY + 336, panel.width - 16, 20}, value->flipXAllowed ? "FLIP X ALLOWED" : "FLIP X DISABLED", value->flipXAllowed);
        if (writable && (input.enterPressed || roleChanged || topologyChanged || flipChanged) && contentFocusedField_ >= fieldSemanticTileset && contentFocusedField_ <= fieldSemanticEdges) {
            auto updated = *value; std::string error; bool ok = true;
            if (contentFocusedField_ == fieldSemanticTileset) updated.tilesetId = simulation::DefinitionId{contentEditValues_[fieldSemanticTileset]};
            else if (contentFocusedField_ == fieldSemanticSource) { const auto p = parseUnsigned(contentEditValues_[fieldSemanticSource]); if (!p) ok = false; else updated.sourceIndex = *p; }
            else if (contentFocusedField_ == fieldSemanticFamily) updated.family = contentEditValues_[fieldSemanticFamily];
            else if (contentFocusedField_ == fieldSemanticPreferredLayer) updated.preferredLayer = contentEditValues_[fieldSemanticPreferredLayer];
            else if (contentFocusedField_ == fieldSemanticRole || roleChanged) { const auto it = std::find(roles.begin(), roles.end(), contentEditValues_[fieldSemanticRole]); if (it == roles.end()) ok = false; else updated.role = static_cast<game::authoring::TileRole>(it - roles.begin()); }
            else if (contentFocusedField_ == fieldSemanticTopology || topologyChanged) { const auto it = std::find(topologies.begin(), topologies.end(), contentEditValues_[fieldSemanticTopology]); if (it == topologies.end()) ok = false; else updated.topology = static_cast<game::authoring::TileTopology>(it - topologies.begin()); }
            else if (contentFocusedField_ == fieldSemanticEdges) { const auto p = parseIntegerList<4>(contentEditValues_[fieldSemanticEdges]); if (!p || (*p)[0] < 0 || (*p)[0] > 4 || (*p)[1] < 0 || (*p)[1] > 4 || (*p)[2] < 0 || (*p)[2] > 4 || (*p)[3] < 0 || (*p)[3] > 4) ok = false; else { updated.north = static_cast<game::authoring::EdgeProfile>((*p)[0]); updated.east = static_cast<game::authoring::EdgeProfile>((*p)[1]); updated.south = static_cast<game::authoring::EdgeProfile>((*p)[2]); updated.west = static_cast<game::authoring::EdgeProfile>((*p)[3]); } }
            if (flipChanged) updated.flipXAllowed = !updated.flipXAllowed;
            if (ok && contentWorkspace_->updateTileSemantic(key.id, updated, error)) refresh("Tile semantic updated"); else status_ = ok ? error : "Tile semantic field is invalid";
        }
        return;
    }
    if (key.kind == ContentDefinitionKind::stamp) {
        const auto* value = contentWorkspace_->stamp(key.id); if (!value) return;
        if (contentEditKey_ != key) { resetContentEditState(); contentEditKey_ = key; contentEditValues_[fieldStampDisplay] = value->displayName; contentEditValues_[fieldStampSize] = std::to_string(value->width) + "," + std::to_string(value->height); contentEditValues_[fieldStampAnchor] = pointText(value->anchor); contentEditValues_[fieldStampConfidence] = game::authoring::toString(value->confidence); contentEditValues_[fieldStampCell] = "0,0,"; }
        const auto confidence = std::array<std::string_view, 3>{"confirmed", "probable", "unverified"};
        field("displayName", fieldStampDisplay, inspectorY); field("width,height", fieldStampSize, inspectorY + 48); field("anchor x,y", fieldStampAnchor, inspectorY + 96); field("cell x,y,tileId", fieldStampCell, inspectorY + 144);
        const auto cellIndex = value->cells.empty() ? noContentIndex : std::min(selectedAnimationMarkerIndex_, value->cells.size() - 1);
        const auto gridWidth = std::min<std::uint32_t>(value->width, 8);
        const auto gridHeight = std::min<std::uint32_t>(value->height, 8);
        const int gridTop = inspectorY + 216;
        ui.label("STAMP GRID", panel.x + 8, gridTop - 14);
        for (std::uint32_t row = 0; row < gridHeight; ++row) for (std::uint32_t column = 0; column < gridWidth; ++column) {
            const auto cell = std::find_if(value->cells.begin(), value->cells.end(), [&](const auto& item) {
                return item.x == static_cast<int>(column) && item.y == static_cast<int>(row);
            });
            const auto cellIndexAtPosition = cell == value->cells.end() ? noContentIndex
                : static_cast<std::size_t>(cell - value->cells.begin());
            if (ui.button({panel.x + 8 + static_cast<int>(column) * 22,
                           gridTop + static_cast<int>(row) * 20, 20, 18},
                          cell == value->cells.end() ? "." : "*", cellIndex == cellIndexAtPosition) &&
                writable) {
                if (cellIndexAtPosition == noContentIndex) {
                    auto updated = *value; updated.cells.push_back({static_cast<int>(column), static_cast<int>(row), {}}); std::string error;
                    if (contentWorkspace_->updateStamp(key.id, updated, error)) {
                        selectedAnimationMarkerIndex_ = updated.cells.size() - 1; refresh("Stamp cell added"); return;
                    }
                    status_ = error;
                } else selectedAnimationMarkerIndex_ = cellIndexAtPosition;
            }
        }
        const int cellControlsY = gridTop + static_cast<int>(gridHeight) * 20 + 4;
        if (cellIndex != noContentIndex) { if (ui.button({panel.x + 8, cellControlsY, panel.width - 16, 16}, std::string("Cell ") + std::to_string(cellIndex) + "  " + std::string(value->cells[cellIndex].tileId.value()), true)) { contentEditValues_[fieldStampCell] = std::to_string(value->cells[cellIndex].x) + "," + std::to_string(value->cells[cellIndex].y) + "," + std::string(value->cells[cellIndex].tileId.value()); } }
        if (ui.button({panel.x + 8, cellControlsY + 20, 54, 18}, "ADD") && writable) { auto updated = *value; updated.cells.push_back({0, 0, {}}); std::string error; if (contentWorkspace_->updateStamp(key.id, updated, error)) { selectedAnimationMarkerIndex_ = updated.cells.size() - 1; refresh("Stamp cell added"); } else status_ = error; return; }
        if (ui.button({panel.x + 66, cellControlsY + 20, 64, 18}, "REMOVE") && writable && cellIndex != noContentIndex) { auto updated = *value; updated.cells.erase(updated.cells.begin() + static_cast<std::ptrdiff_t>(cellIndex)); std::string error; if (contentWorkspace_->updateStamp(key.id, updated, error)) refresh("Stamp cell removed"); else status_ = error; return; }
        const bool confidenceChanged = enumButton("confidence", fieldStampConfidence, cellControlsY + 44, confidence);
        if (writable && (input.enterPressed || confidenceChanged) && contentFocusedField_ >= fieldStampDisplay && contentFocusedField_ <= fieldStampCell) {
            auto updated = *value; std::string error; bool ok = true;
            if (contentFocusedField_ == fieldStampDisplay) updated.displayName = contentEditValues_[fieldStampDisplay];
            else if (contentFocusedField_ == fieldStampSize) { const auto p = parseIntegerList<2>(contentEditValues_[fieldStampSize]); if (!p || (*p)[0] < 0 || (*p)[1] < 0) ok = false; else { updated.width = static_cast<std::uint32_t>((*p)[0]); updated.height = static_cast<std::uint32_t>((*p)[1]); } }
            else if (contentFocusedField_ == fieldStampAnchor) { const auto p = parseIntegerList<2>(contentEditValues_[fieldStampAnchor]); if (!p) ok = false; else updated.anchor = {(*p)[0], (*p)[1]}; }
            else if (contentFocusedField_ == fieldStampCell && cellIndex != noContentIndex) { const auto comma = contentEditValues_[fieldStampCell].find(','); const auto comma2 = contentEditValues_[fieldStampCell].find(',', comma == std::string::npos ? comma : comma + 1); if (comma == std::string::npos || comma2 == std::string::npos) ok = false; else { try { updated.cells[cellIndex].x = std::stoi(contentEditValues_[fieldStampCell].substr(0, comma)); updated.cells[cellIndex].y = std::stoi(contentEditValues_[fieldStampCell].substr(comma + 1, comma2 - comma - 1)); updated.cells[cellIndex].tileId = simulation::DefinitionId{contentEditValues_[fieldStampCell].substr(comma2 + 1)}; } catch (...) { ok = false; } } }
            const auto it = std::find(confidence.begin(), confidence.end(), contentEditValues_[fieldStampConfidence]); if (it != confidence.end()) updated.confidence = static_cast<game::authoring::SemanticConfidence>(it - confidence.begin());
            if (ok && contentWorkspace_->updateStamp(key.id, updated, error)) refresh("Stamp updated"); else status_ = ok ? error : "Stamp field is invalid";
        }
        return;
    }
    if (key.kind == ContentDefinitionKind::staticSprite || key.kind == ContentDefinitionKind::animation || key.kind == ContentDefinitionKind::visualImage || key.kind == ContentDefinitionKind::enemyVisual || key.kind == ContentDefinitionKind::objectVisual || key.kind == ContentDefinitionKind::npcVisual) return;
    showReadOnlyFields("Typed editor", {{"category", ContentWorkspaceDocument::categoryName(key.kind)}, {"id", std::string(key.id.value())}});
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
    if (mapTileSelection_ || drag_.kind == DragState::Kind::tileSelection) {
        const auto origin = mapTileSelection_ ? mapTileSelection_->origin
            : TileCoordinate{static_cast<std::uint32_t>(std::min(drag_.worldStart.x, drag_.worldCurrent.x)),
                             static_cast<std::uint32_t>(std::min(drag_.worldStart.y, drag_.worldCurrent.y))};
        const auto width = mapTileSelection_ ? mapTileSelection_->width
            : static_cast<std::uint32_t>(std::abs(drag_.worldCurrent.x - drag_.worldStart.x) + 1);
        const auto height = mapTileSelection_ ? mapTileSelection_->height
            : static_cast<std::uint32_t>(std::abs(drag_.worldCurrent.y - drag_.worldStart.y) + 1);
        const auto screen = worldToScreen({static_cast<int>(origin.x * data.tileSize),
                                           static_cast<int>(origin.y * data.tileSize)}, viewport);
        outline(renderer, {screen.x, screen.y, static_cast<int>(width * scaled),
                           static_cast<int>(height * scaled)}, selectedColor);
    }
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
        if(tool==EditorTool::tileSelection && tile){drag_.kind=DragState::Kind::tileSelection;drag_.worldStart={static_cast<int>(tile->x),static_cast<int>(tile->y)};drag_.worldCurrent=drag_.worldStart;}
        else if((tool==EditorTool::tilePencil||tool==EditorTool::tileErase||tool==EditorTool::collisionPaint||tool==EditorTool::collisionErase) && tile){drag_.kind=DragState::Kind::brush;drag_.stroke={*tile};}
        else if((tool==EditorTool::tileRectangle||tool==EditorTool::collisionRectangle||tool==EditorTool::collisionRectangleErase) && tile){drag_.kind=DragState::Kind::rectangle;drag_.worldStart={static_cast<int>(tile->x),static_cast<int>(tile->y)};drag_.worldCurrent=drag_.worldStart;}
        else if((tool==EditorTool::tileFill && input.alt)||tool==EditorTool::tileEyedropper){if(tile){const auto index=static_cast<std::size_t>(tile->y)*document_.data().width+tile->x;const auto cell=document_.data().layers[document_.activeLayer()].cells[index];if(cell){const auto& ref=document_.data().tileReferences[*cell];selectedTileset_=ref.tilesetId;selectedTile_=ref.sourceIndex;tileFlipX_=world::hasFlag(ref.flags,world::TileFlags::flipX);tileBrush_={ref.tilesetId,1,1,{ref}};tilePaletteScroll_=0;}if(tool==EditorTool::tileEyedropper)document_.activeTool()=EditorTool::tilePencil;}}
        else if(tool==EditorTool::tileFill && tile)execute(std::make_unique<PaintTilesCommand>(document_.activeLayer(),tileFloodCells(document_.data(),document_.activeLayer(),tile->x,tile->y),selectedTileReference()));
        else if((tool==EditorTool::collisionFill||tool==EditorTool::collisionFillErase) && tile)execute(std::make_unique<SetCollisionCommand>(collisionFloodCells(document_.data(),tile->x,tile->y),tool==EditorTool::collisionFill));
        else if(tool==EditorTool::stampPlace && tile && selectedStamp_<content_.authoringSemantics().stamps().size()) execute(std::make_unique<PlaceStampCommand>(document_.activeLayer(),content_.authoringSemantics().stamps()[selectedStamp_],*tile,content_.authoringSemantics()));
        else if(tool==EditorTool::entityPlace)placeSelected(snapped);
        else if(tool==EditorTool::regionCreate){drag_.kind=DragState::Kind::regionCreate;drag_.worldStart=snapped;drag_.worldCurrent=snapped;}
        else if(tool==EditorTool::select){auto hit=hitTest(worldPoint);if(hit){document_.selection()=*hit;if(hit->kind==SelectionKind::region){const auto it=std::find_if(document_.regions().begin(),document_.regions().end(),[&](const auto& r){return r.id==hit->instanceId;});if(it!=document_.regions().end()&&std::abs(worldPoint.x-(it->bounds.x+it->bounds.width))<8&&std::abs(worldPoint.y-(it->bounds.y+it->bounds.height))<8){drag_.kind=DragState::Kind::regionResize;drag_.regionStart=it->bounds;drag_.worldCurrent=worldPoint;return;}}if(hit->kind!=SelectionKind::none){drag_.kind=DragState::Kind::move;drag_.worldStart=worldPoint;drag_.worldCurrent=snapped;if(hit->kind==SelectionKind::region){auto it=std::find_if(document_.regions().begin(),document_.regions().end(),[&](const auto& r){return r.id==hit->instanceId;});drag_.entityStart={it->bounds.x,it->bounds.y};}else if(hit->kind==SelectionKind::playerSpawn){auto it=std::find_if(document_.data().playerSpawns.begin(),document_.data().playerSpawns.end(),[&](const auto& v){return v.id.value()==hit->authoredId;});drag_.entityStart=it->position;}else if(hit->kind==SelectionKind::mapLink){auto it=std::find_if(document_.data().links.begin(),document_.data().links.end(),[&](const auto& v){return v.id==hit->authoredId;});drag_.entityStart={it->trigger.x,it->trigger.y};}else{auto point=[&](){if(hit->kind==SelectionKind::enemy)return std::find_if(document_.data().enemies.begin(),document_.data().enemies.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;if(hit->kind==SelectionKind::npc)return std::find_if(document_.data().npcs.begin(),document_.data().npcs.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;if(hit->kind==SelectionKind::object)return std::find_if(document_.data().objects.begin(),document_.data().objects.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;return std::find_if(document_.data().pickups.begin(),document_.data().pickups.end(),[&](const auto& v){return v.id==hit->instanceId;})->position;}();drag_.entityStart=point;}}}else document_.selection().clear();}
    }
    if(drag_.kind==DragState::Kind::brush&&input.pointer.leftDown&&tile){if(std::none_of(drag_.stroke.begin(),drag_.stroke.end(),[&](auto v){return v.x==tile->x&&v.y==tile->y;}))drag_.stroke.push_back(*tile);}
    if(drag_.kind==DragState::Kind::rectangle||drag_.kind==DragState::Kind::tileSelection||drag_.kind==DragState::Kind::regionCreate||drag_.kind==DragState::Kind::regionResize||drag_.kind==DragState::Kind::move){if((drag_.kind==DragState::Kind::rectangle||drag_.kind==DragState::Kind::tileSelection)&&tile)drag_.worldCurrent={static_cast<int>(tile->x),static_cast<int>(tile->y)};else if(drag_.kind!=DragState::Kind::rectangle&&drag_.kind!=DragState::Kind::tileSelection)drag_.worldCurrent=snapped;}
    if(input.pointer.leftReleased){
        const auto paintBrush = [&](const std::vector<TileCoordinate>& origins, bool erase) {
            TileBrushSelection brush = tileBrush_;
            if (!brush.valid()) brush = {selectedTileset_, 1, 1, {selectedTileReference()}};
            auto compound = std::make_unique<CompoundEditorCommand>("Paint Tile Brush");
            for (const auto origin : origins) for (const auto& placement : brushPlacements(brush, origin, document_.data()))
                compound->add(std::make_unique<PaintTilesCommand>(document_.activeLayer(), std::vector<TileCoordinate>{placement.first}, erase ? std::nullopt : std::optional<maps::MapTileReference>{placement.second}));
            execute(std::move(compound));
        };
        if(drag_.kind==DragState::Kind::tileSelection){const auto minX=std::min(drag_.worldStart.x,drag_.worldCurrent.x);const auto minY=std::min(drag_.worldStart.y,drag_.worldCurrent.y);const auto maxX=std::max(drag_.worldStart.x,drag_.worldCurrent.x);const auto maxY=std::max(drag_.worldStart.y,drag_.worldCurrent.y);mapTileSelection_=MapTileSelection{{static_cast<std::uint32_t>(minX),static_cast<std::uint32_t>(minY)},static_cast<std::uint32_t>(maxX-minX+1),static_cast<std::uint32_t>(maxY-minY+1)};status_="Map tile selection ready for stamp authoring";}
        else if(drag_.kind==DragState::Kind::brush){if(tool==EditorTool::collisionPaint||tool==EditorTool::collisionErase)execute(std::make_unique<SetCollisionCommand>(drag_.stroke,tool==EditorTool::collisionPaint));else paintBrush(drag_.stroke,tool==EditorTool::tileErase);}
        else if(drag_.kind==DragState::Kind::rectangle){const auto cells=rectangleCells(drag_.worldStart.x,drag_.worldStart.y,drag_.worldCurrent.x,drag_.worldCurrent.y,document_.data());if(tool==EditorTool::collisionRectangle||tool==EditorTool::collisionRectangleErase)execute(std::make_unique<SetCollisionCommand>(cells,tool==EditorTool::collisionRectangle));else paintBrush(cells, false);}
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
    tileBrush_ = {selectedTileset_, 1, 1, {selectedTileReference()}};
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
    if (mapPaletteTab_ == MapPaletteTab::rules) {
        ui.label("WORLD RULES", panel.x + 8, y); y += 18;
        const auto objectFor = [&](bool activation, bool door) -> simulation::PersistentInstanceId {
            for (const auto& object : document_.data().objects) {
                const auto* definition = content_.objects().find(object.definitionId);
                if (!definition) continue;
                if ((!activation || definition->activation.has_value()) &&
                    (!door || definition->door.has_value())) return object.id;
            }
            return {};
        };
        const auto regionTarget = [&]() -> simulation::DefinitionId {
            return document_.regions().empty() ? simulation::DefinitionId{}
                : simulation::DefinitionId{document_.regions().front().regionId};
        };
        const auto encounterTarget = [&]() -> simulation::DefinitionId {
            return document_.encounters().empty() ? simulation::DefinitionId{}
                : document_.encounters().front().id;
        };
        if (ui.button({panel.x + 8, y, 76, 18}, "ADD", false)) {
            maps::WorldRuleDefinition rule;
            rule.id = simulation::DefinitionId{"rule.editor." + std::to_string(document_.rules().size() + 1)};
            const auto object = objectFor(true, false);
            if (object) rule.trigger = {maps::WorldTriggerKind::objectActivated, {}, object};
            else rule.trigger = {maps::WorldTriggerKind::mapEntered, {}, {}};
            rule.actions.push_back({maps::WorldActionKind::setFlag, {"flag.editor.rule"}, {}, {}});
            std::string error;
            if (document_.addRule(std::move(rule), error)) {
                selectedRuleIndex_ = document_.rules().size() - 1;
                status_ = "World rule added";
            } else status_ = error;
        }
        if (document_.rules().empty()) {
            ui.label("No authored rules", panel.x + 8, y + 28);
            return;
        }
        selectedRuleIndex_ = std::min(selectedRuleIndex_, document_.rules().size() - 1);
        int listY = y + 24;
        for (std::size_t index = 0; index < document_.rules().size(); ++index) {
            if (ui.button({panel.x + 8, listY, panel.width - 16, 18},
                          document_.rules()[index].id.value(), selectedRuleIndex_ == index)) {
                selectedRuleIndex_ = index;
            }
            listY += 20;
            if (listY > panel.height - 118) break;
        }
        const auto& rule = document_.rules()[selectedRuleIndex_];
        const auto triggerNames = std::array<std::string_view, 8>{"mapEntered", "regionEntered", "regionExited", "encounterStarted", "encounterCompleted", "objectOpened", "objectActivated", "objectDeactivated"};
        const auto triggerIndex = static_cast<std::size_t>(rule.trigger.kind);
        ui.label("TRIGGER", panel.x + 8, panel.height - 110);
        if (ui.button({panel.x + 8, panel.height - 96, panel.width - 16, 18},
                      triggerNames[std::min(triggerIndex, triggerNames.size() - 1)], false)) {
            const auto next = (triggerIndex + 1) % triggerNames.size();
            maps::WorldTrigger trigger;
            trigger.kind = static_cast<maps::WorldTriggerKind>(next);
            if (trigger.kind == maps::WorldTriggerKind::regionEntered || trigger.kind == maps::WorldTriggerKind::regionExited) trigger.definitionTarget = regionTarget();
            else if (trigger.kind == maps::WorldTriggerKind::encounterStarted || trigger.kind == maps::WorldTriggerKind::encounterCompleted) trigger.definitionTarget = encounterTarget();
            else if (trigger.kind == maps::WorldTriggerKind::objectOpened) trigger.instanceTarget = objectFor(false, false);
            else if (trigger.kind == maps::WorldTriggerKind::objectActivated || trigger.kind == maps::WorldTriggerKind::objectDeactivated) trigger.instanceTarget = objectFor(true, false);
            std::string error;
            if (!document_.setRuleTrigger(rule.id, trigger, error)) status_ = error;
        }
        ui.label("conditions " + std::to_string(rule.conditions.size()) +
                 " / actions " + std::to_string(rule.actions.size()), panel.x + 8, panel.height - 74);
        if (ui.button({panel.x + 8, panel.height - 54, 78, 18}, rule.once ? "ONCE ON" : "ONCE OFF", rule.once)) {
            std::string error; if (!document_.setRuleOnce(rule.id, !rule.once, error)) status_ = error;
        }
        if (ui.button({panel.x + 92, panel.height - 54, 72, 18}, "ADD COND")) {
            const auto object = objectFor(true, false);
            if (!object) status_ = "No activation-capable object placement";
            else { std::string error; if (!document_.addRuleCondition(rule.id, {maps::WorldConditionKind::objectActive, {}, object}, error)) status_ = error; }
        }
        if (ui.button({panel.x + 170, panel.height - 54, 72, 18}, "ADD ACT")) {
            maps::WorldAction action;
            if (!content_.presentationEffects().definitions().empty()) action = {maps::WorldActionKind::playPresentationEffect, content_.presentationEffects().definitions().front().id, {}, {}};
            else action = {maps::WorldActionKind::setFlag, {"flag.editor.action"}, {}, {}};
            std::string error; if (!document_.addRuleAction(rule.id, action, error)) status_ = error;
        }
        if (ui.button({panel.x + 8, panel.height - 30, 78, 18}, "REMOVE")) {
            std::string error; if (!document_.removeRule(rule.id, error)) status_ = error; else selectedRuleIndex_ = 0;
        }
        return;
    }
    if (mapPaletteTab_ == MapPaletteTab::encounters) {
        ui.label("ENCOUNTERS", panel.x + 8, y); y += 18;
        if (ui.button({panel.x + 8, y, 76, 18}, "ADD", false)) {
            maps::EncounterDefinition encounter;
            encounter.id = simulation::DefinitionId{"encounter.editor." + std::to_string(document_.encounters().size() + 1)};
            if (!document_.data().enemies.empty()) encounter.participants.push_back(document_.data().enemies.front().id);
            if (contentWorkspace_) {
                for (const auto& key : contentWorkspace_->index()) {
                    if (key.kind == ContentDefinitionKind::rewardGrant) {
                        encounter.rewardGrantId = key.id;
                        break;
                    }
                }
            }
            std::string error;
            if (document_.addEncounter(std::move(encounter), error)) {
                selectedEncounterIndex_ = document_.encounters().size() - 1;
                status_ = "Encounter added";
            } else status_ = error;
        }
        if (document_.encounters().empty()) { ui.label("No authored encounters", panel.x + 8, y + 28); return; }
        selectedEncounterIndex_ = std::min(selectedEncounterIndex_, document_.encounters().size() - 1);
        int listY = y + 24;
        for (std::size_t index = 0; index < document_.encounters().size(); ++index) {
            if (ui.button({panel.x + 8, listY, panel.width - 16, 18}, document_.encounters()[index].id.value(), selectedEncounterIndex_ == index)) selectedEncounterIndex_ = index;
            listY += 20;
            if (listY > panel.height - 112) break;
        }
        const auto& encounter = document_.encounters()[selectedEncounterIndex_];
        ui.label("participants " + std::to_string(encounter.participants.size()), panel.x + 8, panel.height - 108);
        if (ui.button({panel.x + 8, panel.height - 88, 80, 18}, "ADD ENEMY")) {
            simulation::PersistentInstanceId candidate{};
            for (const auto& enemy : document_.data().enemies) if (std::find(encounter.participants.begin(), encounter.participants.end(), enemy.id) == encounter.participants.end()) { candidate = enemy.id; break; }
            if (!candidate) status_ = "No unused enemy placement";
            else { std::string error; if (!document_.addEncounterParticipant(encounter.id, candidate, error)) status_ = error; }
        }
        if (ui.button({panel.x + 94, panel.height - 88, 80, 18}, "REMOVE" ) && !encounter.participants.empty()) {
            std::string error; if (!document_.removeEncounterParticipant(encounter.id, encounter.participants.back(), error)) status_ = error;
        }
        if (ui.button({panel.x + 8, panel.height - 64, panel.width - 16, 18}, encounter.rewardGrantId ? "CYCLE REWARD" : "SET REWARD")) {
            std::vector<simulation::DefinitionId> grants;
            if (contentWorkspace_) for (const auto& key : contentWorkspace_->index())
                if (key.kind == ContentDefinitionKind::rewardGrant) grants.push_back(key.id);
            if (!grants.empty()) {
                std::optional<simulation::DefinitionId> next = grants.front();
                if (encounter.rewardGrantId) {
                    const auto it = std::find(grants.begin(), grants.end(), *encounter.rewardGrantId);
                    next = it == grants.end() || std::next(it) == grants.end() ? grants.front() : *std::next(it);
                }
                std::string error; if (!document_.setEncounterRewardGrant(encounter.id, next, error)) status_ = error;
            } else status_ = "No RewardGrant definitions available";
        }
        if (ui.button({panel.x + 8, panel.height - 40, panel.width - 16, 18}, "DELETE ENCOUNTER")) {
            std::string error; if (!document_.removeEncounter(encounter.id, error)) status_ = error; else selectedEncounterIndex_ = 0;
        }
        return;
    }
    if(const auto* semantic=content_.authoringSemantics().findTile(selectedTileset_,selectedTile_)) {
        ui.label("TILE: "+std::string(semantic->id.value()),panel.x+8,y); y+=14;
        ui.label(std::string(toString(semantic->role))+" / "+toString(semantic->topology),panel.x+8,y); y+=14;
        ui.label("FlipX "+std::string(semantic->flipXAllowed?"allowed":"not approved"),panel.x+8,y);
        if (ui.button({panel.x + 8, y + 16, panel.width - 16, 18}, "OPEN SEMANTIC")) {
            selectedContentCategory_ = ContentDefinitionKind::tileSemantic;
            selectedContentDefinition_ = ContentDefinitionKey{ContentDefinitionKind::tileSemantic, semantic->id};
            resetContentEditState(); resetContentPreviewState(); contentMode_ = true; return;
        }
        y+=40;
    } else {
        ui.label("TILE: Unclassified (RAW)",panel.x+8,y); y+=18;
        if (contentWorkspace_ && contentWorkspace_->writable() &&
            ui.button({panel.x + 8, y, panel.width - 16, 18}, "CREATE SEMANTIC FROM TILE")) {
            newContentDefinitionId_ = "tile." + std::string(selectedTileset_.value()) + "." + std::to_string(selectedTile_);
            if (!contentWorkspace_->files().empty()) {
                std::error_code relativeError;
                newContentDefinitionFile_ = std::filesystem::relative(
                    contentWorkspace_->files().front().path, contentWorkspace_->root(), relativeError).generic_string();
            }
            selectedContentCategory_ = ContentDefinitionKind::tileSemantic;
            selectedContentDefinition_.reset();
            resetContentEditState(); resetContentPreviewState(); contentMode_ = true;
            status_ = "Semantic draft prepared from selected raw tile";
            return;
        }
        y+=24;
    }
    if (mapTileSelection_ && contentWorkspace_ && contentWorkspace_->writable() &&
        ui.button({panel.x + 8, y, panel.width - 16, 18}, "CREATE STAMP FROM SELECTION")) {
        const auto& data = document_.data();
        const auto& layer = data.layers[document_.activeLayer()];
        game::content::AuthoredStamp stamp;
        stamp.displayName = "Map Selection";
        stamp.width = mapTileSelection_->width;
        stamp.height = mapTileSelection_->height;
        bool valid = true;
        for (std::uint32_t row = 0; row < stamp.height && valid; ++row) for (std::uint32_t column = 0; column < stamp.width; ++column) {
            const auto x = mapTileSelection_->origin.x + column;
            const auto yTile = mapTileSelection_->origin.y + row;
            if (x >= data.width || yTile >= data.height) { valid = false; break; }
            const auto cellIndex = static_cast<std::size_t>(yTile) * data.width + x;
            if (cellIndex >= layer.cells.size() || !layer.cells[cellIndex] ||
                *layer.cells[cellIndex] >= data.tileReferences.size()) { valid = false; break; }
            const auto& reference = data.tileReferences[*layer.cells[cellIndex]];
            const auto* semantic = content_.authoringSemantics().findTile(reference.tilesetId, reference.sourceIndex);
            if (!semantic) { valid = false; break; }
            stamp.cells.push_back({static_cast<int>(column), static_cast<int>(row), semantic->id});
        }
        if (!valid) {
            status_ = "Cannot create stamp: every selected tile needs a semantic definition";
        } else {
            pendingStampFromSelection_ = stamp;
            newContentDefinitionId_ = "stamp.map.selection";
            if (!contentWorkspace_->files().empty()) {
                std::error_code relativeError;
                newContentDefinitionFile_ = std::filesystem::relative(
                    contentWorkspace_->files().front().path, contentWorkspace_->root(), relativeError).generic_string();
            }
            selectedContentCategory_ = ContentDefinitionKind::stamp;
            selectedContentDefinition_.reset();
            resetContentEditState(); resetContentPreviewState(); contentMode_ = true;
            status_ = "Stamp draft prepared from map selection";
            return;
        }
    }
    if (mapTileSelection_) y += 24;
    const auto& selection=document_.selection();if(selection.kind==SelectionKind::none){ui.label("No selection",panel.x+8,y);}else{ui.label("Selection",panel.x+8,y);y+=16;if(selection.instanceId)ui.label("ID "+std::to_string(selection.instanceId.value),panel.x+8,y);else ui.label(selection.authoredId,panel.x+8,y);y+=20;
        simulation::DefinitionId definition; ContentDefinitionKind definitionKind = ContentDefinitionKind::enemy;
        if (selection.kind == SelectionKind::enemy) for(const auto& enemy:document_.data().enemies)if(enemy.id==selection.instanceId){definition=enemy.definitionId;definitionKind=ContentDefinitionKind::enemy;}
        if (selection.kind == SelectionKind::npc) for(const auto& npc:document_.data().npcs)if(npc.id==selection.instanceId){definition=npc.definitionId;definitionKind=ContentDefinitionKind::npc;}
        if (selection.kind == SelectionKind::object) for(const auto& object:document_.data().objects)if(object.id==selection.instanceId){definition=object.definitionId;definitionKind=ContentDefinitionKind::object;}
        if (selection.kind == SelectionKind::pickup) for(const auto& pickup:document_.data().pickups)if(pickup.id==selection.instanceId){definition=pickup.definitionId;definitionKind=ContentDefinitionKind::pickup;}
        if (!definition.empty() && ui.button({panel.x + 8, y, panel.width - 16, 20}, "OPEN DEFINITION")) {
            selectedContentCategory_ = definitionKind; selectedContentDefinition_ = ContentDefinitionKey{definitionKind, definition};
            resetContentEditState(); resetContentPreviewState(); contentMode_ = true; return;
        }
        y+=24;
    if (selection.kind == SelectionKind::region) {
        const auto region = std::find_if(document_.regions().begin(), document_.regions().end(),
            [&](const auto& value) { return value.id == selection.instanceId; });
        if (region != document_.regions().end()) {
            ui.label("Region " + region->regionId, panel.x + 8, y);
            ui.label("Bounds " + std::to_string(region->bounds.x) + "," +
                     std::to_string(region->bounds.y) + "," +
                     std::to_string(region->bounds.width) + "," +
                     std::to_string(region->bounds.height), panel.x + 8, y + 16);
            const auto& effects = content_.presentationEffects().definitions();
            if (ui.button({panel.x + 8, y + 38, panel.width - 16, 18},
                          region->environmentEffectId ? "CYCLE ENVIRONMENT EFFECT" : "SET ENVIRONMENT EFFECT") &&
                !effects.empty()) {
                std::optional<simulation::DefinitionId> next = effects.front().id;
                if (region->environmentEffectId) {
                    const auto it = std::find_if(effects.begin(), effects.end(),
                        [&](const auto& effect) { return effect.id == *region->environmentEffectId; });
                    next = it == effects.end() || std::next(it) == effects.end()
                        ? effects.front().id : std::next(it)->id;
                }
                std::string error;
                if (!document_.setRegionEnvironmentEffect(
                        simulation::DefinitionId{region->regionId}, next, error)) status_ = error;
            }
            if (ui.button({panel.x + 8, y + 62, panel.width - 16, 18}, "CLEAR ENVIRONMENT EFFECT")) {
                std::string error;
                if (!document_.setRegionEnvironmentEffect(
                        simulation::DefinitionId{region->regionId}, std::nullopt, error)) status_ = error;
            }
            if (region->environmentEffectId) {
                ui.label("Effect " + std::string(region->environmentEffectId->value()),
                         panel.x + 8, y + 86);
                if (ui.button({panel.x + 8, y + 104, panel.width - 16, 18}, "OPEN EFFECT")) {
                    selectedContentCategory_ = ContentDefinitionKind::presentationEffect;
                    selectedContentDefinition_ = ContentDefinitionKey{
                        ContentDefinitionKind::presentationEffect, *region->environmentEffectId};
                    resetContentEditState(); resetContentPreviewState(); contentMode_ = true; return;
                }
            }
        }
    }
    const auto schemas=propertySchemasFor(content_,definition);for(const auto& schema:schemas){ui.label(schema.displayName,panel.x+8,y);y+=14;std::int64_t value=std::get<std::int64_t>(schema.defaultValue);const auto outer=document_.propertyOverrides().find(selection.instanceId.value);bool overridden=false;if(outer!=document_.propertyOverrides().end()){const auto found=outer->second.find(schema.id);if(found!=outer->second.end()){value=std::get<std::int64_t>(found->second);overridden=true;}}
            ui.label(std::to_string(value)+(overridden?" *":""),panel.x+8,y+4);if(ui.button({panel.x+90,y,32,18},"-"))execute(std::make_unique<SetPropertyCommand>(selection.instanceId,schema,PropertyValue{value-1},content_));if(ui.button({panel.x+126,y,32,18},"+"))execute(std::make_unique<SetPropertyCommand>(selection.instanceId,schema,PropertyValue{value+1},content_));if(ui.button({panel.x+162,y,72,18},"RESET"))execute(std::make_unique<SetPropertyCommand>(selection.instanceId,schema,std::nullopt,content_));y+=24;}}
    y=panel.height-150;ui.label("VALIDATION",panel.x+8,y);y+=16;if(contentWorkspace_&&!contentWorkspace_->valid()){ui.label("CONTENT: unavailable",panel.x+8,y);y+=14;}validationCache_.refreshIfNeeded(document_,content_);const auto& report=validationCache_.structural();const auto& semanticReport=validationCache_.semantic();ui.label("Structural: "+std::to_string(report.errorCount())+" errors",panel.x+8,y);y+=14;ui.label("Semantic: "+std::to_string(semanticReport.warningCount())+" warnings",panel.x+8,y);y+=16;for(const auto& issue:report.issues){if(y>panel.height-12)break;ui.label(issue.message.substr(0,32),panel.x+8,y);y+=12;}for(const auto& issue:semanticReport.issues){if(y>panel.height-12)break;ui.label(issue.message.substr(0,32),panel.x+8,y);y+=12;}
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
bool EditorApp::saveAll(std::string& error){if(contentWorkspace_&&contentWorkspace_->dirty()){if(!contentWorkspace_->saveAll(error))return false;status_="Content workspace saved";}refreshContentRegistry();if(document_.dirty()){if(!document_.save(content_,error))return false;}error.clear();return true;}
bool EditorApp::validateWorkspace(){const bool contentValid=contentWorkspace_&&contentWorkspace_->validateWorkspace();refreshContentRegistry();const bool visualValid=runVisualValidation();if(!contentValid){status_="Workspace invalid; asset validation skipped";}else if(!visualValid){status_="Content valid; visual asset diagnostics reported";}else{status_="Workspace and visual assets valid";}return contentValid&&visualValid;}
void EditorApp::refreshContentRegistry(){
    if (!contentWorkspace_) return;
    if (contentWorkspace_->compiledRegistry()) content_ = *contentWorkspace_->compiledRegistry();
    else content_ = game::GameContentRegistry{};
    validationCache_.invalidate();
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
    contentDialogueConditionIndex_ = noContentIndex;
    contentDialogueActionIndex_ = noContentIndex;
    contentFocusedField_ = -1;
    contentInspectorScroll_ = 0;
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
void EditorApp::togglePlaytest(){if(playtest_.active()){playtest_.stop();status_="Playtest stopped; editor document unchanged";return;}if(contentWorkspace_&&!contentWorkspace_->compiledRegistry()){status_="Playtest unavailable: Content Workspace is invalid";return;}std::string error;if(!playtest_.start(document_.data(),content_,error)){status_=error;return;}status_="Playtest active: runtime world built from document snapshot";}
std::string EditorApp::windowTitle() const{std::string title=contentMode_?"Dungeon Underworld - Content Studio - ":"Dungeon Underworld - Map Maker - ";if(contentMode_)title.append(contentWorkspace_->builtinReadOnly()?"Builtin content":"Content workspace");else title.append(document_.data().id.value());if(hasUnsavedChanges())title+=" *";return title;}
void EditorApp::updateStatus(core::RectI viewport,const EditorInputState& input){if(contentMode_)return;if(input.pointer.x>=viewport.x&&input.pointer.y>=viewport.y&&input.pointer.x<viewport.x+viewport.width&&input.pointer.y<viewport.y+viewport.height){const auto world=screenToWorld({input.pointer.x,input.pointer.y},viewport);std::ostringstream out;out<<"World "<<world.x<<','<<world.y<<"  Tile "<<world.x/document_.data().tileSize<<','<<world.y/document_.data().tileSize<<"  Zoom "<<static_cast<int>(zoom()*100)<<'%';status_=out.str();}}

} // namespace underworld::editor
