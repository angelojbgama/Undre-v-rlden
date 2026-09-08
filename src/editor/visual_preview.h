#pragma once

#include "editor/content_workspace_document.h"
#include "engine/platform/image_decoder.h"
#include "engine/render/animation.h"
#include "game/presentation/visual_content_loader.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace underworld::editor {

enum class PreviewClipState {
    idle,
    move,
    hurt,
    death,
    dead,
    action,
    opened,
    destroying,
    destroyed,
    activationInactive,
    activationActive,
    doorLocked,
    doorClosed,
    doorOpen
};

struct VisualPreviewRequest final {
    ContentDefinitionKey key{};
    std::size_t frameIndex{};
    PreviewClipState state{PreviewClipState::idle};
    simulation::DefinitionId actionId{};
    game::gameplay::FacingDirection facing{game::gameplay::FacingDirection::down};

    [[nodiscard]] bool operator==(const VisualPreviewRequest&) const noexcept = default;
};

struct VisualPreviewViewport final {
    // -1 is FIT; non-negative values are fixed nearest-neighbor zoom steps.
    int zoomStep{2};
    core::PointI pan{};
    bool gridEnabled{};
    core::PointI gridOrigin{};
    core::PointI gridCell{16, 16};
};

struct VisualPreviewTransform final {
    core::RectI canvas{};
    core::PointI imageOrigin{};
    double scale{1.0};
};

[[nodiscard]] double visualPreviewScale(const VisualPreviewViewport& viewport,
                                        core::RectI canvas,
                                        core::PointI imageSize) noexcept;
[[nodiscard]] VisualPreviewTransform makeVisualPreviewTransform(
    const VisualPreviewViewport& viewport, core::RectI canvas,
    core::PointI imageSize) noexcept;
[[nodiscard]] core::PointI previewImageToScreen(const VisualPreviewTransform& transform,
                                                core::PointI imagePoint) noexcept;
[[nodiscard]] core::PointI previewScreenToImage(const VisualPreviewTransform& transform,
                                                core::PointI screenPoint) noexcept;
[[nodiscard]] core::RectI previewImageRectToScreen(const VisualPreviewTransform& transform,
                                                   core::RectI imageRect) noexcept;
[[nodiscard]] std::optional<core::RectI> previewDragRectangle(
    const VisualPreviewTransform& transform, core::PointI startScreen,
    core::PointI currentScreen, core::PointI imageSize, bool snapToGrid,
    core::PointI gridOrigin, core::PointI gridCell) noexcept;
[[nodiscard]] std::vector<core::RectI> previewGridSelections(
    core::RectI imageRect, core::PointI origin, core::PointI cell) noexcept;

class EditorVisualPreview final {
public:
    EditorVisualPreview(platform::ImageDecoder& decoder,
                        std::filesystem::path gameAssetsRoot) noexcept
        : decoder_(decoder), gameAssetsRoot_(std::move(gameAssetsRoot)) {}

    void invalidate() noexcept;
    void prepare(const ContentWorkspaceDocument& document,
                 const VisualPreviewRequest& request);

    [[nodiscard]] const render::Image* image() const noexcept { return image_.get(); }
    [[nodiscard]] const render::AnimationClip* clip() const noexcept {
        return clip_.get();
    }
    [[nodiscard]] const render::Animator& animator() const noexcept { return animator_; }
    [[nodiscard]] bool hasClip() const noexcept { return clip_ != nullptr; }
    [[nodiscard]] bool flipX() const noexcept { return flipX_; }
    [[nodiscard]] bool playing() const noexcept { return animator_.isPlaying(); }
    [[nodiscard]] const std::vector<game::presentation::VisualContentDiagnostic>&
        diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] const std::vector<render::AnimationMarkerEvent>& markerEvents() const noexcept {
        return markerEvents_;
    }

    void advanceTicks(std::uint64_t ticks);
    void setPlaying(bool playing) noexcept { animator_.setPlaying(playing); }
    void restart() noexcept;
    void stepFrame(int direction) noexcept;

private:
    [[nodiscard]] std::shared_ptr<const render::Image> loadImage(
        const ContentWorkspaceDocument& document,
        const game::content::AuthoredVisualImage& definition);
    [[nodiscard]] std::shared_ptr<const render::AnimationClip> loadAnimation(
        const ContentWorkspaceDocument& document,
        const simulation::DefinitionId& id);
    void prepareAnimation(const ContentWorkspaceDocument& document,
                          const simulation::DefinitionId& id);
    void appendMissingAnimation(const simulation::DefinitionId& owner,
                                const simulation::DefinitionId& animationId);

    platform::ImageDecoder& decoder_;
    std::filesystem::path gameAssetsRoot_;
    std::uint64_t revision_{static_cast<std::uint64_t>(-1)};
    std::optional<VisualPreviewRequest> request_;
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::Image>,
                       simulation::DefinitionIdHash> images_;
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::SpriteSheet>,
                       simulation::DefinitionIdHash> sheets_;
    std::unordered_map<simulation::DefinitionId, std::shared_ptr<const render::AnimationClip>,
                       simulation::DefinitionIdHash> clips_;
    std::shared_ptr<const render::Image> image_;
    std::shared_ptr<const render::AnimationClip> clip_;
    render::Animator animator_;
    bool flipX_{};
    std::vector<game::presentation::VisualContentDiagnostic> diagnostics_;
    std::vector<render::AnimationMarkerEvent> markerEvents_;
};

} // namespace underworld::editor
