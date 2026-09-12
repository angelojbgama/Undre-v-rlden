from __future__ import annotations

import os
import struct
import tempfile
from pathlib import Path

from ..model.types import JsonValue


DMAP_MAJOR = 1
DMAP_MINOR = 5
EMPTY_CELL = 0xFFFFFFFF

FACING = {"down": 0, "up": 1, "left": 2, "right": 3}
PERSISTENCE = {"persistent": 0, "resetOnMapEnter": 1}
TRIGGERS = {
    "mapEntered": 0, "regionEntered": 1, "regionExited": 2,
    "encounterStarted": 3, "encounterCompleted": 4, "objectOpened": 5,
    "objectActivated": 6, "objectDeactivated": 7,
}
CONDITIONS = {
    "flagSet": 0, "flagNotSet": 1, "encounterCompleted": 2,
    "encounterNotCompleted": 3, "doorState": 4, "objectActive": 5,
    "objectInactive": 6,
}
ACTIONS = {
    "setFlag": 0, "clearFlag": 1, "startEncounter": 2,
    "setDoorState": 3, "playPresentationEffect": 4, "startScene": 5,
}
DOOR_STATES = {"locked": 0, "closed": 1, "open": 2}
ACTOR_KINDS = {"player": 0, "npc": 1, "enemy": 2}
TRACK_KINDS = {"actor": 0, "dialogue": 1, "world": 2, "presentation": 3}
CLIP_KINDS = {
    "move": 0, "face": 1, "emote": 2, "hop": 3, "dialogue": 4,
    "presentationEffect": 5, "worldEvent": 6,
}
EMOTES = {"surprise": 0, "question": 1, "ellipsis": 2}


class DmapError(ValueError):
    pass


class _Writer:
    def __init__(self) -> None:
        self.data = bytearray()

    def raw(self, value: bytes | bytearray) -> None:
        self.data.extend(value)

    def u8(self, value: int) -> None:
        self.raw(struct.pack("<B", _integer(value, 0, 0xFF)))

    def u16(self, value: int) -> None:
        self.raw(struct.pack("<H", _integer(value, 0, 0xFFFF)))

    def u32(self, value: int) -> None:
        self.raw(struct.pack("<I", _integer(value, 0, 0xFFFFFFFF)))

    def u64(self, value: int) -> None:
        self.raw(struct.pack("<Q", _integer(value, 0, 0xFFFFFFFFFFFFFFFF)))

    def i32(self, value: int) -> None:
        self.raw(struct.pack("<i", _integer(value, -0x80000000, 0x7FFFFFFF)))

    def string(self, value: str) -> None:
        encoded = value.encode("utf-8")
        if not encoded or len(encoded) > 4096:
            raise DmapError("DMAP strings must contain 1 to 4096 UTF-8 bytes")
        self.u32(len(encoded)); self.raw(encoded)


def _integer(value: object, minimum: int, maximum: int) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or not minimum <= value <= maximum:
        raise DmapError(f"integer {value!r} is outside [{minimum}, {maximum}]")
    return value


def _mapping(value: object, path: str) -> dict[str, JsonValue]:
    if not isinstance(value, dict):
        raise DmapError(f"{path} must be an object")
    return value


def _array(value: object, path: str) -> list[JsonValue]:
    if not isinstance(value, list):
        raise DmapError(f"{path} must be an array")
    return value


def _text(value: object, path: str, *, optional: bool = False) -> str:
    if not isinstance(value, str) or (not optional and not value):
        raise DmapError(f"{path} must be {'a string' if optional else 'a non-empty string'}")
    return value


def _point(value: object, path: str) -> tuple[int, int]:
    point = _mapping(value, path)
    return (_integer(point.get("x"), -0x80000000, 0x7FFFFFFF),
            _integer(point.get("y"), -0x80000000, 0x7FFFFFFF))


def _area(value: object, path: str) -> tuple[int, int, int, int]:
    area = _mapping(value, path)
    result = tuple(_integer(area.get(name), -0x80000000, 0x7FFFFFFF)
                   for name in ("x", "y", "width", "height"))
    if result[2] <= 0 or result[3] <= 0:
        raise DmapError(f"{path} must have positive width and height")
    return result  # type: ignore[return-value]


def _enum(table: dict[str, int], value: object, path: str, default: str | None = None) -> int:
    selected = default if value is None else value
    if not isinstance(selected, str) or selected not in table:
        raise DmapError(f"{path} contains an unknown value: {selected!r}")
    return table[selected]


def _values(map_data: dict[str, JsonValue], name: str) -> list[JsonValue]:
    return _array(map_data.get(name, []), name)


def _collect_strings(map_data: dict[str, JsonValue]) -> list[str]:
    values: set[str] = set()

    def add(value: object) -> None:
        if isinstance(value, str) and value:
            values.add(value)

    add(map_data.get("id"))
    for value in _values(map_data, "tileReferences"): add(_mapping(value, "tileReferences").get("tilesetId"))
    for value in _values(map_data, "layers"): add(_mapping(value, "layers").get("name"))
    for value in _values(map_data, "playerSpawns"): add(_mapping(value, "playerSpawns").get("id"))
    for category in ("enemies", "npcs"):
        for value in _values(map_data, category): add(_mapping(value, category).get("definitionId"))
    for value in _values(map_data, "objects"):
        item = _mapping(value, "objects"); add(item.get("definitionId"))
        for stack in _array(item.get("initialContents", []), "objects.initialContents"):
            add(_mapping(stack, "objects.initialContents").get("itemId"))
    for value in _values(map_data, "pickups"):
        item = _mapping(value, "pickups"); add(item.get("definitionId")); add(item.get("visualId"))
        payload = _mapping(item.get("payload"), "pickups.payload")
        if payload.get("kind") == "item": add(payload.get("itemId"))
    for value in _values(map_data, "links"):
        item = _mapping(value, "links")
        for key in ("id", "targetMapId", "targetSpawnId"): add(item.get(key))
    for value in _values(map_data, "regions"):
        item = _mapping(value, "regions"); add(item.get("id")); add(item.get("environmentEffectId"))
    for value in _values(map_data, "worldRules"):
        rule = _mapping(value, "worldRules"); add(rule.get("id"))
        for target in [rule.get("trigger"), *_array(rule.get("conditions", []), "conditions"),
                       *_array(rule.get("actions", []), "actions")]:
            if isinstance(target, dict): add(target.get("target"))
    for value in _values(map_data, "encounters"):
        item = _mapping(value, "encounters"); add(item.get("id")); add(item.get("rewardGrantId"))
    for value in _values(map_data, "scenes"):
        scene = _mapping(value, "scenes"); add(scene.get("id"))
        for actor in _array(scene.get("actors", []), "scenes.actors"):
            add(_mapping(actor, "scenes.actors").get("slotId"))
        for track in _array(scene.get("tracks", []), "scenes.tracks"):
            item = _mapping(track, "scenes.tracks"); add(item.get("actorSlot"))
            for clip in _array(item.get("clips", []), "scenes.tracks.clips"):
                entry = _mapping(clip, "scenes.tracks.clips")
                for key in ("actorSlot", "dialogueId", "effectId"): add(entry.get(key))
                action = entry.get("worldAction")
                if isinstance(action, dict): add(action.get("target"))
        for marker in _array(scene.get("markers", []), "scenes.markers"):
            add(_mapping(marker, "scenes.markers").get("name"))
    return sorted(values, key=lambda value: value.encode("utf-8"))


def _chunk(tag: bytes, payload: _Writer) -> bytes:
    if len(tag) != 4 or len(payload.data) > 64 * 1024 * 1024:
        raise DmapError("invalid or oversized DMAP chunk")
    return tag + struct.pack("<Q", len(payload.data)) + payload.data


def _index(indices: dict[str, int], value: object, path: str) -> int:
    text = _text(value, path)
    if text not in indices:
        raise DmapError(f"{path} was not interned")
    return indices[text]


def _write_point(output: _Writer, value: object, path: str) -> None:
    x, y = _point(value, path); output.i32(x); output.i32(y)


def _write_area(output: _Writer, value: object, path: str) -> None:
    for component in _area(value, path): output.i32(component)


def _write_target(output: _Writer, value: dict[str, JsonValue], indices: dict[str, int], path: str) -> None:
    instance = value.get("instanceTarget")
    target = value.get("target")
    if instance is not None:
        output.u8(2); output.u64(_integer(instance, 1, 0xFFFFFFFFFFFFFFFF))
    elif isinstance(target, str) and target:
        output.u8(1); output.u32(_index(indices, target, f"{path}.target"))
    else:
        output.u8(0)


def serialize_dmap(map_data: dict[str, JsonValue]) -> bytes:
    strings = _collect_strings(map_data)
    if len(strings) > 100000:
        raise DmapError("too many strings")
    indices = {value: index for index, value in enumerate(strings)}
    chunks: list[bytes] = []

    meta = _Writer(); meta.u32(_index(indices, map_data.get("id"), "id"))
    meta.u32(_integer(map_data.get("width"), 1, 4096)); meta.u32(_integer(map_data.get("height"), 1, 4096))
    meta.u16(_integer(map_data.get("tileSize"), 1, 0xFFFF)); chunks.append(_chunk(b"META", meta))
    table = _Writer(); table.u32(len(strings))
    for value in strings: table.string(value)
    chunks.append(_chunk(b"STRS", table))

    references = _values(map_data, "tileReferences")
    if len(references) > 65536: raise DmapError("too many tile references")
    tref = _Writer(); tref.u32(len(references))
    for index, value in enumerate(references):
        item = _mapping(value, f"tileReferences[{index}]")
        tref.u32(_index(indices, item.get("tilesetId"), f"tileReferences[{index}].tilesetId"))
        tref.u32(_integer(item.get("sourceIndex"), 0, 0xFFFFFFFF))
        flags = _integer(item.get("flags", 0), 0, 0xFF)
        if flags & ~1: raise DmapError(f"tileReferences[{index}].flags contains unsupported bits")
        tref.u8(flags)
    chunks.append(_chunk(b"TREF", tref))

    width = int(map_data["width"]); height = int(map_data["height"]); cell_count = width * height
    layers = _values(map_data, "layers")
    if not 1 <= len(layers) <= 64: raise DmapError("map layer count is invalid")
    layr = _Writer(); layr.u32(len(layers)); layer_names: set[str] = set()
    for index, value in enumerate(layers):
        layer = _mapping(value, f"layers[{index}]"); name = _text(layer.get("name"), f"layers[{index}].name")
        if name in layer_names: raise DmapError(f"duplicate layer name: {name}")
        layer_names.add(name); layr.u32(indices[name]); layr.u8(1 if bool(layer.get("visible", True)) else 0)
        cells = _array(layer.get("cells"), f"layers[{index}].cells")
        if len(cells) != cell_count: raise DmapError(f"layers[{index}] dimensions mismatch")
        layr.u32(len(cells))
        for cell in cells:
            if cell is None: layr.u32(EMPTY_CELL)
            else: layr.u32(_integer(cell, 0, len(references) - 1))
    chunks.append(_chunk(b"LAYR", layr))

    collision = _values(map_data, "collision")
    if len(collision) != cell_count: raise DmapError("collision dimensions mismatch")
    coll = _Writer(); coll.u32(len(collision))
    for value in collision: coll.u8(_integer(value, 0, 1))
    chunks.append(_chunk(b"COLL", coll))

    spawns = _values(map_data, "playerSpawns"); spwn = _Writer(); spwn.u32(len(spawns))
    spawn_ids: set[str] = set()
    for index, value in enumerate(spawns):
        item = _mapping(value, f"playerSpawns[{index}]"); spawn_id = _text(item.get("id"), f"playerSpawns[{index}].id")
        if spawn_id in spawn_ids: raise DmapError(f"duplicate player spawn: {spawn_id}")
        spawn_ids.add(spawn_id); spwn.u32(indices[spawn_id]); _write_point(spwn, item.get("position"), "playerSpawns.position")
        spwn.u8(_enum(FACING, item.get("facing"), "playerSpawns.facing", "down"))
    chunks.append(_chunk(b"SPWN", spwn))

    persistent: set[int] = set()
    def placement_id(item: dict[str, JsonValue], path: str) -> int:
        identifier = _integer(item.get("id"), 1, 0xFFFFFFFFFFFFFFFF)
        if identifier in persistent: raise DmapError(f"{path}.id is duplicated")
        persistent.add(identifier); return identifier

    enemies = _values(map_data, "enemies"); objects = _values(map_data, "objects"); pickups = _values(map_data, "pickups")
    npcs_values = _values(map_data, "npcs")
    if len(enemies) + len(objects) + len(pickups) + len(npcs_values) > 100000: raise DmapError("too many placements")
    ents = _Writer(); ents.u32(len(enemies))
    for index, value in enumerate(enemies):
        item = _mapping(value, f"enemies[{index}]"); ents.u64(placement_id(item, f"enemies[{index}]"))
        ents.u32(_index(indices, item.get("definitionId"), "enemies.definitionId")); _write_point(ents, item.get("position"), "enemies.position")
        ents.u8(_enum(FACING, item.get("facing"), "enemies.facing", "down"))
    ents.u32(len(objects))
    for index, value in enumerate(objects):
        item = _mapping(value, f"objects[{index}]"); ents.u64(placement_id(item, f"objects[{index}]"))
        ents.u32(_index(indices, item.get("definitionId"), "objects.definitionId")); _write_point(ents, item.get("position"), "objects.position")
        ents.u8(_enum(PERSISTENCE, item.get("persistence"), "objects.persistence", "persistent"))
        stacks = _array(item.get("initialContents", []), "objects.initialContents"); ents.u32(len(stacks))
        for stack_value in stacks:
            stack = _mapping(stack_value, "objects.initialContents"); ents.u32(_index(indices, stack.get("itemId"), "objects.initialContents.itemId")); ents.u32(_integer(stack.get("quantity"), 1, 0xFFFFFFFF))
    ents.u32(len(pickups))
    for index, value in enumerate(pickups):
        item = _mapping(value, f"pickups[{index}]"); ents.u64(placement_id(item, f"pickups[{index}]"))
        ents.u32(_index(indices, item.get("definitionId"), "pickups.definitionId")); ents.u32(_index(indices, item.get("visualId"), "pickups.visualId"))
        _write_point(ents, item.get("position"), "pickups.position"); _write_area(ents, item.get("collectionBounds"), "pickups.collectionBounds")
        payload = _mapping(item.get("payload"), "pickups.payload"); kind = payload.get("kind")
        if kind == "health": ents.u8(0); ents.i32(_integer(payload.get("amount"), 1, 0x7FFFFFFF))
        elif kind == "currency": ents.u8(1); ents.u64(_integer(payload.get("amount"), 1, 0xFFFFFFFFFFFFFFFF))
        elif kind == "item": ents.u8(2); ents.u32(_index(indices, payload.get("itemId"), "pickups.payload.itemId")); ents.u32(_integer(payload.get("quantity"), 1, 0xFFFFFFFF))
        else: raise DmapError(f"pickups[{index}].payload.kind is invalid")
    chunks.append(_chunk(b"ENTS", ents))

    npcs = _Writer(); npcs.u32(len(npcs_values))
    for index, value in enumerate(npcs_values):
        item = _mapping(value, f"npcs[{index}]"); npcs.u64(placement_id(item, f"npcs[{index}]"))
        npcs.u32(_index(indices, item.get("definitionId"), "npcs.definitionId")); _write_point(npcs, item.get("position"), "npcs.position")
        npcs.u8(_enum(FACING, item.get("facing"), "npcs.facing", "down"))
    chunks.append(_chunk(b"NPCS", npcs))

    links = _values(map_data, "links"); link = _Writer(); link.u32(len(links))
    for index, value in enumerate(links):
        item = _mapping(value, f"links[{index}]"); link.u32(_index(indices, item.get("id"), "links.id")); _write_area(link, item.get("trigger"), "links.trigger")
        link.u32(_index(indices, item.get("targetMapId"), "links.targetMapId")); link.u32(_index(indices, item.get("targetSpawnId"), "links.targetSpawnId"))
    chunks.append(_chunk(b"LINK", link))

    regions_values = _values(map_data, "regions"); regions = _Writer(); regions.u32(len(regions_values))
    for index, value in enumerate(regions_values):
        item = _mapping(value, f"regions[{index}]"); regions.u32(_index(indices, item.get("id"), "regions.id")); _write_area(regions, item.get("bounds"), "regions.bounds")
        effect = item.get("environmentEffectId"); regions.u8(1 if isinstance(effect, str) and effect else 0)
        if isinstance(effect, str) and effect: regions.u32(_index(indices, effect, "regions.environmentEffectId"))
    chunks.append(_chunk(b"REGN", regions))

    rules = _values(map_data, "worldRules"); world = _Writer(); world.u32(len(rules))
    for index, value in enumerate(rules):
        rule = _mapping(value, f"worldRules[{index}]"); world.u32(_index(indices, rule.get("id"), "worldRules.id"))
        trigger = _mapping(rule.get("trigger"), "worldRules.trigger"); world.u8(_enum(TRIGGERS, trigger.get("kind"), "worldRules.trigger.kind")); _write_target(world, trigger, indices, "worldRules.trigger")
        once = rule.get("once", False)
        if not isinstance(once, bool): raise DmapError("worldRules.once must be boolean")
        world.u8(1 if once else 0)
        conditions = _array(rule.get("conditions", []), "worldRules.conditions"); world.u32(len(conditions))
        for condition_value in conditions:
            condition = _mapping(condition_value, "worldRules.conditions"); world.u8(_enum(CONDITIONS, condition.get("kind"), "worldRules.conditions.kind")); _write_target(world, condition, indices, "worldRules.conditions")
            world.u8(_enum(DOOR_STATES, condition.get("doorState"), "worldRules.conditions.doorState", "closed"))
        actions = _array(rule.get("actions", []), "worldRules.actions"); world.u32(len(actions))
        for action_value in actions:
            action = _mapping(action_value, "worldRules.actions"); world.u8(_enum(ACTIONS, action.get("kind"), "worldRules.actions.kind")); _write_target(world, action, indices, "worldRules.actions")
            world.u8(_enum(DOOR_STATES, action.get("doorState"), "worldRules.actions.doorState", "closed"))
    chunks.append(_chunk(b"WRLD", world))

    encounter_values = _values(map_data, "encounters"); encounters = _Writer(); encounters.u32(len(encounter_values))
    for index, value in enumerate(encounter_values):
        item = _mapping(value, f"encounters[{index}]"); encounters.u32(_index(indices, item.get("id"), "encounters.id"))
        participants = _array(item.get("participants"), "encounters.participants"); encounters.u32(len(participants))
        for participant in participants: encounters.u64(_integer(participant, 1, 0xFFFFFFFFFFFFFFFF))
        reward = item.get("rewardGrantId"); encounters.u8(1 if isinstance(reward, str) and reward else 0)
        if isinstance(reward, str) and reward: encounters.u32(_index(indices, reward, "encounters.rewardGrantId"))
    chunks.append(_chunk(b"ENCT", encounters))

    scenes_values = _values(map_data, "scenes"); scenes = _Writer(); scenes.u32(len(scenes_values))
    def optional_string(output: _Writer, value: object, path: str) -> None:
        text = value if isinstance(value, str) else ""; output.u8(1 if text else 0)
        if text: output.u32(_index(indices, text, path))
    def scene_action(output: _Writer, value: object, path: str) -> None:
        action = value if isinstance(value, dict) else {}
        output.u8(_enum(ACTIONS, action.get("kind"), f"{path}.kind", "setFlag")); _write_target(output, action, indices, path)
        output.u8(_enum(DOOR_STATES, action.get("doorState"), f"{path}.doorState", "closed"))
    for scene_index, value in enumerate(scenes_values):
        scene = _mapping(value, f"scenes[{scene_index}]"); scenes.u32(_index(indices, scene.get("id"), "scenes.id")); scenes.u32(_integer(scene.get("durationTicks"), 1, 0xFFFFFFFF))
        actors = _array(scene.get("actors", []), "scenes.actors"); scenes.u32(len(actors))
        for actor_value in actors:
            actor = _mapping(actor_value, "scenes.actors"); scenes.u32(_index(indices, actor.get("slotId"), "scenes.actors.slotId")); scenes.u8(_enum(ACTOR_KINDS, actor.get("kind"), "scenes.actors.kind"))
            instance = actor.get("instanceId"); scenes.u8(1 if instance is not None else 0)
            if instance is not None: scenes.u64(_integer(instance, 1, 0xFFFFFFFFFFFFFFFF))
        tracks = _array(scene.get("tracks", []), "scenes.tracks"); scenes.u32(len(tracks))
        for track_value in tracks:
            track = _mapping(track_value, "scenes.tracks"); scenes.u8(_enum(TRACK_KINDS, track.get("kind"), "scenes.tracks.kind")); optional_string(scenes, track.get("actorSlot"), "scenes.tracks.actorSlot")
            clips = _array(track.get("clips", []), "scenes.tracks.clips"); scenes.u32(len(clips))
            for clip_value in clips:
                clip = _mapping(clip_value, "scenes.tracks.clips"); scenes.u8(_enum(CLIP_KINDS, clip.get("kind"), "scenes.tracks.clips.kind")); optional_string(scenes, clip.get("actorSlot"), "scenes.tracks.clips.actorSlot")
                scenes.u32(_integer(clip.get("startTick", 0), 0, 0xFFFFFFFF)); scenes.u32(_integer(clip.get("durationTicks", 0), 0, 0xFFFFFFFF))
                _write_point(scenes, clip.get("targetPosition", {"x": 0, "y": 0}), "scenes.tracks.clips.targetPosition")
                scenes.u8(_enum(FACING, clip.get("facing"), "scenes.tracks.clips.facing", "down")); scenes.u8(_enum(EMOTES, clip.get("emote"), "scenes.tracks.clips.emote", "surprise")); scenes.i32(_integer(clip.get("heightPixels", 0), -0x80000000, 0x7FFFFFFF))
                optional_string(scenes, clip.get("dialogueId"), "scenes.tracks.clips.dialogueId")
                wait = clip.get("waitForCompletion", False)
                if not isinstance(wait, bool): raise DmapError("scene waitForCompletion must be boolean")
                scenes.u8(1 if wait else 0); optional_string(scenes, clip.get("effectId"), "scenes.tracks.clips.effectId"); scene_action(scenes, clip.get("worldAction"), "scenes.tracks.clips.worldAction")
        markers = _array(scene.get("markers", []), "scenes.markers"); scenes.u32(len(markers))
        for marker_value in markers:
            marker = _mapping(marker_value, "scenes.markers"); scenes.u32(_index(indices, marker.get("name"), "scenes.markers.name")); scenes.u32(_integer(marker.get("tick"), 0, 0xFFFFFFFF))
    chunks.append(_chunk(b"SCNE", scenes))

    body = b"".join(chunks); result = b"DMAP" + struct.pack("<HHHHQ", DMAP_MAJOR, DMAP_MINOR, 0, 20, 20 + len(body)) + body
    if len(result) > 256 * 1024 * 1024: raise DmapError("DMAP exceeds file size limit")
    return result


def safe_dmap_filename(map_id: str) -> str:
    result = ""
    for byte in map_id.encode("utf-8"):
        character = chr(byte)
        result += (character if byte < 128 and (character.isalnum() or character in "._-")
                   else f"%{byte:02X}")
    return (result or "map") + ".dmap"


def write_dmap(path: Path, map_data: dict[str, JsonValue]) -> None:
    payload = serialize_dmap(map_data); path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(payload); output.flush(); os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists(): temporary.unlink()
