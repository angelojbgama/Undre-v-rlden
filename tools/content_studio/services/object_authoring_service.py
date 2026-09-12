from __future__ import annotations

import copy
from dataclasses import dataclass

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue


@dataclass(frozen=True, slots=True)
class ObjectAuthoringRequest:
    object_id: str
    display_name: str
    animation_id: str
    preset: str = "scenery"
    scenery_loop: bool = False
    container_capacity: int = 5
    maximum_health: int = 1
    damage_frame: int = 1
    damage_duration_ticks: int = 8
    destruction_frame_ticks: int = 8
    destructible_idle_frame: int = 0
    destruction_start_frame: int = 2
    destruction_end_frame: int = 2
    closed_frame: int = 0
    opening_start_frame: int = 1
    opening_end_frame: int = 1
    collision_enabled: bool = False
    collision_width: int = 0
    collision_height: int = 0
    collision_origin_x: int = 0
    collision_origin_y: int = 0
    collision_cells: tuple[int, ...] = ()


class ObjectAuthoringService:
    """Author object capabilities and state clips consumed by runtime catalogs."""

    PRESETS = frozenset({"scenery", "interactable", "destructible", "container", "door"})
    _SOURCE_TAG = "object-source-animation:"
    _CLOSED_TAG = "object-closed-frame:"
    _OPENING_START_TAG = "object-opening-start:"
    _OPENING_END_TAG = "object-opening-end:"
    _SCENERY_LOOP_TAG = "object-scenery-loop:"
    _DAMAGE_ANIMATION_TAG = "object-damage-animation:"  # Legacy metadata.
    _DESTROYING_ANIMATION_TAG = "object-destroying-animation:"  # Legacy metadata.
    _DESTROYED_ANIMATION_TAG = "object-destroyed-animation:"  # Legacy metadata.
    _DESTRUCTIBLE_IDLE_FRAME_TAG = "object-destructible-idle-frame:"
    _DAMAGE_FRAME_TAG = "object-damage-frame:"
    _DAMAGE_FRAME_TICKS_TAG = "object-damage-frame-ticks:"
    _DESTRUCTION_START_TAG = "object-destruction-start:"
    _DESTRUCTION_END_TAG = "object-destruction-end:"
    _DESTRUCTION_FRAME_TICKS_TAG = "object-destruction-frame-ticks:"

    def create(self, workspace: ContentWorkspace,
               request: ObjectAuthoringRequest) -> ContentDefinition:
        return self._write(workspace, request, editing=False)

    def update(self, workspace: ContentWorkspace,
               request: ObjectAuthoringRequest) -> ContentDefinition:
        return self._write(workspace, request, editing=True)

    def _write(self, workspace: ContentWorkspace, request: ObjectAuthoringRequest,
               editing: bool) -> ContentDefinition:
        object_id = request.object_id.strip()
        name = request.display_name.strip()
        if not object_id or not name:
            raise ValueError("object ID and display name are required")
        if request.preset not in self.PRESETS:
            raise ValueError(f"unknown object preset: {request.preset}")
        if request.preset == "destructible" and (
                request.maximum_health < 1 or request.damage_duration_ticks < 1 or
                request.destruction_frame_ticks < 1):
            raise ValueError("destructible health and state durations must be positive")
        animation = workspace.find("animations", request.animation_id)
        if animation is None:
            raise ValueError(f"animation does not exist: {request.animation_id}")
        existing_object = workspace.find("objects", object_id)
        if editing:
            if existing_object is None:
                raise ValueError(f"object does not exist: {object_id}")
            visual_id = str(existing_object.data.get("visualSetId", "")) or f"visual.{object_id}"
        else:
            visual_id = f"visual.{object_id}"
            for category, definition_id in (
                    ("objects", object_id), ("objectVisuals", visual_id),
                    ("authoringDescriptors", object_id)):
                if workspace.find(category, definition_id) is not None:
                    raise ValueError(f"definition already exists: {category}/{definition_id}")

        frames = animation.data.get("frames", [])
        if not isinstance(frames, list) or not frames:
            raise ValueError("the selected animation has no frames")
        if request.preset == "destructible":
            self._validate_destructible_request(request, len(frames))
        width, height = self._frame_size(animation)
        if request.collision_enabled:
            self._validate_collision_request(request)
        existing_visual = workspace.find("objectVisuals", visual_id)
        visual_data: dict[str, JsonValue] = (
            copy.deepcopy(existing_visual.data) if editing and existing_visual else {})
        visual_data["id"] = visual_id
        visual_data["idleAnimationId"] = request.animation_id
        visual_data.pop("openedAnimationId", None)
        if request.preset != "destructible":
            visual_data.pop("damagedAnimationId", None)
            visual_data.pop("destroyingAnimationId", None)
            visual_data.pop("destroyedAnimationId", None)
        bundle: list[tuple[str, str, dict[str, JsonValue]]] = []
        if request.preset == "scenery":
            idle_id = f"animation.{object_id}.idle"
            bundle.append(("animations", idle_id, self._clip_data(
                idle_id, animation, frames, loop=request.scenery_loop)))
            visual_data["idleAnimationId"] = idle_id
        elif request.preset == "destructible":
            idle_id = f"animation.{object_id}.idle"
            damaged_id = f"animation.{object_id}.damaged"
            destroying_id = f"animation.{object_id}.destroying"
            destroyed_id = f"animation.{object_id}.destroyed"
            bundle.append(("animations", idle_id, self._clip_data(
                idle_id, animation, [frames[request.destructible_idle_frame]])))
            bundle.append(("animations", damaged_id, self._clip_data(
                damaged_id, animation, self._frames_with_duration(
                    [frames[request.damage_frame]], request.damage_duration_ticks))))
            destruction_frames = self._frames_with_duration(
                frames[request.destruction_start_frame:
                       request.destruction_end_frame + 1],
                request.destruction_frame_ticks)
            bundle.append(("animations", destroying_id, self._clip_data(
                destroying_id, animation, destruction_frames)))
            bundle.append(("animations", destroyed_id, self._clip_data(
                destroyed_id, animation, [destruction_frames[-1]])))
            visual_data["idleAnimationId"] = idle_id
            visual_data["damagedAnimationId"] = damaged_id
            visual_data["destroyingAnimationId"] = destroying_id
            visual_data["destroyedAnimationId"] = destroyed_id
        elif request.preset == "container":
            self._validate_container_request(request, len(frames))
            closed_id = f"animation.{object_id}.closed"
            opening_id = f"animation.{object_id}.opening"
            bundle.extend([
                ("animations", closed_id,
                 self._clip_data(closed_id, animation, [frames[request.closed_frame]])),
                ("animations", opening_id, self._clip_data(
                    opening_id, animation,
                    frames[request.opening_start_frame:request.opening_end_frame + 1])),
            ])
            visual_data["idleAnimationId"] = closed_id
            visual_data["openedAnimationId"] = opening_id

        object_data = self._object_data(
            object_id, visual_id, request.preset, width, height,
            request.container_capacity, request.maximum_health,
            request.damage_duration_ticks,
            ((request.destruction_end_frame - request.destruction_start_frame + 1)
             * request.destruction_frame_ticks))
        if request.collision_enabled:
            object_data["collision"] = {
                "width": request.collision_width,
                "height": request.collision_height,
                "origin": {
                    "x": request.collision_origin_x,
                    "y": request.collision_origin_y,
                },
                "cells": list(request.collision_cells),
            }
        existing_descriptor = workspace.find("authoringDescriptors", object_id)
        old_tags = existing_descriptor.data.get("tags", []) if existing_descriptor else []
        internal_prefixes = (
            self._SOURCE_TAG, self._CLOSED_TAG,
            self._OPENING_START_TAG, self._OPENING_END_TAG,
            self._SCENERY_LOOP_TAG, self._DAMAGE_ANIMATION_TAG,
            self._DESTROYING_ANIMATION_TAG, self._DESTROYED_ANIMATION_TAG,
            self._DESTRUCTIBLE_IDLE_FRAME_TAG, self._DAMAGE_FRAME_TAG,
            self._DAMAGE_FRAME_TICKS_TAG, self._DESTRUCTION_START_TAG,
            self._DESTRUCTION_END_TAG, self._DESTRUCTION_FRAME_TICKS_TAG)
        tags = [tag for tag in old_tags
                if isinstance(tag, str) and tag not in self.PRESETS and
                not tag.startswith(internal_prefixes)] if isinstance(old_tags, list) else []
        tags.extend([request.preset, f"{self._SOURCE_TAG}{request.animation_id}"])
        if request.preset == "scenery":
            tags.append(f"{self._SCENERY_LOOP_TAG}{str(request.scenery_loop).lower()}")
        elif request.preset == "destructible":
            tags.extend([
                f"{self._DESTRUCTIBLE_IDLE_FRAME_TAG}"
                f"{request.destructible_idle_frame}",
                f"{self._DAMAGE_FRAME_TAG}{request.damage_frame}",
                f"{self._DAMAGE_FRAME_TICKS_TAG}{request.damage_duration_ticks}",
                f"{self._DESTRUCTION_START_TAG}{request.destruction_start_frame}",
                f"{self._DESTRUCTION_END_TAG}{request.destruction_end_frame}",
                f"{self._DESTRUCTION_FRAME_TICKS_TAG}"
                f"{request.destruction_frame_ticks}",
            ])
        elif request.preset == "container":
            tags.extend([
                f"{self._CLOSED_TAG}{request.closed_frame}",
                f"{self._OPENING_START_TAG}{request.opening_start_frame}",
                f"{self._OPENING_END_TAG}{request.opening_end_frame}",
            ])
        descriptor_data: dict[str, JsonValue] = {
            "definitionId": object_id,
            "displayName": name,
            "category": "object",
            "tags": tags,
        }
        bundle.extend([
            ("objectVisuals", visual_id, visual_data),
            ("objects", object_id, object_data),
            ("authoringDescriptors", object_id, descriptor_data),
        ])
        values = (workspace.upsert_definition_bundle("Configure Object", bundle)
                  if editing else workspace.create_definition_bundle("Create Object", bundle))
        return next(value for value in values if value.category == "objects")

    def request_for(self, workspace: ContentWorkspace,
                    definition: ContentDefinition) -> ObjectAuthoringRequest:
        if definition.category != "objects":
            raise ValueError("definition is not an object")
        visual = workspace.find(
            "objectVisuals", str(definition.data.get("visualSetId", "")))
        descriptor = workspace.find("authoringDescriptors", definition.definition_id)
        tags = descriptor.data.get("tags", []) if descriptor else []
        string_tags = [value for value in tags if isinstance(value, str)] if isinstance(tags, list) else []
        animation_id = self._tag_value(string_tags, self._SOURCE_TAG)
        if not animation_id and visual is not None:
            animation_id = str(visual.data.get("idleAnimationId", ""))
        animation = workspace.find("animations", animation_id)
        animation_frames = animation.data.get("frames", []) if animation else []
        frame_count = len(animation_frames) if isinstance(animation_frames, list) else 1
        preset = self._preset_name(definition)
        container = definition.data.get("container")
        capacity = int(container.get("capacity", 5)) if isinstance(container, dict) else 5
        destructible = definition.data.get("destructible")
        maximum_health = int(destructible.get("maximumHealth", 1)) if isinstance(destructible, dict) else 1
        damage_duration = int(destructible.get("damageDurationTicks", 8)) if isinstance(destructible, dict) else 8
        destruction_duration = int(destructible.get("destructionDurationTicks", 1)) if isinstance(destructible, dict) else 1
        destruction_start = self._tag_int(
            string_tags, self._DESTRUCTION_START_TAG,
            min(2, frame_count - 1))
        destruction_end = self._tag_int(
            string_tags, self._DESTRUCTION_END_TAG,
            max(0, frame_count - 1))
        destruction_frame_count = max(
            1, destruction_end - destruction_start + 1)
        collision = definition.data.get("collision")
        collision_enabled = isinstance(collision, dict)
        # Migrate legacy door blockingBounds losslessly when an old door is opened
        # in the new Studio: a filled rectangular mask represents the same AABB.
        if not collision_enabled and preset == "door":
            door = definition.data.get("door")
            blocking = door.get("blockingBounds") if isinstance(door, dict) else None
            if isinstance(blocking, dict):
                legacy_width = max(0, int(blocking.get("width", 0)))
                legacy_height = max(0, int(blocking.get("height", 0)))
                if legacy_width and legacy_height:
                    collision = {
                        "width": legacy_width,
                        "height": legacy_height,
                        "origin": {
                            "x": int(blocking.get("x", 0)),
                            "y": int(blocking.get("y", 0)),
                        },
                        "cells": [1] * (legacy_width * legacy_height),
                    }
                    collision_enabled = True
        collision_origin = collision.get("origin", {}) if collision_enabled else {}
        collision_cells = collision.get("cells", []) if collision_enabled else []
        return ObjectAuthoringRequest(
            object_id=definition.definition_id,
            display_name=definition.display_name,
            animation_id=animation_id,
            preset=preset,
            scenery_loop=self._tag_bool(
                string_tags, self._SCENERY_LOOP_TAG,
                bool(animation.data.get("loop", False)) if animation else False),
            container_capacity=max(1, capacity),
            maximum_health=max(1, maximum_health),
            damage_frame=self._tag_int(
                string_tags, self._DAMAGE_FRAME_TAG, min(1, frame_count - 1)),
            damage_duration_ticks=max(1, self._tag_int(
                string_tags, self._DAMAGE_FRAME_TICKS_TAG, damage_duration)),
            destruction_frame_ticks=max(1, self._tag_int(
                string_tags, self._DESTRUCTION_FRAME_TICKS_TAG,
                max(1, destruction_duration // destruction_frame_count))),
            destructible_idle_frame=self._tag_int(
                string_tags, self._DESTRUCTIBLE_IDLE_FRAME_TAG, 0),
            destruction_start_frame=destruction_start,
            destruction_end_frame=destruction_end,
            closed_frame=self._tag_int(string_tags, self._CLOSED_TAG, 0),
            opening_start_frame=self._tag_int(string_tags, self._OPENING_START_TAG, min(1, frame_count - 1)),
            opening_end_frame=self._tag_int(string_tags, self._OPENING_END_TAG, max(0, frame_count - 1)),
            collision_enabled=collision_enabled,
            collision_width=max(0, int(collision.get("width", 0))) if collision_enabled else 0,
            collision_height=max(0, int(collision.get("height", 0))) if collision_enabled else 0,
            collision_origin_x=int(collision_origin.get("x", 0)) if isinstance(collision_origin, dict) else 0,
            collision_origin_y=int(collision_origin.get("y", 0)) if isinstance(collision_origin, dict) else 0,
            collision_cells=tuple(
                1 if int(value) else 0 for value in collision_cells
            ) if isinstance(collision_cells, list) else (),
        )

    @staticmethod
    def _validate_collision_request(request: ObjectAuthoringRequest) -> None:
        if request.collision_width < 1 or request.collision_height < 1:
            raise ValueError("collision mask dimensions must be positive")
        expected = request.collision_width * request.collision_height
        if len(request.collision_cells) != expected:
            raise ValueError("collision mask size does not match its dimensions")
        if any(value not in (0, 1) for value in request.collision_cells):
            raise ValueError("collision mask cells must be 0 or 1")
        if not any(request.collision_cells):
            raise ValueError("collision mask must contain at least one solid pixel")

    @staticmethod
    def _validate_container_request(request: ObjectAuthoringRequest,
                                    frame_count: int) -> None:
        if request.container_capacity < 1:
            raise ValueError("container capacity must be at least one")
        if frame_count < 2:
            raise ValueError("a chest opening needs at least two animation frames")
        if not 0 <= request.closed_frame < frame_count:
            raise ValueError("closed frame is outside the animation")
        if not 0 <= request.opening_start_frame <= request.opening_end_frame < frame_count:
            raise ValueError("opening frame range is outside the animation")

    @staticmethod
    def _validate_destructible_request(request: ObjectAuthoringRequest,
                                       frame_count: int) -> None:
        if not 0 <= request.destructible_idle_frame < frame_count:
            raise ValueError("destructible idle frame is outside the animation")
        if not 0 <= request.damage_frame < frame_count:
            raise ValueError("destructible damage frame is outside the animation")
        if frame_count < 3:
            raise ValueError(
                "a destructible spritesheet needs idle, damage, and breaking frames")
        if not (0 <= request.destruction_start_frame <=
                request.destruction_end_frame < frame_count):
            raise ValueError("destruction frame range is outside the animation")

    @staticmethod
    def _clip_data(definition_id: str, source: ContentDefinition,
                   frames: list[JsonValue], loop: bool = False) -> dict[str, JsonValue]:
        return {
            "id": definition_id,
            "imageId": str(source.data.get("imageId", "")),
            "loop": loop,
            "frames": copy.deepcopy(frames),
        }

    @staticmethod
    def _frames_with_duration(frames: list[JsonValue],
                              duration_ticks: int) -> list[JsonValue]:
        result = copy.deepcopy(frames)
        for frame in result:
            if isinstance(frame, dict):
                frame["durationTicks"] = max(1, duration_ticks)
        return result

    @staticmethod
    def _frame_size(animation: ContentDefinition) -> tuple[int, int]:
        frames = animation.data.get("frames", [])
        first = frames[0] if isinstance(frames, list) and frames else None
        source = first.get("source") if isinstance(first, dict) else None
        if not isinstance(source, dict):
            return 16, 16
        return max(1, int(source.get("width", 16))), max(1, int(source.get("height", 16)))

    @staticmethod
    def _object_data(object_id: str, visual_id: str, preset: str,
                     width: int, height: int,
                     container_capacity: int, maximum_health: int,
                     damage_duration_ticks: int,
                     destruction_duration_ticks: int) -> dict[str, JsonValue]:
        bounds: dict[str, JsonValue] = {
            "x": -(width // 2), "y": -(height - 1),
            "width": width, "height": height,
        }
        data: dict[str, JsonValue] = {
            "id": object_id,
            "visualSetId": visual_id,
        }
        if preset in {"interactable", "container", "door"}:
            data["interactable"] = dict(bounds)
        if preset == "container":
            data["container"] = {"capacity": max(1, container_capacity)}
        elif preset == "destructible":
            data["destructible"] = {
                "maximumHealth": max(1, maximum_health),
                "hurtbox": dict(bounds),
                "destructionDurationTicks": max(1, destruction_duration_ticks),
                "damageDurationTicks": max(1, damage_duration_ticks),
            }
        elif preset == "door":
            # New Studio doors use the generic optional collision component.
            # Omitting legacy blockingBounds allows a real no-collision door.
            data["door"] = {"initialState": "closed"}
        return data

    @staticmethod
    def _preset_name(definition: ContentDefinition) -> str:
        for key in ("door", "container", "destructible", "interactable"):
            if definition.data.get(key) is not None:
                return key
        return "scenery"

    @staticmethod
    def _tag_value(tags: list[str], prefix: str) -> str:
        return next((tag.removeprefix(prefix) for tag in tags if tag.startswith(prefix)), "")

    @classmethod
    def _tag_int(cls, tags: list[str], prefix: str, default: int) -> int:
        value = cls._tag_value(tags, prefix)
        try:
            return int(value) if value else default
        except ValueError:
            return default

    @classmethod
    def _tag_bool(cls, tags: list[str], prefix: str, default: bool) -> bool:
        value = cls._tag_value(tags, prefix)
        return value == "true" if value in {"true", "false"} else default
