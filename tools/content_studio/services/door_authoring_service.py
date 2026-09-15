from __future__ import annotations

from dataclasses import dataclass
from math import ceil

from ..model.content_workspace import ContentWorkspace
from ..model.types import ContentDefinition


DEFAULT_MODE = "wall"
DEFAULT_ANCHOR = "bottom-center"
DEFAULT_ORIENTATIONS = ("horizontal",)

SUPPORTED_MODES = {
    "wall",
}

SUPPORTED_ANCHORS = {
    "bottom-center",
}

SUPPORTED_ORIENTATIONS = {
    "horizontal",
    "vertical",
}


@dataclass(
    frozen=True,
    slots=True,
)
class DoorPlacementProfile:
    """Authoring-only rules for snapping a door into map geometry."""

    mode: str
    span_tiles: int
    thickness_tiles: int
    anchor: str
    orientations: tuple[str, ...]


@dataclass(
    frozen=True,
    slots=True,
)
class DoorCatalogEntry:
    """Door definition prepared for the specialized Studio library."""

    definition_id: str
    display_name: str
    visual_set_id: str
    source_animation_id: str
    frame_width: int
    frame_height: int
    placement: DoorPlacementProfile


class DoorAuthoringService:
    """Resolve reusable WorldObject doors into Studio placement metadata.

    Doors remain ordinary authored WorldObjects with the `door`
    capability.  This service adds authoring semantics only; it does
    not create a parallel runtime DoorDefinition.
    """

    def __init__(
        self,
        workspace: ContentWorkspace,
    ) -> None:
        self.workspace = workspace

    def entries(
        self,
        tile_size: int,
    ) -> list[DoorCatalogEntry]:
        self._require_tile_size(
            tile_size
        )

        result: list[DoorCatalogEntry] = []

        for definition in self.workspace.definitions(
            "objects"
        ):
            if not isinstance(
                definition.data.get("door"),
                dict,
            ):
                continue

            result.append(
                self._build_entry(
                    definition,
                    tile_size,
                )
            )

        result.sort(
            key=lambda value: (
                value.display_name.casefold(),
                value.definition_id,
            )
        )

        return result

    def entry(
        self,
        definition_id: str,
        tile_size: int,
    ) -> DoorCatalogEntry | None:
        self._require_tile_size(
            tile_size
        )

        definition = self.workspace.find(
            "objects",
            definition_id,
        )

        if definition is None:
            return None

        if not isinstance(
            definition.data.get("door"),
            dict,
        ):
            return None

        return self._build_entry(
            definition,
            tile_size,
        )

    @staticmethod
    def _require_tile_size(
        tile_size: int,
    ) -> None:
        if (
            isinstance(tile_size, bool)
            or not isinstance(tile_size, int)
            or tile_size <= 0
        ):
            raise ValueError(
                "tile_size must be a positive integer"
            )

    def _build_entry(
        self,
        definition: ContentDefinition,
        tile_size: int,
    ) -> DoorCatalogEntry:
        descriptor = self.workspace.find(
            "authoringDescriptors",
            definition.definition_id,
        )

        tags = self._tags(
            descriptor
        )

        visual_set_id = definition.data.get(
            "visualSetId"
        )

        if (
            not isinstance(visual_set_id, str)
            or not visual_set_id
        ):
            raise ValueError(
                f"{definition.definition_id}: "
                "door visualSetId is required"
            )

        source_animation_id = (
            self._tag(
                tags,
                "object-source-animation:",
            )
            or self._visual_animation(
                visual_set_id
            )
        )

        animation = self.workspace.find(
            "animations",
            source_animation_id,
        )

        if animation is None:
            raise ValueError(
                f"{definition.definition_id}: "
                f"unknown door animation: "
                f"{source_animation_id!r}"
            )

        frame_width, frame_height = (
            self._frame_size(
                definition.definition_id,
                animation,
            )
        )

        inferred_span = max(
            1,
            ceil(
                frame_width
                / tile_size
            ),
        )

        span_tiles = self._positive_int_tag(
            tags,
            "door-span-tiles:",
            inferred_span,
        )

        thickness_tiles = self._positive_int_tag(
            tags,
            "door-thickness-tiles:",
            1,
        )

        mode = (
            self._tag(
                tags,
                "door-placement:",
            )
            or DEFAULT_MODE
        )

        if mode not in SUPPORTED_MODES:
            raise ValueError(
                f"{definition.definition_id}: "
                f"unsupported door-placement: {mode}"
            )

        anchor = (
            self._tag(
                tags,
                "door-anchor:",
            )
            or DEFAULT_ANCHOR
        )

        if anchor not in SUPPORTED_ANCHORS:
            raise ValueError(
                f"{definition.definition_id}: "
                f"unsupported door-anchor: {anchor}"
            )

        orientations = self._orientations(
            definition.definition_id,
            tags,
        )

        return DoorCatalogEntry(
            definition_id=definition.definition_id,
            display_name=definition.display_name,
            visual_set_id=visual_set_id,
            source_animation_id=source_animation_id,
            frame_width=frame_width,
            frame_height=frame_height,
            placement=DoorPlacementProfile(
                mode=mode,
                span_tiles=span_tiles,
                thickness_tiles=thickness_tiles,
                anchor=anchor,
                orientations=orientations,
            ),
        )

    def _visual_animation(
        self,
        visual_set_id: str,
    ) -> str:
        visual = self.workspace.find(
            "objectVisuals",
            visual_set_id,
        )

        if visual is None:
            raise ValueError(
                f"unknown door objectVisual: "
                f"{visual_set_id!r}"
            )

        for field in (
            "doorClosedAnimationId",
            "idleAnimationId",
        ):
            animation_id = visual.data.get(
                field
            )

            if (
                isinstance(animation_id, str)
                and animation_id
            ):
                return animation_id

        raise ValueError(
            f"{visual_set_id}: "
            "door visual has no closed/idle animation"
        )

    @staticmethod
    def _frame_size(
        definition_id: str,
        animation: ContentDefinition,
    ) -> tuple[int, int]:
        frames = animation.data.get(
            "frames"
        )

        if (
            not isinstance(frames, list)
            or not frames
            or not isinstance(frames[0], dict)
        ):
            raise ValueError(
                f"{definition_id}: "
                "door animation has no frame"
            )

        source = frames[0].get(
            "source"
        )

        if not isinstance(
            source,
            dict,
        ):
            raise ValueError(
                f"{definition_id}: "
                "door animation frame has no source"
            )

        width = source.get(
            "width"
        )

        height = source.get(
            "height"
        )

        if (
            isinstance(width, bool)
            or isinstance(height, bool)
            or not isinstance(width, int)
            or not isinstance(height, int)
            or width <= 0
            or height <= 0
        ):
            raise ValueError(
                f"{definition_id}: "
                "door animation frame size must be positive"
            )

        return (
            width,
            height,
        )

    @staticmethod
    def _tags(
        descriptor: ContentDefinition | None,
    ) -> tuple[str, ...]:
        if descriptor is None:
            return ()

        value = descriptor.data.get(
            "tags",
            [],
        )

        if not isinstance(
            value,
            list,
        ):
            return ()

        return tuple(
            tag
            for tag in value
            if isinstance(tag, str)
        )

    @staticmethod
    def _tag(
        tags: tuple[str, ...],
        prefix: str,
    ) -> str | None:
        matches = [
            tag[len(prefix):].strip()
            for tag in tags
            if tag.startswith(prefix)
        ]

        if not matches:
            return None

        if len(matches) > 1:
            raise ValueError(
                f"duplicate authoring tag: {prefix}"
            )

        return matches[0]

    def _positive_int_tag(
        self,
        tags: tuple[str, ...],
        prefix: str,
        default: int,
    ) -> int:
        raw = self._tag(
            tags,
            prefix,
        )

        if raw is None:
            return default

        try:
            value = int(
                raw
            )
        except ValueError as error:
            raise ValueError(
                f"{prefix[:-1]} must be a positive integer"
            ) from error

        if value <= 0:
            raise ValueError(
                f"{prefix[:-1]} must be a positive integer"
            )

        return value

    def _orientations(
        self,
        definition_id: str,
        tags: tuple[str, ...],
    ) -> tuple[str, ...]:
        raw = self._tag(
            tags,
            "door-orientations:",
        )

        if raw is None:
            return DEFAULT_ORIENTATIONS

        orientations = tuple(
            value.strip()
            for value in raw.split(",")
            if value.strip()
        )

        if not orientations:
            raise ValueError(
                f"{definition_id}: "
                "door-orientations cannot be empty"
            )

        invalid = [
            value
            for value in orientations
            if value not in SUPPORTED_ORIENTATIONS
        ]

        if invalid:
            raise ValueError(
                f"{definition_id}: "
                f"unsupported door orientation: "
                f"{invalid[0]}"
            )

        return tuple(
            dict.fromkeys(
                orientations
            )
        )
