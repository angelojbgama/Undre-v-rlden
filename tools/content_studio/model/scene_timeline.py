from __future__ import annotations

import copy
from typing import Iterable

from .types import Diagnostic, JsonValue


SCENE_ACTOR_KINDS = ("player", "npc", "enemy")
SCENE_TRACK_KINDS = ("actor", "dialogue", "world", "presentation")
SCENE_CLIP_KINDS = ("move", "face", "emote", "hop", "dialogue", "presentationEffect", "worldEvent")
SCENE_EMOTES = ("surprise", "question", "ellipsis")


def snap_tick(tick: int, interval: int = 5) -> int:
    interval = max(1, int(interval))
    return max(0, round(max(0, int(tick)) / interval) * interval)


def clip_has_duration(kind: str) -> bool:
    return kind in {"move", "emote", "hop"}


def clip_end(clip: dict[str, JsonValue]) -> int:
    return _safe_int(clip.get("startTick", 0)) + _safe_int(clip.get("durationTicks", 0))


def new_scene(scene_id: str, duration_ticks: int = 180) -> dict[str, JsonValue]:
    return {
        "id": scene_id,
        "durationTicks": max(1, int(duration_ticks)),
        "actors": [{"slotId": "player", "kind": "player"}],
        "tracks": [
            {"kind": "actor", "actorSlot": "player", "clips": []},
            {"kind": "dialogue", "clips": []},
            {"kind": "world", "clips": []},
            {"kind": "presentation", "clips": []},
        ],
        "markers": [],
    }


def add_actor(scene: dict[str, JsonValue], slot_id: str, kind: str = "npc",
              instance_id: int | None = None) -> None:
    if not slot_id.strip() or kind not in SCENE_ACTOR_KINDS:
        raise ValueError("scene actor needs a slot ID and a valid kind")
    actors = _array(scene, "actors")
    if any(isinstance(actor, dict) and actor.get("slotId") == slot_id for actor in actors):
        raise ValueError(f"scene actor slot already exists: {slot_id}")
    value: dict[str, JsonValue] = {"slotId": slot_id.strip(), "kind": kind}
    if instance_id is not None:
        value["instanceId"] = int(instance_id)
    actors.append(value)


def remove_actor(scene: dict[str, JsonValue], index: int) -> None:
    actors = _array(scene, "actors")
    if index < 0 or index >= len(actors):
        raise IndexError("scene actor index out of range")
    slot = actors[index].get("slotId") if isinstance(actors[index], dict) else None
    actors.pop(index)
    if isinstance(slot, str):
        for track in _array(scene, "tracks"):
            if isinstance(track, dict) and track.get("actorSlot") == slot:
                track["actorSlot"] = ""


def add_track(scene: dict[str, JsonValue], kind: str = "actor", actor_slot: str = "") -> int:
    if kind not in SCENE_TRACK_KINDS:
        raise ValueError(f"unknown scene track kind: {kind}")
    tracks = _array(scene, "tracks")
    if kind == "actor":
        if not actor_slot or not any(isinstance(actor, dict) and actor.get("slotId") == actor_slot
                                     for actor in _array(scene, "actors")):
            raise ValueError("actor track needs an existing actor slot")
        if any(isinstance(track, dict) and track.get("kind") == "actor" and track.get("actorSlot") == actor_slot
               for track in tracks):
            raise ValueError(f"actor already has a track: {actor_slot}")
    tracks.append({"kind": kind, "actorSlot": actor_slot, "clips": []})
    return len(tracks) - 1


def add_clip(scene: dict[str, JsonValue], track_index: int, clip: dict[str, JsonValue],
             snap: int = 5) -> int:
    tracks = _array(scene, "tracks")
    if track_index < 0 or track_index >= len(tracks) or not isinstance(tracks[track_index], dict):
        raise IndexError("scene track index out of range")
    if not isinstance(clip.get("kind"), str) or clip["kind"] not in SCENE_CLIP_KINDS:
        raise ValueError("unknown scene clip kind")
    value = copy.deepcopy(clip)
    value["startTick"] = snap_tick(int(value.get("startTick", 0)), snap)
    value["durationTicks"] = snap_tick(int(value.get("durationTicks", 0)), snap) if clip_has_duration(str(value["kind"])) else 0
    value.setdefault("actorSlot", "")
    if tracks[track_index].get("kind") == "actor" and str(value["kind"]) in {"move", "face", "emote", "hop"}:
        value["actorSlot"] = str(tracks[track_index].get("actorSlot", ""))
    values = tracks[track_index].setdefault("clips", [])
    if not isinstance(values, list):
        raise ValueError("scene track clips must be an array")
    values.append(value)
    values.sort(key=lambda item: int(item.get("startTick", 0)) if isinstance(item, dict) else 0)
    return next(index for index, item in enumerate(values) if item is value)


def duplicate_clip(scene: dict[str, JsonValue], track_index: int, clip_index: int,
                   target_track: int | None = None, target_tick: int | None = None,
                   snap: int = 5) -> int:
    tracks = _array(scene, "tracks")
    source = _clip(tracks, track_index, clip_index)
    value = copy.deepcopy(source)
    value["startTick"] = int(target_tick if target_tick is not None else clip_end(source))
    return add_clip(scene, track_index if target_track is None else target_track, value, snap)


def remove_clip(scene: dict[str, JsonValue], track_index: int, clip_index: int) -> None:
    tracks = _array(scene, "tracks")
    clips = _clips(tracks, track_index)
    if clip_index < 0 or clip_index >= len(clips):
        raise IndexError("scene clip index out of range")
    clips.pop(clip_index)


def move_clip(scene: dict[str, JsonValue], track_index: int, clip_index: int,
              start_tick: int, snap: int = 5) -> None:
    tracks = _array(scene, "tracks")
    clip = _clip(tracks, track_index, clip_index)
    clip["startTick"] = min(snap_tick(start_tick, snap), max(0, int(scene.get("durationTicks", 1))))
    _clips(tracks, track_index).sort(key=lambda item: int(item.get("startTick", 0)))


def resize_clip(scene: dict[str, JsonValue], track_index: int, clip_index: int,
                duration_ticks: int, snap: int = 5) -> None:
    tracks = _array(scene, "tracks")
    clip = _clip(tracks, track_index, clip_index)
    if not clip_has_duration(str(clip.get("kind", ""))):
        clip["durationTicks"] = 0
        return
    start = int(clip.get("startTick", 0))
    maximum = max(0, int(scene.get("durationTicks", 1)) - start)
    clip["durationTicks"] = min(snap_tick(duration_ticks, snap), maximum)


def add_marker(scene: dict[str, JsonValue], name: str, tick: int, snap: int = 5) -> int:
    if not name.strip():
        raise ValueError("scene marker name cannot be empty")
    markers = _array(scene, "markers")
    if any(isinstance(marker, dict) and marker.get("name") == name.strip() for marker in markers):
        raise ValueError(f"scene marker already exists: {name}")
    markers.append({"name": name.strip(), "tick": min(snap_tick(tick, snap), int(scene.get("durationTicks", 1)))})
    markers.sort(key=lambda item: int(item.get("tick", 0)) if isinstance(item, dict) else 0)
    return next(index for index, marker in enumerate(markers) if marker.get("name") == name.strip())


def rename_marker(scene: dict[str, JsonValue], index: int, name: str) -> None:
    markers = _array(scene, "markers")
    if index < 0 or index >= len(markers) or not isinstance(markers[index], dict) or not name.strip():
        raise ValueError("invalid scene marker")
    if any(i != index and isinstance(marker, dict) and marker.get("name") == name.strip() for i, marker in enumerate(markers)):
        raise ValueError(f"scene marker already exists: {name}")
    markers[index]["name"] = name.strip()


def remove_marker(scene: dict[str, JsonValue], index: int) -> None:
    markers = _array(scene, "markers")
    if index < 0 or index >= len(markers):
        raise IndexError("scene marker index out of range")
    markers.pop(index)


def fit_duration(scene: dict[str, JsonValue]) -> int:
    end = max((clip_end(clip) for track in _read_array(scene, "tracks")
               if isinstance(track, dict) for clip in _read_array(track, "clips") if isinstance(clip, dict)), default=0)
    marker_end = max((_safe_int(marker.get("tick", 0)) for marker in _read_array(scene, "markers")
                      if isinstance(marker, dict)), default=0)
    scene["durationTicks"] = max(1, end, marker_end)
    return int(scene["durationTicks"])


def evaluate_preview(scene: dict[str, JsonValue], tick: int) -> dict[str, JsonValue]:
    """Rebuild the small authoring preview state without changing authored data."""
    current_tick = max(0, min(_safe_int(tick), _safe_int(scene.get("durationTicks", 1), 1)))
    result: dict[str, JsonValue] = {"tick": current_tick, "actors": []}
    actors = result["actors"]
    assert isinstance(actors, list)
    for actor in _read_array(scene, "actors"):
        if not isinstance(actor, dict):
            continue
        state: dict[str, JsonValue] = {
            "slotId": str(actor.get("slotId", "")),
            "kind": str(actor.get("kind", "player")),
            "position": {"x": 0, "y": 0},
            "facing": "down",
            "emote": None,
        }
        slot = str(actor.get("slotId", ""))
        for track in _read_array(scene, "tracks"):
            if not isinstance(track, dict) or track.get("actorSlot") != slot or track.get("kind") != "actor":
                continue
            for clip in _read_array(track, "clips"):
                if not isinstance(clip, dict) or _safe_int(clip.get("startTick", 0)) > current_tick:
                    continue
                kind = str(clip.get("kind", ""))
                if kind == "move" and isinstance(clip.get("targetPosition"), dict):
                    state["position"] = copy.deepcopy(clip["targetPosition"])
                elif kind == "face":
                    state["facing"] = str(clip.get("facing", "down"))
                elif kind == "emote" and current_tick <= clip_end(clip):
                    state["emote"] = str(clip.get("emote", "surprise"))

        actors.append(state)
    return result


def validate_scene(scene: dict[str, JsonValue], map_width: int = 0,
                   map_height: int = 0, npc_instances: set[int] | None = None,
                   enemy_instances: set[int] | None = None) -> list[Diagnostic]:
    """Mirror the C++ scene contract without mutating the authored document."""
    issues: list[Diagnostic] = []
    scene_id = str(scene.get("id", ""))
    duration = scene.get("durationTicks")
    npc_instances = npc_instances or set()
    enemy_instances = enemy_instances or set()
    if not scene_id:
        issues.append(Diagnostic("error", "scene ID is empty", "id", "invalid_scene_id", scene_id))
    if not isinstance(duration, int) or isinstance(duration, bool) or duration <= 0:
        issues.append(Diagnostic("error", "scene duration must be positive", "durationTicks", "invalid_duration", scene_id))

    actors = _read_array(scene, "actors")
    tracks = _read_array(scene, "tracks")
    markers = _read_array(scene, "markers")
    slots: set[str] = set()
    for index, actor in enumerate(actors):
        if not isinstance(actor, dict) or not isinstance(actor.get("slotId"), str) or not actor.get("slotId"):
            issues.append(Diagnostic("error", "scene actor needs a slotId", f"actors[{index}]", "invalid_actor", scene_id))
            continue
        slot = str(actor["slotId"])
        if slot in slots:
            issues.append(Diagnostic("error", f"duplicate actor slot: {slot}", f"actors[{index}]", "duplicate_actor_slot", scene_id))
        slots.add(slot)
        kind = actor.get("kind")
        if kind not in SCENE_ACTOR_KINDS:
            issues.append(Diagnostic("error", "unknown scene actor kind", f"actors[{index}].kind", "invalid_actor_kind", scene_id))
        elif kind in {"npc", "enemy"}:
            instance_id = actor.get("instanceId")
            valid_ids = npc_instances if kind == "npc" else enemy_instances
            if not isinstance(instance_id, int) or isinstance(instance_id, bool) or instance_id <= 0:
                issues.append(Diagnostic("error", "NPC/enemy scene actor requires a persistent instance id", f"actors[{index}].instanceId", "missing_actor_instance", scene_id))
            elif instance_id not in valid_ids:
                issues.append(Diagnostic("error", "scene references an unknown entity instance", f"actors[{index}].instanceId", "missing_actor_instance", scene_id))
        elif "instanceId" in actor:
            issues.append(Diagnostic("error", "player scene actor cannot have an instance id", f"actors[{index}].instanceId", "invalid_actor_instance", scene_id))

    actor_track_slots: set[str] = set()
    for track_index, track in enumerate(tracks):
        if not isinstance(track, dict) or track.get("kind") not in SCENE_TRACK_KINDS:
            issues.append(Diagnostic("error", "unknown scene track kind", f"tracks[{track_index}].kind", "invalid_track_kind", scene_id))
            continue
        track_kind = str(track["kind"])
        actor_slot = track.get("actorSlot", "")
        if track_kind == "actor":
            if not isinstance(actor_slot, str) or not actor_slot or actor_slot not in slots:
                issues.append(Diagnostic("error", f"unknown actor slot: {actor_slot}", f"tracks[{track_index}].actorSlot", "missing_actor_slot", scene_id))
            elif actor_slot in actor_track_slots:
                issues.append(Diagnostic("error", f"an actor may only have one actor track: {actor_slot}", f"tracks[{track_index}].actorSlot", "duplicate_actor_track", scene_id))
            actor_track_slots.add(str(actor_slot))
        clips = _read_array(track, "clips")
        for clip_index, clip in enumerate(clips):
            path = f"tracks[{track_index}].clips[{clip_index}]"
            if not isinstance(clip, dict) or clip.get("kind") not in SCENE_CLIP_KINDS:
                issues.append(Diagnostic("error", "unknown scene clip kind", f"{path}.kind", "invalid_clip_kind", scene_id))
                continue
            kind = str(clip["kind"])
            start = clip.get("startTick", 0)
            end = clip_end(clip)
            if not isinstance(start, int) or isinstance(start, bool) or start < 0 or (isinstance(duration, int) and end > duration):
                issues.append(Diagnostic("error", "scene clip lies outside its duration", path, "clip_out_of_range", scene_id))
            actor_clip = kind in {"move", "face", "emote", "hop"}
            expected_track = {"dialogue": "dialogue", "worldEvent": "world", "presentationEffect": "presentation"}.get(kind)
            if track_kind == "actor" and not actor_clip:
                issues.append(Diagnostic("error", "clip kind does not belong to actor track", f"{path}.kind", "invalid_track_clip", scene_id))
            elif expected_track and track_kind != expected_track:
                issues.append(Diagnostic("error", "clip kind does not belong to this track", f"{path}.kind", "invalid_track_clip", scene_id))
            if actor_clip:
                clip_slot = clip.get("actorSlot")
                if not isinstance(clip_slot, str) or not clip_slot or clip_slot not in slots:
                    issues.append(Diagnostic("error", "clip references an unknown actor slot", f"{path}.actorSlot", "missing_actor_slot", scene_id))
                elif track_kind == "actor" and clip_slot != actor_slot:
                    issues.append(Diagnostic("error", "actor clip does not match its actor track", f"{path}.actorSlot", "mismatched_actor_slot", scene_id))
            clip_duration = _safe_int(clip.get("durationTicks", 0), -1)
            if kind == "face" and clip_duration != 0:
                issues.append(Diagnostic("error", "face clips must be instantaneous", f"{path}.durationTicks", "invalid_clip_duration", scene_id))
            if kind in {"move", "emote", "hop"} and clip_duration <= 0:
                issues.append(Diagnostic("error", "visual clip duration must be positive", f"{path}.durationTicks", "invalid_clip_duration", scene_id))
            if kind == "dialogue" and not clip.get("dialogueId"):
                issues.append(Diagnostic("error", "dialogue clip requires a dialogue id", f"{path}.dialogueId", "missing_dialogue", scene_id))
            if kind == "presentationEffect" and not clip.get("effectId"):
                issues.append(Diagnostic("error", "presentation clip requires an effect id", f"{path}.effectId", "missing_effect", scene_id))
            if kind == "worldEvent" and (not isinstance(clip.get("worldAction"), dict) or not clip["worldAction"].get("kind")):  # type: ignore[union-attr]
                issues.append(Diagnostic("error", "world event clip requires a world action", f"{path}.worldAction", "missing_world_action", scene_id))
            if kind == "worldEvent" and isinstance(clip.get("worldAction"), dict) and clip["worldAction"].get("kind") == "startScene":  # type: ignore[index]
                issues.append(Diagnostic("error", "nested scenes are not supported", f"{path}.worldAction.kind", "nested_scene", scene_id))
        moves = [clip for clip in clips if isinstance(clip, dict) and clip.get("kind") == "move"]
        for left_index, left in enumerate(moves):
            for right in moves[left_index + 1:]:
                if clip_end(left) > _safe_int(right.get("startTick", 0)) and clip_end(right) > _safe_int(left.get("startTick", 0)):
                    issues.append(Diagnostic("error", "movement clips overlap on the same actor track", f"tracks[{track_index}].clips", "overlapping_moves", scene_id))

    for index, marker in enumerate(markers):
        if not isinstance(marker, dict) or not isinstance(marker.get("name"), str) or not marker.get("name"):
            issues.append(Diagnostic("error", "scene marker is invalid", f"markers[{index}]", "invalid_marker", scene_id))
        elif not isinstance(marker.get("tick"), int) or isinstance(marker.get("tick"), bool) or marker.get("tick", 0) < 0 or (isinstance(duration, int) and marker.get("tick", 0) > duration):
            issues.append(Diagnostic("error", "scene marker is outside scene duration", f"markers[{index}].tick", "invalid_marker", scene_id))
    marker_names = [str(marker.get("name")) for marker in markers if isinstance(marker, dict)]
    for name in {value for value in marker_names if marker_names.count(value) > 1}:
        issues.append(Diagnostic("error", f"duplicate scene marker: {name}", "markers", "duplicate_marker", scene_id))
    if map_width and map_height:
        for track in tracks:
            for clip in _read_array(track, "clips") if isinstance(track, dict) else []:
                if isinstance(clip, dict) and clip.get("kind") == "move" and isinstance(clip.get("targetPosition"), dict):
                    position = clip["targetPosition"]
                    x = int(position.get("x", 0)); y = int(position.get("y", 0))
                    if not (0 <= x < map_width and 0 <= y < map_height):
                        issues.append(Diagnostic("error", "scene move target is outside map bounds", "targetPosition", "target_out_of_bounds", scene_id))
    return issues


def _array(value: dict[str, JsonValue], key: str) -> list[JsonValue]:
    result = value.setdefault(key, [])
    if not isinstance(result, list):
        raise ValueError(f"{key} must be an array")
    return result


def _safe_int(value: object, default: int = 0) -> int:
    if isinstance(value, bool):
        return default
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _read_array(value: object, key: str) -> list[JsonValue]:
    if not isinstance(value, dict):
        return []
    result = value.get(key, [])
    return result if isinstance(result, list) else []


def _clips(tracks: list[JsonValue], track_index: int) -> list[dict[str, JsonValue]]:
    if track_index < 0 or track_index >= len(tracks) or not isinstance(tracks[track_index], dict):
        raise IndexError("scene track index out of range")
    values = tracks[track_index].setdefault("clips", [])
    if not isinstance(values, list):
        raise ValueError("scene track clips must be an array")
    if any(not isinstance(value, dict) for value in values):
        raise ValueError("scene track clips must contain objects")
    return values  # type: ignore[return-value]


def _clip(tracks: list[JsonValue], track_index: int, clip_index: int) -> dict[str, JsonValue]:
    clips = _clips(tracks, track_index)
    if clip_index < 0 or clip_index >= len(clips):
        raise IndexError("scene clip index out of range")
    return clips[clip_index]
