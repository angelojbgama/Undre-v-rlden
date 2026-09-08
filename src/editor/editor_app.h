#pragma once

#include "editor/editor_commands.h"
#include "editor/editor_playtest.h"
#include "editor/editor_localization.h"
#include "editor/editor_ui.h"
#include "editor/editor_layout.h"
#include "editor/content_workspace_document.h"
#include "editor/world_project_document.h"
#include "editor/visual_preview.h"
#include "engine/assets/asset_manager.h"
#include "engine/render/framebuffer.h"
#include "game/game_content.h"
#include "game/presentation/visual_content_loader.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace underworld::platform { class ImageDecoder; }
namespace underworld::render { class BitmapFont; class Image; }

namespace underworld::editor {

enum class EditorShellCommand {
    newMap, newProject, undo, redo, toggleGrid, frameMap, playtest,
    mapMode, contentMode, saveAll, validateWorkspace
};

class EditorApp final {
public:
    EditorApp(platform::ImageDecoder& decoder, const std::filesystem::path& assetRoot,
              game::GameContentRegistry content,
              std::optional<ContentWorkspaceDocument> contentWorkspace = std::nullopt);
    ~EditorApp();

    void resize(int width, int height);
    void updateAndRender(const EditorInputState& input);
    void cancelActiveGesture() noexcept;
    void shellCommand(EditorShellCommand command);
    [[nodiscard]] bool open(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool importMap(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool save(std::string& error);
    [[nodiscard]] bool saveAs(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool autosave(std::string& error);
    [[nodiscard]] bool saveAll(std::string& error);
    [[nodiscard]] bool validateWorkspace();
    [[nodiscard]] bool hasUnsavedChanges() const noexcept {
        return worldProject_.dirty() || (contentWorkspace_ && contentWorkspace_->dirty());
    }
    [[nodiscard]] bool playtestActive() const noexcept { return playtest_.active(); }
    [[nodiscard]] bool contentMode() const noexcept { return contentMode_; }
    [[nodiscard]] ContentWorkspaceDocument* contentWorkspace() noexcept { return contentWorkspace_ ? &*contentWorkspace_ : nullptr; }
    [[nodiscard]] const ContentWorkspaceDocument* contentWorkspace() const noexcept { return contentWorkspace_ ? &*contentWorkspace_ : nullptr; }
    [[nodiscard]] const std::vector<game::presentation::VisualContentDiagnostic>&
        visualDiagnostics() const noexcept { return visualDiagnostics_; }
    [[nodiscard]] bool visualValidationAttempted() const noexcept {
        return visualValidationAttempted_;
    }

    [[nodiscard]] const render::Framebuffer& framebuffer() const noexcept { return *framebuffer_; }
    [[nodiscard]] EditorDocument& document() noexcept { return worldProject_.activeDocument(); }
    [[nodiscard]] const EditorDocument& document() const noexcept { return worldProject_.activeDocument(); }
    [[nodiscard]] WorldProjectDocument& worldProject() noexcept { return worldProject_; }
    [[nodiscard]] const WorldProjectDocument& worldProject() const noexcept { return worldProject_; }
    void setPanelWidths(EditorPanelWidths widths) noexcept;
    [[nodiscard]] EditorPanelWidths panelWidths() const noexcept { return panelWidths_; }
    [[nodiscard]] const std::optional<std::filesystem::path>& filePath() const noexcept {
        return worldProject_.filePath();
    }
    [[nodiscard]] std::string windowTitle() const;
    [[nodiscard]] const std::string& status() const noexcept { return status_; }
    void setLanguage(EditorLanguage language) noexcept { localization_.setLanguage(language); }
    [[nodiscard]] EditorLanguage language() const noexcept { return localization_.language(); }
    [[nodiscard]] const EditorLocalization& localization() const noexcept { return localization_; }

private:
    enum class MapPaletteTab { maps, tiles, semantics, stamps, entities, rules, encounters };
    struct MapTileSelection final {
        TileCoordinate origin{};
        std::uint32_t width{};
        std::uint32_t height{};
        [[nodiscard]] bool valid() const noexcept { return width != 0 && height != 0; }
    };
    struct DragState final {
        enum class Kind { none, pan, brush, rectangle, tileSelection, move, regionCreate, regionResize } kind{Kind::none};
        core::PointI pointerStart{};
        core::WorldPointI worldStart{};
        core::WorldPointI worldCurrent{};
        core::WorldPointI entityStart{};
        world::AabbI regionStart{};
        std::vector<TileCoordinate> stroke;
    };

    void drawShell(EditorUiContext& ui, const EditorInputState& input);
    void drawContentShell(EditorUiContext& ui, const EditorInputState& input,
                          core::RectI left, core::RectI center, core::RectI right,
                          core::RectI status);
    void drawViewport(render::Renderer2D& renderer, core::RectI viewport,
                      const EditorInputState& input);
    void handleViewport(core::RectI viewport, const EditorInputState& input);
    void drawMap(render::Renderer2D& renderer, core::RectI viewport) const;
    void drawEntities(render::Renderer2D& renderer, core::RectI viewport) const;
    void drawInspector(EditorUiContext& ui, core::RectI panel);
    void drawNewMapDialog(EditorUiContext& ui, const EditorInputState& input);
    void frameMap(core::RectI viewport) noexcept;
    [[nodiscard]] core::WorldPointI screenToWorld(core::PointI screen,
                                                  core::RectI viewport) const noexcept;
    [[nodiscard]] core::PointI worldToScreen(core::WorldPointI world,
                                             core::RectI viewport) const noexcept;
    [[nodiscard]] double zoom() const noexcept;
    [[nodiscard]] std::optional<EditorSelection> hitTest(core::WorldPointI world) const;
    [[nodiscard]] maps::MapTileReference selectedTileReference() const;
    [[nodiscard]] const game::LoadedTilesetVisual* selectedTilesetVisual() const noexcept;
    void selectTileset(int direction);
    void placeSelected(core::WorldPointI world);
    void execute(std::unique_ptr<EditorCommand> command);
    void updateStatus(core::RectI viewport, const EditorInputState& input);
    void togglePlaytest();
    void refreshContentRegistry();
    void resetContentEditState() noexcept;
    [[nodiscard]] bool runVisualValidation();
    void drawContentPreview(EditorUiContext& ui, const EditorInputState& input,
                            core::RectI canvas);
    void drawGameplayContentInspector(EditorUiContext& ui, const EditorInputState& input,
                                      core::RectI panel, const ContentDefinitionKey& key,
                                      int inspectorY);
    void resetContentPreviewState() noexcept;
    void handleContentPreview(EditorUiContext& ui, const EditorInputState& input,
                              core::RectI canvas);
    void drawMapBrowser(EditorUiContext& ui, const EditorInputState& input, core::RectI panel);
    void drawMapLinkInspector(EditorUiContext& ui, core::RectI panel);
    void handlePanelSplitters(const EditorInputState& input);
    void centerOnWorldPoint(core::WorldPointI point) noexcept;

    game::GameContentRegistry content_;
    WorldProjectDocument worldProject_;
    std::optional<ContentWorkspaceDocument> contentWorkspace_;
    platform::ImageDecoder* decoder_{};
    std::filesystem::path assetRoot_;
    EditorVisualPreview visualPreview_;
    EditorPlaytestSession playtest_;
    mutable EditorValidationCache validationCache_;
    assets::AssetManager assets_;
    game::RuntimeTilesetCatalog runtimeTilesets_{content_.tilesets()};
    game::TilesetVisualCatalog tilesetVisuals_;
    std::unique_ptr<render::BitmapFont> font_;
    std::unique_ptr<render::Framebuffer> framebuffer_;
    core::RectI viewportBounds_{};
    EditorPanelWidths panelWidths_{};
    enum class Splitter { none, left, right } activeSplitter_{Splitter::none};
    DragState drag_;
    simulation::DefinitionId selectedDefinition_{game::gameplay::creatures::soldierEnemyId()};
    game::AuthoringCategory selectedCategory_{game::AuthoringCategory::enemy};
    simulation::DefinitionId selectedTileset_{"tileset.dungeon"};
    std::uint32_t selectedTile_{};
    TileBrushSelection tileBrush_{};
    MapPaletteTab mapPaletteTab_{MapPaletteTab::tiles};
    bool paletteDragging_{};
    std::uint32_t paletteDragStart_{};
    std::uint32_t paletteDragCurrent_{};
    std::string layerNameEdit_;
    bool layerNameFocused_{};
    int tilePaletteScroll_{};
    int mapBrowserScroll_{};
    int layerScroll_{};
    bool tileFlipX_{};
    std::size_t semanticFamilyIndex_{};
    std::size_t selectedStamp_{};
    std::size_t selectedRuleIndex_{};
    std::size_t selectedEncounterIndex_{};
    std::optional<MapTileSelection> mapTileSelection_;
    std::optional<game::content::AuthoredStamp> pendingStampFromSelection_;
    bool rawPalette_{};
    bool showCollision_{true};
    bool newMapDialog_{};
    bool newMapIncludePlayerSpawn_{true};
    int newMapField_{};
    std::string newMapId_{"map.untitled"};
    std::string newMapWidth_{"32"};
    std::string newMapHeight_{"24"};
    std::string newMapTileSize_{"16"};
    bool contentMode_{};
    ContentDefinitionKind selectedContentCategory_{ContentDefinitionKind::visualImage};
    std::optional<ContentDefinitionKey> selectedContentDefinition_;
    std::optional<ContentDefinitionKey> findUsageKey_;
    std::size_t findUsageIndex_{};
    std::uint64_t findUsageMapRevision_{static_cast<std::uint64_t>(-1)};
    std::uint64_t playtestTick_{};
    std::optional<ContentDefinitionKey> contentEditKey_;
    std::array<std::string, 160> contentEditValues_{};
    std::size_t selectedAnimationFrameIndex_{};
    std::size_t selectedAnimationMarkerIndex_{};
    std::size_t contentEditFrame_{static_cast<std::size_t>(-1)};
    std::size_t contentEditMarker_{static_cast<std::size_t>(-1)};
    std::size_t contentDialogueConditionIndex_{static_cast<std::size_t>(-1)};
    std::size_t contentDialogueActionIndex_{static_cast<std::size_t>(-1)};
    int contentFocusedField_{-1};
    bool contentStaticSourceEnabled_{};
    int contentCategoryScroll_{};
    int contentDefinitionScroll_{};
    int contentFrameScroll_{};
    int contentMarkerScroll_{};
    int contentInspectorScroll_{};
    int contentDiagnosticScroll_{};
    int assetDiagnosticScroll_{};
    std::string newContentDefinitionId_;
    std::string newContentDefinitionFile_;
    PreviewClipState previewClipState_{PreviewClipState::idle};
    simulation::DefinitionId previewActionId_{};
    PreviewClipState contentBindingState_{PreviewClipState::idle};
    simulation::DefinitionId contentBindingAction_{};
    game::gameplay::FacingDirection previewFacing_{game::gameplay::FacingDirection::down};
    VisualPreviewViewport previewViewport_{};
    bool previewPanning_{};
    core::PointI previewPanPointerStart_{};
    core::PointI previewPanStart_{};
    bool previewRectangleDragging_{};
    core::PointI previewRectangleStart_{};
    core::PointI previewRectangleCurrent_{};
    std::optional<core::RectI> previewSelectionRect_;
    std::vector<game::presentation::VisualContentDiagnostic> visualDiagnostics_;
    std::uint64_t visualValidationRevision_{static_cast<std::uint64_t>(-1)};
    bool visualValidationAttempted_{};
    std::string status_;
    EditorLocalization localization_;
};

} // namespace underworld::editor
