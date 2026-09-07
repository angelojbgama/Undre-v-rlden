#include "editor/visual_preview.h"

#include "engine/render/image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace underworld::editor {
namespace {

constexpr double explicitZooms[] = {1.0, 2.0, 4.0, 8.0};

int clampCoordinate(int value, int maximum) noexcept {
    return std::clamp(value, 0, std::max(0, maximum));
}

void addDiagnostic(std::vector<game::presentation::VisualContentDiagnostic>& diagnostics,
                   game::presentation::VisualContentDiagnosticStage stage,
                   std::string code, simulation::DefinitionId id, std::string message) {
    diagnostics.push_back({stage, std::move(code), std::move(id), std::nullopt, {},
                           std::move(message)});
}

bool sourceFitsImage(core::RectI source, const render::Image& image) noexcept {
    const auto right = static_cast<std::int64_t>(source.x) + source.width;
    const auto bottom = static_cast<std::int64_t>(source.y) + source.height;
    return !source.empty() && source.x >= 0 && source.y >= 0 &&
           right <= image.width() && bottom <= image.height();
}

const game::presentation::DirectionalAnimationRef* enemyBinding(
    const game::content::AuthoredEnemyVisual& visual, PreviewClipState state,
    const simulation::DefinitionId& actionId) {
    switch (state) {
    case PreviewClipState::idle: return &visual.idle;
    case PreviewClipState::move: return visual.move ? &*visual.move : nullptr;
    case PreviewClipState::hurt: return visual.hurt ? &*visual.hurt : nullptr;
    case PreviewClipState::death: return visual.death ? &*visual.death : nullptr;
    case PreviewClipState::dead: return visual.dead ? &*visual.dead : nullptr;
    case PreviewClipState::action:
        for (const auto& action : visual.attacks) {
            if (action.visualActionId == actionId) return &action.clips;
        }
        return nullptr;
    default: return nullptr;
    }
}

std::optional<simulation::DefinitionId> objectAnimation(
    const game::content::AuthoredWorldObjectVisual& visual, PreviewClipState state) {
    switch (state) {
    case PreviewClipState::idle: return visual.idleAnimationId;
    case PreviewClipState::opened: return visual.openedAnimationId;
    case PreviewClipState::destroying: return visual.destroyingAnimationId;
    case PreviewClipState::activationInactive: return visual.activationInactiveAnimationId;
    case PreviewClipState::activationActive: return visual.activationActiveAnimationId;
    case PreviewClipState::doorLocked: return visual.doorLockedAnimationId;
    case PreviewClipState::doorClosed: return visual.doorClosedAnimationId;
    case PreviewClipState::doorOpen: return visual.doorOpenAnimationId;
    default: return std::nullopt;
    }
}

} // namespace

double visualPreviewScale(const VisualPreviewViewport& viewport, core::RectI canvas,
                          core::PointI imageSize) noexcept {
    if (imageSize.x <= 0 || imageSize.y <= 0) return 1.0;
    if (viewport.zoomStep < 0) {
        const double widthScale = static_cast<double>(std::max(1, canvas.width - 12)) /
                                  static_cast<double>(imageSize.x);
        const double heightScale = static_cast<double>(std::max(1, canvas.height - 12)) /
                                   static_cast<double>(imageSize.y);
        return std::max(0.01, std::min(widthScale, heightScale));
    }
    const auto index = std::clamp(viewport.zoomStep, 0,
                                  static_cast<int>(std::size(explicitZooms) - 1));
    return explicitZooms[index];
}

VisualPreviewTransform makeVisualPreviewTransform(const VisualPreviewViewport& viewport,
                                                  core::RectI canvas,
                                                  core::PointI imageSize) noexcept {
    const double scale = visualPreviewScale(viewport, canvas, imageSize);
    const int width = std::max(1, static_cast<int>(std::lround(imageSize.x * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(imageSize.y * scale)));
    return {canvas,
            {canvas.x + (canvas.width - width) / 2 + viewport.pan.x,
             canvas.y + (canvas.height - height) / 2 + viewport.pan.y},
            scale};
}

core::PointI previewImageToScreen(const VisualPreviewTransform& transform,
                                  core::PointI imagePoint) noexcept {
    return {transform.imageOrigin.x + static_cast<int>(std::lround(imagePoint.x * transform.scale)),
            transform.imageOrigin.y + static_cast<int>(std::lround(imagePoint.y * transform.scale))};
}

core::PointI previewScreenToImage(const VisualPreviewTransform& transform,
                                  core::PointI screenPoint) noexcept {
    return {static_cast<int>(std::floor((screenPoint.x - transform.imageOrigin.x) /
                                        transform.scale)),
            static_cast<int>(std::floor((screenPoint.y - transform.imageOrigin.y) /
                                        transform.scale))};
}

core::RectI previewImageRectToScreen(const VisualPreviewTransform& transform,
                                     core::RectI imageRect) noexcept {
    const auto topLeft = previewImageToScreen(transform, {imageRect.x, imageRect.y});
    const auto bottomRight = previewImageToScreen(transform,
                                                  {imageRect.x + imageRect.width,
                                                   imageRect.y + imageRect.height});
    return {topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y};
}

std::optional<core::RectI> previewDragRectangle(const VisualPreviewTransform& transform,
                                                core::PointI startScreen,
                                                core::PointI currentScreen,
                                                core::PointI imageSize, bool snapToGrid,
                                                core::PointI gridOrigin,
                                                core::PointI gridCell) noexcept {
    auto start = previewScreenToImage(transform, startScreen);
    auto current = previewScreenToImage(transform, currentScreen);
    start.x = clampCoordinate(start.x, imageSize.x);
    start.y = clampCoordinate(start.y, imageSize.y);
    current.x = clampCoordinate(current.x, imageSize.x);
    current.y = clampCoordinate(current.y, imageSize.y);
    int left = std::min(start.x, current.x);
    int top = std::min(start.y, current.y);
    int right = std::max(start.x, current.x);
    int bottom = std::max(start.y, current.y);
    if (snapToGrid && gridCell.x > 0 && gridCell.y > 0) {
        const auto floorGrid = [](int value, int origin, int cell) {
            return origin + static_cast<int>(std::floor(static_cast<double>(value - origin) / cell)) * cell;
        };
        const auto ceilGrid = [](int value, int origin, int cell) {
            return origin + static_cast<int>(std::ceil(static_cast<double>(value - origin) / cell)) * cell;
        };
        left = floorGrid(left, gridOrigin.x, gridCell.x);
        top = floorGrid(top, gridOrigin.y, gridCell.y);
        right = ceilGrid(right, gridOrigin.x, gridCell.x);
        bottom = ceilGrid(bottom, gridOrigin.y, gridCell.y);
    }
    left = clampCoordinate(left, imageSize.x);
    top = clampCoordinate(top, imageSize.y);
    right = clampCoordinate(right, imageSize.x);
    bottom = clampCoordinate(bottom, imageSize.y);
    const core::RectI result{left, top, right - left, bottom - top};
    return result.empty() ? std::nullopt : std::optional<core::RectI>{result};
}

std::vector<core::RectI> previewGridSelections(core::RectI imageRect, core::PointI origin,
                                               core::PointI cell) noexcept {
    std::vector<core::RectI> result;
    if (imageRect.empty() || cell.x <= 0 || cell.y <= 0) return result;
    const int firstX = origin.x + static_cast<int>(std::floor(
        static_cast<double>(imageRect.x - origin.x) / cell.x)) * cell.x;
    const int firstY = origin.y + static_cast<int>(std::floor(
        static_cast<double>(imageRect.y - origin.y) / cell.y)) * cell.y;
    for (int y = firstY; y < imageRect.y + imageRect.height; y += cell.y) {
        for (int x = firstX; x < imageRect.x + imageRect.width; x += cell.x) {
            const core::RectI candidate{x, y, cell.x, cell.y};
            const int right = std::min(candidate.x + candidate.width, imageRect.x + imageRect.width);
            const int bottom = std::min(candidate.y + candidate.height, imageRect.y + imageRect.height);
            const int left = std::max(candidate.x, imageRect.x);
            const int top = std::max(candidate.y, imageRect.y);
            if (right > left && bottom > top) result.push_back({left, top, right - left, bottom - top});
        }
    }
    return result;
}

void EditorVisualPreview::invalidate() noexcept {
    revision_ = static_cast<std::uint64_t>(-1);
    request_.reset();
    images_.clear();
    sheets_.clear();
    clips_.clear();
    image_.reset();
    clip_.reset();
    animator_ = render::Animator{};
    diagnostics_.clear();
    markerEvents_.clear();
}

std::shared_ptr<const render::Image> EditorVisualPreview::loadImage(
    const ContentWorkspaceDocument& document,
    const game::content::AuthoredVisualImage& definition) {
    const auto found = images_.find(definition.id);
    if (found != images_.end()) return found->second;
    const game::presentation::VisualAssetRoots roots{
        gameAssetsRoot_, document.writable()
            ? std::optional<std::filesystem::path>{document.root()} : std::nullopt};
    const game::presentation::VisualImageDefinition runtimeDefinition{
        definition.id, definition.root, definition.relativePath};
    const auto decoded = game::presentation::decodeVisualImage(runtimeDefinition, decoder_, roots);
    diagnostics_.insert(diagnostics_.end(), decoded.diagnostics.begin(), decoded.diagnostics.end());
    if (!decoded.image) return {};
    images_.emplace(definition.id, decoded.image);
    sheets_.emplace(definition.id, std::make_shared<const render::SpriteSheet>(decoded.image));
    return decoded.image;
}

std::shared_ptr<const render::AnimationClip> EditorVisualPreview::loadAnimation(
    const ContentWorkspaceDocument& document, const simulation::DefinitionId& id) {
    const auto cached = clips_.find(id);
    if (cached != clips_.end()) return cached->second;
    const auto* authored = document.animation(id);
    if (!authored) {
        addDiagnostic(diagnostics_, game::presentation::VisualContentDiagnosticStage::compile,
                      "unknown_animation", id, "preview animation definition was not found");
        return {};
    }
    const auto* imageDefinition = document.visualImage(authored->imageId);
    if (!imageDefinition) {
        addDiagnostic(diagnostics_, game::presentation::VisualContentDiagnosticStage::compile,
                      "unknown_image", id, "preview animation image definition was not found");
        return {};
    }
    const auto image = loadImage(document, *imageDefinition);
    if (!image) return {};
    const auto sheet = sheets_.find(imageDefinition->id);
    if (sheet == sheets_.end()) return {};
    // The builder consumes runtime definitions. Keep the conversion local to the
    // preview cache so the authored document remains free of runtime objects.
    game::presentation::AnimationDefinition runtime{authored->id, authored->imageId, {}, authored->loop};
    for (const auto& frame : authored->frames) {
        runtime.frames.push_back({frame.source, frame.anchor, frame.drawOffset,
                                  frame.durationTicks, frame.markers});
    }
    auto clip = game::presentation::buildVisualAnimationClip(runtime, sheet->second, *image,
                                                             diagnostics_);
    if (!clip) return {};
    clips_.emplace(id, clip);
    return clip;
}

void EditorVisualPreview::prepareAnimation(const ContentWorkspaceDocument& document,
                                           const simulation::DefinitionId& id) {
    const auto* authored = document.animation(id);
    if (!authored) {
        appendMissingAnimation(request_ ? request_->key.id : id, id);
        return;
    }
    const auto* imageDefinition = document.visualImage(authored->imageId);
    if (imageDefinition) image_ = loadImage(document, *imageDefinition);
    clip_ = loadAnimation(document, id);
    if (clip_) animator_.play(clip_, true);
}

void EditorVisualPreview::appendMissingAnimation(const simulation::DefinitionId& owner,
                                                 const simulation::DefinitionId& animationId) {
    addDiagnostic(diagnostics_, game::presentation::VisualContentDiagnosticStage::compile,
                  "unknown_animation", owner,
                  "preview references unavailable animation " + std::string(animationId.value()));
}

void EditorVisualPreview::prepare(const ContentWorkspaceDocument& document,
                                  const VisualPreviewRequest& request) {
    if (revision_ != document.revision()) {
        revision_ = document.revision();
        images_.clear();
        sheets_.clear();
        clips_.clear();
        request_.reset();
    }
    if (request_ && *request_ == request) return;
    request_ = request;
    diagnostics_.clear();
    markerEvents_.clear();
    image_.reset();
    clip_.reset();
    animator_ = render::Animator{};
    flipX_ = request.facing == game::gameplay::FacingDirection::right;

    const auto* imageDefinition = [&]() -> const game::content::AuthoredVisualImage* {
        const game::content::AuthoredVisualImage* result = nullptr;
        switch (request.key.kind) {
        case ContentDefinitionKind::visualImage: result = document.visualImage(request.key.id); break;
        case ContentDefinitionKind::staticSprite: {
            const auto* value = document.staticSprite(request.key.id);
            result = value ? document.visualImage(value->imageId) : nullptr;
            break;
        }
        case ContentDefinitionKind::animation: {
            const auto* value = document.animation(request.key.id);
            result = value ? document.visualImage(value->imageId) : nullptr;
            break;
        }
        default: break;
        }
        return result;
    }();
    if (imageDefinition) image_ = loadImage(document, *imageDefinition);

    switch (request.key.kind) {
    case ContentDefinitionKind::visualImage:
    case ContentDefinitionKind::staticSprite: break;
    case ContentDefinitionKind::animation:
        prepareAnimation(document, request.key.id);
        break;
    case ContentDefinitionKind::enemyVisual: {
        const auto* value = document.enemyVisual(request.key.id);
        const auto* binding = value ? enemyBinding(*value, request.state, request.actionId) : nullptr;
        if (binding) {
            const auto animationId = game::presentation::resolveDirectionalAnimationId(*binding,
                                                                                       request.facing);
            if (animationId) prepareAnimation(document, *animationId);
        }
        break;
    }
    case ContentDefinitionKind::objectVisual: {
        const auto* value = document.objectVisual(request.key.id);
        if (value) {
            const auto animationId = objectAnimation(*value, request.state);
            if (animationId) prepareAnimation(document, *animationId);
        }
        break;
    }
    case ContentDefinitionKind::npcVisual: {
        const auto* value = document.npcVisual(request.key.id);
        if (value && value->idle) {
            const auto animationId = game::presentation::resolveDirectionalAnimationId(*value->idle,
                                                                                       request.facing);
            if (animationId) prepareAnimation(document, *animationId);
        }
        break;
    }
    default: break;
    }

    if (request.key.kind == ContentDefinitionKind::staticSprite && image_) {
        const auto* sprite = document.staticSprite(request.key.id);
        if (sprite && sprite->source && !sourceFitsImage(*sprite->source, *image_)) {
            addDiagnostic(diagnostics_, game::presentation::VisualContentDiagnosticStage::validate,
                          "source_out_of_bounds", request.key.id,
                          "static sprite source rectangle is outside the decoded image");
        }
    }
}

void EditorVisualPreview::advanceTicks(std::uint64_t ticks) {
    markerEvents_.clear();
    animator_.updateTicks(ticks, markerEvents_);
}

void EditorVisualPreview::restart() noexcept {
    if (clip_) animator_.play(clip_, true);
    markerEvents_.clear();
}

void EditorVisualPreview::stepFrame(int direction) noexcept {
    markerEvents_.clear();
    animator_.stepFrame(direction);
}

} // namespace underworld::editor
