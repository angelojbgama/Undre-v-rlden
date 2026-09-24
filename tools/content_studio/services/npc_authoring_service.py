"""First-class NPC authoring on top of the ContentWorkspace.

Mirrors the C++ ``ContentValidator`` rules for NPCs and NPC visuals:

- id lives in the ``npc.`` namespace;
- ``visualSetId`` references an ``npcVisuals`` definition (workspace or
  one of the builtin read-only marker visuals);
- ``defaultDialogueId`` references a ``dialogues`` definition;
- the interaction box has positive width/height;
- NPC visual sets need a marker color plus at least one idle animation
  binding (default or per-facing), and every binding must exist.

Deleting an NPC placed in a map is blocked: placements would keep stale
ids. Visual verification walks idle bindings -> animations -> images and
reports missing files, frames outside the sheet and off-frame anchors.
"""

from __future__ import annotations

import copy
import re
import struct
from dataclasses import dataclass, field
from pathlib import Path

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue
from .player_authoring_service import (
    FrameSequenceSpec,
    PlayerAuthoringService,
)

_NPC_ID_PATTERN = re.compile(r"^[A-Za-z0-9_.]+$")

# Builtin marker-only visuals shipped in builtin_content.cpp; listed as
# read-only choices. Editing one creates a workspace override by id.
BUILTIN_NPC_VISUALS: tuple[dict[str, object], ...] = (
    {"id": "visual.npc.guard", "markerColor": {"r": 70, "g": 150, "b": 240, "a": 255}},
    {"id": "visual.npc.scholar", "markerColor": {"r": 220, "g": 180, "b": 70, "a": 255}},
    {"id": "visual.npc.merchant", "markerColor": {"r": 120, "g": 210, "b": 120, "a": 255}},
)

_FACINGS = ("down", "up", "left", "right")
_SEQUENCE_KEYS = ("default", *_FACINGS)


@dataclass(slots=True)
class NpcVisualRequest:
    """Authored NPC visual: marker color plus frame sequences.

    Mirrors the player pipeline (FrameSequenceSpec built in the frame
    editor). ``default`` is the fallback binding; the four facings
    override it per direction at runtime.
    """

    visual_id: str
    marker_color: dict[str, int]
    sequences: dict[str, FrameSequenceSpec] = field(default_factory=dict)


def _png_size(path: Path) -> tuple[int, int] | None:
    try:
        with open(path, "rb") as handle:
            header = handle.read(24)
    except OSError:
        return None
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        return None
    width, height = struct.unpack(">II", header[16:24])
    return int(width), int(height)


class NpcAuthoringService:
    def __init__(self, workspace: ContentWorkspace | None = None) -> None:
        self.workspace = workspace

    def set_context(self, workspace: ContentWorkspace | None) -> None:
        self.workspace = workspace

    def npcs(self, query: str = "") -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("npcs", query))

    def find(self, npc_id: str) -> ContentDefinition | None:
        return self._require_workspace().find("npcs", npc_id)

    def dialogues(self) -> tuple[ContentDefinition, ...]:
        return tuple(self._require_workspace().definitions("dialogues"))

    def visual_sets(self) -> tuple[dict[str, object], ...]:
        """Workspace npcVisuals first, then the builtin read-only ones."""
        workspace = self._require_workspace()
        workspace_sets = [dict(value.data) for value in workspace.definitions("npcVisuals")]
        known = {str(entry.get("id")) for entry in workspace_sets}
        builtin = [dict(entry) for entry in BUILTIN_NPC_VISUALS
                   if str(entry["id"]) not in known]
        return tuple(workspace_sets + builtin)

    def find_visual(self, visual_id: str) -> dict[str, object] | None:
        for entry in self.visual_sets():
            if str(entry.get("id")) == visual_id:
                return entry
        return None

    def placements(self, npc_id: str) -> tuple[str, ...]:
        project = getattr(self.workspace, "world_project", None) if self.workspace else None
        if project is None:
            return ()
        placed_in: list[str] = []
        for document in getattr(project, "maps", []):
            for entry in document.data.get("npcs", []) or []:
                if isinstance(entry, dict) and entry.get("definitionId") == npc_id:
                    placed_in.append(document.map_id)
                    break
        return tuple(placed_in)

    def create_npc(self, npc_id: str, data: dict[str, object]) -> ContentDefinition:
        workspace = self._require_workspace()
        normalized_id = self._normalize_npc_id(npc_id)
        if workspace.find("npcs", normalized_id):
            raise ValueError(f"NPC already exists: {normalized_id}")
        payload = dict(data)
        authored_id = payload.get("id")
        if isinstance(authored_id, str) and authored_id and authored_id != normalized_id:
            raise ValueError(
                f"NPC id does not match the created definition: "
                f"{authored_id} != {normalized_id}")
        payload["id"] = normalized_id
        normalized = self._validate(payload)
        workspace.create_definition_bundle(
            "Create NPC", [("npcs", normalized_id, normalized)])
        result = workspace.find("npcs", normalized_id)
        if result is None:
            raise RuntimeError("created NPC could not be indexed")
        return result

    def configure(self, npc_id: str, data: dict[str, object]) -> None:
        workspace = self._require_workspace()
        definition = workspace.find("npcs", npc_id)
        if definition is None:
            raise ValueError(f"unknown NPC: {npc_id}")
        if not isinstance(data.get("id"), str) or data["id"] != npc_id:
            raise ValueError("NPC id does not match the edited definition")
        normalized = self._validate(data)
        workspace.upsert_definition_bundle(
            "Edit NPC", [("npcs", npc_id, normalized)])

    def delete(self, npc_id: str) -> None:
        workspace = self._require_workspace()
        definition = workspace.find("npcs", npc_id)
        if definition is None:
            raise ValueError(f"unknown NPC: {npc_id}")
        placed_in = self.placements(npc_id)
        if placed_in:
            raise ValueError("NPC is placed in maps: " + ", ".join(placed_in))
        workspace.delete_definition(definition)

    # -- visual sets ---------------------------------------------------------

    def create_visual_set(self, visual_id: str, data: dict[str, object]) -> ContentDefinition:
        workspace = self._require_workspace()
        normalized_id = self._normalize_visual_id(visual_id)
        if workspace.find("npcVisuals", normalized_id):
            raise ValueError(f"NPC visual set already exists: {normalized_id}")
        payload = dict(data)
        payload["id"] = normalized_id
        normalized = self._validate_visual(payload)
        workspace.create_definition_bundle(
            "Create NPC Visual", [("npcVisuals", normalized_id, normalized)])
        result = workspace.find("npcVisuals", normalized_id)
        if result is None:
            raise RuntimeError("created NPC visual could not be indexed")
        return result

    def configure_visual(self, visual_id: str, data: dict[str, object]) -> None:
        """Edit a workspace NPC visual set. Builtin marker visuals are not
        editable here (they have no animations to edit); authoring a new
        workspace set with the same id shadows the builtin at runtime."""
        workspace = self._require_workspace()
        definition = workspace.find("npcVisuals", visual_id)
        if definition is None:
            raise ValueError(f"unknown NPC visual set: {visual_id}")
        if not isinstance(data.get("id"), str) or data["id"] != visual_id:
            raise ValueError("NPC visual id does not match the edited definition")
        normalized = self._validate_visual(data)
        workspace.upsert_definition_bundle(
            "Edit NPC Visual", [("npcVisuals", visual_id, normalized)])

    def _validate_visual(self, data: dict[str, object]) -> dict[str, object]:
        workspace = self._require_workspace()
        visual_id = data.get("id")
        if not isinstance(visual_id, str) or not _NPC_ID_PATTERN.fullmatch(visual_id):
            raise ValueError(f"invalid NPC visual id: {visual_id!r}")
        marker = data.get("markerColor")
        if not isinstance(marker, dict) or any(
                not isinstance(marker.get(channel), int) or isinstance(marker.get(channel), bool)
                for channel in ("r", "g", "b", "a")):
            raise ValueError("NPC visual needs an integer rgba marker color")
        idle = data.get("idle")
        bindings: list[tuple[str, object]] = []
        if isinstance(idle, dict):
            bindings = [(key, idle.get(key)) for key in ("defaultAnimation", *_FACINGS)
                        if isinstance(idle.get(key), str) and idle.get(key)]
        if not bindings:
            raise ValueError(
                "NPC visual idle needs at least one animation binding "
                "(default or a facing)")
        for _key, animation_id in bindings:
            if workspace.find("animations", str(animation_id)) is None:
                raise ValueError(f"animation does not exist: {animation_id}")
        return copy.deepcopy(data)

    # -- developed-standard visual authoring (spritesheet pipeline) ----------

    @staticmethod
    def _slug(value: str) -> str:
        slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
        return slug or "npc"

    def save_visual(
        self, workspace: ContentWorkspace, request: NpcVisualRequest,
        editing: bool = False,
    ) -> ContentDefinition:
        """Materialize visualImage/animation/npcVisual definitions from
        frame sequences built in the frame editor — the same pipeline the
        player library uses. Editing reuses the same animation ids."""
        visual_id = request.visual_id.strip()
        existing = workspace.find("npcVisuals", visual_id)
        if editing and existing is None:
            raise ValueError("O visual selecionado não existe mais.")
        if not editing and existing is not None:
            raise ValueError(f"Visual de NPC já existe: {visual_id}")

        sequences = {key: spec for key, spec in request.sequences.items()
                     if spec is not None}
        if not sequences:
            raise ValueError(
                "Monte pelo menos uma sequência de frames antes de salvar.")

        slug = self._slug(visual_id)
        animation_entries: list[tuple[str, str, dict[str, JsonValue]]] = []
        refs: dict[str, str] = {}
        for key in _SEQUENCE_KEYS:
            spec = sequences.get(key)
            if spec is None:
                continue
            if workspace.find("visualImages", spec.image_id) is None:
                raise ValueError(f"Imagem importada não existe: {spec.image_id}")
            animation_id = f"anim.npc.{slug}.{key}"
            animation_entries.append((
                "animations", animation_id, {
                    "id": animation_id,
                    "imageId": spec.image_id,
                    "loop": spec.loop,
                    "frames": PlayerAuthoringService._materialize(spec),
                }))
            refs[key] = animation_id

        idle: dict[str, str] = {}
        if "default" in refs:
            idle["defaultAnimation"] = refs["default"]
        for facing in _FACINGS:
            if facing in refs:
                idle[facing] = refs[facing]

        entries = animation_entries + [
            ("npcVisuals", visual_id, {
                "id": visual_id,
                "markerColor": dict(request.marker_color),
                "idle": idle,
            }),
        ]
        bundle = ("Configure NPC Visual" if editing else "Create NPC Visual")
        workspace.upsert_definition_bundle(bundle, entries) if editing else             workspace.create_definition_bundle(bundle, entries)
        result = workspace.find("npcVisuals", visual_id)
        if result is None:
            raise RuntimeError("created NPC visual could not be indexed")
        return result

    def visual_request_for(
        self, workspace: ContentWorkspace, visual_id: str,
        asset_root: Path | None,
    ) -> tuple[dict[str, object], dict[str, FrameSequenceSpec]]:
        """Editing round-trip: rebuild frame sequences from the authored
        animations so the frame editor opens pre-filled."""
        definition = workspace.find("npcVisuals", visual_id)
        if definition is None:
            raise ValueError(f"unknown NPC visual set: {visual_id}")
        marker = definition.data.get("markerColor")
        marker = dict(marker) if isinstance(marker, dict) else {
            "r": 255, "g": 255, "b": 255, "a": 255}
        idle = definition.data.get("idle")
        idle = idle if isinstance(idle, dict) else {}
        sequences: dict[str, FrameSequenceSpec] = {}
        animation_ids: list[tuple[str, str]] = []
        if isinstance(idle.get("defaultAnimation"), str) and idle["defaultAnimation"]:
            animation_ids.append(("default", str(idle["defaultAnimation"])))
        for facing in _FACINGS:
            if isinstance(idle.get(facing), str) and idle[facing]:
                animation_ids.append((facing, str(idle[facing])))
        for key, animation_id in animation_ids:
            animation = workspace.find("animations", animation_id)
            if animation is None:
                continue
            sequences[key] = PlayerAuthoringService._sequence_from_animation(
                workspace, animation, asset_root)
        return marker, sequences

    # -- verification --------------------------------------------------------

    def verify_visual(
        self, npc_id: str, asset_root: Path | None = None,
    ) -> list[dict[str, str]]:
        """Check the NPC's sprite chain: visual set, idle bindings, images."""
        workspace = self._require_workspace()
        diagnostics: list[dict[str, str]] = []

        def error(message: str, path: str) -> None:
            diagnostics.append({"severity": "error", "message": message, "path": path})

        def warn(message: str, path: str) -> None:
            diagnostics.append({"severity": "warning", "message": message, "path": path})

        definition = self.find(npc_id)
        if definition is None:
            error(f"unknown NPC: {npc_id}", npc_id)
            return diagnostics

        visual_set_id = definition.data.get("visualSetId")
        visual_set = (self.find_visual(str(visual_set_id))
                      if isinstance(visual_set_id, str) else None)
        if visual_set is None:
            error(f"NPC visual set does not exist: {visual_set_id}", "visualSetId")
            return diagnostics

        idle = visual_set.get("idle")
        if not isinstance(idle, dict):
            warn(
                "visual set has no idle animation: the NPC renders as a "
                "colored marker in game", "idle")
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

        for key in ("defaultAnimation", *_FACINGS):
            animation_id = idle.get(key)
            if not isinstance(animation_id, str) or not animation_id:
                continue
            path = f"idle.{key}"
            animation = workspace.find("animations", animation_id)
            if animation is None:
                error(f"animation does not exist: {animation_id}", path)
                continue
            frames = animation.data.get("frames")
            if not isinstance(frames, list) or not frames:
                error(f"animation has no frames: {animation_id}", path)
                continue
            image_id = animation.data.get("imageId")
            image_id = image_id if isinstance(image_id, str) else ""
            image_definition = (workspace.find("visualImages", image_id)
                                if image_id else None)
            if image_definition is None:
                error(f"visual image does not exist: {image_id or '(empty)'}", path)
                continue
            image_path = self._visual_image_path(image_definition, asset_root)
            if image_path is None:
                continue
            if not image_path.is_file():
                error(f"image file is missing: {image_path}", path)
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
                        error(f"frame {index} source rect is outside the "
                              f"image {width}x{height}: {animation_id}",
                              f"{path}.frames[{index}]")
        return diagnostics

    def _visual_image_path(self, image_definition, asset_root):
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

    # -- validation ----------------------------------------------------------

    def _validate(self, data: dict[str, object]) -> dict[str, object]:
        workspace = self._require_workspace()
        npc_id = data.get("id")
        if not isinstance(npc_id, str) or not _NPC_ID_PATTERN.fullmatch(npc_id):
            raise ValueError(f"invalid NPC id: {npc_id!r}")

        visual_set_id = data.get("visualSetId")
        if not isinstance(visual_set_id, str) or not visual_set_id:
            raise ValueError("NPC visualSetId is required")
        if self.find_visual(visual_set_id) is None:
            raise ValueError(f"NPC visual set does not exist: {visual_set_id}")

        dialogue_id = data.get("defaultDialogueId")
        if not isinstance(dialogue_id, str) or not dialogue_id:
            raise ValueError("NPC defaultDialogueId is required")
        if workspace.find("dialogues", dialogue_id) is None:
            raise ValueError(f"dialogue definition does not exist: {dialogue_id}")

        box = data.get("interaction")
        if not isinstance(box, dict) or any(
                not isinstance(box.get(key), int) or isinstance(box.get(key), bool)
                for key in ("x", "y", "width", "height")):
            raise ValueError("NPC interaction needs integer x/y/width/height")
        if int(box["width"]) <= 0 or int(box["height"]) <= 0:
            raise ValueError("NPC interaction box must have a positive area")

        tags = data.get("tags", [])
        if not isinstance(tags, list) or any(not isinstance(tag, str) for tag in tags):
            raise ValueError("NPC tags must be a list of strings")

        normalized = copy.deepcopy(data)
        normalized["interactionEnabled"] = bool(data.get("interactionEnabled", True))
        return normalized

    def _require_workspace(self) -> ContentWorkspace:
        if self.workspace is None:
            raise ValueError("NPC authoring requires an open content workspace")
        return self.workspace

    @staticmethod
    def _normalize_npc_id(npc_id: str) -> str:
        normalized = npc_id.strip()
        if not _NPC_ID_PATTERN.fullmatch(normalized):
            raise ValueError(f"invalid NPC id: {npc_id!r}")
        if not normalized.startswith("npc."):
            normalized = f"npc.{normalized}"
        return normalized

    @staticmethod
    def _normalize_visual_id(visual_id: str) -> str:
        normalized = visual_id.strip()
        if not _NPC_ID_PATTERN.fullmatch(normalized):
            raise ValueError(f"invalid NPC visual id: {visual_id!r}")
        if normalized.startswith("visual.npc."):
            return normalized
        if normalized.startswith("visual."):
            return normalized
        return f"visual.npc.{normalized}"
