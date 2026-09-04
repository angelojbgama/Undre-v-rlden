#pragma once

#include "editor/editor_commands.h"
#include "editor/editor_ui.h"
#include "engine/assets/asset_manager.h"
#include "engine/render/framebuffer.h"

#include <memory>
#include <optional>
#include <string>

namespace underworld::platform { class ImageDecoder; }
namespace underworld::render { class BitmapFont; class Image; }

namespace underworld::editor {

enum class EditorShellCommand { newMap, undo, redo, toggleGrid, frameMap };

class EditorApp final {
public:
    EditorApp(platform::ImageDecoder& decoder, const std::filesystem::path& assetRoot);
    ~EditorApp();

    void resize(int width, int height);
    void updateAndRender(const EditorInputState& input);
    void cancelActiveGesture() noexcept;
    void shellCommand(EditorShellCommand command);
    [[nodiscard]] bool open(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] bool save(std::string& error);
    [[nodiscard]] bool saveAs(const std::filesystem::path& path, std::string& error);

    [[nodiscard]] const render::Framebuffer& framebuffer() const noexcept { return *framebuffer_; }
    [[nodiscard]] EditorDocument& document() noexcept { return document_; }
    [[nodiscard]] const EditorDocument& document() const noexcept { return document_; }
    [[nodiscard]] std::string windowTitle() const;
    [[nodiscard]] const std::string& status() const noexcept { return status_; }

private:
    struct DragState final {
        enum class Kind { none, pan, brush, rectangle, move, regionCreate, regionResize } kind{Kind::none};
        core::PointI pointerStart{};
        core::WorldPointI worldStart{};
        core::WorldPointI worldCurrent{};
        core::WorldPointI entityStart{};
        world::AabbI regionStart{};
        std::vector<TileCoordinate> stroke;
    };

    void drawShell(EditorUiContext& ui, const EditorInputState& input);
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
    void placeSelected(core::WorldPointI world);
    void execute(std::unique_ptr<EditorCommand> command);
    void updateStatus(core::RectI viewport, const EditorInputState& input);

    game::GameContentRegistry content_;
    EditorDocument document_;
    assets::AssetManager assets_;
    std::shared_ptr<const render::Image> tileset_;
    std::unique_ptr<render::BitmapFont> font_;
    std::unique_ptr<render::Framebuffer> framebuffer_;
    core::RectI viewportBounds_{};
    DragState drag_;
    simulation::DefinitionId selectedDefinition_{game::gameplay::creatures::soldierEnemyId()};
    game::AuthoringCategory selectedCategory_{game::AuthoringCategory::enemy};
    std::uint32_t selectedTile_{};
    bool tileFlipX_{};
    bool showCollision_{true};
    bool newMapDialog_{};
    int newMapField_{};
    std::string newMapId_{"map.untitled"};
    std::string newMapWidth_{"32"};
    std::string newMapHeight_{"24"};
    std::string newMapTileSize_{"16"};
    std::string status_;
};

} // namespace underworld::editor
