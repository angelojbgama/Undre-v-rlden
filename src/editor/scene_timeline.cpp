#include "editor/scene_timeline.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace underworld::editor {
namespace {

using game::gameplay::scenes::SceneClip;
using game::gameplay::scenes::SceneClipKind;
using game::gameplay::scenes::SceneDefinition;
using game::gameplay::scenes::SceneTrackKind;

bool validClipLocation(const SceneDefinition& scene, SceneClipLocation location) noexcept {
    return location.track < scene.tracks.size() &&
           location.clip < scene.tracks[location.track].clips.size();
}

bool uniqueMarkerName(const SceneDefinition& scene, std::string_view name,
                      std::optional<std::size_t> except = std::nullopt) noexcept {
    for (std::size_t index = 0; index < scene.markers.size(); ++index) {
        if (except && index == *except) continue;
        if (scene.markers[index].name == name) return false;
    }
    return true;
}

std::uint32_t clampStart(const SceneDefinition& scene, std::uint32_t tick) noexcept {
    return std::min(tick, scene.durationTicks);
}

core::WorldPointI positionAt(const SceneDefinition& scene, std::string_view slot,
                             core::WorldPointI initial, std::uint32_t tick) {
    std::vector<const SceneClip*> moves;
    for (const auto& track : scene.tracks) {
        if (track.kind != SceneTrackKind::actor || track.actorSlot != slot) continue;
        for (const auto& clip : track.clips) {
            if (clip.kind == SceneClipKind::move) moves.push_back(&clip);
        }
    }
    std::stable_sort(moves.begin(), moves.end(), [](const SceneClip* left, const SceneClip* right) {
        return left->startTick < right->startTick;
    });
    auto position = initial;
    for (const auto* clip : moves) {
        if (clip->startTick > tick) break;
        const auto end = sceneClipEndTick(*clip);
        const auto start = position;
        if (clip->durationTicks == 0 || tick >= end) {
            position = clip->targetPosition;
            continue;
        }
        const auto elapsed = static_cast<std::int64_t>(tick - clip->startTick);
        const auto duration = static_cast<std::int64_t>(clip->durationTicks);
        position.x = start.x + static_cast<int>(
            (static_cast<std::int64_t>(clip->targetPosition.x - start.x) * elapsed) / duration);
        position.y = start.y + static_cast<int>(
            (static_cast<std::int64_t>(clip->targetPosition.y - start.y) * elapsed) / duration);
    }
    return position;
}

} // namespace

std::uint32_t snapSceneTick(std::uint32_t tick, SceneTimelineSnap snap) noexcept {
    if (!snap.enabled || snap.intervalTicks <= 1) return tick;
    const auto remainder = tick % snap.intervalTicks;
    if (remainder >= (snap.intervalTicks + 1U) / 2U &&
        tick <= std::numeric_limits<std::uint32_t>::max() - (snap.intervalTicks - remainder)) {
        return tick + (snap.intervalTicks - remainder);
    }
    return tick - remainder;
}

bool sceneClipHasDuration(SceneClipKind kind) noexcept {
    return kind == SceneClipKind::move || kind == SceneClipKind::emote ||
           kind == SceneClipKind::hop;
}

std::uint32_t sceneClipEndTick(const SceneClip& clip) noexcept {
    if (clip.durationTicks > std::numeric_limits<std::uint32_t>::max() - clip.startTick) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return clip.startTick + clip.durationTicks;
}

bool addSceneClip(SceneDefinition& scene, std::size_t track, SceneClip clip,
                  SceneTimelineSnap snap, std::string& error) {
    if (track >= scene.tracks.size()) { error = "scene track does not exist"; return false; }
    clip.startTick = clampStart(scene, snapSceneTick(clip.startTick, snap));
    if (sceneClipHasDuration(clip.kind)) {
        clip.durationTicks = snapSceneTick(clip.durationTicks, snap);
        if (clip.durationTicks == 0) {
            clip.durationTicks = snap.enabled ? std::max(1U, snap.intervalTicks) : 1U;
        }
        clip.durationTicks = std::min(clip.durationTicks, scene.durationTicks - clip.startTick);
        if (clip.durationTicks == 0) { error = "duration clip does not fit in scene"; return false; }
    } else {
        clip.durationTicks = 0;
    }
    scene.tracks[track].clips.push_back(std::move(clip));
    error.clear();
    return true;
}

bool duplicateSceneClip(SceneDefinition& scene, SceneClipLocation source, std::size_t targetTrack,
                        std::uint32_t targetTick, SceneTimelineSnap snap, std::string& error) {
    if (!validClipLocation(scene, source)) { error = "scene clip does not exist"; return false; }
    auto copy = scene.tracks[source.track].clips[source.clip];
    copy.startTick = targetTick;
    return addSceneClip(scene, targetTrack, std::move(copy), snap, error);
}

bool removeSceneClip(SceneDefinition& scene, SceneClipLocation location, std::string& error) {
    if (!validClipLocation(scene, location)) { error = "scene clip does not exist"; return false; }
    auto& clips = scene.tracks[location.track].clips;
    clips.erase(clips.begin() + static_cast<std::ptrdiff_t>(location.clip));
    error.clear();
    return true;
}

bool moveSceneClip(SceneDefinition& scene, SceneClipLocation location, std::uint32_t startTick,
                   SceneTimelineSnap snap, std::string& error) {
    if (!validClipLocation(scene, location)) { error = "scene clip does not exist"; return false; }
    auto& clip = scene.tracks[location.track].clips[location.clip];
    const auto newStart = clampStart(scene, snapSceneTick(startTick, snap));
    auto newDuration = clip.durationTicks;
    if (sceneClipHasDuration(clip.kind) && newDuration > scene.durationTicks - newStart) {
        newDuration = scene.durationTicks - newStart;
    }
    if (sceneClipHasDuration(clip.kind) && newDuration == 0) {
        error = "duration clip does not fit in scene";
        return false;
    }
    clip.startTick = newStart;
    clip.durationTicks = newDuration;
    error.clear();
    return true;
}

bool resizeSceneClip(SceneDefinition& scene, SceneClipLocation location, std::uint32_t durationTicks,
                     SceneTimelineSnap snap, std::string& error) {
    if (!validClipLocation(scene, location)) { error = "scene clip does not exist"; return false; }
    auto& clip = scene.tracks[location.track].clips[location.clip];
    if (!sceneClipHasDuration(clip.kind)) { error = "instant scene event cannot be resized"; return false; }
    const auto newDuration = std::min(snapSceneTick(durationTicks, snap),
                                      scene.durationTicks - clip.startTick);
    if (newDuration == 0) { error = "scene clip duration must be positive"; return false; }
    clip.durationTicks = newDuration;
    error.clear();
    return true;
}

bool setSceneMoveTarget(SceneDefinition& scene, SceneClipLocation location,
                        core::WorldPointI target, std::string& error) {
    if (!validClipLocation(scene, location)) { error = "scene clip does not exist"; return false; }
    auto& clip = scene.tracks[location.track].clips[location.clip];
    if (clip.kind != SceneClipKind::move) { error = "only MOVE clips have a map target"; return false; }
    clip.targetPosition = target;
    error.clear();
    return true;
}

bool addSceneMarker(SceneDefinition& scene, game::gameplay::scenes::SceneMarker marker,
                    SceneTimelineSnap snap, std::string& error) {
    marker.tick = clampStart(scene, snapSceneTick(marker.tick, snap));
    if (marker.name.empty()) { error = "scene marker name is empty"; return false; }
    if (!uniqueMarkerName(scene, marker.name)) { error = "scene marker name is duplicated"; return false; }
    scene.markers.push_back(std::move(marker));
    error.clear();
    return true;
}

bool renameSceneMarker(SceneDefinition& scene, std::size_t marker, std::string name,
                       std::string& error) {
    if (marker >= scene.markers.size()) { error = "scene marker does not exist"; return false; }
    if (name.empty()) { error = "scene marker name is empty"; return false; }
    if (!uniqueMarkerName(scene, name, marker)) { error = "scene marker name is duplicated"; return false; }
    scene.markers[marker].name = std::move(name);
    error.clear();
    return true;
}

bool removeSceneMarker(SceneDefinition& scene, std::size_t marker, std::string& error) {
    if (marker >= scene.markers.size()) { error = "scene marker does not exist"; return false; }
    scene.markers.erase(scene.markers.begin() + static_cast<std::ptrdiff_t>(marker));
    error.clear();
    return true;
}

std::optional<std::uint32_t> jumpToSceneMarker(const SceneDefinition& scene,
                                               std::string_view name) noexcept {
    const auto found = std::find_if(scene.markers.begin(), scene.markers.end(),
        [&](const auto& marker) { return marker.name == name; });
    if (found == scene.markers.end()) return std::nullopt;
    return found->tick;
}

std::uint32_t fitSceneDurationToContent(SceneDefinition& scene) noexcept {
    std::uint32_t end{};
    for (const auto& track : scene.tracks) {
        for (const auto& clip : track.clips) end = std::max(end, sceneClipEndTick(clip));
    }
    for (const auto& marker : scene.markers) end = std::max(end, marker.tick);
    scene.durationTicks = std::max(1U, end);
    return scene.durationTicks;
}

ScenePreviewState evaluateScenePreview(const SceneDefinition& scene,
                                       const std::vector<ScenePreviewActorInitial>& initialActors,
                                       std::uint32_t tick) {
    ScenePreviewState state;
    state.tick = std::min(tick, scene.durationTicks);
    for (const auto& binding : scene.actors) {
        const auto initial = std::find_if(initialActors.begin(), initialActors.end(),
            [&](const ScenePreviewActorInitial& value) { return value.slotId == binding.slotId; });
        if (initial == initialActors.end()) continue;
        ScenePreviewActorState actor{binding.slotId,
                                     positionAt(scene, binding.slotId, initial->position, state.tick),
                                     initial->facing, 0, std::nullopt};
        std::uint32_t latestFace{};
        bool hasFace = false;
        for (const auto& track : scene.tracks) {
            if (track.kind != SceneTrackKind::actor || track.actorSlot != binding.slotId) continue;
            for (const auto& clip : track.clips) {
                if (clip.kind == SceneClipKind::face && clip.startTick <= state.tick &&
                    (!hasFace || clip.startTick >= latestFace)) {
                    actor.facing = clip.facing;
                    latestFace = clip.startTick;
                    hasFace = true;
                }
                if (clip.startTick > state.tick || state.tick >= sceneClipEndTick(clip)) continue;
                if (clip.kind == SceneClipKind::emote) actor.emote = clip.emote;
                if (clip.kind == SceneClipKind::hop && clip.durationTicks != 0) {
                    const auto elapsed = static_cast<std::int64_t>(state.tick - clip.startTick);
                    const auto duration = static_cast<std::int64_t>(clip.durationTicks);
                    const auto height = static_cast<std::int64_t>(std::max(0, clip.heightPixels));
                    actor.visualOffsetY = -static_cast<int>(
                        (elapsed * (duration - elapsed) * height * 4) / (duration * duration));
                }
            }
        }
        state.actors.push_back(std::move(actor));
    }
    return state;
}

game::gameplay::scenes::SceneValidationResult validateSceneTimeline(
    const SceneDefinition& scene, const SceneValidationContext& context) {
    return game::gameplay::scenes::validateScene(scene, context.mapWidthPixels,
        context.mapHeightPixels, context.npcInstances, context.enemyInstances);
}

int sceneTimelineXForTick(const SceneTimelineGeometry& geometry, std::uint32_t tick) noexcept {
    const auto relative = static_cast<std::int64_t>(tick) -
                          static_cast<std::int64_t>(geometry.visibleStartTick);
    return geometry.originX + static_cast<int>(static_cast<float>(relative) *
                                                std::max(geometry.pixelsPerTick, 0.0F));
}

std::uint32_t sceneTimelineTickForX(const SceneTimelineGeometry& geometry, int x,
                                    SceneTimelineSnap snap) noexcept {
    const auto scale = std::max(geometry.pixelsPerTick, 0.0001F);
    const auto offset = std::max(0, x - geometry.originX);
    const auto ticks = static_cast<std::uint64_t>(geometry.visibleStartTick) +
        static_cast<std::uint64_t>(static_cast<float>(offset) / scale + 0.5F);
    return snapSceneTick(static_cast<std::uint32_t>(std::min<std::uint64_t>(
        ticks, std::numeric_limits<std::uint32_t>::max())), snap);
}

std::vector<SceneTimelineRulerMark> sceneTimelineRulerMarks(const SceneTimelineGeometry& geometry,
                                                            std::uint32_t minorTicks,
                                                            std::uint32_t majorTicks) {
    std::vector<SceneTimelineRulerMark> result;
    if (geometry.width <= 0 || geometry.pixelsPerTick <= 0.0F) return result;
    minorTicks = std::max(1U, minorTicks);
    majorTicks = std::max(minorTicks, majorTicks);
    const auto visibleCount = static_cast<std::uint32_t>(
        static_cast<float>(geometry.width) / geometry.pixelsPerTick) + 1U;
    const auto first = ((geometry.visibleStartTick + minorTicks - 1U) / minorTicks) * minorTicks;
    for (std::uint64_t tick = first;
         tick <= static_cast<std::uint64_t>(geometry.visibleStartTick) + visibleCount;
         tick += minorTicks) {
        const auto value = static_cast<std::uint32_t>(tick);
        const auto x = sceneTimelineXForTick(geometry, value);
        if (x < geometry.originX || x > geometry.originX + geometry.width) continue;
        result.push_back({value, x, value % majorTicks == 0});
    }
    return result;
}

SceneTimelineClipBounds sceneTimelineClipBounds(const SceneTimelineGeometry& geometry,
                                                const SceneClip& clip) noexcept {
    const auto x = sceneTimelineXForTick(geometry, clip.startTick);
    const auto width = sceneClipHasDuration(clip.kind)
        ? std::max(1, static_cast<int>(static_cast<float>(clip.durationTicks) * geometry.pixelsPerTick))
        : 1;
    return {x, width};
}

} // namespace underworld::editor
