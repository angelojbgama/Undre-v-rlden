from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import re

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition, JsonValue
from .import_service import read_image_dimensions


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
class PlayerCollisionMaskSpec:
    width: int
    height: int
    origin_x: int
    origin_y: int
    cells: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class PlayerAuthoringRequest:
    player_id: str
    display_name: str
    progression_id: str
    sequences: dict[str, dict[str, FrameSequenceSpec]]
    movement_collision_enabled: bool = False
    movement_collision: dict[str, PlayerCollisionMaskSpec] = field(
        default_factory=dict)


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

    @staticmethod
    def _collision_data(spec: PlayerCollisionMaskSpec) -> dict[str, JsonValue]:
        return {
            "width": spec.width,
            "height": spec.height,
            "origin": {
                "x": spec.origin_x,
                "y": spec.origin_y,
            },
            "cells": list(spec.cells),
        }

    @classmethod
    def _validate_movement_collision(
            cls, request: PlayerAuthoringRequest) -> None:
        if not request.movement_collision_enabled:
            return
        missing = [
            direction for direction in cls.DIRECTIONS
            if direction not in request.movement_collision
        ]
        if missing:
            raise ValueError(
                "Movement Collision: defina Down, Up e Side antes de salvar.")

        idle = request.sequences.get("idle", {})
        for direction in cls.DIRECTIONS:
            mask = request.movement_collision[direction]
            if mask.width <= 0 or mask.height <= 0:
                raise ValueError(
                    f"Movement Collision {direction}: dimensões inválidas.")
            expected = mask.width * mask.height
            if len(mask.cells) != expected:
                raise ValueError(
                    f"Movement Collision {direction}: a máscara não combina "
                    "com suas dimensões.")
            if any(cell not in (0, 1) for cell in mask.cells):
                raise ValueError(
                    f"Movement Collision {direction}: células devem ser 0 ou 1.")
            if not any(mask.cells):
                raise ValueError(
                    f"Movement Collision {direction}: a máscara está vazia.")
            idle_spec = idle.get(direction)
            if idle_spec is None:
                raise ValueError(
                    f"Movement Collision {direction}: configure Idle primeiro.")
            if (
                mask.width != idle_spec.frame_width or
                mask.height != idle_spec.frame_height
            ):
                raise ValueError(
                    f"Movement Collision {direction}: a máscara deve usar "
                    "o mesmo tamanho do frame Idle dessa direção.")

    @staticmethod
    def _collision_spec(value: object, direction: str) -> PlayerCollisionMaskSpec:
        if not isinstance(value, dict):
            raise ValueError(
                f"Movement Collision {direction}: definição inválida.")
        origin = value.get("origin")
        cells = value.get("cells")
        if not isinstance(origin, dict) or not isinstance(cells, list):
            raise ValueError(
                f"Movement Collision {direction}: origin/cells inválidos.")
        width = int(value.get("width", 0))
        height = int(value.get("height", 0))
        parsed = PlayerCollisionMaskSpec(
            width=width,
            height=height,
            origin_x=int(origin.get("x", 0)),
            origin_y=int(origin.get("y", 0)),
            cells=tuple(int(cell) for cell in cells),
        )
        if width <= 0 or height <= 0 or len(parsed.cells) != width * height:
            raise ValueError(
                f"Movement Collision {direction}: dimensões/células inválidas.")
        if any(cell not in (0, 1) for cell in parsed.cells):
            raise ValueError(
                f"Movement Collision {direction}: células devem ser 0 ou 1.")
        return parsed

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

        self._validate_movement_collision(request)

        existing = workspace.find("players", player_id)
        if editing and existing is None:
            raise ValueError("O Player selecionado não existe mais.")
        if not editing and existing is not None:
            raise ValueError(f"Player já existe: {player_id}")
        visual_id = f"visual.{self._slug(player_id)}"
        if editing and existing is not None:
            visual_id = str(existing.data.get("visualSetId", "")) or visual_id

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

        player_data: dict[str, JsonValue] = {
            "id": player_id,
            "visualSetId": visual_id,
            "progressionId": progression_id,
        }
        if request.movement_collision_enabled:
            player_data["movementCollision"] = {
                direction: self._collision_data(
                    request.movement_collision[direction])
                for direction in self.DIRECTIONS
            }

        entries = animation_entries + [
            ("playerVisuals", visual_id, {
                "id": visual_id,
                "idle": idle,
                "walk": walk,
                "hurt": hurt,
                "actions": actions,
            }),
            ("players", player_id, player_data),
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

    def request_for(self, workspace: ContentWorkspace,
                    definition: ContentDefinition,
                    asset_root: Path | None = None) -> PlayerAuthoringRequest:
        """Rebuild the Player editor model from already-authored content."""
        if definition.category != "players":
            raise ValueError("A definição selecionada não é um Player.")

        visual_id = str(definition.data.get("visualSetId", ""))
        visual = workspace.find("playerVisuals", visual_id)
        if visual is None:
            raise ValueError(
                f"PlayerVisual associado não existe: {visual_id or '<vazio>'}")

        descriptor = workspace.find(
            "authoringDescriptors", definition.definition_id)
        display_name = (
            descriptor.display_name if descriptor is not None
            else definition.display_name)
        progression_id = str(definition.data.get(
            "progressionId", "progression.player.default"))

        state_refs: dict[str, dict[str, str]] = {
            "idle": self._refs(visual.data.get("idle")),
            "walk": self._refs(visual.data.get("walk")),
        }
        hurt_refs = self._refs(visual.data.get("hurt"))
        if hurt_refs:
            state_refs["hurt"] = hurt_refs

        actions = visual.data.get("actions", [])
        if isinstance(actions, list):
            for action in actions:
                if not isinstance(action, dict):
                    continue
                state = str(action.get("actionId", ""))
                if state in self.OPTIONAL_STATES and state != "hurt":
                    refs = self._refs(action.get("clips"))
                    if refs:
                        state_refs[state] = refs

        sequences: dict[str, dict[str, FrameSequenceSpec]] = {}
        for state, refs in state_refs.items():
            for direction, animation_id in refs.items():
                animation = workspace.find("animations", animation_id)
                if animation is None:
                    raise ValueError(
                        f"Animação referenciada não existe: {animation_id}")
                sequences.setdefault(state, {})[direction] = (
                    self._sequence_from_animation(
                        workspace, animation, asset_root))

        movement_value = definition.data.get("movementCollision")
        movement_collision_enabled = isinstance(movement_value, dict)
        movement_collision: dict[str, PlayerCollisionMaskSpec] = {}
        if movement_collision_enabled:
            assert isinstance(movement_value, dict)
            for direction in self.DIRECTIONS:
                if direction not in movement_value:
                    raise ValueError(
                        "Movement Collision salvo está incompleto: "
                        f"falta {direction}.")
                movement_collision[direction] = self._collision_spec(
                    movement_value[direction], direction)

        return PlayerAuthoringRequest(
            player_id=definition.definition_id,
            display_name=display_name,
            progression_id=progression_id,
            sequences=sequences,
            movement_collision_enabled=movement_collision_enabled,
            movement_collision=movement_collision,
        )

    @classmethod
    def _sequence_from_animation(
            cls, workspace: ContentWorkspace,
            animation: ContentDefinition,
            asset_root: Path | None) -> FrameSequenceSpec:
        image_id = str(animation.data.get("imageId", ""))
        image = workspace.find("visualImages", image_id)
        if image is None:
            raise ValueError(
                f"Imagem da animação não existe: {image_id or '<vazio>'}")

        raw_frames = animation.data.get("frames", [])
        if not isinstance(raw_frames, list) or not raw_frames:
            raise ValueError(
                f"Animação sem frames: {animation.definition_id}")

        sources: list[tuple[int, int, int, int]] = []
        duration_ticks: int | None = None
        flip_x: bool | None = None
        frame_width: int | None = None
        frame_height: int | None = None
        for raw_frame in raw_frames:
            if not isinstance(raw_frame, dict):
                raise ValueError(
                    f"Frame inválido em {animation.definition_id}")
            source = raw_frame.get("source")
            if not isinstance(source, dict):
                raise ValueError(
                    f"Frame sem source em {animation.definition_id}")
            rect = (
                int(source.get("x", 0)),
                int(source.get("y", 0)),
                int(source.get("width", 0)),
                int(source.get("height", 0)),
            )
            if rect[0] < 0 or rect[1] < 0 or rect[2] <= 0 or rect[3] <= 0:
                raise ValueError(
                    f"Source inválido em {animation.definition_id}")
            if frame_width is None:
                frame_width, frame_height = rect[2], rect[3]
            elif (rect[2], rect[3]) != (frame_width, frame_height):
                raise ValueError(
                    "Configurar Player exige frames de mesmo tamanho dentro "
                    f"da animação {animation.definition_id}.")

            current_duration = max(1, int(raw_frame.get("durationTicks", 1)))
            current_flip = bool(raw_frame.get("flipX", False))
            if duration_ticks is None:
                duration_ticks = current_duration
            elif current_duration != duration_ticks:
                raise ValueError(
                    "Configurar Player ainda exige duração uniforme dentro "
                    f"da animação {animation.definition_id}.")
            if flip_x is None:
                flip_x = current_flip
            elif current_flip != flip_x:
                raise ValueError(
                    "Configurar Player ainda exige flip X uniforme dentro "
                    f"da animação {animation.definition_id}.")
            sources.append(rect)

        assert frame_width is not None and frame_height is not None
        assert duration_ticks is not None and flip_x is not None
        image_width, image_height = cls._image_dimensions(
            workspace, image, asset_root, sources)
        spacing, origin_x, origin_y, columns, frame_indices = cls._infer_grid(
            sources, frame_width, frame_height,
            image_width, image_height)

        return FrameSequenceSpec(
            image_id=image_id,
            frame_width=frame_width,
            frame_height=frame_height,
            spacing=spacing,
            origin_x=origin_x,
            origin_y=origin_y,
            duration_ticks=duration_ticks,
            loop=bool(animation.data.get("loop", True)),
            flip_x=flip_x,
            frame_indices=frame_indices,
            columns=columns,
        )

    @staticmethod
    def _refs(value: object) -> dict[str, str]:
        if not isinstance(value, dict):
            return {}
        refs: dict[str, str] = {}
        for direction in PlayerAuthoringService.DIRECTIONS:
            animation_id = str(value.get(direction, ""))
            if animation_id:
                refs[direction] = animation_id
        return refs

    @staticmethod
    def _image_dimensions(
            workspace: ContentWorkspace,
            image: ContentDefinition,
            asset_root: Path | None,
            sources: list[tuple[int, int, int, int]]) -> tuple[int, int]:
        root = (
            asset_root if image.data.get("root") == "gameAssets"
            else workspace.root)
        relative = image.data.get("relativePath")
        if root is not None and isinstance(relative, str):
            path = root / relative
            if path.is_file():
                try:
                    dimensions = read_image_dimensions(path)
                    return dimensions.width, dimensions.height
                except (OSError, ValueError):
                    pass
        return (
            max(x + width for x, _, width, _ in sources),
            max(y + height for _, y, _, height in sources),
        )

    @staticmethod
    def _infer_grid(
            sources: list[tuple[int, int, int, int]],
            frame_width: int, frame_height: int,
            image_width: int, image_height: int,
            ) -> tuple[int, int, int, int, tuple[int, ...]]:
        # The authored animation stores exact source rectangles, not editor-grid
        # metadata. Recover the smallest regular grid that reproduces every
        # source exactly. This keeps saved Players editable without changing the
        # runtime content format.
        max_spacing = min(max(image_width, image_height, 0), 4096)
        for spacing in range(max_spacing + 1):
            pitch_x = frame_width + spacing
            pitch_y = frame_height + spacing
            x_residues = {x % pitch_x for x, _, _, _ in sources}
            y_residues = {y % pitch_y for _, y, _, _ in sources}
            if len(x_residues) != 1 or len(y_residues) != 1:
                continue
            origin_x = next(iter(x_residues))
            origin_y = next(iter(y_residues))
            columns = (image_width - origin_x + spacing) // pitch_x
            rows = (image_height - origin_y + spacing) // pitch_y
            if columns <= 0 or rows <= 0:
                continue

            indices: list[int] = []
            valid = True
            for x, y, width, height in sources:
                if width != frame_width or height != frame_height:
                    valid = False
                    break
                dx = x - origin_x
                dy = y - origin_y
                if dx < 0 or dy < 0 or dx % pitch_x or dy % pitch_y:
                    valid = False
                    break
                column = dx // pitch_x
                row = dy // pitch_y
                if column >= columns or row >= rows:
                    valid = False
                    break
                index = row * columns + column
                check_row, check_column = divmod(index, columns)
                if (
                    origin_x + check_column * pitch_x != x or
                    origin_y + check_row * pitch_y != y
                ):
                    valid = False
                    break
                indices.append(index)
            if valid:
                return (
                    spacing, origin_x, origin_y,
                    columns, tuple(indices))

        raise ValueError(
            "A animação não corresponde a uma grade regular que o editor "
            "frame a frame consiga reabrir.")
