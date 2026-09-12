from __future__ import annotations

from dataclasses import dataclass
import re

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue


@dataclass(frozen=True, slots=True)
class FrameSequenceSpec:
    image_id: str
    frame_width: int
    frame_height: int
    spacing: int
    origin_x: int
    origin_y: int
    duration_ticks: int
    loop: bool
    flip_x: bool
    frame_indices: tuple[int, ...]
    columns: int


@dataclass(frozen=True, slots=True)
class PlayerAuthoringRequest:
    player_id: str
    display_name: str
    progression_id: str
    sequences: dict[str, dict[str, FrameSequenceSpec]]


class PlayerAuthoringService:
    CORE_STATES = ("idle", "walk")
    OPTIONAL_STATES = (
        "hurt", "sword", "bow", "shield", "death", "dead",
        "sleeping", "wake_up",
    )
    DIRECTIONS = ("down", "up", "side")

    @staticmethod
    def _slug(value: str) -> str:
        value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip()).strip("._")
        return value or "player"

    @classmethod
    def animation_id(cls, player_id: str, state: str, direction: str) -> str:
        return f"animation.{cls._slug(player_id)}.{state}.{direction}"

    @staticmethod
    def _directional(refs: dict[str, str]) -> dict[str, JsonValue]:
        return {key: value for key, value in refs.items() if value}

    @staticmethod
    def _materialize(spec: FrameSequenceSpec) -> list[JsonValue]:
        if spec.columns <= 0:
            raise ValueError("spritesheet sem colunas válidas")
        if not spec.frame_indices:
            raise ValueError("adicione pelo menos um frame")
        pitch_x = spec.frame_width + spec.spacing
        pitch_y = spec.frame_height + spec.spacing
        frames: list[JsonValue] = []
        for index in spec.frame_indices:
            if index < 0:
                raise ValueError("índice de frame inválido")
            row, column = divmod(index, spec.columns)
            frames.append({
                "source": {
                    "x": spec.origin_x + column * pitch_x,
                    "y": spec.origin_y + row * pitch_y,
                    "width": spec.frame_width,
                    "height": spec.frame_height,
                },
                "anchor": {
                    "x": spec.frame_width // 2,
                    "y": spec.frame_height - 1,
                },
                "drawOffset": {"x": 0, "y": 0},
                "flipX": spec.flip_x,
                "durationTicks": spec.duration_ticks,
                "markers": [],
            })
        return frames

    def save(self, workspace: ContentWorkspace,
             request: PlayerAuthoringRequest,
             editing: bool = False) -> ContentDefinition:
        player_id = request.player_id.strip()
        display_name = request.display_name.strip()
        progression_id = request.progression_id.strip()
        if not player_id or not display_name or not progression_id:
            raise ValueError("Nome, ID e Progression são obrigatórios.")

        for state in self.CORE_STATES:
            directions = request.sequences.get(state, {})
            missing = [
                direction for direction in self.DIRECTIONS
                if direction not in directions
            ]
            if missing:
                raise ValueError(
                    f"{state}: defina Down, Up e Side antes de salvar.")

        visual_id = f"visual.{self._slug(player_id)}"
        existing = workspace.find("players", player_id)
        if editing and existing is None:
            raise ValueError("O Player selecionado não existe mais.")
        if not editing and existing is not None:
            raise ValueError(f"Player já existe: {player_id}")

        animation_entries: list[tuple[str, str, dict[str, JsonValue]]] = []
        state_refs: dict[str, dict[str, str]] = {}
        for state, directions in request.sequences.items():
            refs: dict[str, str] = {}
            for direction, spec in directions.items():
                if direction not in self.DIRECTIONS:
                    continue
                if workspace.find("visualImages", spec.image_id) is None:
                    raise ValueError(
                        f"Imagem importada não existe: {spec.image_id}")
                animation_id = self.animation_id(
                    player_id, state, direction)
                animation_entries.append((
                    "animations", animation_id, {
                        "id": animation_id,
                        "imageId": spec.image_id,
                        "loop": spec.loop,
                        "frames": self._materialize(spec),
                    }))
                refs[direction] = animation_id
            if refs:
                state_refs[state] = refs

        idle = self._directional(state_refs["idle"])
        walk = self._directional(state_refs["walk"])
        hurt = (
            self._directional(state_refs["hurt"])
            if "hurt" in state_refs else None)

        actions: list[JsonValue] = []
        for state in self.OPTIONAL_STATES:
            if state == "hurt" or state not in state_refs:
                continue
            actions.append({
                "actionId": state,
                "clips": self._directional(state_refs[state]),
            })

        entries = animation_entries + [
            ("playerVisuals", visual_id, {
                "id": visual_id,
                "idle": idle,
                "walk": walk,
                "hurt": hurt,
                "actions": actions,
            }),
            ("players", player_id, {
                "id": player_id,
                "visualSetId": visual_id,
                "progressionId": progression_id,
            }),
            ("authoringDescriptors", player_id, {
                "definitionId": player_id,
                "displayName": display_name,
                "category": "player",
                "tags": ["player"],
            }),
        ]
        values = (
            workspace.upsert_definition_bundle("Configure Player", entries)
            if editing
            else workspace.create_definition_bundle("Create Player", entries)
        )
        return next(value for value in values if value.category == "players")
