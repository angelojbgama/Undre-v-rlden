#include "game/gameplay/scenes/scene_definition.h"

#include <algorithm>
#include <unordered_set>

namespace underworld::game::gameplay::scenes {
namespace {

SceneValidationResult fail(std::string error, std::string path) {
    return {false, std::move(error), std::move(path)};
}

bool contains(const std::vector<simulation::PersistentInstanceId>& values,
              simulation::PersistentInstanceId value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool validActorSlot(const SceneDefinition& scene, const std::string& slot) {
    return std::any_of(scene.actors.begin(), scene.actors.end(),
                       [&](const SceneActorBinding& actor) { return actor.slotId == slot; });
}

} // namespace

SceneValidationResult validateScene(
    const SceneDefinition& scene, std::uint32_t mapWidthPixels,
    std::uint32_t mapHeightPixels,
    const std::vector<simulation::PersistentInstanceId>& npcInstances,
    const std::vector<simulation::PersistentInstanceId>& enemyInstances) {
    if (scene.id.empty()) return fail("scene id is empty", "id");
    if (scene.durationTicks == 0) return fail("scene duration must be positive", "durationTicks");
    std::unordered_set<std::string> aliases;
    std::unordered_set<std::string> actorTrackSlots;
    for (std::size_t index = 0; index < scene.actors.size(); ++index) {
        const auto& actor = scene.actors[index];
        const auto path = "actors[" + std::to_string(index) + "]";
        if (actor.slotId.empty() || !aliases.emplace(actor.slotId).second) {
            return fail("scene actor slot is empty or duplicated", path + ".slotId");
        }
        if (actor.kind == SceneActorKind::player) {
            if (actor.instanceId) return fail("player actor cannot have an instance id", path);
        } else if (!actor.instanceId) {
            return fail("NPC/enemy actor requires a persistent instance id", path);
        } else if (actor.kind == SceneActorKind::npc && !contains(npcInstances, actor.instanceId)) {
            return fail("scene references an unknown NPC instance", path + ".instanceId");
        } else if (actor.kind == SceneActorKind::enemy && !contains(enemyInstances, actor.instanceId)) {
            return fail("scene references an unknown enemy instance", path + ".instanceId");
        }
    }
    for (std::size_t trackIndex = 0; trackIndex < scene.tracks.size(); ++trackIndex) {
        const auto& track = scene.tracks[trackIndex];
        const auto trackPath = "tracks[" + std::to_string(trackIndex) + "]";
        if (track.kind == SceneTrackKind::actor) {
            if (!validActorSlot(scene, track.actorSlot)) {
                return fail("actor track references an unknown actor slot", trackPath + ".actorSlot");
            }
            if (!actorTrackSlots.emplace(track.actorSlot).second) {
                return fail("an actor may only have one actor track", trackPath + ".actorSlot");
            }
        }
        for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const auto& clip = track.clips[clipIndex];
            const auto path = trackPath + ".clips[" + std::to_string(clipIndex) + "]";
            if (clip.startTick > scene.durationTicks) {
                return fail("clip starts after scene duration", path + ".startTick");
            }
            if (clip.durationTicks > scene.durationTicks - clip.startTick) {
                return fail("clip ends after scene duration", path + ".durationTicks");
            }
            const bool actorClip = clip.kind == SceneClipKind::move ||
                                   clip.kind == SceneClipKind::face ||
                                   clip.kind == SceneClipKind::emote ||
                                   clip.kind == SceneClipKind::hop;
            const bool belongsToTrack =
                (track.kind == SceneTrackKind::actor && actorClip) ||
                (track.kind == SceneTrackKind::dialogue &&
                 clip.kind == SceneClipKind::dialogue) ||
                (track.kind == SceneTrackKind::world &&
                 clip.kind == SceneClipKind::worldEvent) ||
                (track.kind == SceneTrackKind::presentation &&
                 clip.kind == SceneClipKind::presentationEffect);
            if (!belongsToTrack) {
                return fail("clip kind does not belong to its track", path + ".kind");
            }
            if ((actorClip || track.kind == SceneTrackKind::actor) &&
                !validActorSlot(scene, clip.actorSlot)) {
                return fail("clip references an unknown actor slot", path + ".actorSlot");
            }
            if (track.kind == SceneTrackKind::actor && actorClip &&
                clip.actorSlot != track.actorSlot) {
                return fail("actor clip does not match its actor track", path + ".actorSlot");
            }
            if ((clip.kind == SceneClipKind::move || clip.kind == SceneClipKind::emote ||
                 clip.kind == SceneClipKind::hop) && clip.durationTicks == 0) {
                return fail("visual clip duration must be positive", path + ".durationTicks");
            }
            if (clip.kind == SceneClipKind::dialogue && clip.dialogueId.empty()) {
                return fail("dialogue clip requires a dialogue id", path + ".dialogueId");
            }
            if (clip.kind == SceneClipKind::presentationEffect && clip.effectId.empty()) {
                return fail("presentation clip requires an effect id", path + ".effectId");
            }
            if (clip.kind == SceneClipKind::worldEvent &&
                clip.worldAction.kind == maps::WorldActionKind::startScene) {
                return fail("nested scenes are not supported", path + ".worldAction.kind");
            }
            if (clip.kind == SceneClipKind::move &&
                (clip.targetPosition.x < 0 || clip.targetPosition.y < 0 ||
                 clip.targetPosition.x >= static_cast<int>(mapWidthPixels) ||
                 clip.targetPosition.y >= static_cast<int>(mapHeightPixels))) {
                return fail("move target is outside the map", path + ".targetPosition");
            }
        }
        if (track.kind == SceneTrackKind::actor) {
            for (std::size_t a = 0; a < track.clips.size(); ++a) {
                if (track.clips[a].kind != SceneClipKind::move) continue;
                const auto aEnd = track.clips[a].startTick + track.clips[a].durationTicks;
                for (std::size_t b = a + 1; b < track.clips.size(); ++b) {
                    if (track.clips[b].kind != SceneClipKind::move) continue;
                    const auto bEnd = track.clips[b].startTick + track.clips[b].durationTicks;
                    if (track.clips[a].actorSlot == track.clips[b].actorSlot &&
                        track.clips[a].startTick < bEnd && track.clips[b].startTick < aEnd) {
                        return fail("movement clips overlap on the same actor track",
                                    trackPath + ".clips");
                    }
                }
            }
        }
    }
    std::unordered_set<std::string> markerNames;
    for (std::size_t index = 0; index < scene.markers.size(); ++index) {
        const auto& marker = scene.markers[index];
        if (marker.name.empty() || !markerNames.emplace(marker.name).second ||
            marker.tick > scene.durationTicks) {
            return fail("scene marker is invalid", "markers[" + std::to_string(index) + "]");
        }
    }
    return {true, {}, {}};
}

} // namespace underworld::game::gameplay::scenes
