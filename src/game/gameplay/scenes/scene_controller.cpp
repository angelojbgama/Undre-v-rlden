#include "game/gameplay/scenes/scene_controller.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace underworld::game::gameplay::scenes {

const SceneActorBinding* SceneController::actor(std::string_view slot) const noexcept {
    const auto found = std::find_if(scene_.actors.begin(), scene_.actors.end(),
        [&](const SceneActorBinding& value) { return value.slotId == slot; });
    return found == scene_.actors.end() ? nullptr : &*found;
}

core::WorldPointI SceneController::positionAt(const SceneActorBinding& binding,
                                              std::uint32_t tick) const {
    const auto initial = initialActors_.find(binding.slotId);
    if (initial == initialActors_.end()) return {};
    core::WorldPointI position = initial->second.position;

    // Authoring collection order must not affect simulation.  Validation
    // guarantees that an actor has exactly one track and that its MOVE clips
    // do not overlap, so chronological evaluation gives each clip a single,
    // unambiguous derived start position.
    std::vector<const SceneClip*> moves;
    for (const auto& track : scene_.tracks) {
        if (track.kind != SceneTrackKind::actor || track.actorSlot != binding.slotId) continue;
        for (const auto& clip : track.clips) {
            if (clip.kind == SceneClipKind::move) moves.push_back(&clip);
        }
    }
    std::sort(moves.begin(), moves.end(), [](const SceneClip* left, const SceneClip* right) {
        return left->startTick < right->startTick;
    });
    for (const SceneClip* clip : moves) {
        if (clip->startTick > tick) break;
        const auto end = clip->startTick + clip->durationTicks;
        const auto start = position;
        if (tick >= end || clip->durationTicks == 0) {
            position = clip->targetPosition;
            continue;
        }
        const auto elapsed = static_cast<std::int64_t>(tick - clip->startTick);
        const auto duration = static_cast<std::int64_t>(clip->durationTicks);
        position.x = start.x + static_cast<int>(
            (static_cast<std::int64_t>(clip->targetPosition.x - start.x) * elapsed) /
            duration);
        position.y = start.y + static_cast<int>(
            (static_cast<std::int64_t>(clip->targetPosition.y - start.y) * elapsed) /
            duration);
    }
    return position;
}

void SceneController::applyActorState(simulation::EventBuffer& events) {
    for (const auto& binding : scene_.actors) {
        const auto initial = initialActors_.find(binding.slotId);
        if (initial == initialActors_.end()) continue;
        if (hooks_.relocateActor && !hooks_.relocateActor(binding, positionAt(binding, tick_))) {
            lastError_ = "scene actor disappeared during movement";
            abort(events, lastError_);
            return;
        }
        auto facing = initial->second.facing;
        std::uint32_t latestTick = 0;
        bool hasFace = false;
        for (const auto& track : scene_.tracks) {
            if (track.kind != SceneTrackKind::actor || track.actorSlot != binding.slotId) continue;
            for (const auto& clip : track.clips) {
                if (clip.kind == SceneClipKind::face && clip.startTick <= tick_ &&
                    (!hasFace || clip.startTick >= latestTick)) {
                    latestTick = clip.startTick;
                    facing = clip.facing;
                    hasFace = true;
                }
            }
        }
        if (hooks_.setFacing && !hooks_.setFacing(binding, facing)) {
            lastError_ = "scene actor disappeared while changing facing";
            abort(events, lastError_);
            return;
        }
    }
}

void SceneController::fireEvents(simulation::EventBuffer& events) {
    for (std::size_t trackIndex = 0; trackIndex < scene_.tracks.size(); ++trackIndex) {
        const auto& track = scene_.tracks[trackIndex];
        for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const ClipKey key{trackIndex, clipIndex};
            const auto fired = fired_.find(key);
            if (fired != fired_.end() && fired->second) continue;
            const auto& clip = track.clips[clipIndex];
            if (clip.startTick > tick_) continue;
            fired_[key] = true;
            if (clip.kind == SceneClipKind::dialogue) {
                if (!hooks_.beginDialogue) {
                    lastError_ = "scene dialogue hook is unavailable";
                    abort(events, lastError_);
                    return;
                }
                std::string error;
                if (!hooks_.beginDialogue(clip.dialogueId, error)) {
                    lastError_ = error.empty() ? "scene dialogue could not be opened" : error;
                    abort(events, lastError_);
                    return;
                }
                sceneDialogueOpen_ = true;
                if (clip.waitForCompletion) waitingForDialogue_ = true;
            } else if (clip.kind == SceneClipKind::presentationEffect) {
                if (hooks_.requestEffect) hooks_.requestEffect(clip.effectId, events);
            } else if (clip.kind == SceneClipKind::worldEvent) {
                if (!hooks_.executeWorldAction ||
                    !hooks_.executeWorldAction(clip.worldAction, events)) {
                    lastError_ = "scene world event could not be executed";
                    abort(events, lastError_);
                    return;
                }
            }
        }
    }
}

void SceneController::updatePresentation() {
    presentation_.clear();
    for (const auto& binding : scene_.actors) {
        SceneActorPresentation state{binding.kind, binding.slotId, binding.instanceId, 0, std::nullopt};
        for (const auto& track : scene_.tracks) {
            if (track.kind != SceneTrackKind::actor || track.actorSlot != binding.slotId) continue;
            for (const auto& clip : track.clips) {
                const auto end = clip.startTick + clip.durationTicks;
                if (clip.startTick <= tick_ && tick_ < end) {
                    if (clip.kind == SceneClipKind::emote) state.emote = clip.emote;
                    if (clip.kind == SceneClipKind::hop && clip.durationTicks != 0) {
                        const auto elapsed = static_cast<std::int64_t>(tick_ - clip.startTick);
                        const auto duration = static_cast<std::int64_t>(clip.durationTicks);
                        const auto height = static_cast<std::int64_t>(std::max(0, clip.heightPixels));
                        const auto numerator = elapsed * (duration - elapsed) * height * 4;
                        const auto denominator = duration * duration;
                        state.offsetY = denominator == 0 ? 0 : -static_cast<int>(numerator / denominator);
                    }
                }
            }
        }
        if (state.offsetY != 0 || state.emote) presentation_.actors.push_back(std::move(state));
    }
}

bool SceneController::start(const simulation::MapId& mapId, const SceneDefinition& scene,
                           SceneRuntimeHooks hooks, simulation::EventBuffer& events,
                           std::string& error) {
    lastError_.clear();
    if (active_) {
        error = "another scene is already active";
        lastError_ = error;
        return false;
    }
    if (scene.id.empty() || scene.durationTicks == 0) {
        error = "scene id and positive duration are required";
        lastError_ = error;
        return false;
    }
    if (!hooks.snapshotActor) {
        error = "scene actor resolver is unavailable";
        lastError_ = error;
        return false;
    }
    scene_ = scene;
    mapId_ = mapId;
    hooks_ = std::move(hooks);
    events_ = &events;
    initialActors_.clear();
    fired_.clear();
    for (const auto& binding : scene_.actors) {
        const auto snapshot = hooks_.snapshotActor(binding);
        if (!snapshot) {
            error = "scene actor instance is unavailable: " + binding.slotId;
            lastError_ = error;
            events.emit(simulation::SceneAborted{mapId_, scene_.id, error});
            scene_ = {};
            hooks_ = {};
            return false;
        }
        initialActors_.emplace(binding.slotId, *snapshot);
    }
    tick_ = 0;
    active_ = true;
    finished_ = false;
    waitingForDialogue_ = false;
    sceneDialogueOpen_ = false;
    lastError_.clear();
    events.emit(simulation::SceneStarted{mapId_, scene_.id});
    applyActorState(events);
    if (active_) fireEvents(events);
    if (active_) updatePresentation();
    error.clear();
    return active_;
}

void SceneController::advance(simulation::EventBuffer& events) {
    if (!active_) return;
    if (sceneDialogueOpen_ && hooks_.dialogueOpen && !hooks_.dialogueOpen()) {
        sceneDialogueOpen_ = false;
    }
    if (waitingForDialogue_) {
        if (hooks_.dialogueOpen && hooks_.dialogueOpen()) return;
        waitingForDialogue_ = false;
        sceneDialogueOpen_ = false;
    }
    if (tick_ < scene_.durationTicks) ++tick_;
    applyActorState(events);
    if (!active_) return;
    fireEvents(events);
    if (!active_) return;
    updatePresentation();
    if (!waitingForDialogue_ && tick_ >= scene_.durationTicks) finish(events);
}

void SceneController::finish(simulation::EventBuffer& events) {
    if (!active_) return;
    active_ = false;
    finished_ = true;
    if (sceneDialogueOpen_ && hooks_.closeDialogue) hooks_.closeDialogue();
    waitingForDialogue_ = false;
    sceneDialogueOpen_ = false;
    presentation_.clear();
    events.emit(simulation::SceneCompleted{mapId_, scene_.id});
}

void SceneController::abort(simulation::EventBuffer& events, std::string reason) {
    if (!active_) return;
    if (!reason.empty()) lastError_ = std::move(reason);
    active_ = false;
    finished_ = false;
    if (sceneDialogueOpen_ && hooks_.closeDialogue) hooks_.closeDialogue();
    waitingForDialogue_ = false;
    sceneDialogueOpen_ = false;
    presentation_.clear();
    events.emit(simulation::SceneAborted{mapId_, scene_.id, lastError_});
}

} // namespace underworld::game::gameplay::scenes
