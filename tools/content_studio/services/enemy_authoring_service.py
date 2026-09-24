"""First-class Enemy authoring on top of the ContentWorkspace.

Mirrors the C++ ``ContentValidator`` rules for enemies so authored data
that passes here also compiles in the runtime:

- id lives in the ``enemy.`` namespace;
- ``visualSetId`` references an ``enemyVisuals`` definition;
- ``behaviorProfileId`` references a ``behaviors`` definition;
- ``attackIds`` is a non-empty list of ``attacks`` definitions (the
  creature engine rejects enemies without attacks);
- ``rewardProfileId`` optionally references ``rewardProfiles``;
- faction is always ``enemy``; stats and footprints obey the runtime's
  positive-value rules.

Deleting a referenced enemy is blocked: map placements (through the open
WorldProject) keep stale ids otherwise.
"""

from __future__ import annotations

import copy
import re
import struct
from pathlib import Path

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition

_ENEMY_ID_PATTERN = re.compile(r"^[A-Za-z0-9_.]+$")
_MAX_HEALTH = 100000
_MAX_SPEED = 100000

# Clip slots of an EnemyVisualSet: the first three are required with all
# four facings; hurt/dead are optional but, when present, must also be
# complete (mirrors the C++ validateClips).
_VISUAL_CLIP_SLOTS = (
    ("idle", True),
    ("move", True),
    ("death", True),
    ("hurt", False),
    ("dead", False),
)
_FACINGS = ("down", "up", "left", "right")


def _png_size(path):
    """Width/height of a PNG via its IHDR, or None when unreadable."""
    try:
        with open(path, "rb") as handle:
            header = handle.read(24)
    except OSError:
        return None
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", header[16:24])
    return int(width), int(height)


class EnemyAuthoringService:
    def __init__(self, workspace: ContentWorkspace | None = None) -> None:
        self.workspace = workspace

    def set_context(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    def enemies(self, query: str = "") -> tuple[ContentDefinition, ...]:
        workspace = self._require_workspace()
        return tuple(workspace.definitions("enemies", query))

    def find(self, enemy_id: str) -> ContentDefinition | None:
        workspace = self._require_workspace()
        return workspace.find("enemies", enemy_id)

    def behaviors(self) -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("behaviors"))

    def visual_sets(self) -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("enemyVisuals"))

    def attacks(self) -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("attacks"))

    def reward_profiles(self) -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("rewardProfiles"))

    def placements(self, enemy_id: str) -> tuple[str, ...]:
        """Map documents that place this enemy (open WorldProject)."""
        project = getattr(self.workspace, "world_project", None) if self.workspace else None
        if project is None:
            return ()
        placed_in: list[str] = []
        for document in getattr(project, "maps", []):
            for entry in document.data.get("enemies", []) or []:
                if isinstance(entry, dict) and entry.get("definitionId") == enemy_id:
                    placed_in.append(document.map_id)
                    break
        return tuple(placed_in)

    def create_enemy(self, enemy_id: str, data: dict[str, object]) -> ContentDefinition:
        workspace = self._require_workspace()
        normalized_id = self._normalize_enemy_id(enemy_id)
        if workspace.find("enemies", normalized_id):
            raise ValueError(f"enemy already exists: {normalized_id}")

        payload = dict(data)
        authored_id = payload.get("id")
        if isinstance(authored_id, str) and authored_id and authored_id != normalized_id:
            # Same contract as configure(): the payload id and the argument
            # must agree, otherwise the stored definition surprises callers.
            raise ValueError(
                f"enemy id does not match the created definition: "
                f"{authored_id} != {normalized_id}")
        payload["id"] = normalized_id
        normalized = self._validate(payload)

        workspace.create_definition_bundle(
            "Create Enemy",
            [("enemies", normalized_id, normalized)],
        )

        result = workspace.find("enemies", normalized_id)
        if result is None:
            raise RuntimeError("created enemy could not be indexed")
        return result

    def configure(self, enemy_id: str, data: dict[str, object]) -> None:
        workspace = self._require_workspace()
        definition = workspace.find("enemies", enemy_id)
        if definition is None:
            raise ValueError(f"unknown enemy: {enemy_id}")
        if not isinstance(data.get("id"), str) or data["id"] != enemy_id:
            raise ValueError("enemy id does not match the edited definition")

        normalized = self._validate(data)

        workspace.upsert_definition_bundle(
            "Edit Enemy",
            [("enemies", enemy_id, normalized)],
        )

    def delete(self, enemy_id: str) -> None:
        workspace = self._require_workspace()
        definition = workspace.find("enemies", enemy_id)
        if definition is None:
            raise ValueError(f"unknown enemy: {enemy_id}")

        placed_in = self.placements(enemy_id)
        if placed_in:
            raise ValueError(
                "enemy is placed in maps: " + ", ".join(placed_in)
            )

        workspace.delete_definition(definition)

    def verify_visual(
        self, enemy_id: str, asset_root: Path | None = None,
    ) -> list[dict[str, str]]:
        """Check that the enemy's sprite chain is correct and coherent.

        Walks visualSetId -> EnemyVisualSet clips -> animations -> images
        and reports diagnostics dicts (``severity``/``message``/``path``):
        facing coverage per clip slot (idle/move/death need all four),
        animation/image references that do not resolve, image files missing
        on disk (when a root is resolvable), frame source rects outside the
        image, zero-duration frames and out-of-frame anchors (warnings).
        """
        workspace = self._require_workspace()
        diagnostics: list[dict[str, str]] = []

        def error(message: str, path: str) -> None:
            diagnostics.append({"severity": "error", "message": message, "path": path})

        def warn(message: str, path: str) -> None:
            diagnostics.append({"severity": "warning", "message": message, "path": path})

        definition = self.find(enemy_id)
        if definition is None:
            error(f"unknown enemy: {enemy_id}", enemy_id)
            return diagnostics

        visual_set_id = definition.data.get("visualSetId")
        visual_set = (workspace.find("enemyVisuals", str(visual_set_id))
                      if isinstance(visual_set_id, str) else None)
        if visual_set is None:
            error(f"enemy visual set does not exist: {visual_set_id}", "visualSetId")
            return diagnostics

        def image_size(image_id: str):
            if image_id in image_cache:
                return image_cache[image_id]
            image_definition = workspace.find("visualImages", image_id)
            path = self._visual_image_path(image_definition, asset_root)
            size = _png_size(path) if path is not None else None
            image_cache[image_id] = size
            return size

        image_cache: dict[str, tuple[int, int] | None] = {}

        def check_clip(slot: str, clip_data: object, path: str, required: bool) -> None:
            if not isinstance(clip_data, dict):
                if required:
                    error(f"{slot} clip is missing", path)
                return
            for facing in _FACINGS:
                animation_id = clip_data.get(facing)
                facing_path = f"{path}.{facing}"
                if not isinstance(animation_id, str) or not animation_id:
                    if required:
                        error(f"{slot} clip has no {facing} facing", facing_path)
                    continue
                animation = workspace.find("animations", animation_id)
                if animation is None:
                    error(f"animation does not exist: {animation_id}", facing_path)
                    continue
                frames = animation.data.get("frames")
                if not isinstance(frames, list) or not frames:
                    error(f"animation has no frames: {animation_id}", facing_path)
                    continue
                image_id = animation.data.get("imageId")
                image_id = image_id if isinstance(image_id, str) else ""
                image_definition = (workspace.find("visualImages", image_id)
                                    if image_id else None)
                if image_definition is None:
                    error(f"visual image does not exist: {image_id or '(empty)'}",
                          facing_path)
                    continue
                image_path = self._visual_image_path(image_definition, asset_root)
                if image_path is None:
                    # No asset root resolvable (headless contexts): disk and
                    # bounds checks are skipped rather than guessed.
                    continue
                if not image_path.is_file():
                    error(f"image file is missing: {image_path}", facing_path)
                    continue
                size = image_size(image_id)
                if size is not None:
                    width, height = size
                    for index, frame in enumerate(frames):
                        if not isinstance(frame, dict):
                            continue
                        source = frame.get("source")
                        if not isinstance(source, dict):
                            continue
                        right = int(source.get("x", 0)) + int(source.get("width", 0))
                        bottom = int(source.get("y", 0)) + int(source.get("height", 0))
                        if (int(source.get("width", 0)) <= 0
                                or int(source.get("height", 0)) <= 0
                                or right > width or bottom > height):
                            error(
                                f"frame {index} source rect is outside the "
                                f"image {width}x{height}: {animation_id}",
                                f"{facing_path}.frames[{index}]")
                for index, frame in enumerate(frames):
                    if not isinstance(frame, dict):
                        continue
                    if frame.get("durationTicks", 0) in (None, 0):
                        warn(f"frame {index} has zero duration: {animation_id}",
                             f"{facing_path}.frames[{index}]")
                    anchor = frame.get("anchor")
                    source = frame.get("source")
                    if isinstance(anchor, dict) and isinstance(source, dict):
                        inside = (0 <= int(anchor.get("x", 0)) <= int(source.get("width", 0))
                                  and 0 <= int(anchor.get("y", 0)) <= int(source.get("height", 0)))
                        if not inside:
                            warn(f"frame {index} anchor sits outside the frame: {animation_id}",
                                 f"{facing_path}.frames[{index}]")

        for slot, required in _VISUAL_CLIP_SLOTS:
            check_clip(slot, visual_set.data.get(slot), slot, required)
        for index, action in enumerate(visual_set.data.get("actions", []) or []):
            if isinstance(action, dict):
                check_clip(f"actions[{index}]", action.get("clips"),
                           f"actions[{index}].clips", required=True)
        return diagnostics

    def _visual_image_path(
        self, image_definition, asset_root,
    ):
        """Mirror of StudioVisualResolver path semantics without Qt."""
        if image_definition is None:
            return None
        relative = image_definition.data.get("relativePath")
        if not isinstance(relative, str) or not relative:
            return None
        root_name = image_definition.data.get("root", "gameAssets")
        if root_name == "contentWorkspace":
            root = self.workspace.root if self.workspace is not None else None
        else:
            root = asset_root
        if root is None:
            return None
        return Path(root) / relative

    def blank_enemy(self, enemy_id: str) -> dict[str, object]:
        """A valid-by-construction starter enemy bound to real references."""
        workspace = self._require_workspace()
        behaviors = workspace.definitions("behaviors")
        visuals = workspace.definitions("enemyVisuals")
        attacks = workspace.definitions("attacks")
        if not behaviors or not visuals or not attacks:
            raise ValueError(
                "author at least one behavior, enemy visual and attack first"
            )
        return {
            "id": self._normalize_enemy_id(enemy_id),
            "visualSetId": visuals[0].definition_id,
            "behaviorProfileId": behaviors[0].definition_id,
            "faction": "enemy",
            "maximumHealth": 3,
            "movementSpeedSubpixelsPerTick": 96,
            "collisionBody": {"offsetX": -5, "offsetY": -8, "width": 10, "height": 8},
            "hurtbox": {"offsetX": -7, "offsetY": -22, "width": 14, "height": 22},
            "attackIds": [attacks[0].definition_id],
            "rewardProfileId": None,
        }

    def _validate(self, data: dict[str, object]) -> dict[str, object]:
        workspace = self._require_workspace()
        enemy_id = data.get("id")
        if not isinstance(enemy_id, str) or not _ENEMY_ID_PATTERN.fullmatch(enemy_id):
            raise ValueError(f"invalid enemy id: {enemy_id!r}")

        visual_set_id = data.get("visualSetId")
        if not isinstance(visual_set_id, str) or not visual_set_id:
            raise ValueError("enemy visualSetId is required")
        if workspace.find("enemyVisuals", visual_set_id) is None:
            raise ValueError(f"enemy visual set does not exist: {visual_set_id}")

        behavior_id = data.get("behaviorProfileId")
        if not isinstance(behavior_id, str) or not behavior_id:
            raise ValueError("enemy behaviorProfileId is required")
        if workspace.find("behaviors", behavior_id) is None:
            raise ValueError(f"behavior profile does not exist: {behavior_id}")

        health = data.get("maximumHealth")
        if not self._positive_int(health) or int(health) > _MAX_HEALTH:
            raise ValueError("enemy maximumHealth must be a positive integer")

        speed = data.get("movementSpeedSubpixelsPerTick")
        if not self._positive_int(speed) or int(speed) > _MAX_SPEED:
            raise ValueError("enemy movementSpeedSubpixelsPerTick must be a positive integer")

        for field in ("collisionBody", "hurtbox"):
            box = data.get(field)
            if not isinstance(box, dict) or not self._valid_box(box):
                raise ValueError(f"enemy {field} needs integer offsets and positive size")

        attack_ids = data.get("attackIds")
        if not isinstance(attack_ids, list) or not attack_ids:
            raise ValueError("enemy needs at least one attack (the creature engine rejects empty lists)")
        seen: set[str] = set()
        for attack_id in attack_ids:
            if not isinstance(attack_id, str) or not attack_id:
                raise ValueError("enemy attackIds entries must be non-empty strings")
            if attack_id in seen:
                raise ValueError(f"enemy attackIds has a duplicate entry: {attack_id}")
            seen.add(attack_id)
            if workspace.find("attacks", attack_id) is None:
                raise ValueError(f"attack definition does not exist: {attack_id}")

        reward_profile_id = data.get("rewardProfileId")
        if reward_profile_id is not None:
            if not isinstance(reward_profile_id, str) or not reward_profile_id:
                raise ValueError("enemy rewardProfileId must be a string or null")
            if workspace.find("rewardProfiles", reward_profile_id) is None:
                raise ValueError(f"reward profile does not exist: {reward_profile_id}")

        normalized = copy.deepcopy(data)
        normalized["faction"] = "enemy"
        return normalized

    @staticmethod
    def _valid_box(box: dict[str, object]) -> bool:
        for key in ("offsetX", "offsetY", "width", "height"):
            value = box.get(key)
            if not isinstance(value, int) or isinstance(value, bool):
                return False
        return int(box["width"]) > 0 and int(box["height"]) > 0

    @staticmethod
    def _positive_int(value: object) -> bool:
        return isinstance(value, int) and not isinstance(value, bool) and value > 0

    def _require_workspace(self) -> ContentWorkspace:
        if self.workspace is None:
            raise ValueError("enemy authoring requires an open content workspace")
        return self.workspace

    @staticmethod
    def _normalize_enemy_id(enemy_id: str) -> str:
        normalized = enemy_id.strip()
        if not _ENEMY_ID_PATTERN.fullmatch(normalized):
            raise ValueError(f"invalid enemy id: {enemy_id!r}")
        if not normalized.startswith("enemy."):
            normalized = f"enemy.{normalized}"
        return normalized
